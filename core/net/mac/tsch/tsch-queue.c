/*
 * Copyright (c) 2014, SICS Swedish ICT.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is part of the Contiki operating system.
 *
 */

/**
 * \file
 *         Per-neighbor packet queues for TSCH MAC.
 *         The list of neighbors uses the TSCH lock, but per-neighbor packet array are lock-free.
 *         Read-only operation on neighbor and packets are allowed from interrupts and outside of them.
 *         *Other operations are allowed outside of interrupt only.*
 * \author
 *         Simon Duquennoy <simonduq@sics.se>
 *         Beshr Al Nahas <beshr@sics.se>
 *         Domenico De Guglielmo <d.deguglielmo@iet.unipi.it >
 */

#include "contiki.h"
#include "lib/list.h"
#include "lib/memb.h"
#include "lib/random.h"
#include "net/queuebuf.h"
#include "net/mac/rdc.h"
#include "net/mac/tsch/tsch.h"
#include "net/mac/tsch/tsch-private.h"
#include "net/mac/tsch/tsch-queue.h"
#include "net/mac/tsch/tsch-schedule.h"
#include "net/mac/tsch/tsch-slot-operation.h"
#include "net/mac/tsch/tsch-log.h"
#include <string.h>

#if TESLA
  #include "orchestra.h"//JSB
#endif 

#if PROPOSED
  #include "net/ipv6/uip-ds6-route.h"
  #include "net/ipv6/uip-ds6-nbr.h"
  #include "sys/ctimer.h" //JSB:mod1
  #include "orchestra.h"
#endif

#if TSCH_LOG_LEVEL >= 1
#define DEBUG DEBUG_PRINT
#else /* TSCH_LOG_LEVEL */
#define DEBUG DEBUG_NONE
#endif /* TSCH_LOG_LEVEL */
#include "net/net-debug.h"

/* Check if TSCH_QUEUE_NUM_PER_NEIGHBOR is power of two */
#if (TSCH_QUEUE_NUM_PER_NEIGHBOR & (TSCH_QUEUE_NUM_PER_NEIGHBOR - 1)) != 0
#error TSCH_QUEUE_NUM_PER_NEIGHBOR must be power of two
#endif

/* We have as many packets are there are queuebuf in the system */
MEMB(packet_memb, struct tsch_packet, QUEUEBUF_NUM);
MEMB(neighbor_memb, struct tsch_neighbor, TSCH_QUEUE_MAX_NEIGHBOR_QUEUES);
LIST(neighbor_list);

/* Broadcast and EB virtual neighbors */
struct tsch_neighbor *n_broadcast;
struct tsch_neighbor *n_eb;

#if PROPOSED
  static struct ctimer select_N_timer;
#endif


#if PROPOSED
uint8_t is_routing_nbr(uip_ds6_nbr_t *nbr)
{
    uint16_t parent_id;
    uint16_t child_id;
    linkaddr_t *addr;
    uint16_t nbr_id;

    //parent?
    addr=&orchestra_parent_linkaddr;
    parent_id=TSCH_LOG_ID_FROM_LINKADDR(addr);
    nbr_id=ID_FROM_IPADDR(&(nbr->ipaddr));    
    if(parent_id== nbr_id)
    {
      return 1;
    }

    //1-hop child? i.e., existing next-hop
    nbr_table_item_t *item = nbr_table_head(nbr_routes);
    while(item != NULL) {
      addr = nbr_table_get_lladdr(nbr_routes, item);
      child_id= TSCH_LOG_ID_FROM_LINKADDR(addr);

      if(nbr_id==child_id)
      {
        return 1;
      }

      item = nbr_table_next(nbr_routes, item);
    }

    return 0;
    
}

void
change_queue_N_update(uint16_t nbr_id, uint16_t updated_N)
{
  struct tsch_neighbor * n= tsch_queue_get_nbr_from_id(nbr_id);
  if(n!=NULL) //checkkk
  {
    if(!ringbufindex_empty(&n->tx_ringbuf))
    {
      int16_t get_index = ringbufindex_peek_get(&n->tx_ringbuf);
      //int16_t put_index = ringbufindex_peek_put(&n->tx_ringbuf);
      uint8_t num_elements= ringbufindex_elements(&n->tx_ringbuf);

      printf("change_queue_N_update: %u %u (nbr %u)\n", updated_N, num_elements,nbr_id);



      uint8_t j;
      for(j=get_index;j<get_index+num_elements;j++)
      {
        int16_t index;

        if(j>=ringbufindex_size(&n->tx_ringbuf)) //16
        {
          index= j-ringbufindex_size(&n->tx_ringbuf);
        }
        else
        {
          index=j;  
        }
          uint8_t * packet= (uint8_t *)queuebuf_dataptr(n->tx_array[index]->qb);

          packet[2] = updated_N & 0xff;
          packet[3] = (updated_N >> 8) & 0xff;

      }
      

    }


  }
}  

void select_N(void* ptr)
{
  uip_ds6_nbr_t *nbr;
  uint16_t nbr_id;
  uint16_t traffic_load;
  int i;

  //N selection
  nbr = nbr_table_head(ds6_neighbors);
  printf("\n");
  printf("Update my_N\n");
  if(tsch_get_lock()){
    while(nbr != NULL) {
      //nbr_id=((nbr->ipaddr.u8[14]) << 8) | (nbr->ipaddr.u8[15]);
      nbr_id=ID_FROM_IPADDR(&(nbr->ipaddr));

      if(is_routing_nbr(nbr) && nbr->new_add==0)
      {

        uint16_t num_slots=N_SELECTION_PERIOD*1000/10;

        traffic_load= (1<<N_MAX) * nbr->num_tx / num_slots; //unit: packet/slot multiplied by 2^N_MAX
        //printf("%u %u %u\n",(1<<N_MAX), nbr->num_tx,num_slots);
        //printf("traffic (nbr %u): %u\n",nbr_id, traffic_load);
        for(i=1;i<=N_MAX;i++)
        {
          if( (traffic_load >> i) < 1)
          {
             uint16_t old_N=nbr->my_N;

             uint16_t new_N = N_MAX-i+1-MORE_UNDER_PROVISION;

             if(old_N != new_N)
             {
                uint8_t change_N=0;

                if(new_N > old_N)
                {
                  //inc
                  nbr->consecutive_my_N_inc++;
                  if(nbr->consecutive_my_N_inc >= THRES_CONSEQUTIVE_N_INC)
                  {
                    nbr->consecutive_my_N_inc=0;
                    change_N=1;
                  }
                  else
                  {
                    change_N=0;
                  }
                }
                else{
                  //dec
                  nbr->consecutive_my_N_inc=0;
                  change_N=1;
                }


                if(change_N){
                  printf("%u->%u (r_nbr %u)\n", old_N, new_N, nbr_id);
                  change_queue_N_update(nbr_id,new_N);
                  nbr->my_N=new_N;
                }
                else
                {
                  printf("%u->%u (X, %u) (r_nbr %u)\n", old_N, new_N, nbr->consecutive_my_N_inc, nbr_id);
                }

             }
             else{
              //No change
              nbr->consecutive_my_N_inc=0;
             }


             break;
          }
        }
      }
      else
      {
        nbr->my_N=5;

        if(nbr->new_add==1)
        { 
          printf("%u->%u (r_nbr %u, new_add)\n",nbr->my_N,nbr->my_N,nbr_id);
          nbr->new_add=0;
        }

      }

        nbr = nbr_table_next(ds6_neighbors, nbr);

       
    }

    //Reset all num_tx
    nbr = nbr_table_head(ds6_neighbors);
    printf("Reset num_tx\n");
    while(nbr != NULL) {
      nbr_id=ID_FROM_IPADDR(&(nbr->ipaddr));
      //printf("%u->0 (nbr %u)\n",nbr->num_tx, nbr_id); 
      nbr->num_tx=0;

      nbr = nbr_table_next(ds6_neighbors, nbr);
    }
    tsch_release_lock();
  }
  //printf("\n");
  ctimer_reset(&select_N_timer);
}



#endif //PROPOSED



/*---------------------------------------------------------------------------*/

#if TESLA
  void shared_tx_expired(void* ptr)
  {
    if(ptr!=NULL){
      struct tsch_neighbor *n= (struct tsch_neighbor*)ptr;
      if(n!=NULL)
      {
        if(!tsch_queue_is_empty(n))
        {
          printf("shared_tx_expired (%u)\n",(n->addr).u8[LINKADDR_SIZE-1]);
                  //add_rm_nbr(old_addr);
          change_attr_in_tx_queue(&(n->addr), 0 , 1); //change only 1st packet in queue
          
          ctimer_stop(&(n->shared_tx_timer)); //insurance
          ctimer_set(&(n->shared_tx_timer), SHARED_TX_INTERVAL, shared_tx_expired, (void*)n); 
        }
      }
      else
      {
        printf("ERROR: n=NULL in shared_tx_expired(%u)\n",(n->addr).u8[LINKADDR_SIZE-1]);
      }
    }
    else
    {
      printf("ERROR: shared_tx_expired ptr NULL\n");
    }
  }
#endif

/*---------------------------------------------------------------------------*/
/* Add a TSCH neighbor */
struct tsch_neighbor *
tsch_queue_add_nbr(const linkaddr_t *addr)
{
  struct tsch_neighbor *n = NULL;
  /* If we have an entry for this neighbor already, we simply update it */
  n = tsch_queue_get_nbr(addr);
  if(n == NULL) {
    if(tsch_get_lock()) {
      /* Allocate a neighbor */
      n = memb_alloc(&neighbor_memb);
      if(n != NULL) {
        /* Initialize neighbor entry */
        memset(n, 0, sizeof(struct tsch_neighbor));
        ringbufindex_init(&n->tx_ringbuf, TSCH_QUEUE_NUM_PER_NEIGHBOR);
        linkaddr_copy(&n->addr, addr);
        n->is_broadcast = linkaddr_cmp(addr, &tsch_eb_address)
          || linkaddr_cmp(addr, &tsch_broadcast_address);
        tsch_queue_backoff_reset(n);
        /* Add neighbor to the list */
        list_add(neighbor_list, n);
        //PRINTF("TSCH-queue: Add neighbor %u\n",addr->u8[LINKADDR_SIZE-1]);

      }
      tsch_release_lock();
    }

    /*****************JSB add******************/
    /*PRINTF("TSCH-queue: Neighbor add %u\n",TSCH_LOG_ID_FROM_LINKADDR(&(n->addr)));
    
    struct tsch_neighbor *curr_nbr = list_head(neighbor_list);
    PRINTF("TSCH: Neighbor List\n");
    while(curr_nbr != NULL) {
        PRINTF("ID: %u, ",TSCH_LOG_ID_FROM_LINKADDR(&(curr_nbr->addr)));
        PRINTF("is_broadcast: %u, ",curr_nbr->is_broadcast);
        PRINTF("tx_links_count: %u, ",curr_nbr->tx_links_count);
        PRINTF("\n");
      curr_nbr = list_item_next(curr_nbr);
    }*/
    /*****************JSB end******************/
  }
  return n;
}
/*---------------------------------------------------------------------------*/

struct tsch_neighbor *
tsch_queue_get_nbr_from_id(const uint16_t id)
{
  //if(!tsch_is_locked()) { //checkkk
    struct tsch_neighbor *n = list_head(neighbor_list);
    while(n != NULL) {
      //if(linkaddr_cmp(&n->addr, addr)) {
      if(TSCH_LOG_ID_FROM_LINKADDR(&n->addr)==id){
        return n;
      }
      n = list_item_next(n);
    }
  //}
  return NULL;
}

/*---------------------------------------------------------------------------*/
/* Get a TSCH neighbor */
struct tsch_neighbor *
tsch_queue_get_nbr(const linkaddr_t *addr)
{
  if(!tsch_is_locked()) {
    struct tsch_neighbor *n = list_head(neighbor_list);
    while(n != NULL) {
      if(linkaddr_cmp(&n->addr, addr)) {
        return n;
      }
      n = list_item_next(n);
    }
  }
  return NULL;
}
/*---------------------------------------------------------------------------*/
/* Get a TSCH time source (we currently assume there is only one) */
struct tsch_neighbor *
tsch_queue_get_time_source(void)
{
  if(!tsch_is_locked()) {
    struct tsch_neighbor *curr_nbr = list_head(neighbor_list);
    while(curr_nbr != NULL) {
      if(curr_nbr->is_time_source) {
        return curr_nbr;
      }
      curr_nbr = list_item_next(curr_nbr);
    }
  }
  return NULL;
}
/*---------------------------------------------------------------------------*/
/* Update TSCH time source */
int
tsch_queue_update_time_source(const linkaddr_t *new_addr)
{
  if(!tsch_is_locked()) {
    if(!tsch_is_coordinator) {
      struct tsch_neighbor *old_time_src = tsch_queue_get_time_source();
      struct tsch_neighbor *new_time_src = NULL;

      if(new_addr != NULL) {
        /* Get/add neighbor, return 0 in case of failure */
        new_time_src = tsch_queue_add_nbr(new_addr);
        if(new_time_src == NULL) {
          
          return 0;
        }
      }
      else
      {
        //printf("c1\n");
      }


      if(new_time_src != old_time_src) {
        printf("TSCH: update time source: %u -> %u\n",
               TSCH_LOG_ID_FROM_LINKADDR(old_time_src ? &old_time_src->addr : NULL),
               TSCH_LOG_ID_FROM_LINKADDR(new_time_src ? &new_time_src->addr : NULL));

        /* Update time source */
        if(new_time_src != NULL) {
          new_time_src->is_time_source = 1;
          /* (Re)set keep-alive timeout */
          tsch_set_ka_timeout(TSCH_KEEPALIVE_TIMEOUT);
          /* Start sending keepalives */
          tsch_schedule_keepalive();
        } else {
          /* Stop sending keepalives */
          tsch_set_ka_timeout(0);
        }

        if(old_time_src != NULL) {
          old_time_src->is_time_source = 0;
        }

#ifdef TSCH_CALLBACK_NEW_TIME_SOURCE
       
        TSCH_CALLBACK_NEW_TIME_SOURCE(old_time_src, new_time_src);
        
#endif
      }

#if PROPOSED
      else
      {
        //For First assciation (First DIO Rx) 
        reset_nbr(new_addr,1,0);
      }
#endif

      return 1;
    }
  }
  return 0;
}
/*---------------------------------------------------------------------------*/
/* Flush a neighbor queue */
void
tsch_queue_flush_nbr_queue(struct tsch_neighbor *n) //JSB: originally static
{
  while(!tsch_queue_is_empty(n)) {
    struct tsch_packet *p = tsch_queue_remove_packet_from_queue(n);
    if(p != NULL) {
      /* Set return status for packet_sent callback */
      p->ret = MAC_TX_ERR;
      //PRINTF("TSCH-queue:! flushing packet\n");
      /* Call packet_sent callback */
      mac_call_sent_callback(p->sent, p->ptr, p->ret, p->transmissions);
      /* Free packet queuebuf */
      tsch_queue_free_packet(p);
    }
    //PRINTF("TSCH-queue: packet is deleted packet=%p\n", p);
    if(linkaddr_cmp(&n->addr, &tsch_eb_address))
    {
      //don't count EB Loss
    }
    else{
      static uint32_t flush_queue_Loss=0;
      flush_queue_Loss++;

      /*TSCH_LOG_ADD(tsch_log_message,
            snprintf(log->message, sizeof(log->message),
            "Fq_Loss=%u\n",flush_queue_Loss));*/
      #if PRINT_SELECT_1
      //printf("Fq_Loss=%u\n",flush_queue_Loss);
      #else

      #endif
    }
    

  }
}
/*---------------------------------------------------------------------------*/
/* Remove TSCH neighbor queue */
static void
tsch_queue_remove_nbr(struct tsch_neighbor *n)  //JSB: originally static
{
  if(n != NULL) {
    if(tsch_get_lock()) {

      /*****************JSB add******************/
      //PRINTF("TSCH-queue: Remove neighbor %u\n",n->addr.u8[LINKADDR_SIZE-1]);
      //PRINTF("TSCH: Neighbor remove %u\n",TSCH_LOG_ID_FROM_LINKADDR(&(n->addr)));
      /*****************JSB end******************/

#if TESLA
      if(!ctimer_expired(&(n->shared_tx_timer)))
      {
        ctimer_stop(&(n->shared_tx_timer));
        #if PRINT_SELECT
        printf("Remove neighbor: Stop shared_tx_timer\n");
        #endif
      }
#endif

      /* Remove neighbor from list */
      list_remove(neighbor_list, n);

      tsch_release_lock();

      /* Flush queue */
      tsch_queue_flush_nbr_queue(n);

      /* Free neighbor */
      memb_free(&neighbor_memb, n);

      /*****************JSB add******************/
      /*struct tsch_neighbor *curr_nbr = list_head(neighbor_list);
      PRINTF("TSCH: Neighbor List\n");
      while(curr_nbr != NULL) {
          PRINTF("ID: %u, ",TSCH_LOG_ID_FROM_LINKADDR(&(curr_nbr->addr)));
          PRINTF("is_broadcast: %u, ",curr_nbr->is_broadcast);
          PRINTF("tx_links_count: %u, ",curr_nbr->tx_links_count);
          PRINTF("\n");
        curr_nbr = list_item_next(curr_nbr);
      }*/
    /*****************JSB end******************/



    }
  }
}
/*---------------------------------------------------------------------------*/
/* Add packet to neighbor queue. Use same lockfree implementation as ringbuf.c (put is atomic) */
struct tsch_packet *
tsch_queue_add_packet(const linkaddr_t *addr, mac_callback_t sent, void *ptr)
{
  struct tsch_neighbor *n = NULL;
  int16_t put_index = -1;
  struct tsch_packet *p = NULL;

#if PROPOSED
  uint16_t dest_id=TSCH_LOG_ID_FROM_LINKADDR(addr);
  uip_ds6_nbr_t *nbr = nbr_table_head(ds6_neighbors);
  
  while(nbr != NULL) {
    uint16_t nbr_id=((nbr->ipaddr.u8[14]) << 8) | (nbr->ipaddr.u8[15]);
    if(dest_id==nbr_id)
    {
      nbr->num_tx++; //even if q_loss
      //printf("num_tx++ %u (nbr %u)\n",nbr->num_tx, nbr_id); 
      break;
    }
    nbr = nbr_table_next(ds6_neighbors, nbr);
  }

#endif

  if(!tsch_is_locked()) {
    n = tsch_queue_add_nbr(addr);
    if(n != NULL) {
      put_index = ringbufindex_peek_put(&n->tx_ringbuf);

      if(put_index != -1) {
        p = memb_alloc(&packet_memb); //JSB: includes outgoing packets for all neighbors --> size 16
        if(p != NULL) {
          /* Enqueue packet */
#ifdef TSCH_CALLBACK_PACKET_READY
          TSCH_CALLBACK_PACKET_READY();
#endif
          p->qb = queuebuf_new_from_packetbuf();
          if(p->qb != NULL) {
            p->sent = sent;
            p->ptr = ptr;
            p->ret = MAC_TX_DEFERRED;
            p->transmissions = 0;
            /* Add to ringbuf (actual add committed through atomic operation) */
            n->tx_array[put_index] = p;
#if TESLA
            /*if(n->is_broadcast)
            {
              printf("n->is_broadcast\n");
            }*/

            if(tsch_queue_is_empty(n) && !n->is_broadcast) //n->is_broadcast means EB, multi dio, dis
            {
              #if PRINT_SELECT
              printf("shared_tx_timer: start (%u)\n",(n->addr).u8[LINKADDR_SIZE-1]);
              #endif
              ctimer_set(&(n->shared_tx_timer), SHARED_TX_INTERVAL, shared_tx_expired, (void*)n); 
            } 
#endif
            ringbufindex_put(&n->tx_ringbuf);
            /*PRINTF("TSCH-queue: packet is added put_index=%u, free_q_num=%d, addr=%u\n",
                   put_index,  memb_numfree(&packet_memb), (n->addr).u8[LINKADDR_SIZE-1]);*/
            PRINTF("Add p_i=%u, free=%d, addr=%u\n",
                   put_index,  memb_numfree(&packet_memb), (n->addr).u8[LINKADDR_SIZE-1]);
            return p;
          } else {
            memb_free(&packet_memb, p);
          }
        }
      }
    }
  }
  //PRINTF("TSCH-queue:! add packet failed: %u %p %d %p %p\n", tsch_is_locked(), n, put_index, p, p ? p->qb : NULL);
  static uint16_t num_q_loss=0;
  num_q_loss++;
  printf("Q_Loss=%u\n",num_q_loss);
  return 0;
}
/*---------------------------------------------------------------------------*/
/* Returns the number of packets currently in the queue */
int
tsch_queue_packet_count(const linkaddr_t *addr)
{
  //JSB: includes outgoing packets for each neighbor --> size: 16 per neighbor
  struct tsch_neighbor *n = NULL;
  if(!tsch_is_locked()) {
    n = tsch_queue_add_nbr(addr);
    if(n != NULL) {
      return ringbufindex_elements(&n->tx_ringbuf);
    }
  }
  return -1;
}
/*---------------------------------------------------------------------------*/
/* Remove first packet from a neighbor queue */
struct tsch_packet *
tsch_queue_remove_packet_from_queue(struct tsch_neighbor *n)
{
  if(!tsch_is_locked()) {
    if(n != NULL) {
      /* Get and remove packet from ringbuf (remove committed through an atomic operation */
      int16_t get_index = ringbufindex_get(&n->tx_ringbuf);
      if(get_index != -1) {
        //PRINTF("TSCH-queue: packet is removed, get_index=%u\n", get_index);
       //PRINTF("Remove g_i=%u\n", get_index);
        return n->tx_array[get_index];
      } else {
        return NULL;
      }
    }
  }
  return NULL;
}
/*---------------------------------------------------------------------------*/
/* Free a packet */
void
tsch_queue_free_packet(struct tsch_packet *p)
{
  if(p != NULL) {
    queuebuf_free(p->qb);
    memb_free(&packet_memb, p);
  }
}
/*---------------------------------------------------------------------------*/
/* Flush all neighbor queues */
void
tsch_queue_reset(void)
{
  /* Deallocate unneeded neighbors */
  if(!tsch_is_locked()) {
    struct tsch_neighbor *n = list_head(neighbor_list);
    while(n != NULL) {
      struct tsch_neighbor *next_n = list_item_next(n);
      /* Flush queue */
      tsch_queue_flush_nbr_queue(n);
      /* Reset backoff exponent */
      tsch_queue_backoff_reset(n);
      n = next_n;
    }
  }
}
/*---------------------------------------------------------------------------*/
/* Deallocate neighbors with empty queue */
void
tsch_queue_free_unused_neighbors(void)
{
  /* Deallocate unneeded neighbors */
  if(!tsch_is_locked()) {
    struct tsch_neighbor *n = list_head(neighbor_list);
    while(n != NULL) {
      struct tsch_neighbor *next_n = list_item_next(n);
      /* Queue is empty, no tx link to this neighbor: deallocate.
       * Always keep time source and virtual broadcast neighbors. */
      if(!n->is_broadcast && !n->is_time_source && !n->tx_links_count
         && tsch_queue_is_empty(n)) {
        tsch_queue_remove_nbr(n);
      }
      n = next_n;
    }
  }
}
/*---------------------------------------------------------------------------*/
/* Is the neighbor queue empty? */
int
tsch_queue_is_empty(const struct tsch_neighbor *n)
{
  return !tsch_is_locked() && n != NULL && ringbufindex_empty(&n->tx_ringbuf);
}
/*---------------------------------------------------------------------------*/
/* Returns the first packet from a neighbor queue */
struct tsch_packet *
tsch_queue_get_packet_for_nbr(const struct tsch_neighbor *n, struct tsch_link *link)
{
  if(!tsch_is_locked()) {
    int is_shared_link = link != NULL && link->link_options & LINK_OPTION_SHARED;
    if(n != NULL) {
      int16_t get_index = ringbufindex_peek_get(&n->tx_ringbuf);
      /*TSCH_LOG_ADD(tsch_log_message,
                        snprintf(log->message, sizeof(log->message),
                        "%d %d %d"
                        ,get_index,is_shared_link,tsch_queue_backoff_expired(n))
                        );*/
      if(get_index != -1 &&
          !(is_shared_link && !tsch_queue_backoff_expired(n))) {    /* If this is a shared link,
                                                                    make sure the backoff has expired */
#if TSCH_WITH_LINK_SELECTOR

#if PROPOSED && RESIDUAL_ALLOC
        if(link->slotframe_handle > SSQ_SCHEDULE_HANDLE_OFFSET && link->link_options == LINK_OPTION_TX)
        {
          uint16_t target_nbr_id= (link->slotframe_handle - SSQ_SCHEDULE_HANDLE_OFFSET-1)/2;
          
          if(TSCH_LOG_ID_FROM_LINKADDR(&(n->addr)) == target_nbr_id )
          {

            return n->tx_array[get_index];
          }
          else
          {
            return NULL;
          }


        }
#endif        
        int packet_attr_slotframe = queuebuf_attr(n->tx_array[get_index]->qb, PACKETBUF_ATTR_TSCH_SLOTFRAME);
        int packet_attr_timeslot = queuebuf_attr(n->tx_array[get_index]->qb, PACKETBUF_ATTR_TSCH_TIMESLOT);
        if(packet_attr_slotframe != 0xffff && packet_attr_slotframe != link->slotframe_handle) {
          return NULL;
        }
        if(packet_attr_timeslot != 0xffff && packet_attr_timeslot != link->timeslot) {
          return NULL;
        }
#endif
        
        return n->tx_array[get_index];
      }
    }
  }
  return NULL;
}
/*---------------------------------------------------------------------------*/
/* Returns the head packet from a neighbor queue (from neighbor address) */
struct tsch_packet *
tsch_queue_get_packet_for_dest_addr(const linkaddr_t *addr, struct tsch_link *link)
{
  if(!tsch_is_locked()) {
    return tsch_queue_get_packet_for_nbr(tsch_queue_get_nbr(addr), link);
  }
  return NULL;
}
/*---------------------------------------------------------------------------*/
/* Returns the head packet of any neighbor queue with zero backoff counter.
 * Writes pointer to the neighbor in *n */
struct tsch_packet *
tsch_queue_get_unicast_packet_for_any(struct tsch_neighbor **n, struct tsch_link *link)
{
  if(link==NULL)
  {
    printf("ERROR: link=NULL (tsch_queue)\n");
    return NULL;
  }
  if(!tsch_is_locked()) {
    struct tsch_neighbor *curr_nbr = list_head(neighbor_list);
    struct tsch_packet *p = NULL;
    while(curr_nbr != NULL) {
      if(!curr_nbr->is_broadcast && curr_nbr->tx_links_count == 0) {
        /* Only look up for non-broadcast neighbors we do not have a tx link to */
         /*TSCH_LOG_ADD(tsch_log_message,
                        snprintf(log->message, sizeof(log->message),
                        "Ch pkt for %u"
                        ,TSCH_LOG_ID_FROM_LINKADDR(&(curr_nbr->addr)))
                        );*/
        p = tsch_queue_get_packet_for_nbr(curr_nbr, link);
        if(p != NULL) {
          if(n != NULL) {
            *n = curr_nbr;
            /*TSCH_LOG_ADD(tsch_log_message,
                      snprintf(log->message, sizeof(log->message),
                          "shared slot used to %u, %u",
                            TSCH_LOG_ID_FROM_LINKADDR(&(curr_nbr->addr))
                            ,curr_nbr->tx_links_count
                            );
            );*/
          }

          return p;
        }
        else
        {
          /*TSCH_LOG_ADD(tsch_log_message,
                        snprintf(log->message, sizeof(log->message),
                        "No pkt for %u"
                        ,TSCH_LOG_ID_FROM_LINKADDR(&(curr_nbr->addr)))
                        );*/
        }
      }
      curr_nbr = list_item_next(curr_nbr);
    }

#if PROPOSED & RESIDUAL_ALLOC    
    if(link->slotframe_handle > SSQ_SCHEDULE_HANDLE_OFFSET && link->link_options == LINK_OPTION_TX)
    {
      uint16_t target_nbr_id= (link->slotframe_handle - SSQ_SCHEDULE_HANDLE_OFFSET-1)/2;
      printf("ERROR: No ssq Tx packet (nbr %u)\n",target_nbr_id); // This could be printed when Tx occurs by RB before reserved ssq Tx

      struct tsch_neighbor* target_nbr=tsch_queue_get_nbr_from_id(target_nbr_id);
      if(target_nbr==NULL)
      {
        printf("ERROR: c1\n");
      }
      else
      {
        printf("ERROR: c2 %u %u\n", !target_nbr->is_broadcast, target_nbr->tx_links_count == 0);
        //both should be 1

        int16_t get_index = ringbufindex_peek_get(&target_nbr->tx_ringbuf);
        int is_shared_link = link != NULL && link->link_options & LINK_OPTION_SHARED;
        printf("ERROR: c3 %u %u\n",get_index!=-1,is_shared_link!=0);
        //both should be 1
      }
      

    }
#endif
  }
  
  else
  {
#if PROPOSED & RESIDUAL_ALLOC    
    if(link->slotframe_handle > SSQ_SCHEDULE_HANDLE_OFFSET && link->link_options == LINK_OPTION_TX)
    {
      printf("ERROR: Locked\n");
    }
#endif
  }  
  
  return NULL;
}
/*---------------------------------------------------------------------------*/
/* May the neighbor transmit over a shared link? */
int
tsch_queue_backoff_expired(const struct tsch_neighbor *n)
{
  return n->backoff_window == 0;
}
/*---------------------------------------------------------------------------*/
/* Reset neighbor backoff */
void
tsch_queue_backoff_reset(struct tsch_neighbor *n)
{
  n->backoff_window = 0;
  n->backoff_exponent = TSCH_MAC_MIN_BE;
}
/*---------------------------------------------------------------------------*/
/* Increment backoff exponent, pick a new window */
void
tsch_queue_backoff_inc(struct tsch_neighbor *n)
{
  /* Increment exponent */
  n->backoff_exponent = MIN(n->backoff_exponent + 1, TSCH_MAC_MAX_BE);
  /* Pick a window (number of shared slots to skip). Ignore least significant
   * few bits, which, on some embedded implementations of rand (e.g. msp430-libc),
   * are known to have poor pseudo-random properties. */
  //printf("prev BW %u\n",n->backoff_window);
  n->backoff_window = (random_rand() >> 6) % (1 << n->backoff_exponent);
  /* Add one to the window as we will decrement it at the end of the current slot
   * through tsch_queue_update_all_backoff_windows */
  n->backoff_window++;
  //printf("BW %u (nbr %u %x.%lx)\n",n->backoff_window,TSCH_LOG_ID_FROM_LINKADDR(&n->addr),tsch_current_asn.ms1b,tsch_current_asn.ls4b);
}
/*---------------------------------------------------------------------------*/
/* Decrement backoff window for all queues directed at dest_addr */
void
tsch_queue_update_all_backoff_windows(const linkaddr_t *dest_addr)
{
  if(!tsch_is_locked()) {
    int is_broadcast = linkaddr_cmp(dest_addr, &tsch_broadcast_address);
    struct tsch_neighbor *n = list_head(neighbor_list);
    while(n != NULL) {
      if(n->backoff_window != 0 /* Is the queue in backoff state? */
         && ((n->tx_links_count == 0 && is_broadcast)
             || (n->tx_links_count > 0 && linkaddr_cmp(dest_addr, &n->addr)))) {
        n->backoff_window--;
        //printf("BW dec %u (nbr %u %x.%lx)\n",n->backoff_window,TSCH_LOG_ID_FROM_LINKADDR(&n->addr),tsch_current_asn.ms1b,tsch_current_asn.ls4b);
      }
      n = list_item_next(n);
    }
  }
}
/*---------------------------------------------------------------------------*/
/* Initialize TSCH queue module */
void
tsch_queue_init(void)
{
  list_init(neighbor_list);
  memb_init(&neighbor_memb);
  memb_init(&packet_memb);
  /* Add virtual EB and the broadcast neighbors */
  n_eb = tsch_queue_add_nbr(&tsch_eb_address);
  n_broadcast = tsch_queue_add_nbr(&tsch_broadcast_address);

#if PROPOSED
  ctimer_set(&select_N_timer, N_SELECTION_PERIOD*CLOCK_SECOND, select_N, NULL);
#endif
}
/*---------------------------------------------------------------------------*/
