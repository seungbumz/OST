/*
 * Copyright (c) 2015, Swedish Institute of Computer Science.
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
 */
/**
 * \file
 *         Orchestra: a slotframe dedicated to unicast data transmission. Designed for
 *         RPL storing mode only, as this is based on the knowledge of the children (and parent).
 *         If receiver-based:
 *           Nodes listen at a timeslot defined as hash(MAC) % ORCHESTRA_SB_UNICAST_PERIOD
 *           Nodes transmit at: for each nbr in RPL children and RPL preferred parent,
 *                                             hash(nbr.MAC) % ORCHESTRA_SB_UNICAST_PERIOD
 *         If sender-based: the opposite
 *
 * \author Simon Duquennoy <simonduq@sics.se>
 */

#include "contiki.h"
#include "orchestra.h"
#include "net/ipv6/uip-ds6-route.h"
#include "net/packetbuf.h"
#include "net/rpl/rpl-conf.h"
#include "net/rpl/rpl-private.h"

#include "sys/ctimer.h" //JSB:mod1
#include "sys/clock.h" //JSB:mod1
#include "net/mac/tsch/tsch-log.h" //JSB

#include "net/ip/uip-debug.h" //JSB

#include "net/queuebuf.h" //JSB

#if TESLA
  #include "net/mac/tsch/tsch-queue.h"
  #include "net/mac/tsch/tsch-private.h"
  #include "node-id.h"
#endif

#include "lib/list.h"
#include "lib/memb.h"

/*
 * The body of this rule should be compiled only when "nbr_routes" is available,
 * otherwise a link error causes build failure. "nbr_routes" is compiled if
 * UIP_CONF_MAX_ROUTES != 0. See uip-ds6-route.c.
 */
#if UIP_CONF_MAX_ROUTES != 0

#if ORCHESTRA_UNICAST_SENDER_BASED && ORCHESTRA_COLLISION_FREE_HASH
#define UNICAST_SLOT_SHARED_FLAG    ((ORCHESTRA_UNICAST_PERIOD < (ORCHESTRA_MAX_HASH + 1)) ? LINK_OPTION_SHARED : 0)
#else
#define UNICAST_SLOT_SHARED_FLAG      LINK_OPTION_SHARED
#endif

static uint16_t slotframe_handle = 0;
static uint16_t channel_offset = 0;
static struct tsch_slotframe *sf_unicast;
#if TESLA
static struct tsch_slotframe *temp_sf_unicast=NULL;
static struct ctimer rm_temp_sf_timer;
#endif

#if TESLA
  uint16_t my_sf_size = ORCHESTRA_UNICAST_PERIOD;
  uint8_t my_sf_size_version=1;
  uint8_t allow_rm_tx_sf = 0;
  uint8_t forbid_nbr_sf_size_zero=0;

  struct ctimer wait_eack_timer;
  uint8_t is_waiting_eack=0;
  static struct ctimer timer1;
#endif

void change_attr_in_tx_queue(const linkaddr_t * dest, uint8_t is_adjust_tx_sf_size, uint8_t only_first_packet);


#if PROPOSED
void
print_nbr(void)
{
    uip_ds6_nbr_t *nbr = nbr_table_head(ds6_neighbors);
    printf("\n[Neighbors]");
    printf(" r_nbr / my / nbr / num_tx / new_add / my_uninstallable / rx_no_path / my_low_prr\n");
    while(nbr != NULL) {
      uint16_t nbr_id= ID_FROM_IPADDR(&(nbr->ipaddr));

      printf("[ID:%u]",nbr_id);

      printf(" %u / ",is_routing_nbr(nbr));
      
      printf("Tx %u,%u / Rx %u,%u / %u / %u / %u / %u / %u (%u, %u, %u, %u, %u)\n",
        nbr->my_N,nbr->my_t_offset,nbr->nbr_N,nbr->nbr_t_offset,nbr->num_tx,
        nbr->new_add, nbr->my_uninstallable, nbr->rx_no_path , nbr->my_low_prr, 
        nbr->num_tx_mac, nbr->num_tx_succ_mac, nbr->num_consecutive_tx_fail_mac, nbr->consecutive_my_N_inc, 
        nbr->consecutive_new_tx_request);

      nbr = nbr_table_next(ds6_neighbors, nbr);
    }
    printf("\n");
}

void
reset_nbr(const linkaddr_t *addr, uint8_t new_add, uint8_t rx_no_path)
{
  if(addr!=NULL){
    
    uint16_t id= TSCH_LOG_ID_FROM_LINKADDR(addr);

    uip_ds6_nbr_t *nbr = nbr_table_head(ds6_neighbors);
    
    while(nbr != NULL) {

      uint16_t nbr_id= ID_FROM_IPADDR(&(nbr->ipaddr));

      if(id==nbr_id)
      {
        printf("reset_nbr %u\n",nbr_id);

        nbr->my_N=5;
        change_queue_N_update(nbr_id,nbr->my_N);

        nbr->my_t_offset=65535;

        nbr->nbr_N=65535;
        nbr->nbr_t_offset=65535;

        nbr->num_tx=0;

        if(new_add==1)
        {
          nbr->new_add=1;
        }
        else
        {
          nbr->new_add=0;
        }

        nbr->my_uninstallable=0;

        if(rx_no_path==1)
        {
          nbr->rx_no_path=1;
        }
        else
        {
          nbr->rx_no_path=0;
        }

        nbr->my_low_prr=0;
        nbr->num_tx_mac=0;
        nbr->num_tx_succ_mac=0;
        nbr->num_consecutive_tx_fail_mac=0;
        nbr->consecutive_my_N_inc=0;

        nbr->consecutive_new_tx_request=0;

        
        break;
      }

      nbr = nbr_table_next(ds6_neighbors, nbr);

    }

  }

  //print_nbr();
}
#endif

/*---------------------------------------------------------------------------*/

#if PROPOSED
uint16_t
get_tx_sf_handle_from_id(const uint16_t id)
{
 
  return 2*id+1; 
  
}

uint16_t
get_rx_sf_handle_from_id(const uint16_t id)
{
 
  return 2*id+2; 
  
}

uint16_t
get_id_from_tx_sf_handle(const uint16_t handle)
{
 
  return (handle-1)/2; 
  
}

uint16_t
get_id_from_rx_sf_handle(const uint16_t handle)
{
 
  return (handle-2)/2; 
  
}


#endif

/*---------------------------------------------------------------------------*/
#if TESLA

uint16_t
get_tx_sf_handle_from_id(const uint16_t id)
{
 
  return id +3 ; //id=1 --> 4
  
}


uint16_t
get_tx_sf_handle_from_linkaddr(const linkaddr_t *addr)
{
  if(addr==NULL)
  {
    printf("ERROR: NULL addr (get_tx_sf_handle_from_linkaddr)\n");
    return 0;
  }
  else
  {
    uint16_t id=(((addr)->u8[LINKADDR_SIZE - 2]) << 8) | ((addr)->u8[LINKADDR_SIZE - 1]);

    return get_tx_sf_handle_from_id(id);
  }
}


#endif



#if TESLA

static uint16_t
get_node_timeslot(const linkaddr_t *addr, uint16_t size)
{
  if(addr != NULL && size > 0) {
    //printf("Hash %u for %u\n", (ORCHESTRA_LINKADDR_HASH(addr)), addr->u8[LINKADDR_SIZE-1]);
    return ORCHESTRA_LINKADDR_HASH(addr) % size;
  } else {
    return 0xffff;
  }
}
#else

static uint16_t
get_node_timeslot(const linkaddr_t *addr)
{
  if(addr != NULL && ORCHESTRA_UNICAST_PERIOD > 0) {
  
/*    #if !PROPOSED & !TESLA & !USE_6TISCH_MINIMAL
    if(TSCH_LOG_ID_FROM_LINKADDR(addr)==2)
    {
      return 1;
    }
    #endif*/

    return ORCHESTRA_LINKADDR_HASH(addr) % ORCHESTRA_UNICAST_PERIOD;
  } else {
    return 0xffff;
  }
}
#endif

/*---------------------------------------------------------------------------*/
int //JSB: it was static function
neighbor_has_uc_link(const linkaddr_t *linkaddr)
{
  if(linkaddr != NULL && !linkaddr_cmp(linkaddr, &linkaddr_null)) {
    if((orchestra_parent_knows_us || !ORCHESTRA_UNICAST_SENDER_BASED)
       && linkaddr_cmp(&orchestra_parent_linkaddr, linkaddr)) {
      return 1;
    }
    if(nbr_table_get_from_lladdr(nbr_routes, (linkaddr_t *)linkaddr) != NULL) {
      return 1;
    }
  }
  return 0;
}
/*---------------------------------------------------------------------------*/
#if TESLA==0
static void
add_uc_link(const linkaddr_t *linkaddr)
{
  if(linkaddr != NULL) {
    uint16_t timeslot = get_node_timeslot(linkaddr);
    uint8_t link_options = ORCHESTRA_UNICAST_SENDER_BASED ? LINK_OPTION_RX : LINK_OPTION_TX | UNICAST_SLOT_SHARED_FLAG;

    if(timeslot == get_node_timeslot(&linkaddr_node_addr)) {
      /* This is also our timeslot, add necessary flags */
      link_options |= ORCHESTRA_UNICAST_SENDER_BASED ? LINK_OPTION_TX | UNICAST_SLOT_SHARED_FLAG: LINK_OPTION_RX;
    }

    /* Add/update link */
    tsch_schedule_add_link(sf_unicast, link_options, LINK_TYPE_NORMAL, &tsch_broadcast_address,
          timeslot, channel_offset);
  }
}
/*---------------------------------------------------------------------------*/
static void
remove_uc_link(const linkaddr_t *linkaddr)
{
  uint16_t timeslot;
  struct tsch_link *l;

  //printf("remove_uc_link: c1\n");
  if(linkaddr == NULL) {
    #if PRINT_SELECT_1
    printf("remove_uc_link: NULL addr\n");
    #endif
    return;
  }

  timeslot = get_node_timeslot(linkaddr);
  l = tsch_schedule_get_link_by_timeslot(sf_unicast, timeslot);
  if(l == NULL) {
    return;
  }
  /* Does our current parent need this timeslot? */
  if(timeslot == get_node_timeslot(&orchestra_parent_linkaddr)) {
    /* Yes, this timeslot is being used, return */
    return;
  }
  /* Does any other child need this timeslot?
   * (lookup all route next hops) */
  nbr_table_item_t *item = nbr_table_head(nbr_routes);
  while(item != NULL) {
    linkaddr_t *addr = nbr_table_get_lladdr(nbr_routes, item);
    if(timeslot == get_node_timeslot(addr)) {
      /* Yes, this timeslot is being used, return */
      return;
    }
    item = nbr_table_next(nbr_routes, item);
  }

  /* Do we need this timeslot? */
  if(timeslot == get_node_timeslot(&linkaddr_node_addr)) {
    /* This is our link, keep it but update the link options */
    uint8_t link_options = ORCHESTRA_UNICAST_SENDER_BASED ? LINK_OPTION_TX | UNICAST_SLOT_SHARED_FLAG: LINK_OPTION_RX;
    tsch_schedule_add_link(sf_unicast, link_options, LINK_TYPE_NORMAL, &tsch_broadcast_address,
              timeslot, channel_offset);
  } else {

    /* Remove link */
    tsch_schedule_remove_link(sf_unicast, l);
  }
}
/*---------------------------------------------------------------------------*/
/*static void
remove_uc_link_after_expired(void *linkaddr) //After rm_timer expired
{
  remove_uc_link((linkaddr_t *) linkaddr);

  struct rm_neighbor *n= get_rm_nbr((linkaddr_t *) linkaddr);

  remove_rm_nbr(n);

}*/
#endif //TESLA==0 
/*---------------------------------------------------------------------------*/
#if TESLA
static void 
add_tx_sf(const linkaddr_t *linkaddr)
{
  uint16_t new_sf_handle;
  struct tsch_slotframe * new_sf;
  uip_ds6_nbr_t * nbr;
  uint16_t new_sf_size; 

  uint8_t link_options= LINK_OPTION_TX | UNICAST_SLOT_SHARED_FLAG; // This is slotframe for only Tx
  



  if(linkaddr != NULL) {
    new_sf_handle= get_tx_sf_handle_from_linkaddr(linkaddr);

    nbr= uip_ds6_nbr_ll_lookup((uip_lladdr_t*)linkaddr);
    if(nbr==NULL)
    {
      #if PRINT_SELECT_1
      printf("add_tx_sf: 1st association after EB\\n"); //It should be printed in 1st association.
      #endif
      return;
     
    }
    else{
      new_sf_size = nbr->sf_size;
    }

    uint16_t timeslot = get_node_timeslot(linkaddr,new_sf_size);

    new_sf=tsch_schedule_add_slotframe(new_sf_handle, new_sf_size);

#if SPECIAL_OFFSET

    if (new_sf_size==0)
    {
      printf("ERROR: new_sf_size=0\n");
    }
    else if (((TSCH_LOG_ID_FROM_LINKADDR(linkaddr))/2)%2==1)
    {
      #if PRINT_SELECT_1
      printf("add_tx_sf: special channel offset %u (%u)\n",channel_offset+2,(TSCH_LOG_ID_FROM_LINKADDR(linkaddr)));
      #endif

      tsch_schedule_add_link(new_sf, link_options, LINK_TYPE_NORMAL, &tsch_broadcast_address,
              timeslot, channel_offset+2); //NOTE1
    }
    /*else if (((TSCH_LOG_ID_FROM_LINKADDR(linkaddr))/2)%3==2)
    {
      #if PRINT_SELECT_1
      printf("add_tx_sf: special channel offset %u (%u)\n",channel_offset+1,(TSCH_LOG_ID_FROM_LINKADDR(linkaddr)));
      #endif

      tsch_schedule_add_link(new_sf, link_options, LINK_TYPE_NORMAL, &tsch_broadcast_address,
              timeslot, channel_offset+1); //NOTE1
    }*/
    else
    {
      tsch_schedule_add_link(new_sf, link_options, LINK_TYPE_NORMAL, &tsch_broadcast_address,
              timeslot, channel_offset);
    }
#else
    if (new_sf_size==0)
    {
      printf("ERROR: new_sf_size=0\n");
    }
    else
    {
      tsch_schedule_add_link(new_sf, link_options, LINK_TYPE_NORMAL, &tsch_broadcast_address,
              timeslot, channel_offset);
    }
#endif    
  }
  else{
    #if PRINT_SELECT_1
    printf("PROP: NULL addr(add_tx_sf)\n");
    #endif
  }
  
}
/*---------------------------------------------------------------------------*/
void 
remove_tx_sf(const linkaddr_t *linkaddr)
{
  uint16_t rm_sf_handle;
  struct tsch_slotframe * rm_sf;

  if(linkaddr != NULL) {
    rm_sf_handle = get_tx_sf_handle_from_linkaddr(linkaddr);
    rm_sf = tsch_schedule_get_slotframe_by_handle(rm_sf_handle);

    if(rm_sf==NULL)
    {
      #if PRINT_SELECT_1
      printf("remove_tx_sf: No prev_tx_sf\n");
      #endif
      return;
    }

    /* Does our current parent need this slotframe? */
    if(rm_sf_handle == get_tx_sf_handle_from_linkaddr(&orchestra_parent_linkaddr)) {
      /* Yes, this slotframe is being used, return */
      if(allow_rm_tx_sf==0)
      {
        #if PRINT_SELECT_1
        printf("PROP: No remove_tx_sf, still used for Parent\n");
        #endif
        return;
      }
    }


    /* Does any other child need this slotframe?
    * (lookup all route next hops) */
    nbr_table_item_t *item = nbr_table_head(nbr_routes);
    while(item != NULL) {
      linkaddr_t *addr = nbr_table_get_lladdr(nbr_routes, item);
      if(rm_sf_handle == get_tx_sf_handle_from_linkaddr(addr)) {
        /* Yes, this slotframe is being used, return */
        if(allow_rm_tx_sf==0)
        {
          #if PRINT_SELECT_1
          printf("PROP: No remove_tx_sf, still used for child\n");
          #endif
          return;
        }
      }
      item = nbr_table_next(nbr_routes, item);
    }


    if(rm_sf_handle == get_tx_sf_handle_from_linkaddr(&linkaddr_node_addr)) {
      printf("ERROR: Why is tx sf handle for me in my tx_sf list?\n");
      return;
    }

    /* Do we need this slotframe */
     if(0<=rm_sf_handle && rm_sf_handle<=2) {  //NOTE1
      printf("ERROR: No remove basic slotframes\n");
      return;
    }


    
      uint16_t rm_id=TSCH_LOG_ID_FROM_LINKADDR(linkaddr);
      uip_ds6_nbr_t *nbr = nbr_table_head(ds6_neighbors);
      
      while(nbr != NULL) {
        uint16_t nbr_id=((nbr->ipaddr.u8[14]) << 8) | (nbr->ipaddr.u8[15]);
        if(rm_id==nbr_id)
        {
          if(forbid_nbr_sf_size_zero==0){
          /*Before remove tx_sf, makes sf_size=0 in dst-nbr*/
          #if PRINT_SELECT_1
          printf("remove_tx_sf: nbr sf_size set 0\n");
          #endif
            nbr->sf_size=0;
            nbr->sf_size_version=0;
          }
#if PRINT_SELECT
          printf("num_I_tx set 0 (%u)\n",nbr_id);    
#endif    
          nbr->num_I_tx=0;

          struct tsch_neighbor *n=tsch_queue_get_nbr_from_id(nbr_id);
          check_queued_packet(n); //update num_I_tx written in queued packet


          break;
        }
        nbr = nbr_table_next(ds6_neighbors, nbr);
      }
    

    tsch_schedule_remove_slotframe(rm_sf);
  }

  else{
    #if PRINT_SELECT_1
    printf("PROP: NULL addr (remove_tx_sf)\n");
    #endif
  }
  
}
/*---------------------------------------------------------------------------*/
/*static void 
remove_tx_sf_after_expired(void *linkaddr) //After rm_timer expired
{

  remove_tx_sf((linkaddr_t *) linkaddr);

  struct rm_neighbor *n= get_rm_nbr((linkaddr_t *) linkaddr);

  remove_rm_nbr(n);


}*/
/*---------------------------------------------------------------------------*/
#endif //TESLA
/*---------------------------------------------------------------------------*/
static void
child_added(const linkaddr_t *linkaddr)  //child means next-hop
{
#if TESLA
  add_tx_sf(linkaddr); //sf_size was in DAO
#else

#if PROPOSED
  reset_nbr(linkaddr,1,0);
#endif 

  add_uc_link(linkaddr);
#endif
}
/*---------------------------------------------------------------------------*/
static void
child_removed(const linkaddr_t *linkaddr) //child means next-hop
{

struct tsch_neighbor * nbr = tsch_queue_get_nbr(linkaddr);
    //uint8_t locked= tsch_is_locked();
#if TESLA

    if(tsch_queue_is_empty(nbr) || nbr==NULL)
    {
      #if PRINT_SELECT_1
      printf("child_removed: now\n");
      #endif
      remove_tx_sf(linkaddr);
    }
    else
    {
    
      //add_rm_nbr(linkaddr);
      if(bootstrap_period)
      {
        #if PRINT_SELECT_1
        printf("child_removed: Queued packets, Flush (bootstrap)\n");
        #endif
        tsch_queue_flush_nbr_queue(nbr);
      }
      else
      {
        #if PRINT_SELECT_1
        printf("child_removed: Queued packets, Use shared slot\n");
        #endif
        change_attr_in_tx_queue(linkaddr, 0, 0);
      }  
      
      
      remove_tx_sf(linkaddr);
    }

#else
    if(tsch_queue_is_empty(nbr) || nbr==NULL)
    {
      #if PRINT_SELECT_1
      printf("child_removed: now\n");
      #endif
      remove_uc_link(linkaddr);
    }
    else
    {
      if(bootstrap_period)
      {
        #if PRINT_SELECT_1
        printf("child_removed: Queued packets, Flush (bootstrap)\n");
        #endif
        tsch_queue_flush_nbr_queue(nbr);
      }
      else
      {
        #if PRINT_SELECT_1
        printf("child_removed: Queued packets, Use shared slot\n");
        #endif
        change_attr_in_tx_queue(linkaddr, 0, 0);
      }  
      remove_uc_link(linkaddr);
    }

#if PROPOSED
    uip_ds6_nbr_t * ds6_nbr= uip_ds6_nbr_ll_lookup((uip_lladdr_t*)linkaddr);  
    if(ds6_nbr!=NULL)
    { 
      //If still routing nbr? do not remove
      if(is_routing_nbr(ds6_nbr)==0) //Added this condition for the preparation of routing loop
      {
        reset_nbr(linkaddr,0,0);

        //it was deleted possibly already when no-path DAO rx
        printf("child_removed: remove_tx & remove_rx\n");
        if(linkaddr!=NULL){
          remove_tx(TSCH_LOG_ID_FROM_LINKADDR(linkaddr));
          remove_rx(TSCH_LOG_ID_FROM_LINKADDR(linkaddr));
          //tsch_schedule_print_proposed();
        }
      }
      else
      {
        printf("Do not remove this child. Still needed\n");
      }
    }
#endif 
    

#endif

}

/*---------------------------------------------------------------------------*/
static int
select_packet(uint16_t *slotframe, uint16_t *timeslot)
{
  /* Select data packets we have a unicast link to */
  const linkaddr_t *dest = packetbuf_addr(PACKETBUF_ADDR_RECEIVER);
  if(packetbuf_attr(PACKETBUF_ATTR_FRAME_TYPE) == FRAME802154_DATAFRAME
     && neighbor_has_uc_link(dest)) {
    //printf("Select packet\n");

#if PROPOSED

  uint16_t dest_id=TSCH_LOG_ID_FROM_LINKADDR(dest);
  uint16_t tx_sf_handle= get_tx_sf_handle_from_id(dest_id);
  struct tsch_slotframe * tx_sf= tsch_schedule_get_slotframe_by_handle(tx_sf_handle);

#endif

#if TESLA
    if(l_l_ts==1 && linkaddr_cmp(&orchestra_parent_linkaddr, dest))
    {
      #if PRINT_SELECT_1
      printf("Select_packet: L_L_TS -> Use shared slot\n");
      #endif
      return 0; 
    }
#endif

    if(slotframe != NULL) {
#if TESLA
      
      uint16_t tx_sf_handle =get_tx_sf_handle_from_linkaddr(dest);
 
      if(!tsch_schedule_get_slotframe_by_handle(tx_sf_handle))
      {
        #if PRINT_SELECT_1
        printf("Select_packet: No tx_sf -> Use shared slot\n"); //DAO
        #endif
        return 0;
      }

      *slotframe =tx_sf_handle; 
#else

#if PROPOSED
      if(tx_sf!=NULL)
      {
        //printf("Select_packet: tx_sf exist\n");

        *slotframe=tx_sf_handle;
      }
      else
      {
        printf("Select_packet: Use RB\n");
        *slotframe = slotframe_handle;
      }
#else     
      *slotframe = slotframe_handle;
#endif
#endif      
    }


    if(timeslot != NULL) {
#if TESLA

      uip_ds6_nbr_t * nbr = uip_ds6_nbr_ll_lookup((uip_lladdr_t*)dest);
      if(nbr==NULL)
      {
        #if PRINT_SELECT_1
        printf("Select_packet: No nbr -> Use shared slot\n");
        #endif
        return 0;
        //new_sf_size = ORCHESTRA_UNICAST_PERIOD;
       
      }
      else{
        if(nbr->sf_size==0)
        {
          #if PRINT_SELECT_1
           printf("Select_packet: sf_size=0 -> Use shared slot\n");
           #endif
          return 0;
        }
        else{

          *timeslot= get_node_timeslot(dest, nbr->sf_size);
        }
      }

#else

#if PROPOSED


      if(tx_sf!=NULL)
      {
        struct tsch_link *l = list_head(tx_sf->links_list);
        /* Loop over all items. Assume there is max one link per timeslot */
        while(l != NULL) {
          if(l->slotframe_handle == tx_sf_handle) {
            break;
          }
          else{
            printf("ERROR: Weird slotframe_handle for Tx link\n");
          }

          l = list_item_next(l);
        }

        *timeslot= l->timeslot;
      }
      else
      {
        *timeslot = ORCHESTRA_UNICAST_SENDER_BASED ? get_node_timeslot(&linkaddr_node_addr) : get_node_timeslot(dest);
      }

#else

      *timeslot = ORCHESTRA_UNICAST_SENDER_BASED ? get_node_timeslot(&linkaddr_node_addr) : get_node_timeslot(dest);
#endif
#endif

    }
    return 1;
  }


  return 0;
}
/*---------------------------------------------------------------------------*/
#if TESLA
static void resend_dao(void* ptr)
{
  if(is_waiting_eack==0)
  {
    printf("ERROR: resend_dao: is_waiting_eack==0\n");
  }
  else
  {
    #if PRINT_SELECT_1
    printf("EACK: resend_dao\n");
    #endif
    inform_sf_size_dao();

  }
}

/*---------------------------------------------------------------------------*/
static void nnt_nir_set_zero (void * nbr)
{
  uip_ds6_nbr_t *n= (uip_ds6_nbr_t*) nbr;
  if(n!=NULL)
  {
    #if PRINT_SELECT
     printf("num_nbr_tx/num_I_rx set 0, after sending no-path DAO\n");
    #endif
     n->num_nbr_tx=0;
     n->num_I_rx=0;
  }
}
#endif

/*---------------------------------------------------------------------------*/

static void
new_time_source(const struct tsch_neighbor *old, const struct tsch_neighbor *new)
{
  if(new != old) {
    const linkaddr_t *old_addr = old != NULL ? &old->addr : NULL;
    const linkaddr_t *new_addr = new != NULL ? &new->addr : NULL;
    if(new_addr != NULL) {
      linkaddr_copy(&orchestra_parent_linkaddr, new_addr);
    } else {
      linkaddr_copy(&orchestra_parent_linkaddr, &linkaddr_null);
    }

    //uint8_t locked= tsch_is_locked();

#if TESLA
    if(old_addr!=NULL){
      //ctimer_set(&rm_timer, 5 * CLOCK_SECOND, remove_tx_sf_void, (void*)ptr_temp_old_addr);
      
      uint16_t rm_id= TSCH_LOG_ID_FROM_LINKADDR(old_addr);

      uip_ds6_nbr_t *nbr = nbr_table_head(ds6_neighbors);
      
      while(nbr != NULL) {

        uint16_t nbr_id=((nbr->ipaddr.u8[14]) << 8) | (nbr->ipaddr.u8[15]);

        if(rm_id==nbr_id)
        {
          #if PRINT_SELECT_1
          printf("nbr all set 0 from old parent\n");
          #endif
          nbr->num_I_tx=0;

          nbr->num_nbr_tx_offset=0;

          ctimer_set(&timer1, 3*CLOCK_SECOND, nnt_nir_set_zero, (void*) nbr);
          //nbr->num_nbr_tx=0;
          //nbr->num_I_rx=0;

          break;
        }

        nbr = nbr_table_next(ds6_neighbors, nbr);

      }
  
   

      if(tsch_queue_is_empty(old) || old==NULL)
      {
        #if PRINT_SELECT_1
        printf("remove_old_parent: now\n");
        #endif
        remove_tx_sf(old_addr); //No-path DAO can be sent in shared slots
      }
      else
      {

        if(bootstrap_period)
        {
          #if PRINT_SELECT_1
          printf("remove_old_parent: Queued packets, Flush (bootstrap)\n");
          #endif
          tsch_queue_flush_nbr_queue(old);
        }
        else
        {
          #if PRINT_SELECT_1
          printf("remove_old_parent: Queued packets, Use shared slot\n");
          #endif
          change_attr_in_tx_queue(old_addr, 0, 0);
        }  

        remove_tx_sf(old_addr);
      }

      /*
      #if PRINT_SELECT_1
      printf("Wait EACK\n");
      #endif

      is_waiting_eack=1;
      clock_time_t max_waiting_time= RPL_DAO_DELAY*3/2 + 5*CLOCK_SECOND; 
      ctimer_set(&wait_eack_timer, max_waiting_time, resend_dao, NULL);
      //add_tx_sf(new_addr); //Temporary
      */
    }
    else  //1st association after EB, not yet joined RPL network
    {
      #if PRINT_SELECT_1
      printf("new_time_source: 2. 1st association\n");
      #endif
      //add_tx_sf(new_addr);
    }

#else

    /**************************JSB:mod1*****************************/
    //uint32_t rm_msec= (1<< TSCH_MAC_MAX_BE)*ORCHESTRA_UNICAST_PERIOD*TSCH_CONF_DEFAULT_TIMESLOT_LENGTH/1000;
    //ctimer_set(&rm_timer, rm_msec/1000 * CLOCK_SECOND, remove_uc_link_void, (void*)old_addr);
    //ctimer_set(&rm_timer, 5 * CLOCK_SECOND, remove_uc_link_void, (void*)ptr_temp_old_addr);
    //printf("Prev uc_link will be removed after %lu ms\n",rm_msec);
    //remove_uc_link(old_addr);


    if(old_addr!=NULL){
      if(tsch_queue_is_empty(old) || old==NULL)
      {
        #if PRINT_SELECT_1
        printf("remove_old_parent: now\n");
        #endif
        remove_uc_link(old_addr); //No-path DAO can be sent in shared slots
      }
      else
      {
        if(bootstrap_period)
        {
          #if PRINT_SELECT_1
          printf("remove_old_parent: Queued packets, Flush (bootstrap)\n");
          #endif
          tsch_queue_flush_nbr_queue(old);
        }
        else
        {
          #if PRINT_SELECT_1
          printf("remove_old_parent: Queued packets, Use shared slot\n");
          #endif
          change_attr_in_tx_queue(old_addr, 0, 0);
        }  

        remove_uc_link(old_addr);
      }
    }
    /**************************JSB:end*****************************/

#if PROPOSED
    reset_nbr(old_addr,0,0);
    reset_nbr(new_addr,1,0);

    printf("new_time_source: remove_tx & remove_rx\n");
    if(old_addr!=NULL){
      remove_tx(TSCH_LOG_ID_FROM_LINKADDR(old_addr));
      remove_rx(TSCH_LOG_ID_FROM_LINKADDR(old_addr));
      //tsch_schedule_print_proposed();
    }
#endif 

    add_uc_link(new_addr);
#endif
  }
}
/*---------------------------------------------------------------------------*/
#if TESLA
static void
add_rx_link(uint16_t sf_size) //Only for my rx slotframe
{
  uint16_t timeslot = get_node_timeslot(&linkaddr_node_addr, sf_size);

#if SPECIAL_OFFSET
  if ((node_id/2)%2==1)
  {
    #if PRINT_SELECT_1 
    printf("add_rx_sf: special channel offset %u\n",channel_offset+2); //3
    #endif
    tsch_schedule_add_link(sf_unicast, LINK_OPTION_RX,
          LINK_TYPE_NORMAL, &tsch_broadcast_address,
          timeslot, channel_offset+2); //NOTE1
  }
  /*else if ((node_id/2)%3==2)
  {
    #if PRINT_SELECT_1 
    printf("add_rx_sf: special channel offset %u\n",channel_offset+1); //2 ,shared with shard sf
    #endif
    tsch_schedule_add_link(sf_unicast, LINK_OPTION_RX,
          LINK_TYPE_NORMAL, &tsch_broadcast_address,
          timeslot, channel_offset+1); //NOTE1
  }*/
  else
  {
   tsch_schedule_add_link(sf_unicast, LINK_OPTION_RX, 
          LINK_TYPE_NORMAL, &tsch_broadcast_address,
          timeslot, channel_offset);  //2
  }
#else
  tsch_schedule_add_link(sf_unicast, LINK_OPTION_RX, 
          LINK_TYPE_NORMAL, &tsch_broadcast_address,
          timeslot, channel_offset);  
#endif

}
#endif
/*---------------------------------------------------------------------------*/
static void
init(uint16_t sf_handle)
{
  slotframe_handle = sf_handle;
  channel_offset = sf_handle;
  //channel_offset=2;
  /* Slotframe for unicast transmissions */
  sf_unicast = tsch_schedule_add_slotframe(slotframe_handle, ORCHESTRA_UNICAST_PERIOD);
  
#if TESLA //NOTE1
  add_rx_link(ORCHESTRA_UNICAST_PERIOD);  
#else  
  uint16_t timeslot = get_node_timeslot(&linkaddr_node_addr);
  tsch_schedule_add_link(sf_unicast,
            ORCHESTRA_UNICAST_SENDER_BASED ? LINK_OPTION_TX | UNICAST_SLOT_SHARED_FLAG: LINK_OPTION_RX,
            LINK_TYPE_NORMAL, &tsch_broadcast_address,
            timeslot, channel_offset);
#endif

}
/*---------------------------------------------------------------------------*/
#if USE_6TISCH_MINIMAL
#else
void 
change_attr_in_tx_queue(const linkaddr_t * dest, uint8_t is_adjust_tx_sf_size, uint8_t only_first_packet)
{
    uint16_t timeslot;
    uint16_t sf_handle;
    int16_t get_index=-100;
    int16_t put_index=-200;
    uint8_t num_elements=0;

    


    if(is_adjust_tx_sf_size==0)
    {
      //Use shared slot
      timeslot=0;  //NOTE1
      sf_handle=2;

    }
    else{ 
#if TESLA
      uip_ds6_nbr_t * nbr = uip_ds6_nbr_ll_lookup((uip_lladdr_t*)dest);

      if(nbr==NULL)
      {
        printf("ERROR: No nbr (change_attr_in_tx_queue)\n");
        return ;
       
      }
      else{

        timeslot= get_node_timeslot(dest, nbr->sf_size);
        sf_handle= get_tx_sf_handle_from_linkaddr(dest);
      }
#else
      printf("ERROR: is_adjust_tx_sf_size cannot be 0 in Orchestra\n");
#endif
    }


  if(!tsch_is_locked()) {
    struct tsch_neighbor *n1 = tsch_queue_get_nbr(dest);

    
    if(n1==NULL)
    {
      #if PRINT_SELECT_1
      printf("change_attr_in_tx_queue: My child, Tx queue for him is empty, so n1==NULL\n");
      #endif
      //thus, we don't have packets to change
      return ;
    }

    tsch_queue_backoff_reset(n1);

    get_index = ringbufindex_peek_get(&n1->tx_ringbuf);
    put_index = ringbufindex_peek_put(&n1->tx_ringbuf);
    num_elements= ringbufindex_elements(&n1->tx_ringbuf);
    #if PRINT_SELECT_1
    printf("get_index: %d, put_index: %d, %u\n",get_index,put_index,num_elements);
    #endif


    if(only_first_packet==1 && num_elements>0)
    {
        #if PRINT_SELECT_1
        //printf("only first packet\n");
        #endif
        num_elements=1;
      
    }

    

    uint8_t j;
    for(j=get_index;j<get_index+num_elements;j++)
    {
      int16_t index;

      if(j>=ringbufindex_size(&n1->tx_ringbuf)) //16
      {
        index= j-ringbufindex_size(&n1->tx_ringbuf);
      }
      else
      {
        index=j;  
      }
      set_queuebuf_attr(n1->tx_array[index]->qb, PACKETBUF_ATTR_TSCH_SLOTFRAME, sf_handle);
      set_queuebuf_attr(n1->tx_array[index]->qb, PACKETBUF_ATTR_TSCH_TIMESLOT, timeslot);
      #if PRINT_SELECT_1
      //printf("index: %u, %u %u\n",j,queuebuf_attr(n1->tx_array[index]->qb,PACKETBUF_ATTR_TSCH_SLOTFRAME),queuebuf_attr(n1->tx_array[index]->qb,PACKETBUF_ATTR_TSCH_TIMESLOT));
      #endif
    }
  }
  else
  {
    printf("ERROR: lock fail (change_attr_in_tx_queue)\n");
  }

}
#endif
/*---------------------------------------------------------------------------*/
#if TESLA
void 
tsch_schedule_remove_slotframe_void(void * ptr)
{
  
  //tsch_schedule_remove_slotframe((struct tsch_slotframe *)slotframe);
  tsch_schedule_remove_slotframe(temp_sf_unicast);
  temp_sf_unicast=NULL;
  //printf("c2\n");
}
/*---------------------------------------------------------------------------*/
void 
adjust_sf_size(void)
{
  //my_sf_size is update already
  if(temp_sf_unicast!=NULL)
  {
    printf("ERROR: temp_sf_unicast exists\n");
    return ;
  }

  if(tsch_schedule_change_slotframe_link_handle(sf_unicast,TEMP_SF_HANDLE)==0)
  {
    printf("ERROR: Handle change fail in temp sf\n");
  }

  temp_sf_unicast=sf_unicast;

  //printf("c1 %u\n",temp_sf_unicast==NULL);

  //tsch_schedule_remove_slotframe_void((void*)temp_sf_unicast);
  ctimer_set(&rm_temp_sf_timer, RETAIN_RX_SF_DURATION_BEFORE_RM , tsch_schedule_remove_slotframe_void, NULL);



  sf_unicast = tsch_schedule_add_slotframe(slotframe_handle, my_sf_size);

  add_rx_link(my_sf_size); 

}
/*---------------------------------------------------------------------------*/
void adjust_tx_sf_size(const uip_ipaddr_t* from)
{
  const uip_lladdr_t* target = uip_ds6_nbr_lladdr_from_ipaddr(from);

  if(target==NULL)
  {
    printf("ERROR: No nbr (adjust_tx_sf_size)\n");
    return;
  }


  /* My parent? */
  if(linkaddr_cmp((linkaddr_t*) target,&orchestra_parent_linkaddr)==1){
    #if PRINT_SELECT_1
    printf("adjust_tx_sf_size: For parent\n");
    #endif
    goto aaa;
  }

  if(first_dio_rx==1 && node_id!=ROOT_ID)
  {
    #if PRINT_SELECT_1
    printf("adjust_tx_sf_size: For parent (1st dio rx)\n");
    #endif
    goto aaa;
  }

  /* My child? */
  nbr_table_item_t *item = nbr_table_head(nbr_routes);

  while(item != NULL) {
    linkaddr_t *addr = nbr_table_get_lladdr(nbr_routes, item);
    if(linkaddr_cmp((linkaddr_t*) target,addr)==1) {
      /* Yes, this timeslot is being used, return */
      #if PRINT_SELECT_1
      printf("adjust_tx_sf_size: For child\n");
      #endif
      goto aaa;
    }
    item = nbr_table_next(nbr_routes, item);
  }



  return;


  aaa:

  forbid_nbr_sf_size_zero=1; //temporary forbid
  allow_rm_tx_sf=1;  //temporary allow
  remove_tx_sf((linkaddr_t *)target);
  forbid_nbr_sf_size_zero=0;
  allow_rm_tx_sf=0;


  add_tx_sf((linkaddr_t *)target);


  change_attr_in_tx_queue((linkaddr_t *)target, 1, 0);


}
#endif
/*---------------------------------------------------------------------------*/
struct orchestra_rule unicast_per_neighbor_rpl_storing = {
  init,
  new_time_source,
  select_packet,
  child_added,
  child_removed,
#if TESLA
  adjust_sf_size,
  adjust_tx_sf_size,
#endif
};

#endif /* UIP_MAX_ROUTES */
