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
 *         Log functions for TSCH, meant for logging from interrupt
 *         during a timeslot operation. Saves ASN, slot and link information
 *         and adds the log to a ringbuf for later printout.
 * \author
 *         Simon Duquennoy <simonduq@sics.se>
 *
 */

#include "contiki.h"
#include <stdio.h>
//#include "net/mac/mac.h" //JSB
//#include "orchestra.h"//JSB
#include "net/mac/tsch/tsch.h"
#include "net/mac/tsch/tsch-queue.h"
#include "net/mac/tsch/tsch-private.h"
#include "net/mac/tsch/tsch-log.h"
#include "net/mac/tsch/tsch-packet.h"
#include "net/mac/tsch/tsch-schedule.h"
#include "net/mac/tsch/tsch-slot-operation.h"
#include "lib/ringbufindex.h"
#include "orchestra.h"

#if TESLA
  
  #include "net/rpl/rpl-private.h"
  #include "node-id.h"
#endif

#if TESLA

  #include "net/queuebuf.h"

#endif

#if TSCH_LOG_LEVEL >= 1
#define DEBUG DEBUG_NONE
#else /* TSCH_LOG_LEVEL */
#define DEBUG DEBUG_NONE
#endif /* TSCH_LOG_LEVEL */
#include "net/net-debug.h"

#if TSCH_LOG_LEVEL >= 2 /* Skip this file for log levels 0 or 1 */

PROCESS_NAME(tsch_pending_events_process);

/* Check if TSCH_LOG_QUEUE_LEN is a power of two */
#if (TSCH_LOG_QUEUE_LEN & (TSCH_LOG_QUEUE_LEN - 1)) != 0
#error TSCH_LOG_QUEUE_LEN must be power of two
#endif
static struct ringbufindex log_ringbuf;
static struct tsch_log_t log_array[TSCH_LOG_QUEUE_LEN];
static int log_dropped = 0;

//static uint16_t num_mac_tx_ok=0; //JSB
//static uint16_t num_mac_tx_noack=0; //JSB

//static uint16_t num_mac_tx_ok_ss=0; //JSB ss:shared slot
//static uint16_t num_mac_tx_noack_ss=0; //JSB

//static struct ctimer reset_num_tx_timer; //JSB

#if TESLA
  uint8_t l_l_ts;
#endif

/*---------------------------------------------------------------------------*/
#if TESLA
static void
check_sf_size_in_eack(int dest_id, uint16_t ack_sf_size, uint8_t ack_sf_size_version)
{
  struct tsch_neighbor *time_src = tsch_queue_get_time_source();
  uint16_t time_src_ID;

  if(time_src==NULL)
  {
    if(node_id!=ROOT_ID)
      printf("ERROR: check_sf_size_in_eack: No time source\n");

    return ;
  }
  else
  {
    time_src_ID=TSCH_LOG_ID_FROM_LINKADDR(&time_src->addr);
  }

  if(dest_id==time_src_ID) //Only if time source
  {
    linkaddr_t * addr;
    uip_ds6_nbr_t *nbr = nbr_table_head(ds6_neighbors);

    while(nbr !=NULL)
    {  
      addr=(linkaddr_t *)uip_ds6_nbr_get_ll(nbr);

      if(dest_id==TSCH_LOG_ID_FROM_LINKADDR(addr))
      {
        if(SEQNO_LT(nbr->sf_size_version,ack_sf_size_version))
        {

          if(nbr->sf_size_version!=0 && ack_sf_size_version-nbr->sf_size_version>2)
          {
            printf("ERROR: Weird sf version\n");
          }

          
          #if PRINT_SELECT_1
          printf("TSCH: EACK from %u, sf_size changed",dest_id);


          printf(" %u(%u)->%u(%u)\n", nbr->sf_size , nbr->sf_size_version, ack_sf_size, ack_sf_size_version); 
          #endif
          nbr->sf_size= ack_sf_size;

          nbr->sf_size_version= ack_sf_size_version;

          

          orchestra_adjust_tx_sf_size(&(nbr->ipaddr));

        }


        if(is_waiting_eack==1)
        {
          is_waiting_eack=0;
          ctimer_stop(&wait_eack_timer);
          #if PRINT_SELECT_1
          printf("Rxed waiting EACK\n");
          #endif
        
        }

        if(is_waiting_eack2==1)
        {
          is_waiting_eack2=0;
          ctimer_stop(&wait_eack_timer2);
          #if PRINT_SELECT_1
          printf("Rxed waiting EACK2\n");
          #endif
        
        }

        if(l_l_ts==1) //Until now, I used shared slot
        {
          #if PRINT_SELECT_1
          printf("Rx EACK: l_l_ts=0\n");
          #endif
          l_l_ts=0;

        }
        break;
      }
      nbr = nbr_table_next(ds6_neighbors, nbr);
    }

    if(nbr==NULL)
    {
        #if PRINT_SELECT_1
        printf("check_sf_size_in_eack: 1. 1st association\n");
        #endif
        //When joining RPL network, sf_size will be updated.
    }
  }

}

#endif    
/*---------------------------------------------------------------------------*/
/* Process pending log messages */
void
tsch_log_process_pending(void)
{
  static int last_log_dropped = 0;

  int16_t log_index;
  /* Loop on accessing (without removing) a pending input packet */
  if(log_dropped != last_log_dropped) {
    printf("TSCH:! logs dropped %u\n", log_dropped);
    last_log_dropped = log_dropped;
  }
  while((log_index = ringbufindex_peek_get(&log_ringbuf)) != -1) {
    struct tsch_log_t *log = &log_array[log_index];
    if(log->link == NULL) {
      printf("TSCH: {asn-%x.%lx link-NULL} ", log->asn.ms1b, log->asn.ls4b);
    } else {
      struct tsch_slotframe *sf = tsch_schedule_get_slotframe_by_handle(log->link->slotframe_handle);
      
      //printf("TSCH: {asn-%x.%lx link-%u-%u-%u-%u ch-%u} ",
      //       log->asn.ms1b, log->asn.ls4b,
      //       log->link->slotframe_handle, sf ? sf->size.val : 0, log->link->timeslot, log->link->channel_offset,
      //       tsch_calculate_channel(&log->asn, log->link->channel_offset));
      //simplified
      printf("{%x.%lx %u-%u-%u-%u} ", log->asn.ms1b, log->asn.ls4b, 
        log->link->slotframe_handle, sf ? sf->size.val : 0, log->link->timeslot, log->link->channel_offset);

    }
    switch(log->type) {
      case tsch_log_tx:
       
        //printf("%s-%u-%u %u tx %d, st %d-%d",
        //    log->tx.dest == 0 ? "bc" : "uc", log->tx.is_data, log->tx.sec_level,
        //        log->tx.datalen,
        //        log->tx.dest,
        //        log->tx.mac_tx_status, log->tx.num_tx);

        printf("%s tx %d, st %d-%d\n",
            log->tx.dest == 0 ? "bc" : "uc",
                log->tx.dest,
                log->tx.mac_tx_status, log->tx.num_tx);
        


        if(log->tx.drift_used) {
          
          //printf(", dr %d", log->tx.drift);
          
        }

        //printf("\n");

#if PROPOSED

         if(log->tx.dest!=0){ //unicast
          if(log->link->slotframe_handle>=3 && log->link->slotframe_handle < SSQ_SCHEDULE_HANDLE_OFFSET){ //tx_sf

            uip_ds6_nbr_t *nbr = nbr_table_head(ds6_neighbors);
        
            while(nbr != NULL) {
              uint16_t nbr_id=((nbr->ipaddr.u8[14]) << 8) | (nbr->ipaddr.u8[15]);
              if((log->tx.dest)==nbr_id)
              {
                nbr->num_tx_mac++;
                //printf("num_tx_mac++ %u (nbr %u)\n",nbr->num_tx_mac ,nbr_id);

                if(log->tx.mac_tx_status==MAC_TX_OK)
                {
                  nbr->num_tx_succ_mac++;
                  nbr->num_consecutive_tx_fail_mac=0;
                  //printf("num_tx_succ_mac++ %u (nbr %u)\n", nbr->num_tx_succ_mac, nbr_id);
                }
                else{
                  nbr->num_consecutive_tx_fail_mac++;
                }
   
                uint16_t prr = 100*nbr->num_tx_succ_mac/nbr->num_tx_mac;
                //printf("prr %u\n",prr);
                if(prr<=PRR_THRES_TX_CHANGE && nbr->num_tx_mac >= NUM_TX_MAC_THRES_TX_CHANGE)
                {
                  //printf("Low PRR (nbr %u)\n", nbr_id);
                  nbr->my_low_prr=1;
                  change_queue_N_update(nbr_id, nbr->my_N + INC_N_NEW_TX_REQUEST);
                }

                if(nbr->num_consecutive_tx_fail_mac >= NUM_TX_FAIL_THRES)
                {
                  struct tsch_neighbor * n = tsch_queue_get_nbr_from_id(nbr_id);
                  if(n!=NULL)
                  {
                    if(!tsch_queue_is_empty(n))
                    {
                      if(neighbor_has_uc_link(&(n->addr)))
                      {
                        printf("Csct Tx fail -> Use RB %u\n",ringbufindex_elements(&n->tx_ringbuf));
                        change_queue_select_packet(nbr_id, 1, nbr_id % ORCHESTRA_CONF_UNICAST_PERIOD); //Use RB
                      }
                      else
                      {
                        printf("Csct Tx fail -> Use shared slot %u\n",ringbufindex_elements(&n->tx_ringbuf));
                        change_queue_select_packet(nbr_id, 2, 0); //Use shared slot
                      }
                    }
                  }
                }

                break;
              }
              nbr = nbr_table_next(ds6_neighbors, nbr);
            }
          }
        }
#endif


        if(log->tx.mac_tx_status!=MAC_TX_OK && log->tx.num_tx== TSCH_MAC_MAX_FRAME_RETRIES+1)
       {
        static uint16_t num_l_loss=0;
        num_l_loss++;
        printf("L_Loss=%u (sf %u, asn %x.%lx)\n",num_l_loss,log->link->slotframe_handle,log->asn.ms1b, log->asn.ls4b);
#if TESLA
        struct tsch_neighbor * n= tsch_queue_get_time_source();

        if(n!=NULL){
          if( (log->tx.dest == (TSCH_LOG_ID_FROM_LINKADDR(&(n->addr)))) 
            && (log->link->slotframe_handle==get_tx_sf_handle_from_linkaddr(&(n->addr))) )
          {
            #if PRINT_SELECT_1
            printf("L_L_TS: Link Loss to time source\n");
            #endif

            tsch_queue_backoff_reset(n);

            l_l_ts=1;

            if(tsch_queue_is_empty(n) || n==NULL)
            {
              //printf("L_L_TS: remove_tx_sf now\n");
              //remove_tx_sf(&(n->addr)); 
              //printf("check it\n"); 
              inform_sf_size_dao(); //DAO is sent using shared slot
            }
            else
            {
              #if PRINT_SELECT_1
              printf("L_L_TS: 1st Queued packet, Use shared slot\n");
              #endif
              //add_rm_nbr(old_addr);
              change_attr_in_tx_queue(&(n->addr), 0 , 1); //change only 1st packet in queue
                                                          //it the 1st packet is lost, L_L_TS will occur again
              //remove_tx_sf(&(n->addr));
            }

            l_l_ts=0;

           


          }
        }
        else{
          #if PRINT_SELECT_1
          printf("L_L_TS: time source NULL\n");
          #endif
        }
#endif
       }

#if TESLA
       if(log->tx.mac_tx_status==MAC_TX_OK)
       {
        int dest_id=log->tx.dest;
        if(dest_id!=0) //unicast
        {
          struct tsch_neighbor * n = tsch_queue_get_nbr_from_id((uint16_t)dest_id);
          if(n!=NULL)
          {
            if(tsch_queue_is_empty(n))
            {
            #if PRINT_SELECT
              printf("Empty queue (%u) --> Stop shared_tx_timer\n",(n->addr).u8[LINKADDR_SIZE-1]);
            #endif
              ctimer_stop(&(n->shared_tx_timer));
            }
            else
            {
              #if PRINT_SELECT   
                printf("Not empty queue (%u) --> Reset shared_tx_timer\n",(n->addr).u8[LINKADDR_SIZE-1]);
              #endif
                if(n!=NULL){  
                  ctimer_stop(&(n->shared_tx_timer));
                  ctimer_set(&(n->shared_tx_timer), SHARED_TX_INTERVAL, shared_tx_expired, (void*)n);
                }
                else
                {
                  printf("ERROR: update_neighbor_state, shared_tx_timer\n");
                }
            }
          }

        }

       }
#endif       

#if TESLA

        if(log->tx.mac_tx_status==MAC_TX_OK && log->tx.ack_len!=0 && log->tx.dest != 0) //Successful ACK Rx && Unicast
        {
          check_sf_size_in_eack(log->tx.dest, log->tx.ack_sf_size, log->tx.ack_sf_size_version);

        }
#endif


        break;
      case tsch_log_rx:
        //printf("%s-%u-%u %u rx %d",
        //    log->rx.is_unicast == 0 ? "bc" : "uc", log->rx.is_data, log->rx.sec_level,
        //        log->rx.datalen,
        //        log->rx.src);

        printf("%s rx %d",
            log->rx.is_unicast == 0 ? "bc" : "uc",
                log->rx.src);
        if(log->rx.is_unicast != 0) //uc
        {
          //printf(", d %d %d", log->rx.delay_prN, log->rx.delay_prs);
        }
        //printf("\n");

#if IOT_LAB_M3
        //printf(", rssi %d", log->rx.rssi);
#endif
        if(log->rx.drift_used) {
          //printf(", dr %d", log->rx.drift);
        }
        
        //printf(", edr %d", (int)log->rx.estimated_drift);
#if PROPOSED
        printf(", a_rx %lu %lu\n",num_total_rx_accum, num_total_auto_rx_accum);
#else        
        printf(", a_rx %lu\n",num_total_rx_accum);
#endif


#if TESLA
       
        if(log->link->slotframe_handle == TEMP_SF_HANDLE)
        {
          #if PRINT_SELECT_1
          printf("Found Rx in TEMP sf\n");
          #endif
          //inform_sf_size_eb(0);
        }

#endif


#if TESLA

        if(log->rx.is_unicast){ //unicast
          if(log->link->slotframe_handle==1){ //NOTE1  

            uip_ds6_nbr_t *nbr = nbr_table_head(ds6_neighbors);
        
            while(nbr != NULL) {
              uint16_t nbr_id=((nbr->ipaddr.u8[14]) << 8) | (nbr->ipaddr.u8[15]);
              if((log->rx.src)==nbr_id)
              {
                nbr->num_I_rx++;
              #if PRINT_SELECT  
                printf("num_I_rx++ %u (%u)\n",nbr->num_I_rx ,nbr_id);
              #endif

                nbr->num_nbr_tx= log->rx.num_src_tx+1;
              #if PRINT_SELECT
                printf("num_nbr_tx %u+1 (%u)\n",log->rx.num_src_tx, nbr_id);
              #endif        
   
                break;
              }
              nbr = nbr_table_next(ds6_neighbors, nbr);
            }
          }
          else if (log->link->slotframe_handle==2) //shared slot  //NOTE1  
          {


            uip_ds6_nbr_t *nbr = nbr_table_head(ds6_neighbors);
        
            while(nbr != NULL) {
              uint16_t nbr_id=((nbr->ipaddr.u8[14]) << 8) | (nbr->ipaddr.u8[15]);
              if((log->rx.src)==nbr_id)
              {
                //nbr->num_I_rx++;
                //printf("num_I_rx++ %u (%u)\n",nbr->num_I_rx ,nbr_id);

                if(nbr->num_nbr_tx!=log->rx.num_src_tx)
                {
                #if PRINT_SELECT
                  printf("SS: num_nbr_tx %u->%u (%u)\n",nbr->num_nbr_tx,log->rx.num_src_tx, nbr_id);
                #endif
                  nbr->num_nbr_tx= log->rx.num_src_tx;
                 
                }        
   
                break;
              }
              nbr = nbr_table_next(ds6_neighbors, nbr);
            }
           
          }
        }


#endif

        break;
      case tsch_log_message:
        printf("%s\n", log->message);
        break;
    }
    /* Remove input from ringbuf */
    ringbufindex_get(&log_ringbuf);
  }
}
/*---------------------------------------------------------------------------*/
/* Prepare addition of a new log.
 * Returns pointer to log structure if success, NULL otherwise */
struct tsch_log_t *
tsch_log_prepare_add(void)
{
  int log_index = ringbufindex_peek_put(&log_ringbuf);
  if(log_index != -1) {
    struct tsch_log_t *log = &log_array[log_index];
    log->asn = tsch_current_asn;
    log->link = current_link;
    return log;
  } else {
    log_dropped++;
    return NULL;
  }
}
/*---------------------------------------------------------------------------*/
/* Actually add the previously prepared log */
void
tsch_log_commit(void)
{
  ringbufindex_put(&log_ringbuf);
  process_poll(&tsch_pending_events_process);
}
/*--------------------------------------JSB----------------------------------*/
/*static void
 reset_num_tx (void *ptr)
{

  num_mac_tx_ok=0;
  num_mac_tx_noack=0;
  num_mac_tx_ok_ss=0;
  num_mac_tx_noack_ss=0;

  printf("Reset num_*_tx\n");
 
}*/
/*--------------------------------------END----------------------------------*/
/* Initialize log module */
void
tsch_log_init(void)
{
  ringbufindex_init(&log_ringbuf, TSCH_LOG_QUEUE_LEN);
  //ctimer_set(&reset_num_tx_timer, (NO_DATA_PERIOD+2*UPLINK_PERIOD)*CLOCK_SECOND, reset_num_tx, NULL); //JSB
}

#endif /* TSCH_LOG_LEVEL */
