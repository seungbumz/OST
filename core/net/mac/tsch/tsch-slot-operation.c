/*
 * Copyright (c) 2015, SICS Swedish ICT.
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
 *         TSCH slot operation implementation, running from interrupt.
 * \author
 *         Simon Duquennoy <simonduq@sics.se>
 *         Beshr Al Nahas <beshr@sics.se>
 *         Atis Elsts <atis.elsts@bristol.ac.uk>
 *
 */

#include "contiki.h"
#include "dev/radio.h"
#include "net/netstack.h"
#include "net/packetbuf.h"
#include "net/queuebuf.h"
#include "net/mac/framer-802154.h"
#include "net/mac/tsch/tsch.h"
#include "net/mac/tsch/tsch-slot-operation.h"
#include "net/mac/tsch/tsch-queue.h"
#include "net/mac/tsch/tsch-private.h"
#include "net/mac/tsch/tsch-log.h"
#include "net/mac/tsch/tsch-packet.h"
#include "net/mac/tsch/tsch-security.h"
#include "net/mac/tsch/tsch-adaptive-timesync.h"

//
#include "sys/ctimer.h" //JSB
#include "sys/clock.h" //JSB

#if PROPOSED
  #include "node-id.h"
  #include "orchestra.h"
  #include "net/ipv6/uip-ds6-route.h"
  #include "net/ipv6/uip-ds6-nbr.h"
  #include "net/mac/tsch/tsch-schedule.h"
  #include "lib/random.h"

#if RESIDUAL_ALLOC
  #include "net/mac/frame802154.h"
#endif

#endif

#if TESLA
  #include "net/rpl/rpl-private.h"
  #include "node-id.h"
  #include "orchestra.h"//JSB
  #include "lib/random.h"
#endif

#if IOT_LAB_M3

#include "sys/node-id.h"
#include "drivers/unique_id.h"

const uint16_t m3_uid1[] = M3_UID;
#define NUM_M3_UID (sizeof(m3_uid1) / sizeof(uint16_t))


#endif


#if CONTIKI_TARGET_COOJA || CONTIKI_TARGET_COOJA_IP64
#include "lib/simEnvChange.h"
#include "sys/cooja_mt.h"
#endif /* CONTIKI_TARGET_COOJA || CONTIKI_TARGET_COOJA_IP64 */

#if TSCH_LOG_LEVEL >= 1
#define DEBUG DEBUG_NONE
#else /* TSCH_LOG_LEVEL */
#define DEBUG DEBUG_NONE
#endif /* TSCH_LOG_LEVEL */
#include "net/net-debug.h"

/* TSCH debug macros, i.e. to set LEDs or GPIOs on various TSCH
 * timeslot events */
#ifndef TSCH_DEBUG_INIT
#define TSCH_DEBUG_INIT()
#endif
#ifndef TSCH_DEBUG_INTERRUPT
#define TSCH_DEBUG_INTERRUPT()
#endif
#ifndef TSCH_DEBUG_RX_EVENT
#define TSCH_DEBUG_RX_EVENT()
#endif
#ifndef TSCH_DEBUG_TX_EVENT
#define TSCH_DEBUG_TX_EVENT()
#endif
#ifndef TSCH_DEBUG_SLOT_START
#define TSCH_DEBUG_SLOT_START()
#endif
#ifndef TSCH_DEBUG_SLOT_END
#define TSCH_DEBUG_SLOT_END()
#endif

/* Check if TSCH_MAX_INCOMING_PACKETS is power of two */
#if (TSCH_MAX_INCOMING_PACKETS & (TSCH_MAX_INCOMING_PACKETS - 1)) != 0
#error TSCH_MAX_INCOMING_PACKETS must be power of two
#endif

/* Check if TSCH_DEQUEUED_ARRAY_SIZE is power of two and greater or equal to QUEUEBUF_NUM */
#if TSCH_DEQUEUED_ARRAY_SIZE < QUEUEBUF_NUM
#error TSCH_DEQUEUED_ARRAY_SIZE must be greater or equal to QUEUEBUF_NUM
#endif
#if (TSCH_DEQUEUED_ARRAY_SIZE & (TSCH_DEQUEUED_ARRAY_SIZE - 1)) != 0
#error TSCH_DEQUEUED_ARRAY_SIZE must be power of two
#endif

/* Truncate received drift correction information to maximum half
 * of the guard time (one fourth of TSCH_DEFAULT_TS_RX_WAIT) */
#define SYNC_IE_BOUND ((int32_t)US_TO_RTIMERTICKS(TSCH_DEFAULT_TS_RX_WAIT / 4))

/* By default: check that rtimer runs at >=32kHz and use a guard time of 10us */
#if RTIMER_SECOND < (32 * 1024)
#error "TSCH: RTIMER_SECOND < (32 * 1024)"
#endif
#if CONTIKI_TARGET_COOJA || CONTIKI_TARGET_COOJA_IP64
/* Use 0 usec guard time for Cooja Mote with a 1 MHz Rtimer*/
#define RTIMER_GUARD 0u
#elif RTIMER_SECOND >= 200000
#define RTIMER_GUARD (RTIMER_SECOND / 100000)
#else
#define RTIMER_GUARD 2u
#endif

enum tsch_radio_state_on_cmd {
  TSCH_RADIO_CMD_ON_START_OF_TIMESLOT,
  TSCH_RADIO_CMD_ON_WITHIN_TIMESLOT,
  TSCH_RADIO_CMD_ON_FORCE,
};

enum tsch_radio_state_off_cmd {
  TSCH_RADIO_CMD_OFF_END_OF_TIMESLOT,
  TSCH_RADIO_CMD_OFF_WITHIN_TIMESLOT,
  TSCH_RADIO_CMD_OFF_FORCE,
};

/* A ringbuf storing outgoing packets after they were dequeued.
 * Will be processed layer by tsch_tx_process_pending */
struct ringbufindex dequeued_ringbuf;
struct tsch_packet *dequeued_array[TSCH_DEQUEUED_ARRAY_SIZE];
/* A ringbuf storing incoming packets.
 * Will be processed layer by tsch_rx_process_pending */
struct ringbufindex input_ringbuf;
struct input_packet input_array[TSCH_MAX_INCOMING_PACKETS];

/* Last time we received Sync-IE (ACK or data packet from a time source) */
static struct tsch_asn_t last_sync_asn;

/* A global lock for manipulating data structures safely from outside of interrupt */
static volatile int tsch_locked = 0;
/* As long as this is set, skip all slot operation */
static volatile int tsch_lock_requested = 0;

/* Last estimated drift in RTIMER ticks
 * (Sky: 1 tick = 30.517578125 usec exactly) */
static int32_t drift_correction = 0;
/* Is drift correction used? (Can be true even if drift_correction == 0) */
static uint8_t is_drift_correction_used;

/* The neighbor last used as our time source */
struct tsch_neighbor *last_timesource_neighbor = NULL;

/* Used from tsch_slot_operation and sub-protothreads */
static rtimer_clock_t volatile current_slot_start;

/* Are we currently inside a slot? */
static volatile int tsch_in_slot_operation = 0;

/* If we are inside a slot, this tells the current channel */
static uint8_t current_channel;

#if TESLA
  extern uint16_t my_sf_size;
  static struct ctimer periodic_timer;

  const uint16_t prime_numbers[] = PRIME_NUMBERS;
#define NUM_PRIME_NUMBERS (sizeof(prime_numbers) / sizeof(uint16_t))

  static unsigned long t_now;
  static unsigned long t_last_check=0;
  static unsigned long t_last_update=0;

  static uint8_t consecutive_inc_decision=0;

#endif

  rtimer_clock_t over_time;
  int cca_result[100];
  rtimer_clock_t cca_delay[100];


/* Info about the link, packet and neighbor of
 * the current (or next) slot */
struct tsch_link *current_link = NULL;
/* A backup link with Rx flag, overlapping with current_link.
 * If the current link is Tx-only and the Tx queue
 * is empty while executing the link, fallback to the backup link. */
static struct tsch_link *backup_link = NULL;
static struct tsch_packet *current_packet = NULL;
static struct tsch_neighbor *current_neighbor = NULL;

/* Protothread for association */
PT_THREAD(tsch_scan(struct pt *pt));
/* Protothread for slot operation, called from rtimer interrupt
 * and scheduled from tsch_schedule_slot_operation */
static PT_THREAD(tsch_slot_operation(struct rtimer *t, void *ptr));
static struct pt slot_operation_pt;
/* Sub-protothreads of tsch_slot_operation */
static PT_THREAD(tsch_tx_slot(struct pt *pt, struct rtimer *t));
static PT_THREAD(tsch_rx_slot(struct pt *pt, struct rtimer *t));

#if TESLA
  static void slotframe_size_adaptation (void * ptr);
  static struct ieee802154_ies ack_ies_store;
  static int ack_len_store=0;

#endif


static uint32_t num_idle_rx; //JSB
static uint32_t num_succ_rx; //JSB
static uint32_t num_coll_rx; //JSB
static uint32_t num_total_rx; //JSB

uint32_t num_total_rx_accum;

#if PROPOSED
  uint32_t num_total_auto_rx_accum;
#endif

static uint32_t num_idle_rx_ss; //JSB
static uint32_t num_succ_rx_ss; //JSB
static uint32_t num_coll_rx_ss; //JSB
static uint32_t num_total_rx_ss;//JSB

//static uint8_t done_1st_reset_num_rx=0;


static uint16_t ratio_coll_succ_over_total=0;

static uint16_t ratio_ss_coll_succ_over_total=0;


//static struct ctimer reset_num_rx_timer; //JSB
#if PROPOSED

typedef struct t_offset_candidate{
  uint8_t available;
  uint8_t priority;

} t_offset_candidate_t;

static uint16_t prN_new_N;
static uint16_t prN_new_t_offset;
static uip_ds6_nbr_t * prN_nbr;

static uint16_t prt_new_t_offset;
static uip_ds6_nbr_t * prt_nbr;

static uint8_t todo_rx_schedule_change;
static uint8_t todo_tx_schedule_change;

static uint8_t todo_N_update;

static uint8_t todo_N_inc;

uint8_t todo_no_resource;

uint8_t todo_consecutive_new_tx_request;

static uint8_t changed_t_offset_default;

  #if RESIDUAL_ALLOC
  struct ssq_schedule_t ssq_schedule_list[16];
  #endif


#endif

#if PROPOSED

#if MULTI_CHANNEL
//ksh..// Thomas Wang  32bit-Integer Mix Function
uint16_t
hash_ftn(uint16_t value, uint16_t mod){ //Thomas Wang method..

  uint32_t input=(uint32_t)value;
  uint32_t a=input;//|(input<<16);
  
  a = (a ^ 61) ^ (a >> 16);
  a = a + (a << 3);
  a = a ^ (a >> 4);
  a = a * 0x27d4eb2d;
  a = a ^ (a >> 15);
  
//  a=a^(a>>16);
  return (uint16_t)a%mod;
}
#endif

#if RESIDUAL_ALLOC


uint16_t
select_matching_schedule(uint16_t rx_schedule_info)
{
  uint16_t my_schedule = tsch_schedule_get_subsequent_schedule(&tsch_current_asn);
  //printf("my subsq schedule %u\n",my_schedule);
  uint16_t compare_schedule = my_schedule | rx_schedule_info;

  uint8_t i;
  for(i=0;i<16;i++)
  {
    if( (compare_schedule>>i)%2 ==0) //To find the earliest 0
    {
      return i+1;
    }

  }
  return 65535;
}

void
print_ssq_schedule_list(void)
{
  PRINTF("[SSQ_SCHEDULE] index / asn / option\n");
  uint8_t i;
  uint16_t nbr_id;
  for(i=0; i<16; i++)
  {
    if(ssq_schedule_list[i].asn.ls4b==0 && ssq_schedule_list[i].asn.ms1b==0)
    {

    }
    else
    {
      if(ssq_schedule_list[i].link.link_options==LINK_OPTION_TX)
      {
        nbr_id=(ssq_schedule_list[i].link.slotframe_handle-SSQ_SCHEDULE_HANDLE_OFFSET-1)/2;
        PRINTF("[ID:%u] %u / %x.%lx / Tx %u\n", nbr_id, i, ssq_schedule_list[i].asn.ms1b, ssq_schedule_list[i].asn.ls4b,ssq_schedule_list[i].link.slotframe_handle);
      }
      else
      {
        nbr_id=(ssq_schedule_list[i].link.slotframe_handle-SSQ_SCHEDULE_HANDLE_OFFSET-2)/2;
        PRINTF("[ID:%u] %u / %x.%lx / Rx %u\n", nbr_id, i, ssq_schedule_list[i].asn.ms1b, ssq_schedule_list[i].asn.ls4b, ssq_schedule_list[i].link.slotframe_handle);

      }
    }
  }
}

uint8_t
exist_matching_slot(struct tsch_asn_t *target_asn)
{
  uint8_t i;
  for(i=0; i<16; i++)
  {
    if(ssq_schedule_list[i].asn.ls4b==target_asn->ls4b && ssq_schedule_list[i].asn.ms1b==target_asn->ms1b)
    {

      return 1;
    }
  }

  return 0;
}

void
remove_matching_slot(void)
{
  uint8_t i;
  for(i=0; i<16; i++)
  {
    if(ssq_schedule_list[i].asn.ls4b==tsch_current_asn.ls4b && ssq_schedule_list[i].asn.ms1b==tsch_current_asn.ms1b)
    {
      PRINTF("remove_matching_slot: ssq_schedule_list[%u]\n",i);
      ssq_schedule_list[i].asn.ls4b=0;
      ssq_schedule_list[i].asn.ms1b=0;
      break;
    }


  }

  if(i==16)
  {
    printf("ERROR: Not found in remove_matching_slot\n");
  }

  print_ssq_schedule_list();
}

void
add_matching_slot(uint16_t matching_slot, uint8_t is_tx, uint16_t nbr_id)
{
  
  if(1<=matching_slot && matching_slot<=16)
  {  
    uint8_t i;
    for(i=0; i<16; i++)
    {
      if(ssq_schedule_list[i].asn.ls4b==0 && ssq_schedule_list[i].asn.ms1b==0)
      {
        ssq_schedule_list[i].asn.ls4b=tsch_current_asn.ls4b;
        ssq_schedule_list[i].asn.ms1b=tsch_current_asn.ms1b;
        TSCH_ASN_INC(ssq_schedule_list[i].asn, matching_slot);


        ssq_schedule_list[i].link.next=NULL;                              //not used
        ssq_schedule_list[i].link.handle=65535;                          //not used
        ssq_schedule_list[i].link.timeslot=65535;                         //not used

        ssq_schedule_list[i].link.channel_offset=3;                      //will be updated using hash before TX or RX

        ssq_schedule_list[i].link.link_type = LINK_TYPE_NORMAL;
        ssq_schedule_list[i].link.data=NULL;

        linkaddr_copy(&(ssq_schedule_list[i].link.addr),&tsch_broadcast_address);

        if(is_tx)
        {
          ssq_schedule_list[i].link.slotframe_handle = SSQ_SCHEDULE_HANDLE_OFFSET+2*nbr_id+1;
          ssq_schedule_list[i].link.link_options = LINK_OPTION_TX;
        }
        else
        {
          ssq_schedule_list[i].link.slotframe_handle = SSQ_SCHEDULE_HANDLE_OFFSET+2*nbr_id+2;
          ssq_schedule_list[i].link.link_options = LINK_OPTION_RX;
        }

        PRINTF("add_matching_slot: ssq_schedule_list[%u] %x.%lx %u %u\n"
          , i, ssq_schedule_list[i].asn.ms1b, ssq_schedule_list[i].asn.ls4b, ssq_schedule_list[i].link.slotframe_handle, is_tx);
        break;
        
      }
    }

    if(i==16)
    {
      printf("ERROR: ssq_schedule_list is full\n");
    }

    print_ssq_schedule_list();
  }
}

uint16_t 
process_rx_schedule_info(frame802154_t* frame) 
{

  uint16_t src_id=TSCH_LOG_ID_FROM_LINKADDR((linkaddr_t *)&(frame->src_addr));
  uip_ds6_nbr_t *nbr = nbr_table_head(ds6_neighbors);

  
  while(nbr != NULL) 
  {
    uint16_t nbr_id=((nbr->ipaddr.u8[14]) << 8) | (nbr->ipaddr.u8[15]);
    if(src_id==nbr_id &&
      !frame802154_is_broadcast_addr((frame->fcf).dest_addr_mode, frame->dest_addr) &&
      is_routing_nbr(nbr)==1)
    {
      
      if((frame->fcf).frame_pending)
      {
        PRINTF("Rx uc: ssq_schedule found 0x%x (r_nbr %u)\n",frame->pigg2, nbr_id);  

        uint16_t time_to_matching_slot = select_matching_schedule(frame->pigg2);

        if(time_to_matching_slot==65535)
        {
          printf("No matching slots\n");
        }
        else if(1<=time_to_matching_slot && time_to_matching_slot<=16)
        {
            PRINTF("matching slot=%u\n",time_to_matching_slot);
            
            add_matching_slot(time_to_matching_slot,0,nbr_id);            

            return time_to_matching_slot;
        } 
        else
        {
          printf("ERROR: time_to_matching_slot\n");
        }

      }
      else if( !((frame->fcf).frame_pending) && frame->pigg2!=0xffff)
      {
        printf("ERROR: schedule info is updated only when pending bit is set\n");
      }

  

      break;
    }
    nbr = nbr_table_next(ds6_neighbors, nbr);
  }

  if(frame802154_is_broadcast_addr((frame->fcf).dest_addr_mode, frame->dest_addr))
  {
    PRINTF("Rx uc: ssq_schedule found %u (BC)\n",frame->pigg2);
  }
  else if(nbr==NULL){
    PRINTF("Rx uc: ssq_schedule found %u (No nbr)\n",frame->pigg2);
  }
  else if(is_routing_nbr(nbr)==0)
  {
    PRINTF("Rx uc: ssq_schedule found %u (No r_nbr)\n",frame->pigg2);
  }

  return 65535;

}

void
process_rx_matching_slot(frame802154_t* frame)
{
  linkaddr_t* eack_src= &(current_neighbor->addr);
  uint16_t eack_src_id=TSCH_LOG_ID_FROM_LINKADDR(eack_src);
  uip_ds6_nbr_t *nbr = nbr_table_head(ds6_neighbors);

  while(nbr != NULL) {
  uint16_t nbr_id=((nbr->ipaddr.u8[14]) << 8) | (nbr->ipaddr.u8[15]);
  
    if(eack_src_id==nbr_id && is_routing_nbr(nbr)==1)
    {
      
      PRINTF("Rx EACK: matching slot found %u (nbr %u)\n",frame->pigg2, nbr_id); 

      add_matching_slot(frame->pigg2, 1, nbr_id);

    }
    nbr = nbr_table_next(ds6_neighbors, nbr);
  }
}

#endif


/**************************************Rx N from Data -> Change Rx schedule *********************************/
void 
add_rx(uint16_t id, uint16_t N, uint16_t t_offset)
{
  if(N!=65535 && t_offset!=65535)
  {
    uint16_t handle = get_rx_sf_handle_from_id(id);
    uint16_t size = (1<<N);
    uint16_t channel_offset = 3;

    struct tsch_slotframe* sf;
    struct tsch_link *l;

    PRINTF("add_rx: handle:%u, size:%u (nbr %u)\n",handle,size,id);

    if(tsch_schedule_get_slotframe_by_handle(handle)!=NULL)
    {
      printf("ERROR(add_rx): handle already exist\n");
      return ;
    }

    sf=tsch_schedule_add_slotframe(handle,size);

    if(sf!=NULL)
    {
      l=tsch_schedule_add_link(sf,LINK_OPTION_RX,LINK_TYPE_NORMAL,&tsch_broadcast_address,
        t_offset, channel_offset);

      if(l==NULL)
      {
        printf("ERROR(add_rx): add_link fail\n");
      }


    }
    else
    {
      printf("ERROR(add_rx): add_slotframe fail\n");
    }

  }
}

void 
remove_rx(uint16_t id)
{
  struct tsch_slotframe * rm_sf;
  uint16_t rm_sf_handle=get_rx_sf_handle_from_id(id);
  rm_sf = tsch_schedule_get_slotframe_by_handle(rm_sf_handle);

  if(rm_sf!=NULL)
  {  
    PRINTF("remove_rx: handle:%u, size:%u (nbr %u)\n",rm_sf_handle,rm_sf->size.val,id);
    tsch_schedule_remove_slotframe(rm_sf);
  }

}




void eliminate_overlap_toc(t_offset_candidate_t* toc, uint16_t target_N, uint16_t used_N, uint16_t used_t_offset)
{
  PRINTF("(%u,%u) ",used_N,used_t_offset);
  if(target_N < used_N) // Lower-tier used
  {
    uint16_t parent_N = used_N-1;
    uint16_t parent_t_offset = used_t_offset % (1<<(used_N-1)); 

    eliminate_overlap_toc(toc,target_N,parent_N,parent_t_offset);
  }
  else if (target_N > used_N) // Higher-tier used
  {
    uint16_t child_N = used_N+1;
    uint16_t left_child_t_offset = used_t_offset;
    uint16_t right_child_t_offset = used_t_offset + (1<<used_N);

    eliminate_overlap_toc(toc,target_N,child_N,left_child_t_offset);
    eliminate_overlap_toc(toc,target_N,child_N,right_child_t_offset);

  }
  else // target_N == used_N
  {
    if(used_t_offset>=(1<<target_N))
    {
      printf("ERROR: used_t_offset is too big %u %u\n",used_t_offset,target_N);
      return ;
    }
    toc[used_t_offset].available=0;
  }

}


uint32_t 
select_t_offset(uint16_t target_id, uint16_t N)  //similar with tx_installable
{
  t_offset_candidate_t toc[1<<N_MAX];

  //Initialize 2^N toc
  uint16_t i;
  for(i=0; i<(1<<N); i++)
  {
    toc[i].available=1;
    toc[i].priority=255; //TODO
  }

  //check resource overlap
  PRINTF("\nselect_t_offset (target N=%u)\n",N);
  
  struct tsch_slotframe *sf = tsch_schedule_get_slotframe_head();
  while(sf != NULL) {
    if(sf->handle>2 && sf->handle!=get_rx_sf_handle_from_id(target_id)){

      uint16_t n;
      for(n=1;n<=N_MAX;n++)
      {
        if( (sf->size.val>>n) ==1)
        {
          uint16_t used_N=n;

          struct tsch_link *l = list_head(sf->links_list);
          if(l==NULL)
          {
            printf("ERROR: link is null (select_t_offset)\n");

          }
          else
          {

            uint16_t used_t_offset= l->timeslot;
            if(used_t_offset>=(1<<used_N))
            {
              printf("ERROR: used_t_offset is too big (select_t_offset) %u %u %u %u\n",sf->handle,used_t_offset,used_N, target_id);
            }
            eliminate_overlap_toc(toc, N, used_N,used_t_offset);
            PRINTF("\n");
          }
          
        
          break;
        }
        else if (n==N_MAX)
        {
          printf("ERROR: weird size of slotframe");
        }
      }

    }
    sf = list_item_next(sf);
  }



  uint32_t rand= random_rand() % (1<<31);
  uint32_t i1;

  for(i=0; i<(1<<N); i++)
  {
    i1= (i+rand) % (1<<N);


    if(toc[i1].available==1)
    {
      PRINTF("select_t_offset: %u chosen (nbr %u)\n",i1,target_id);

      break;
    }
  }

  if(i==(1<<N))  
  {
    //printf("ERROR: select_t_offset: No available t_offset\n");
    return 65535+1;
  }
  else
  {
    return i1;
  }
  
}

void
process_rx_N(frame802154_t* frame) //In short, prN
{

  if(tsch_is_locked()) 
  {
    TSCH_LOG_ADD(tsch_log_message,
            snprintf(log->message, sizeof(log->message),
                "process_rx_N: locked");
    );
    return;
  }

  uint16_t src_id=TSCH_LOG_ID_FROM_LINKADDR((linkaddr_t *)&(frame->src_addr));
  uip_ds6_nbr_t *nbr = nbr_table_head(ds6_neighbors);

  while(nbr != NULL) {
    uint16_t nbr_id=((nbr->ipaddr.u8[14]) << 8) | (nbr->ipaddr.u8[15]);
    if(src_id==nbr_id &&
      !frame802154_is_broadcast_addr((frame->fcf).dest_addr_mode, frame->dest_addr) &&
      is_routing_nbr(nbr)==1 &&
      nbr->rx_no_path ==0) // No need to allocate rx for nbr who sent no-path dao
    {
      
      PRINTF("Rx uc: N found %u (r_nbr %u)\n",frame->pigg1, nbr_id);        


      if(nbr->nbr_N!=frame->pigg1)
      {

        uint16_t old_N = nbr->nbr_N;

        //To be used in post_process_rx_N
        if(frame->pigg1 >= INC_N_NEW_TX_REQUEST)
        {
          prN_new_N = (frame->pigg1) - INC_N_NEW_TX_REQUEST;
          PRINTF("process_rx_N: detect uninstallable or low PRR\n");

          nbr->consecutive_new_tx_request++;
          printf("check %u\n", nbr->consecutive_new_tx_request);

          if(nbr->consecutive_new_tx_request>=THRES_CONSECUTIVE_NEW_TX_REQUEST)
          {
            nbr->consecutive_new_tx_request=0;
            todo_consecutive_new_tx_request=1;
            return;
          }
        }
        else
        {
          prN_new_N = frame->pigg1;

          nbr->consecutive_new_tx_request=0;
        }

        PRINTF("Rx uc: New N (%u->%u)\n",old_N,prN_new_N);

        uint32_t result = select_t_offset(nbr_id,prN_new_N);
            

        if(result<=65535)
        {
          todo_rx_schedule_change=1;

          prN_new_t_offset=(uint16_t)result;
          prN_nbr=nbr;

          nbr->nbr_N=prN_new_N; 
          nbr->nbr_t_offset=prN_new_t_offset;
          
          return;  
        }
        else
        {
          todo_no_resource=1;
          //printf("ERROR(process_rx_N): Allocation fail\n");
          return;
          
        }


      }
      else
      {
        nbr->consecutive_new_tx_request=0;
      }

      break;
    }
    nbr = nbr_table_next(ds6_neighbors, nbr);
  }

  if(frame802154_is_broadcast_addr((frame->fcf).dest_addr_mode, frame->dest_addr))
  {
    PRINTF("Rx uc: N found %u (BC)\n",frame->pigg1);
  }
  else if(nbr==NULL){
    PRINTF("Rx uc: N found %u (No nbr)\n",frame->pigg1);
  }
  else if(is_routing_nbr(nbr)==0)
  {
    PRINTF("Rx uc: N found %u (No r_nbr)\n",frame->pigg1);
  }

}

//called from tsch_rx_process_pending (after slot_operation)
void
post_process_rx_N(void)
{
    if(todo_rx_schedule_change==1)
    {
      todo_rx_schedule_change=0;

      if(prN_nbr!=NULL)
      {
        uint16_t nbr_id=ID_FROM_IPADDR(&(prN_nbr->ipaddr));
        remove_rx(nbr_id);

        add_rx(nbr_id,prN_new_N,prN_new_t_offset);
        printf("New Rx schedule: %u,%u (nbr %u)\n",prN_new_N,prN_new_t_offset,nbr_id);

        //print_nbr();
        //tsch_schedule_print_proposed();

      }

    }


    if(todo_consecutive_new_tx_request==1)
    {
      todo_consecutive_new_tx_request=0;
      printf("EACK (CONSECUTIVE_NEW_TX_REQUEST)\n");
    }


    if(todo_no_resource==1)
    {
      todo_no_resource=0;
      printf("EACK (ALLOCATION_FAIL)\n");
    }



}


uint8_t get_todo_no_resource()
{
  return todo_no_resource;
}

uint8_t get_todo_consecutive_new_tx_request()
{
  return todo_consecutive_new_tx_request;
}
/**************************************Rx t_offset from EACK -> Change Tx schedule *********************************/
void change_queue_select_packet(uint16_t id, uint16_t handle, uint16_t timeslot)
{
  struct tsch_neighbor * n= tsch_queue_get_nbr_from_id(id);
  if(!tsch_is_locked() && n!=NULL)
  {
    if(!ringbufindex_empty(&n->tx_ringbuf))
    {
      int16_t get_index = ringbufindex_peek_get(&n->tx_ringbuf);
      //int16_t put_index = ringbufindex_peek_put(&n->tx_ringbuf);
      uint8_t num_elements= ringbufindex_elements(&n->tx_ringbuf);

      PRINTF("change %u queued packets (nbr %u)\n", num_elements,id);

    

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
        set_queuebuf_attr(n->tx_array[index]->qb, PACKETBUF_ATTR_TSCH_SLOTFRAME, handle);
        set_queuebuf_attr(n->tx_array[index]->qb, PACKETBUF_ATTR_TSCH_TIMESLOT, timeslot);

        PRINTF("index: %u, %u %u\n",
          j,queuebuf_attr(n->tx_array[index]->qb,PACKETBUF_ATTR_TSCH_SLOTFRAME),queuebuf_attr(n->tx_array[index]->qb,PACKETBUF_ATTR_TSCH_TIMESLOT));

      }
      

    }


  }
  


}


void 
add_tx(uint16_t id, uint16_t N, uint16_t t_offset)
{
  if(N!=65535 && t_offset!=65535)
  {
    uint16_t handle = get_tx_sf_handle_from_id(id);
    uint16_t size = (1<<N);
    uint16_t channel_offset = 3;

    struct tsch_slotframe* sf;
    struct tsch_link *l;

    PRINTF("add_tx: handle:%u, size:%u (nbr %u)\n",handle,size,id);

    if(tsch_schedule_get_slotframe_by_handle(handle)!=NULL)
    {
      printf("ERROR(add_tx): handle already exist\n");
      return ;
    }

    sf=tsch_schedule_add_slotframe(handle,size);

    if(sf!=NULL)
    {
      l=tsch_schedule_add_link(sf,LINK_OPTION_TX,LINK_TYPE_NORMAL,&tsch_broadcast_address,
        t_offset, channel_offset);

      if(l==NULL)
      {
        printf("ERROR(add_tx): add_link fail\n");
      }
      else{
        change_queue_select_packet(id, handle, t_offset);
      }

      uip_ds6_nbr_t *nbr = nbr_table_head(ds6_neighbors);
      
      //reset some nbr info. 
      while(nbr != NULL) {
        uint16_t nbr_id=((nbr->ipaddr.u8[14]) << 8) | (nbr->ipaddr.u8[15]);
        if(id==nbr_id)
        {
          if(nbr->my_low_prr==1)
          {
            change_queue_N_update(nbr_id, nbr->my_N);
          }

          PRINTF("add_tx: reset my_low_prr/num_tx_mac/num_tx_succ_mac\n");
          nbr->my_low_prr=0;
          nbr->num_tx_mac=0;
          nbr->num_tx_succ_mac=0;
          nbr->num_consecutive_tx_fail_mac=0;
          nbr->consecutive_my_N_inc=0;

          break;
        }
        nbr = nbr_table_next(ds6_neighbors, nbr);
      }
      //end

    }
    else
    {
      printf("ERROR(add_tx): add_slotframe fail\n");
    }

  }
}

void 
remove_tx(uint16_t id)
{
  struct tsch_slotframe * rm_sf;
  uint16_t rm_sf_handle=get_tx_sf_handle_from_id(id);
  rm_sf = tsch_schedule_get_slotframe_by_handle(rm_sf_handle);

  if(rm_sf!=NULL)
  {  
    PRINTF("remove_tx: handle:%u, size:%u (nbr %u)\n",rm_sf_handle, rm_sf->size.val,id);
    tsch_schedule_remove_slotframe(rm_sf);

    struct tsch_neighbor * n= tsch_queue_get_nbr_from_id(id);
    if(n!=NULL)
    {
      if(!tsch_queue_is_empty(n))
      {
        if(neighbor_has_uc_link(&(n->addr)))
        {
          printf("remove_tx: Use RB %u\n",ringbufindex_elements(&n->tx_ringbuf));
          change_queue_select_packet(id, 1, id % ORCHESTRA_CONF_UNICAST_PERIOD); //Use RB
        }
        else
        {
          printf("remove_tx: Use shared slot %u\n",ringbufindex_elements(&n->tx_ringbuf));
          change_queue_select_packet(id, 2, 0); //Use shared slot
        }
      }
    }
  }

}

uint8_t
has_my_N_changed(uip_ds6_nbr_t * nbr)
{
    //To check whether to match nbr->my_N and installed tx_sf
    uint16_t nbr_id= ID_FROM_IPADDR(&(nbr->ipaddr));
    uint16_t tx_sf_handle = get_tx_sf_handle_from_id(nbr_id);
    struct tsch_slotframe* tx_sf = tsch_schedule_get_slotframe_by_handle(tx_sf_handle);
    uint16_t tx_sf_size;

    if(tx_sf!=NULL)
    {
      tx_sf_size = tx_sf->size.val;

      if(tx_sf_size!=1<<nbr->my_N) 
      {
        return 1;
      }
      else{
        return 0;
      }

      
    }
    else
    {
      return 0;
    }

}


int8_t
tx_installable(uint16_t target_id, uint16_t N, uint16_t t_offset)  //similar with select_t_offset
{
  if(tsch_is_locked()) 
  {
    TSCH_LOG_ADD(tsch_log_message,
            snprintf(log->message, sizeof(log->message),
                "tx_installable: locked");
    );
    return -1;
  }

  t_offset_candidate_t toc[1<<N_MAX];

  //Initialize 2^N toc
  uint16_t i;

  if(t_offset==65535)
  {
    return -2;
  }

  for(i=0; i<(1<<N); i++)
  {
    toc[i].priority=255; //TODO
  }

  if( t_offset < (1<<N) ) // For preparation of weird t_offset
  {
    toc[t_offset].available=1;
  }
  else
  {
    printf("ERROR: weird t_offset %u\n",t_offset);
    return -1;
  }


  //check resource overlap
  PRINTF("\ntx_installable (target %u,%u)\n",N, t_offset);

  struct tsch_slotframe *sf = tsch_schedule_get_slotframe_head();
  while(sf != NULL) {
    if(sf->handle>2  && sf->handle!=get_tx_sf_handle_from_id(target_id)){

      uint16_t n;
      for(n=1;n<=N_MAX;n++)
      {
        if( (sf->size.val>>n) ==1)
        {
          uint16_t used_N=n;

          struct tsch_link *l = list_head(sf->links_list);
          if(l==NULL)
          {
            printf("ERROR: link is null (tx_installable)\n");

          }
          else
          {
            uint16_t used_t_offset= l->timeslot;
            if(used_t_offset>=(1<<used_N))
            {
              printf("ERROR: used_t_offset is too big (tx_installable) %u %u %u %u\n",sf->handle,used_t_offset,used_N, target_id);
            }
            eliminate_overlap_toc(toc, N, used_N,used_t_offset);
            PRINTF("\n");
          }

          break;
        }
        else if (n==N_MAX)
        {
          printf("ERROR: weird size of slotframe");
        }
      }

    }

    sf = list_item_next(sf);
  }




  if(toc[t_offset].available==1)
  {
    PRINTF("tx_installable: %u installable\n\n",t_offset);
    return 1;
  }
  else
  {
    PRINTF("tx_installable: %u uninstallable\n\n",t_offset); 
    return -1;
  }

}

void
process_rx_t_offset(frame802154_t* frame) //In short, prt
{

  linkaddr_t* eack_src= &(current_neighbor->addr);
  uint16_t eack_src_id=TSCH_LOG_ID_FROM_LINKADDR(eack_src);
  uip_ds6_nbr_t *nbr = nbr_table_head(ds6_neighbors);

  
  while(nbr != NULL) {
    uint16_t nbr_id=((nbr->ipaddr.u8[14]) << 8) | (nbr->ipaddr.u8[15]);
    
    if(eack_src_id==nbr_id && is_routing_nbr(nbr)==1)
    {
      
      PRINTF("Rx EACK: t_offset found %u (r_nbr %u)\n",frame->pigg1, nbr_id);        

      if(nbr->my_t_offset!=frame->pigg1 || has_my_N_changed(nbr))
      //has_my_N_changed: corner case
      {

        uint16_t old_t_offset = nbr->my_t_offset;
        
        //To be used in post_process_rx_t_offset
        prt_new_t_offset = frame->pigg1;
        prt_nbr=nbr;

        if(frame->pigg1==T_OFFSET_ALLOCATION_FAIL)
        {
         todo_N_inc=1; 
         return;
          
        }

        if(frame->pigg1==T_OFFSET_CONSECUTIVE_NEW_TX_REQUEST)
        {
         todo_N_inc=1; 
         return;
          
        }

        nbr->my_t_offset = prt_new_t_offset;

        PRINTF("Rx EACK: New t_offset (%u->%u)\n",old_t_offset,prt_new_t_offset);

        if(nbr->my_t_offset==old_t_offset && has_my_N_changed(nbr))
        {
          PRINTF("corner case: has_my_N_changed\n");
        }


        int8_t result=tx_installable(nbr_id,nbr->my_N,nbr->my_t_offset);

        if(result==1) 
        {
          if(nbr->my_uninstallable!=0)
          {
            nbr->my_uninstallable=0;
            todo_N_update=1;
          }
          changed_t_offset_default=0;
          todo_tx_schedule_change=1;
          return;  
        }
        else if(result==-1)
        {
          //printf("%u %u %u\n",old_t_offset,frame->pigg1,has_my_N_changed(nbr));
          if(nbr->my_uninstallable!=1)
          {
            nbr->my_uninstallable=1;
            todo_N_update=1;
          }
          changed_t_offset_default=0;
          todo_tx_schedule_change=1;  //Do not add, just remove

          return;
        }
        else if(result==-2) //when t_offset==65535
        {
          if(nbr->my_uninstallable!=0)
          {
            nbr->my_uninstallable=0;
            todo_N_update=1;
          }
          changed_t_offset_default=1;   
          todo_tx_schedule_change=1;  //Do not add, just remove
        }


      }


      break;
    }


    nbr = nbr_table_next(ds6_neighbors, nbr);
  }

  
  if(is_routing_nbr(nbr)==0)
  {
    PRINTF("Rx EACK: t_offset found %u (No r_nbr)\n",frame->pigg1);
  }
  else if(nbr==NULL){
    PRINTF("Rx EACK: t_offset found %u (No nbr)\n",frame->pigg1);
  }

}

//called from tsch_rx_process_pending (after slot_operation)
void
post_process_rx_t_offset(void)
{

    if(todo_N_inc==1)
    {
      uint16_t nbr_id=ID_FROM_IPADDR(&(prt_nbr->ipaddr));

      todo_N_inc=0;

      if(prt_nbr->my_N < N_MAX)
      {
        printf("N inc %u->%u\n",prt_nbr->my_N,(prt_nbr->my_N)+1);
        (prt_nbr->my_N)++;
        change_queue_N_update(nbr_id,prt_nbr->my_N);
      }
      else
      {
        printf("N inc MAX\n");
      }


    }
    else if(todo_tx_schedule_change==1)
    {
      todo_tx_schedule_change=0;

      if(prt_nbr!=NULL)
      {
        uint16_t nbr_id=ID_FROM_IPADDR(&(prt_nbr->ipaddr));
        remove_tx(nbr_id);

        if(prt_nbr->my_uninstallable==0)
        {
          if(changed_t_offset_default==1)
          {
            printf("New t_offset 65535 (nbr %u)\n",nbr_id);
          }
          else
          {
            add_tx(nbr_id,prt_nbr->my_N,prt_new_t_offset);
            printf("New Tx schedule: %u,%u (nbr %u)\n",prt_nbr->my_N,prt_new_t_offset,nbr_id);
          }
         
        }
        else
        {

          printf("Uninstallable (nbr %u)\n",nbr_id);
        }


        if(todo_N_update==1)
        {
          todo_N_update=0;

          if(prt_nbr->my_uninstallable==0)
          {
            change_queue_N_update(nbr_id,prt_nbr->my_N);
          }
          else
          {
            change_queue_N_update(nbr_id, prt_nbr->my_N + INC_N_NEW_TX_REQUEST);
          }

        }
        //print_nbr();
        //tsch_schedule_print_proposed();

      }

    }

}



#endif //PROPOSED


#if TESLA
uint32_t
load_sum(void)
{
  uint32_t sum=0;
  uip_ds6_nbr_t *nbr=nbr_table_head(ds6_neighbors);;
  while(nbr != NULL) {
    if(nbr->sf_size!=0){  //nbr->sf_size!=0 means nbr is child or parent 
     
      sum+=nbr->num_nbr_tx - nbr->num_nbr_tx_offset;
      
    }
    nbr = nbr_table_next(ds6_neighbors, nbr);
  }//while(nbr != NULL) {

    return sum;
}

int
prr_observed(uip_ds6_nbr_t *nbr)
{

  if((int)(nbr->num_nbr_tx - nbr->num_nbr_tx_offset)>0)
  {
    return 100*(nbr->num_I_rx)/(nbr->num_nbr_tx-nbr->num_nbr_tx_offset);
  }
  else
  {

    return -1;
  }
}

int
prr_contention(uip_ds6_nbr_t *nbr, uint32_t W)
{
  if(W!=0)
  {
    uip_ds6_nbr_t *nbr1 = nbr_table_head(ds6_neighbors);
    int temp_multiple=100;
    while(nbr1 != NULL) {
      if(nbr1!=nbr && nbr1->sf_size!=0) //nbr1->sf_size!=0 means nbr1 is child or parent 
      {
        if(nbr1->num_nbr_tx-nbr1->num_nbr_tx_offset<0)
        {
          return -1;
        }
        temp_multiple=temp_multiple*(100-((nbr1->num_nbr_tx-nbr1->num_nbr_tx_offset)*100/W))/100;
      }
      nbr1 = nbr_table_next(ds6_neighbors, nbr1);
    }
    return temp_multiple;
  }
  else
  {
    return -1;
  }
}
#endif


static void
 reset_num_rx ()
{

  num_idle_rx=0;
  num_idle_rx_ss=0;
  num_succ_rx=0;
  num_succ_rx_ss=0;
  num_coll_rx=0;
  num_coll_rx_ss=0;
  num_total_rx=0;
  num_total_rx_ss=0;

#if PRINT_SELECT_1
  
  TSCH_LOG_ADD(tsch_log_message,
            snprintf(log->message, sizeof(log->message),
                "Reset W");
  );
#endif

#if IOT_LAB_M3

    uint16_t index;
    for(index=1; index<NUM_M3_UID; index++)
    {
        if(m3_uid1[index]==platform_uid())
        {
          #if PRINT_SELECT_1
            //node_id=index;
            printf("Node id is set to %u, %05x\n", index,platform_uid());
          #endif  
            break;
        }
    }

    if(index==NUM_M3_UID)
    {
        printf("ERROR: Not found uid\n");
    }
        

#endif

}


#if TESLA

void
change_num_I_tx_in_packet(uint16_t dest_id, uint8_t * packet)
{
  if(packet==NULL)
  {
    printf("ERROR: NULL in change_num_I_tx_in_packet");
    return ;
  }

  uip_ds6_nbr_t *nbr = nbr_table_head(ds6_neighbors);
  
  while(nbr != NULL) {
    uint16_t nbr_id=((nbr->ipaddr.u8[14]) << 8) | (nbr->ipaddr.u8[15]);
    if(dest_id==nbr_id)
    {
      
      //printf("num_I_tx written update %u ->", packet[2] + (packet[3] << 8));      
      //params.num_I_tx= nbr->num_I_tx;
#if INCLUDE_QUEUE

      uint8_t queue_size=0;

      if(!tsch_is_locked()) {
        
        struct tsch_neighbor *n1 = tsch_queue_get_nbr_from_id(dest_id);
        if(n1!=NULL)
        {
          queue_size= ringbufindex_elements(&n1->tx_ringbuf);
        }
        

      }
    #if PRINT_SELECT  
      printf("num_I_tx written update %u ->", packet[2] + (packet[3] << 8));
    #endif
      packet[2]= (nbr->num_I_tx + queue_size)  & 0xff;
      packet[3]= ( (nbr->num_I_tx + queue_size) >> 8) & 0xff;

      //printf(" %u\n", packet[2] + (packet[3] << 8));
    #if PRINT_SELECT
      printf(" %u + %u(q)\n", nbr->num_I_tx , queue_size);
    #endif

#else
      packet[2]= nbr->num_I_tx & 0xff;
      packet[3]= (nbr->num_I_tx >> 8) & 0xff;
#endif

         

      break;
    }
    nbr = nbr_table_next(ds6_neighbors, nbr);
  }
}        


void 
inc_num_I_tx(int dest_id, uint8_t mac_tx_status, uint8_t num_tx, uint16_t slotframe_handle, struct queuebuf *qb){

  if(dest_id!=0){ //unicast
    if(slotframe_handle==get_tx_sf_handle_from_id((uint16_t)dest_id)){  // unicast tx_sf

      uip_ds6_nbr_t *nbr = nbr_table_head(ds6_neighbors);
  
      while(nbr != NULL) {
        uint16_t nbr_id=((nbr->ipaddr.u8[14]) << 8) | (nbr->ipaddr.u8[15]);
        if(dest_id==nbr_id)
        {
          nbr->num_I_tx++;
        #if PRINT_SELECT
          printf("num_I_tx++ %u (%u)\n",nbr->num_I_tx ,nbr_id);
        #endif

          if(mac_tx_status!=MAC_TX_OK && num_tx < TSCH_MAC_MAX_FRAME_RETRIES+1)
          {

            void* packet = queuebuf_dataptr(qb);

            change_num_I_tx_in_packet(nbr_id,(uint8_t *)packet);
          }

          break;
        }
        nbr = nbr_table_next(ds6_neighbors, nbr);
      }
    }
  }
}

#endif
void update_rx_ratio(void)
{

  if(num_total_rx!=0){
    
    ratio_coll_succ_over_total= 100 * (COLLISION_WEIGHT*num_coll_rx + num_succ_rx) / num_total_rx;


  }

}
void update_rx_ratio_ss(void)
{

  if(num_total_rx_ss!=0){
    ratio_ss_coll_succ_over_total= 100 * (COLLISION_WEIGHT*num_coll_rx_ss + num_succ_rx_ss) / num_total_rx_ss;
  }
}

void collision_detect(uint16_t sf_handle){

  if(sf_handle==1){  //NOTE1    
    
    num_coll_rx++; //JSB
    update_rx_ratio();


    /*TSCH_LOG_ADD(tsch_log_message,
            snprintf(log->message, sizeof(log->message),
            "Rx_CD1 %lu %lu %lu", num_idle_rx, num_succ_rx, num_coll_rx));

    TSCH_LOG_ADD(tsch_log_message,
            snprintf(log->message, sizeof(log->message),
            "Rx_CD2 %lu=%lu, %u%%", num_idle_rx+num_succ_rx+num_coll_rx, num_total_rx,
            ratio_coll_succ_over_total));*/
  }
  else if(sf_handle==2){ //NOTE1
    num_coll_rx_ss++; //JSB: In shared "Rx" slot, we foucs on both unicast and broadcast
    update_rx_ratio_ss();

    /*TSCH_LOG_ADD(tsch_log_message,
            snprintf(log->message, sizeof(log->message),
            "Rx_CD_SS1 %lu %lu %lu", num_idle_rx_ss, num_succ_rx_ss, num_coll_rx_ss));

    TSCH_LOG_ADD(tsch_log_message,
            snprintf(log->message, sizeof(log->message),
            "Rx_CD_SS2 %lu=%lu, %u%%", num_idle_rx_ss+num_succ_rx_ss+num_coll_rx_ss, num_total_rx_ss,
            ratio_ss_coll_succ_over_total));*/
  }

}

void success_detect(uint16_t sf_handle)
{
  if(sf_handle==1){  //NOTE1

    num_succ_rx++; //JSB
    update_rx_ratio();
  }
  else if(sf_handle==2){ //NOTE1
    num_succ_rx_ss++; //JSB
    update_rx_ratio_ss();

  } 
}

void idle_detect(uint16_t sf_handle)
{
  if(sf_handle==1){  //NOTE1

    num_idle_rx++; //JSB
    update_rx_ratio();
  }
  else if(sf_handle==2){ //NOTE1
    num_idle_rx_ss++; //JSB
    update_rx_ratio_ss();

  }
}

/*---------------------------------------------------------------------------*/
/* TSCH locking system. TSCH is locked during slot operations */

/* Is TSCH locked? */
int
tsch_is_locked(void)
{
  return tsch_locked;
}

/* Lock TSCH (no slot operation) */
int
tsch_get_lock(void)
{
  if(!tsch_locked) {
    rtimer_clock_t busy_wait_time;
    int busy_wait = 0; /* Flag used for logging purposes */
    /* Make sure no new slot operation will start */
    tsch_lock_requested = 1;
    /* Wait for the end of current slot operation. */
    if(tsch_in_slot_operation) {
      busy_wait = 1;
      busy_wait_time = RTIMER_NOW();
      while(tsch_in_slot_operation) {
#if CONTIKI_TARGET_COOJA || CONTIKI_TARGET_COOJA_IP64
        simProcessRunValue = 1;
        cooja_mt_yield();
#endif /* CONTIKI_TARGET_COOJA || CONTIKI_TARGET_COOJA_IP64 */
      }
      busy_wait_time = RTIMER_NOW() - busy_wait_time;
    }
    if(!tsch_locked) {
      /* Take the lock if it is free */
      tsch_locked = 1;
      tsch_lock_requested = 0;
      if(busy_wait) {
        /* Issue a log whenever we had to busy wait until getting the lock */
        TSCH_LOG_ADD(tsch_log_message,
            snprintf(log->message, sizeof(log->message),
                "!get lock delay %u", (unsigned)busy_wait_time);
        );
      }
      return 1;
    }
  }
  TSCH_LOG_ADD(tsch_log_message,
      snprintf(log->message, sizeof(log->message),
                      "!failed to lock");
          );
  return 0;
}

/* Release TSCH lock */
void
tsch_release_lock(void)
{
  tsch_locked = 0;
}

/*---------------------------------------------------------------------------*/
/* Channel hopping utility functions */

/* Return channel from ASN and channel offset */
uint8_t
tsch_calculate_channel(struct tsch_asn_t *asn, uint8_t channel_offset)
{
  uint16_t index_of_0 = TSCH_ASN_MOD(*asn, tsch_hopping_sequence_length);
  uint16_t index_of_offset = (index_of_0 + channel_offset) % tsch_hopping_sequence_length.val;
  return tsch_hopping_sequence[index_of_offset];
}

/*---------------------------------------------------------------------------*/
/* Timing utility functions */

/* Checks if the current time has passed a ref time + offset. Assumes
 * a single overflow and ref time prior to now. */
static uint8_t
check_timer_miss(rtimer_clock_t ref_time, rtimer_clock_t offset, rtimer_clock_t now)
{
  rtimer_clock_t target = ref_time + offset;
  int now_has_overflowed = now < ref_time;
  int target_has_overflowed = target < ref_time; // offset < 0

  if(now_has_overflowed == target_has_overflowed) {
    /* Both or none have overflowed, just compare now to the target */
    return target <= now;                       // ref_time +offset <= now
  } else {
    /* Either now or target of overflowed.
     * If it is now, then it has passed the target.
     * If it is target, then we haven't reached it yet.
     *  */
    return now_has_overflowed;
  }
}
/*---------------------------------------------------------------------------*/
#if TESLA
void reset_num_nbr()
{
  uip_ds6_nbr_t *nbr = nbr_table_head(ds6_neighbors);
  while(nbr != NULL) {
    //uint16_t nbr_id=((nbr->ipaddr.u8[14]) << 8) | (nbr->ipaddr.u8[15]);
    //printf("num_I_rx set 0 (%u)\n",nbr_id);        
    nbr->num_I_tx=0;
    nbr->num_nbr_tx=0;
    nbr->num_I_rx=0;

    nbr->num_nbr_tx_offset=0;

    nbr = nbr_table_next(ds6_neighbors, nbr);
  }

  reset_num_rx();

  uint16_t rand=random_rand()%SF_SIZE_CHECK_PERIOD;
  ctimer_set(&periodic_timer, SF_SIZE_CHECK_PERIOD/2+rand, slotframe_size_adaptation, NULL); //JSB
  

}
#endif
/*---------------------------------------------------------------------------*/
/* Schedule a wakeup at a specified offset from a reference time.
 * Provides basic protection against missed deadlines and timer overflows
 * A return value of zero signals a missed deadline: no rtimer was scheduled. */
/*static uint8_t
tsch_schedule_slot_operation_prev(struct rtimer *tm, rtimer_clock_t ref_time, rtimer_clock_t offset, const char *str)
{
  rtimer_clock_t now = RTIMER_NOW();
  int r;
  // Subtract RTIMER_GUARD before checking for deadline miss
  // because we can not schedule rtimer less than RTIMER_GUARD in the future 
  int missed = check_timer_miss(ref_time, offset - RTIMER_GUARD, now);

 
  if(missed) {
    TSCH_LOG_ADD(tsch_log_message,
                snprintf(log->message, sizeof(log->message),
                    "!dl-miss %s %d %d",
                        str, (int)(now-ref_time), (int)offset);
    );

    return 0;
  }
  ref_time += offset;
  r = rtimer_set(tm, ref_time, 1, (void (*)(struct rtimer *, void *))tsch_slot_operation, NULL);
  if(r != RTIMER_OK) {
    return 0;
  }

  




  return 1;
}*/

static uint8_t
tsch_schedule_slot_operation(struct rtimer *tm, rtimer_clock_t ref_time, rtimer_clock_t offset, const char *str)
{
  rtimer_clock_t now = RTIMER_NOW();
  int r;
  /* Subtract RTIMER_GUARD before checking for deadline miss
   * because we can not schedule rtimer less than RTIMER_GUARD in the future */
  int missed = check_timer_miss(ref_time, offset - RTIMER_GUARD, now);
  //printf("c-1\n");
  if(missed) {
    /*TSCH_LOG_ADD(tsch_log_message,
                snprintf(log->message, sizeof(log->message),
                    "!dl-miss %s %d %d",
                        str, (int)(now-ref_time), (int)offset);
    );*/
  } else {
    r = rtimer_set(tm, ref_time + offset, 1, (void (*)(struct rtimer *, void *))tsch_slot_operation, NULL);
    if(r == RTIMER_OK) {

      static uint8_t reset = 0;
      if(reset==0)
      {
        if(tsch_current_asn.ls4b > (NO_DATA_PERIOD)*1000/10){
        //if(tsch_current_asn.ls4b > (NO_DATA_PERIOD)*1000/10){
    #if TESLA
          reset_num_nbr();
    #else
          reset_num_rx();
    #endif
          num_total_rx_accum=0;
    #if PROPOSED
          num_total_auto_rx_accum=0;
    #endif
          reset=1;
        }
      }
      //printf("c0 good\n");
      return 1;

    }
  }

  //printf("c0 bad\n");
  /* block until the time to schedule comes */
  BUSYWAIT_UNTIL_ABS(0, ref_time, offset);
  return 0;
}
/*---------------------------------------------------------------------------*/
/* Schedule slot operation conditionally, and YIELD if success only.
 * Always attempt to schedule RTIMER_GUARD before the target to make sure to wake up
 * ahead of time and then busy wait to exactly hit the target. */

#define TSCH_SCHEDULE_AND_YIELD(pt, tm, ref_time, offset, str) \
  do { \
    if(tsch_schedule_slot_operation(tm, ref_time, offset - RTIMER_GUARD, str)) { \
      PT_YIELD(pt); \
      BUSYWAIT_UNTIL_ABS(0, ref_time, offset); \
    } \
  } while(0);  
/*---------------------------------------------------------------------------*/
/* Get EB, broadcast or unicast packet to be sent, and target neighbor. */
static struct tsch_packet *
get_packet_and_neighbor_for_link(struct tsch_link *link, struct tsch_neighbor **target_neighbor)
{
  struct tsch_packet *p = NULL;
  struct tsch_neighbor *n = NULL;
  //uint8_t check1=0;
  /* Is this a Tx link? */
  if(link->link_options & LINK_OPTION_TX) {
    /* is it for advertisement of EB? */
    if(link->link_type == LINK_TYPE_ADVERTISING || link->link_type == LINK_TYPE_ADVERTISING_ONLY) {
      /* fetch EB packets */
      n = n_eb;
      p = tsch_queue_get_packet_for_nbr(n, link);

    }
    

    if(link->link_type != LINK_TYPE_ADVERTISING_ONLY) {
      /* NORMAL link or no EB to send, pick a data packet */
      if(p == NULL) {
        /* Get neighbor queue associated to the link and get packet from it */
        n = tsch_queue_get_nbr(&link->addr);
        p = tsch_queue_get_packet_for_nbr(n, link);

        /* if it is a broadcast slot and there were no broadcast packets, pick any unicast packet */
        if(p == NULL && n == n_broadcast) {
          /*TSCH_LOG_ADD(tsch_log_message,
                        snprintf(log->message, sizeof(log->message),
                        "SH"));*/
          /*TSCH_LOG_ADD(tsch_log_message,
                        snprintf(log->message, sizeof(log->message),
                        "n->addr1 %u %u %u",link->addr.u8[LINKADDR_SIZE-1],n->addr.u8[LINKADDR_SIZE-1] ,n == n_broadcast));
          */
          //check1=1;
          p = tsch_queue_get_unicast_packet_for_any(&n, link);

        
        }
      }
    }
  }
  /* return nbr (by reference) */
  if(target_neighbor != NULL) {
    *target_neighbor = n;
  }

  /*if(p!=NULL)
  {
    TSCH_LOG_ADD(tsch_log_message,
                        snprintf(log->message, sizeof(log->message),
                        "n->addr2 %u %u %u",link->addr.u8[LINKADDR_SIZE-1],n->addr.u8[LINKADDR_SIZE-1] ,n == n_broadcast));
  }*/
#if PROPOSED & RESIDUAL_ALLOC
  if(link->slotframe_handle > SSQ_SCHEDULE_HANDLE_OFFSET && link->link_options == LINK_OPTION_TX && p==NULL)
  {
      //struct tsch_neighbor *n1 = tsch_queue_get_nbr(&link->addr);
      //printf("ERROR: c4 %u %u %u %u %d\n",link->link_type != LINK_TYPE_ADVERTISING_ONLY,  
        //n1!=NULL, n1 == n_broadcast, check1, tsch_is_locked());
  }
#endif
  return p;
}
/*---------------------------------------------------------------------------*/

#if TESLA
void check_queued_packet(struct tsch_neighbor *n) //check only 1st packet in queue
{
  if(n!=NULL)
  {
    if(!tsch_queue_is_empty(n))
    {
      int16_t get_index = ringbufindex_peek_get(&n->tx_ringbuf);
      if(get_index!=-1)
      {
        struct tsch_packet * next_packet = n->tx_array[get_index];
        
        void* packet=queuebuf_dataptr(next_packet->qb);

        uint16_t dest_id=TSCH_LOG_ID_FROM_LINKADDR(queuebuf_addr(next_packet->qb, PACKETBUF_ADDR_RECEIVER));

        change_num_I_tx_in_packet(dest_id,(uint8_t *)packet);

      }
      else
      {
        printf("ERROR: Not tsch_queue_is_empty, but get_index=-1\n");
      }
    }
  }
}
#endif
/*---------------------------------------------------------------------------*/
/* Post TX: Update neighbor state after a transmission */
static int
update_neighbor_state(struct tsch_neighbor *n, struct tsch_packet *p,
                      struct tsch_link *link, uint8_t mac_tx_status)
{
  int in_queue = 1;
  int is_shared_link = link->link_options & LINK_OPTION_SHARED;
  int is_unicast = !n->is_broadcast;

  if(mac_tx_status == MAC_TX_OK) {
    /* Successful transmission */
    tsch_queue_remove_packet_from_queue(n);
#if TESLA

    int dest_id= TSCH_LOG_ID_FROM_LINKADDR(queuebuf_addr(p->qb, PACKETBUF_ADDR_RECEIVER));
    if(dest_id!=0){ //unicast
      if(link->slotframe_handle==get_tx_sf_handle_from_id((uint16_t)dest_id)) // unicast tx_sf
      {
        check_queued_packet(n);

      }
      else if(link->slotframe_handle==2)
      {
        check_queued_packet(n);
      }
      
    }  

#endif
    in_queue = 0;

    /* Update CSMA state in the unicast case */
    if(is_unicast) {
      if(is_shared_link || tsch_queue_is_empty(n)) {
        /* If this is a shared link, reset backoff on success.
         * Otherwise, do so only is the queue is empty */
        tsch_queue_backoff_reset(n);
      }
    }
  } else {
    /* Failed transmission */
    if(p->transmissions >= TSCH_MAC_MAX_FRAME_RETRIES + 1) {
      /* Drop packet */
      tsch_queue_remove_packet_from_queue(n);
#if TESLA

    int dest_id= TSCH_LOG_ID_FROM_LINKADDR(queuebuf_addr(p->qb, PACKETBUF_ADDR_RECEIVER));
    if(dest_id!=0){ //unicast
      if(link->slotframe_handle==get_tx_sf_handle_from_id((uint16_t)dest_id)) // unicast tx_sf
      {
        check_queued_packet(n);
      }
      else if(link->slotframe_handle==2)
      {
        check_queued_packet(n);
      }
    }  

#endif
      in_queue = 0;
    }
    /* Update CSMA state in the unicast case */
    if(is_unicast) {
      /* Failures on dedicated (== non-shared) leave the backoff
       * window nor exponent unchanged */
      if(is_shared_link) {
        /* Shared link: increment backoff exponent, pick a new window */
        tsch_queue_backoff_inc(n);

      }
    }
  }

  return in_queue;
}
/*---------------------------------------------------------------------------*/
/**
 * This function turns on the radio. Its semantics is dependent on
 * the value of TSCH_RADIO_ON_DURING_TIMESLOT constant:
 * - if enabled, the radio is turned on at the start of the slot
 * - if disabled, the radio is turned on within the slot,
 *   directly before the packet Rx guard time and ACK Rx guard time.
 */
static void
tsch_radio_on(enum tsch_radio_state_on_cmd command)
{
  int do_it = 0;
  switch(command) {
  case TSCH_RADIO_CMD_ON_START_OF_TIMESLOT:
    if(TSCH_RADIO_ON_DURING_TIMESLOT) {
      do_it = 1;
    }
    break;
  case TSCH_RADIO_CMD_ON_WITHIN_TIMESLOT:
    if(!TSCH_RADIO_ON_DURING_TIMESLOT) {
      do_it = 1;
    }
    break;
  case TSCH_RADIO_CMD_ON_FORCE:
    do_it = 1;
    break;
  }
  if(do_it) {
    NETSTACK_RADIO.on();
  }
}
/*---------------------------------------------------------------------------*/
/**
 * This function turns off the radio. In the same way as for tsch_radio_on(),
 * it depends on the value of TSCH_RADIO_ON_DURING_TIMESLOT constant:
 * - if enabled, the radio is turned off at the end of the slot
 * - if disabled, the radio is turned off within the slot,
 *   directly after Tx'ing or Rx'ing a packet or Tx'ing an ACK.
 */
static void
tsch_radio_off(enum tsch_radio_state_off_cmd command)
{
  int do_it = 0;
  switch(command) {
  case TSCH_RADIO_CMD_OFF_END_OF_TIMESLOT:
    if(TSCH_RADIO_ON_DURING_TIMESLOT) {
      do_it = 1;
    }
    break;
  case TSCH_RADIO_CMD_OFF_WITHIN_TIMESLOT:
    if(!TSCH_RADIO_ON_DURING_TIMESLOT) {
      do_it = 1;
    }
    break;
  case TSCH_RADIO_CMD_OFF_FORCE:
    do_it = 1;
    break;
  }
  if(do_it) {
    NETSTACK_RADIO.off();
  }
}
/*---------------------------------------------------------------------------*/

#if PROPOSED && RESIDUAL_ALLOC
uint8_t
reserved_ssq(uint16_t nbr_id)
{
  uint8_t i;
  for(i=0; i<16; i++)
  {
    if(ssq_schedule_list[i].asn.ls4b==0 && ssq_schedule_list[i].asn.ms1b==0)
    {

    }
    else if(!(ssq_schedule_list[i].asn.ls4b==tsch_current_asn.ls4b && ssq_schedule_list[i].asn.ms1b==tsch_current_asn.ms1b))
    {
      if(ssq_schedule_list[i].link.link_options==LINK_OPTION_TX)
      {
        uint16_t id=(ssq_schedule_list[i].link.slotframe_handle-SSQ_SCHEDULE_HANDLE_OFFSET-1)/2;
        
        if(nbr_id==id)
        {
          printf("reserved for %u\n",id);
          return 1;
        }
      }
      
    }
  }

  return 0;

}

void
remove_reserved_ssq(uint16_t nbr_id)
{
  uint8_t i;
  for(i=0; i<16; i++)
  {
    if(ssq_schedule_list[i].asn.ls4b==0 && ssq_schedule_list[i].asn.ms1b==0)
    {

    }
    else if(!(ssq_schedule_list[i].asn.ls4b==tsch_current_asn.ls4b && ssq_schedule_list[i].asn.ms1b==tsch_current_asn.ms1b))
    {
      if(ssq_schedule_list[i].link.link_options==LINK_OPTION_TX)
      {
        uint16_t id=(ssq_schedule_list[i].link.slotframe_handle-SSQ_SCHEDULE_HANDLE_OFFSET-1)/2;
        
        if(nbr_id==id)
        {
          ssq_schedule_list[i].asn.ls4b=0;
          ssq_schedule_list[i].asn.ms1b=0;
        }
      }
      
    }
  }


}


#endif



static
PT_THREAD(tsch_tx_slot(struct pt *pt, struct rtimer *t))
{
  /**
   * TX slot:
   * 1. Copy packet to radio buffer
   * 2. Perform CCA if enabled
   * 3. Sleep until it is time to transmit
   * 4. Wait for ACK if it is a unicast packet
   * 5. Extract drift if we received an E-ACK from a time source neighbor
   * 6. Update CSMA parameters according to TX status
   * 7. Schedule mac_call_sent_callback
   **/

  /* tx status */
  static uint8_t mac_tx_status;
  /* is the packet in its neighbor's queue? */
  uint8_t in_queue;
  static int dequeued_index;
  static int packet_ready = 1;

  PT_BEGIN(pt);

  TSCH_DEBUG_TX_EVENT();

  /* First check if we have space to store a newly dequeued packet (in case of
   * successful Tx or Drop) */
  dequeued_index = ringbufindex_peek_put(&dequeued_ringbuf);
  if(dequeued_index != -1) {
    if(current_packet == NULL || current_packet->qb == NULL) {
      mac_tx_status = MAC_TX_ERR_FATAL;
    } else {
      /* packet payload */
      static void *packet;
#if LLSEC802154_ENABLED
      /* encrypted payload */
      static uint8_t encrypted_packet[TSCH_PACKET_MAX_LEN];
#endif /* LLSEC802154_ENABLED */
      /* packet payload length */
      static uint8_t packet_len;
      /* packet seqno */
      static uint8_t seqno;
      /* is this a broadcast packet? (wait for ack?) */
      static uint8_t is_broadcast;
      static rtimer_clock_t tx_start_time;

#if CCA_ENABLED
      static uint8_t cca_status;
#endif

      /* get payload */
      packet = queuebuf_dataptr(current_packet->qb);
      packet_len = queuebuf_datalen(current_packet->qb);
      /* is this a broadcast packet? (wait for ack?) */
      is_broadcast = current_neighbor->is_broadcast;
      /* read seqno from payload */

      seqno = ((uint8_t *)(packet))[2];

#if TESLA
 
      seqno = ((uint8_t *)(packet))[4];
#endif

#if PROPOSED

#if RESIDUAL_ALLOC
      frame802154_fcf_t fcf;

      frame802154_parse_fcf((uint8_t *)(packet), &fcf);

      //printf("Orig frame pending=%u\n",fcf.frame_pending);
      
      int queued_pkts=ringbufindex_elements(&current_neighbor->tx_ringbuf);
      uint16_t nbr_id=TSCH_LOG_ID_FROM_LINKADDR(&current_neighbor->addr);
      if(fcf.ack_required)
      {
#if !NO_ON_DEMAND_PROVISION
        if(queued_pkts > 1)
        {
          if(!reserved_ssq(nbr_id))
          {

            PRINTF("Tx queue %u\n",queued_pkts);
            fcf.frame_pending= 1;

            uint16_t ssq_schedule = tsch_schedule_get_subsequent_schedule(&tsch_current_asn);

            /*********To disable ssq bit after periodic provision Tx********/
            uint16_t tx_sf_handle= get_tx_sf_handle_from_id(nbr_id);
            struct tsch_slotframe* sf=tsch_schedule_get_slotframe_by_handle(tx_sf_handle); //Periodic provision

            if(sf!=NULL)
            {
              uint16_t timeslot = TSCH_ASN_MOD(tsch_current_asn, sf->size);
              struct tsch_link *l = list_head(sf->links_list);
              if(l!=NULL)
              {
                uint16_t time_to_timeslot =
                  l->timeslot > timeslot ?
                  l->timeslot - timeslot :
                  sf->size.val + l->timeslot - timeslot;

                //printf("c tts %u\n");
                if((time_to_timeslot-1)<16)
                {
                  uint8_t i;
                  for(i=time_to_timeslot-1; i<16; i++)
                  {
                    ssq_schedule = ssq_schedule | (1<<i);
                  }
                }

              }
              else
              {
                printf("ERROR: No tx link in tx sf\n");
              }

            }
            
            /******************************End**********************/


            PRINTF("Tx uc: ssq_schedule make 0x%x\n",ssq_schedule);

            ((uint8_t *)(packet))[4] = ssq_schedule & 0xff;
            ((uint8_t *)(packet))[5] = (ssq_schedule >> 8) & 0xff;

            //printf("Tx uc: ssq_schedule make %u %u\n",((uint8_t *)(packet))[4],((uint8_t *)(packet))[5]);
          }
          else
          {
            fcf.frame_pending= 0;
            ((uint8_t *)(packet))[4]=255;
            ((uint8_t *)(packet))[5]=255;
          }

        }
        else if (queued_pkts == 1)
        {
          fcf.frame_pending= 0;
          ((uint8_t *)(packet))[4]=255;
          ((uint8_t *)(packet))[5]=255;     //65535
        }
        else
        {
          printf("ERROR: No packet in Tx queue\n");
        }     
#else
        fcf.frame_pending= 0;
        ((uint8_t *)(packet))[4]=255;
        ((uint8_t *)(packet))[5]=255;
#endif
        

        frame802154_create_fcf(&fcf,(uint8_t *)(packet));
      }
      else
      {
        ((uint8_t *)(packet))[4]=255;
        ((uint8_t *)(packet))[5]=255;
      }




      seqno = ((uint8_t *)(packet))[6];
#else
      seqno = ((uint8_t *)(packet))[4];
#endif

#endif      


      /* if this is an EB, then update its Sync-IE */
      if(current_neighbor == n_eb) {
        packet_ready = tsch_packet_update_eb(packet, packet_len, current_packet->tsch_sync_ie_offset);
      } else {
        packet_ready = 1;
      }

#if LLSEC802154_ENABLED
      if(tsch_is_pan_secured) {
        /* If we are going to encrypt, we need to generate the output in a separate buffer and keep
         * the original untouched. This is to allow for future retransmissions. */
        int with_encryption = queuebuf_attr(current_packet->qb, PACKETBUF_ATTR_SECURITY_LEVEL) & 0x4;
        packet_len += tsch_security_secure_frame(packet, with_encryption ? encrypted_packet : packet, current_packet->header_len,
            packet_len - current_packet->header_len, &tsch_current_asn);
        if(with_encryption) {
          packet = encrypted_packet;
        }
      }
#endif /* LLSEC802154_ENABLED */

      /* prepare packet to send: copy to radio buffer */
      if(packet_ready && NETSTACK_RADIO.prepare(packet, packet_len) == 0) { /* 0 means success */
        static rtimer_clock_t tx_duration;

#if CCA_ENABLED
        cca_status = 1;
        /* delay before CCA */
        TSCH_SCHEDULE_AND_YIELD(pt, t, current_slot_start, TS_CCA_OFFSET, "cca");
        TSCH_DEBUG_TX_EVENT();
        tsch_radio_on(TSCH_RADIO_CMD_ON_WITHIN_TIMESLOT);
        /* CCA */
        BUSYWAIT_UNTIL_ABS(!(cca_status |= NETSTACK_RADIO.channel_clear()),
                           current_slot_start, TS_CCA_OFFSET + TS_CCA);
        TSCH_DEBUG_TX_EVENT();
        /* there is not enough time to turn radio off */
        /*  NETSTACK_RADIO.off(); */
        if(cca_status == 0) {
          mac_tx_status = MAC_TX_COLLISION;
        } else
#endif /* CCA_ENABLED */
        {
          /* delay before TX */
          TSCH_SCHEDULE_AND_YIELD(pt, t, current_slot_start, tsch_timing[tsch_ts_tx_offset] - RADIO_DELAY_BEFORE_TX, "TxBeforeTx");
          TSCH_DEBUG_TX_EVENT();
          /* send packet already in radio tx buffer */
          mac_tx_status = NETSTACK_RADIO.transmit(packet_len);
          /* Save tx timestamp */
          tx_start_time = current_slot_start + tsch_timing[tsch_ts_tx_offset];
          /* calculate TX duration based on sent packet len */
          tx_duration = TSCH_PACKET_DURATION(packet_len);
          /* limit tx_time to its max value */
          tx_duration = MIN(tx_duration, tsch_timing[tsch_ts_max_tx]);
          /* turn tadio off -- will turn on again to wait for ACK if needed */
          tsch_radio_off(TSCH_RADIO_CMD_OFF_WITHIN_TIMESLOT);

          if(mac_tx_status == RADIO_TX_OK) {
            if(!is_broadcast) {
              uint8_t ackbuf[TSCH_PACKET_MAX_LEN];
              int ack_len;
              rtimer_clock_t ack_start_time;
              int is_time_source;
              struct ieee802154_ies ack_ies;
              uint8_t ack_hdrlen;
              frame802154_t frame;

#if TSCH_HW_FRAME_FILTERING
              radio_value_t radio_rx_mode;
              /* Entering promiscuous mode so that the radio accepts the enhanced ACK */
              NETSTACK_RADIO.get_value(RADIO_PARAM_RX_MODE, &radio_rx_mode);
              NETSTACK_RADIO.set_value(RADIO_PARAM_RX_MODE, radio_rx_mode & (~RADIO_RX_MODE_ADDRESS_FILTER));
#endif /* TSCH_HW_FRAME_FILTERING */
              /* Unicast: wait for ack after tx: sleep until ack time */
              TSCH_SCHEDULE_AND_YIELD(pt, t, current_slot_start,
                  tsch_timing[tsch_ts_tx_offset] + tx_duration + tsch_timing[tsch_ts_rx_ack_delay] - RADIO_DELAY_BEFORE_RX, "TxBeforeAck");
              TSCH_DEBUG_TX_EVENT();
              tsch_radio_on(TSCH_RADIO_CMD_ON_WITHIN_TIMESLOT);
              /* Wait for ACK to come */
              BUSYWAIT_UNTIL_ABS(NETSTACK_RADIO.receiving_packet(),
                  tx_start_time, tx_duration + tsch_timing[tsch_ts_rx_ack_delay] + tsch_timing[tsch_ts_ack_wait] + RADIO_DELAY_BEFORE_DETECT);
              TSCH_DEBUG_TX_EVENT();

              ack_start_time = RTIMER_NOW() - RADIO_DELAY_BEFORE_DETECT;

              /* Wait for ACK to finish */
              BUSYWAIT_UNTIL_ABS(!NETSTACK_RADIO.receiving_packet(),
                                 ack_start_time, tsch_timing[tsch_ts_max_ack]);
              TSCH_DEBUG_TX_EVENT();
              tsch_radio_off(TSCH_RADIO_CMD_OFF_WITHIN_TIMESLOT);

#if TSCH_HW_FRAME_FILTERING
              /* Leaving promiscuous mode */
              NETSTACK_RADIO.get_value(RADIO_PARAM_RX_MODE, &radio_rx_mode);
              NETSTACK_RADIO.set_value(RADIO_PARAM_RX_MODE, radio_rx_mode | RADIO_RX_MODE_ADDRESS_FILTER);
#endif /* TSCH_HW_FRAME_FILTERING */

              /* Read ack frame */
              ack_len = NETSTACK_RADIO.read((void *)ackbuf, sizeof(ackbuf));

              is_time_source = 0;
              /* The radio driver should return 0 if no valid packets are in the rx buffer */
              if(ack_len > 0) {
                is_time_source = current_neighbor != NULL && current_neighbor->is_time_source;
#if PROPOSED

                if(tsch_packet_parse_eack(ackbuf, ack_len, seqno,
                    &frame, &ack_ies, &ack_hdrlen, &(current_neighbor->addr)) == 0) {
                  //printf("ERROR: tsch_packet_parse_eack\n");
                  ack_len = 0;
                }

                todo_tx_schedule_change=0;
                todo_N_update=0;
                todo_N_inc=0;

                if(ack_len != 0) {
                  process_rx_t_offset(&frame);
                  process_rx_matching_slot(&frame);
                }

#else
                if(tsch_packet_parse_eack(ackbuf, ack_len, seqno,
                    &frame, &ack_ies, &ack_hdrlen) == 0) {
                  ack_len = 0;
                }
#endif 


#if TESLA                
                 ack_len_store=ack_len;
#endif


#if LLSEC802154_ENABLED
                if(ack_len != 0) {
                  if(!tsch_security_parse_frame(ackbuf, ack_hdrlen, ack_len - ack_hdrlen - tsch_security_mic_len(&frame),
                      &frame, &current_neighbor->addr, &tsch_current_asn)) {
                    TSCH_LOG_ADD(tsch_log_message,
                        snprintf(log->message, sizeof(log->message),
                        "!failed to authenticate ACK"));
                    ack_len = 0;
                  }
                } else {
                  TSCH_LOG_ADD(tsch_log_message,
                      snprintf(log->message, sizeof(log->message),
                      "!failed to parse ACK"));
                }
#endif /* LLSEC802154_ENABLED */
              }

              if(ack_len != 0) {
#if TESLA                
                 ack_ies_store=ack_ies;
#endif
                if(is_time_source) {
                  int32_t eack_time_correction = US_TO_RTIMERTICKS(ack_ies.ie_time_correction);
                  int32_t since_last_timesync = TSCH_ASN_DIFF(tsch_current_asn, last_sync_asn);
                  if(eack_time_correction > SYNC_IE_BOUND) {
                    drift_correction = SYNC_IE_BOUND;
                  } else if(eack_time_correction < -SYNC_IE_BOUND) {
                    drift_correction = -SYNC_IE_BOUND;
                  } else {
                    drift_correction = eack_time_correction;
                  }
                  if(drift_correction != eack_time_correction) {
                    TSCH_LOG_ADD(tsch_log_message,
                        snprintf(log->message, sizeof(log->message),
                            "!truncated dr %d %d", (int)eack_time_correction, (int)drift_correction);
                    );
                  }
                  is_drift_correction_used = 1;
                  tsch_timesync_update(current_neighbor, since_last_timesync, drift_correction);
                  /* Keep track of sync time */
                  last_sync_asn = tsch_current_asn;
                  tsch_schedule_keepalive();
                }
                mac_tx_status = MAC_TX_OK;
              } else {
                mac_tx_status = MAC_TX_NOACK;
              }
            } else {
              mac_tx_status = MAC_TX_OK;
            }
          } else {
            mac_tx_status = MAC_TX_ERR;
          }
        }
      }

      /*TSCH_LOG_ADD(tsch_log_message,
        snprintf(log->message, sizeof(log->message),
          "Tx Length, Duration: %u %uus",packet_len, 32 * ((packet_len) + 3)));*/
    }

    tsch_radio_off(TSCH_RADIO_CMD_OFF_END_OF_TIMESLOT);
    

    current_packet->transmissions++;
    current_packet->ret = mac_tx_status;

#if TESLA

    int dest_id= TSCH_LOG_ID_FROM_LINKADDR(queuebuf_addr(current_packet->qb, PACKETBUF_ADDR_RECEIVER));
    inc_num_I_tx(dest_id, mac_tx_status,current_packet->transmissions, 
      current_link->slotframe_handle, current_packet->qb);

#endif

    /* Post TX: Update neighbor state */
    in_queue = update_neighbor_state(current_neighbor, current_packet, current_link, mac_tx_status);

    /* The packet was dequeued, add it to dequeued_ringbuf for later processing */
    if(in_queue == 0) {
      dequeued_array[dequeued_index] = current_packet;
      ringbufindex_put(&dequeued_ringbuf);
    }

    /* Log every tx attempt */
    TSCH_LOG_ADD(tsch_log_tx,
        log->tx.mac_tx_status = mac_tx_status;
    log->tx.num_tx = current_packet->transmissions;
    log->tx.datalen = queuebuf_datalen(current_packet->qb);
    log->tx.drift = drift_correction;
    log->tx.drift_used = is_drift_correction_used;
    log->tx.is_data = ((((uint8_t *)(queuebuf_dataptr(current_packet->qb)))[0]) & 7) == FRAME802154_DATAFRAME;
#if LLSEC802154_ENABLED
    log->tx.sec_level = queuebuf_attr(current_packet->qb, PACKETBUF_ATTR_SECURITY_LEVEL);
#else /* LLSEC802154_ENABLED */
    log->tx.sec_level = 0;
#endif /* LLSEC802154_ENABLED */
    log->tx.dest = TSCH_LOG_ID_FROM_LINKADDR(queuebuf_addr(current_packet->qb, PACKETBUF_ADDR_RECEIVER));

#if TESLA
    log->tx.ack_len= ack_len_store;
    log->tx.ack_sf_size=ack_ies_store.sf_size;
    log->tx.ack_sf_size_version=ack_ies_store.sf_size_version;

    ack_len_store=0;

#endif

    );
    
    /* Poll process for later processing of packet sent events and logs */
    process_poll(&tsch_pending_events_process);
  }

  #if PROPOSED && RESIDUAL_ALLOC
    if(current_link->slotframe_handle > SSQ_SCHEDULE_HANDLE_OFFSET)
    {
      remove_matching_slot();
    }

    if(current_link->slotframe_handle==1) //RB
    {
      int queued_pkts=ringbufindex_elements(&current_neighbor->tx_ringbuf);
      uint16_t nbr_id=TSCH_LOG_ID_FROM_LINKADDR(&current_neighbor->addr);

      if(reserved_ssq(nbr_id) && queued_pkts==0) //Tx occurs by RB before reserved ssq Tx
      {
        printf("Remove Tx ssq by RB (nbr %u)\n",nbr_id);
        remove_reserved_ssq(nbr_id);
      } 
    }
  #endif

  TSCH_DEBUG_TX_EVENT();

  PT_END(pt);
}
/*---------------------------------------------------------------------------*/
static
PT_THREAD(tsch_rx_slot(struct pt *pt, struct rtimer *t))
{
  /**
   * RX slot:
   * 1. Check if it is used for TIME_KEEPING
   * 2. Sleep and wake up just before expected RX time (with a guard time: TS_LONG_GT)
   * 3. Check for radio activity for the guard time: TS_LONG_GT
   * 4. Prepare and send ACK if needed
   * 5. Drift calculated in the ACK callback registered with the radio driver. Use it if receiving from a time source neighbor.
   **/

  struct tsch_neighbor *n;
  static linkaddr_t source_address;
  static linkaddr_t destination_address;
  static int16_t input_index;
  static int input_queue_drop = 0;


  PT_BEGIN(pt);

  TSCH_DEBUG_RX_EVENT();

  input_index = ringbufindex_peek_put(&input_ringbuf);
  if(input_index == -1) {
    input_queue_drop++;
  } else {
    static struct input_packet *current_input;
    /* Estimated drift based on RX time */
    static int32_t estimated_drift;
    /* Rx timestamps */
    static rtimer_clock_t rx_start_time;
    static rtimer_clock_t expected_rx_time;
    static rtimer_clock_t packet_duration;
    uint8_t packet_seen;
    //struct tsch_slotframe *sf1 = tsch_schedule_get_slotframe_by_handle(current_link->slotframe_handle); //JSB
    static int delay_prN;
    static int delay_prs;

    expected_rx_time = current_slot_start + tsch_timing[tsch_ts_tx_offset];
    /* Default start time: expected Rx time */
    rx_start_time = expected_rx_time;

    current_input = &input_array[input_index];

    /* Wait before starting to listen */
    TSCH_SCHEDULE_AND_YIELD(pt, t, current_slot_start, tsch_timing[tsch_ts_rx_offset] - RADIO_DELAY_BEFORE_RX, "RxBeforeListen");
    TSCH_DEBUG_RX_EVENT();

    /* Start radio for at least guard time */
   
    //rtimer_clock_t on1=RTIMER_NOW();

    tsch_radio_on(TSCH_RADIO_CMD_ON_WITHIN_TIMESLOT);

    //rtimer_clock_t on2=RTIMER_NOW();


    /*TSCH_LOG_ADD(tsch_log_message,
          snprintf(log->message, sizeof(log->message),
                "ON %lu %lu Delay %u",on1,on2,RADIO_DELAY_BEFORE_DETECT));*/


    packet_seen = NETSTACK_RADIO.receiving_packet() || NETSTACK_RADIO.pending_packet();

    //TSCH_LOG_ADD(tsch_log_message,
    //            snprintf(log->message, sizeof(log->message),
    //            "1. rp=%u pp=%u", NETSTACK_RADIO.receiving_packet(), NETSTACK_RADIO.pending_packet()));



#if !PROPOSED & !TESLA & !USE_6TISCH_MINIMAL & !ORCHESTRA_CONF_UNICAST_SENDER_BASED
//RB
    if(current_link->slotframe_handle==1){  //NOTE1   
      num_total_rx_accum++;
    }
#endif

#if !PROPOSED & !TESLA & !USE_6TISCH_MINIMAL & ORCHESTRA_CONF_UNICAST_SENDER_BASED
//SB
    if(current_link->slotframe_handle==1){  //NOTE1   
      num_total_rx_accum++;
    }

#endif


#if PROPOSED
    if(current_link->slotframe_handle!=0 && current_link->slotframe_handle!=2){  //NOTE1  

      num_total_rx_accum++;

      if(current_link->slotframe_handle==1)
      {
        num_total_auto_rx_accum++;
      }
    }

#endif


/*
    if(current_link->slotframe_handle==1){  //NOTE1   
      //num_total_rx++;
      num_total_rx_accum++;
    }
    else if(current_link->slotframe_handle==2){
      num_total_rx_ss++;
    }
*/
#if USE_6TISCH_MINIMAL
    if(current_link->slotframe_handle==0){  //NOTE1   
      num_total_rx_accum++;
    }
#endif


#if RX_CCA_ENABLED
    int i=0;
#endif

    if(!packet_seen) {
      /* Check if receiving within guard time */

#if RX_CCA_ENABLED

      rtimer_clock_t offset = tsch_timing[tsch_ts_rx_offset] + tsch_timing[tsch_ts_rx_wait] + RADIO_DELAY_BEFORE_DETECT;
      
      
      while(!(packet_seen = (NETSTACK_RADIO.receiving_packet() || NETSTACK_RADIO.pending_packet())) 
        && RTIMER_CLOCK_LT(RTIMER_NOW(), (current_slot_start) + (offset)))
      {
        
        //rtimer_clock_t cca_start=RTIMER_NOW();

        cca_result[i]=NETSTACK_RADIO.channel_clear();

        /*cca_delay[i]=RTIMER_NOW()-cca_start;

        if(++i>99)
        {
          TSCH_LOG_ADD(tsch_log_message,
          snprintf(log->message, sizeof(log->message),
                "ERROR: CCA overflow"));

          break;
        }*/


      }

      //over_time= RTIMER_NOW()- ((current_slot_start) + (offset));

      packet_seen = (NETSTACK_RADIO.receiving_packet() || NETSTACK_RADIO.pending_packet());

#else
      BUSYWAIT_UNTIL_ABS((packet_seen = NETSTACK_RADIO.receiving_packet()),
          current_slot_start, tsch_timing[tsch_ts_rx_offset] + tsch_timing[tsch_ts_rx_wait] + RADIO_DELAY_BEFORE_DETECT);
#endif

      /*if(NETSTACK_RADIO.receiving_packet())
      {
          TSCH_LOG_ADD(tsch_log_message,
          snprintf(log->message, sizeof(log->message),
                "WHAT?"));
      }
      else if(!RTIMER_CLOCK_LT(RTIMER_NOW(), current_slot_start+ tsch_timing[tsch_ts_rx_offset] + tsch_timing[tsch_ts_rx_wait] + RADIO_DELAY_BEFORE_DETECT))
      {
        TSCH_LOG_ADD(tsch_log_message,
          snprintf(log->message, sizeof(log->message),
                "now  %lu, target %lu",RTIMER_NOW(), current_slot_start+ tsch_timing[tsch_ts_rx_offset] + tsch_timing[tsch_ts_rx_wait] + RADIO_DELAY_BEFORE_DETECT));
      }*/
      
    }

    /*TSCH_LOG_ADD(tsch_log_message,
          snprintf(log->message, sizeof(log->message),
                "rx_wait %u",tsch_timing[tsch_ts_rx_wait]));*/

    if(!packet_seen) {
      /* no packets on air */
      


#if RX_CCA_ENABLED
      uint8_t cca_status1 = 1;
      rtimer_clock_t now1=RTIMER_NOW();
      /* delay before CCA */
      //TSCH_SCHEDULE_AND_YIELD(pt, t, current_slot_start, TS_CCA_OFFSET, "cca");
      //TSCH_DEBUG_TX_EVENT();
      //tsch_radio_on(TSCH_RADIO_CMD_ON_WITHIN_TIMESLOT);
      /* CCA */
      BUSYWAIT_UNTIL_ABS(!(cca_status1 &= NETSTACK_RADIO.channel_clear()),
                         now1, tsch_timing[tsch_ts_cca]);
      //TSCH_DEBUG_TX_EVENT();
      /* there is not enough time to turn radio off */
      /*  NETSTACK_RADIO.off(); */
      if(cca_status1 == 0) {
        //mac_tx_status = MAC_TX_COLLISION;
        /*TSCH_LOG_ADD(tsch_log_message,
          snprintf(log->message, sizeof(log->message),
                "Idle: Not clear"));*/
      } 
      else{
        /*TSCH_LOG_ADD(tsch_log_message,
          snprintf(log->message, sizeof(log->message),
                "Idle: clear"));*/
      }

#endif

      tsch_radio_off(TSCH_RADIO_CMD_OFF_FORCE);
      //idle_detect(current_link->slotframe_handle);

      /*TSCH_LOG_ADD(tsch_log_message,
          snprintf(log->message, sizeof(log->message),
                "!packet_seen "));*/
      /*TSCH_LOG_ADD(tsch_log_message,
          snprintf(log->message, sizeof(log->message),
                "over time %u, num_cca %u", over_time,i));

      idle_detect(current_link->slotframe_handle);

      int j;
      for(j=0; j<2;j++){
        TSCH_LOG_ADD(tsch_log_message,
            snprintf(log->message, sizeof(log->message),
                  "R %u %u %u %u %u %u %u %u %u %u",
                  cca_result[10*j],cca_result[10*j+1], 
                  cca_result[10*j+2],cca_result[10*j+3],
                  cca_result[10*j+4],cca_result[10*j+5],
                  cca_result[10*j+6],cca_result[10*j+7],
                  cca_result[10*j+8],cca_result[10*j+9]
                  ));
      }

      for(j=0; j<2;j++){
        TSCH_LOG_ADD(tsch_log_message,
            snprintf(log->message, sizeof(log->message),
                  "D %u %u %u %u %u %u %u %u %u %u",
                  cca_delay[10*j],cca_delay[10*j+1], 
                  cca_delay[10*j+2],cca_delay[10*j+3],
                  cca_delay[10*j+4],cca_delay[10*j+5],
                  cca_delay[10*j+6],cca_delay[10*j+7],
                  cca_delay[10*j+8],cca_delay[10*j+9]
                  ));
      }*/

      /*TSCH_LOG_ADD(tsch_log_message,
                  snprintf(log->message, sizeof(log->message),
                  "Rx_(idle)"));*/

    } else {
      TSCH_DEBUG_RX_EVENT();
      /* Save packet timestamp */
      rx_start_time = RTIMER_NOW() - RADIO_DELAY_BEFORE_DETECT;

      /* Wait until packet is received, turn radio off */
      BUSYWAIT_UNTIL_ABS(!NETSTACK_RADIO.receiving_packet(),
          current_slot_start, tsch_timing[tsch_ts_rx_offset] + tsch_timing[tsch_ts_rx_wait] + tsch_timing[tsch_ts_max_tx]);
      TSCH_DEBUG_RX_EVENT();
      tsch_radio_off(TSCH_RADIO_CMD_OFF_WITHIN_TIMESLOT);

      if(NETSTACK_RADIO.pending_packet()) {
        static int frame_valid;
        static int header_len;
        static frame802154_t frame;
        radio_value_t radio_last_rssi;


        /* Read packet */
        current_input->len = NETSTACK_RADIO.read((void *)current_input->payload, TSCH_PACKET_MAX_LEN);
        if(current_input->len==0)
        {
           /*TSCH_LOG_ADD(tsch_log_message,
                  snprintf(log->message, sizeof(log->message),
                  "Rx_(Read fail)"));*/
        }
        NETSTACK_RADIO.get_value(RADIO_PARAM_LAST_RSSI, &radio_last_rssi);
        current_input->rx_asn = tsch_current_asn;
        current_input->rssi = (signed)radio_last_rssi;
        current_input->channel = current_channel;
        header_len = frame802154_parse((uint8_t *)current_input->payload, current_input->len, &frame);
        frame_valid = header_len > 0 &&
          frame802154_check_dest_panid(&frame) &&
          frame802154_extract_linkaddr(&frame, &source_address, &destination_address);

        /*TSCH_LOG_ADD(tsch_log_message,
                        snprintf(log->message, sizeof(log->message),
                        "Rx seq %u %u",current_input->payload[2], current_input->len));*/

        /*TSCH_LOG_ADD(tsch_log_message,
                        snprintf(log->message, sizeof(log->message),
                        "Rx1 %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x", current_input->payload[0], current_input->payload[1], current_input->payload[2], current_input->payload[3], current_input->payload[4], current_input->payload[5], current_input->payload[6], current_input->payload[7], current_input->payload[8], current_input->payload[9], current_input->payload[10]));

        TSCH_LOG_ADD(tsch_log_message,
                        snprintf(log->message, sizeof(log->message),
                        "Rx2 %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x", current_input->payload[11], current_input->payload[12], current_input->payload[13], current_input->payload[14], current_input->payload[15], current_input->payload[16], current_input->payload[17], current_input->payload[18], current_input->payload[19], current_input->payload[20]));
        */
#if TSCH_RESYNC_WITH_SFD_TIMESTAMPS
        /* At the end of the reception, get an more accurate estimate of SFD arrival time */
        NETSTACK_RADIO.get_object(RADIO_PARAM_LAST_PACKET_TIMESTAMP, &rx_start_time, sizeof(rtimer_clock_t));
#endif

        packet_duration = TSCH_PACKET_DURATION(current_input->len);

#if LLSEC802154_ENABLED
        /* Decrypt and verify incoming frame */
        if(frame_valid) {
          if(tsch_security_parse_frame(
               current_input->payload, header_len, current_input->len - header_len - tsch_security_mic_len(&frame),
               &frame, &source_address, &tsch_current_asn)) {
            current_input->len -= tsch_security_mic_len(&frame);
          } else {
            TSCH_LOG_ADD(tsch_log_message,
                snprintf(log->message, sizeof(log->message),
                "!failed to authenticate frame %u", current_input->len));
            frame_valid = 0;
          }
        } else {
          TSCH_LOG_ADD(tsch_log_message,
              snprintf(log->message, sizeof(log->message),
              "!failed to parse frame %u %u", header_len, current_input->len));
          frame_valid = 0;
        }
#endif /* LLSEC802154_ENABLED */

        if(frame_valid) {
          if(linkaddr_cmp(&destination_address, &linkaddr_node_addr)
             || linkaddr_cmp(&destination_address, &linkaddr_null)) {
            int do_nack = 0;
            estimated_drift = RTIMER_CLOCK_DIFF(expected_rx_time, rx_start_time);

#if TSCH_TIMESYNC_REMOVE_JITTER
            /* remove jitter due to measurement errors */
            if(ABS(estimated_drift) <= TSCH_TIMESYNC_MEASUREMENT_ERROR) {
              estimated_drift = 0;
            } else if(estimated_drift > 0) {
              estimated_drift -= TSCH_TIMESYNC_MEASUREMENT_ERROR;
            } else {
              estimated_drift += TSCH_TIMESYNC_MEASUREMENT_ERROR;
            }
#endif

#ifdef TSCH_CALLBACK_DO_NACK
            if(frame.fcf.ack_required) {
              do_nack = TSCH_CALLBACK_DO_NACK(current_link,
                  &source_address, &destination_address);
            }
#endif

            if(frame.fcf.ack_required) {
              static uint8_t ack_buf[TSCH_PACKET_MAX_LEN];
              static int ack_len;

#if PROPOSED
              rtimer_clock_t temp_now1=RTIMER_NOW();
              //before EACK Tx
              todo_rx_schedule_change=0;
              todo_no_resource=0;
              todo_consecutive_new_tx_request=0;

              process_rx_N(&frame);
              rtimer_clock_t temp_now2=RTIMER_NOW();
#if RESIDUAL_ALLOC              
              uint16_t matching_slot = process_rx_schedule_info(&frame);
#endif
              rtimer_clock_t temp_now3=RTIMER_NOW();
              delay_prN=(int)(temp_now2-temp_now1);
              delay_prs=(int)(temp_now3-temp_now2);

#endif

#if PROPOSED && RESIDUAL_ALLOC
              ack_len = tsch_packet_create_eack(ack_buf, sizeof(ack_buf),
                  &source_address, frame.seq, (int16_t)RTIMERTICKS_TO_US(estimated_drift), do_nack, matching_slot);
#else
              /* Build ACK frame */
              //printf("create EACK\n");
              ack_len = tsch_packet_create_eack(ack_buf, sizeof(ack_buf),
                  &source_address, frame.seq, (int16_t)RTIMERTICKS_TO_US(estimated_drift), do_nack);
#endif


              if(ack_len > 0) {
#if LLSEC802154_ENABLED
                if(tsch_is_pan_secured) {
                  /* Secure ACK frame. There is only header and header IEs, therefore data len == 0. */
                  ack_len += tsch_security_secure_frame(ack_buf, ack_buf, ack_len, 0, &tsch_current_asn);
                }
#endif /* LLSEC802154_ENABLED */

                /* Copy to radio buffer */
                NETSTACK_RADIO.prepare((const void *)ack_buf, ack_len);

                /* Wait for time to ACK and transmit ACK */
                TSCH_SCHEDULE_AND_YIELD(pt, t, rx_start_time,
                                        packet_duration + tsch_timing[tsch_ts_tx_ack_delay] - RADIO_DELAY_BEFORE_TX, "RxBeforeAck");
                TSCH_DEBUG_RX_EVENT();
                NETSTACK_RADIO.transmit(ack_len);
                tsch_radio_off(TSCH_RADIO_CMD_OFF_WITHIN_TIMESLOT);

                /*TSCH_LOG_ADD(tsch_log_message,
                snprintf(log->message, sizeof(log->message),
                "ACK Length, Duration: %u %uus",ack_len, 32 * ((ack_len) + 3)));*/

              }
              else
              {
                printf("ERROR: Tx EACK fail\n");
              }
            }

            /* If the sender is a time source, proceed to clock drift compensation */
            n = tsch_queue_get_nbr(&source_address);
            if(n != NULL && n->is_time_source) {
              int32_t since_last_timesync = TSCH_ASN_DIFF(tsch_current_asn, last_sync_asn);
              /* Keep track of last sync time */
              last_sync_asn = tsch_current_asn;
              /* Save estimated drift */
              drift_correction = -estimated_drift;
              is_drift_correction_used = 1;
              tsch_timesync_update(n, since_last_timesync, -estimated_drift);
              tsch_schedule_keepalive();
            }

            /* Add current input to ringbuf */
            ringbufindex_put(&input_ringbuf);

            /* Log every reception */
            TSCH_LOG_ADD(tsch_log_rx,
              log->rx.src = TSCH_LOG_ID_FROM_LINKADDR((linkaddr_t*)&frame.src_addr);
              log->rx.is_unicast = frame.fcf.ack_required;
              log->rx.datalen = current_input->len;
              log->rx.drift = drift_correction;
              log->rx.drift_used = is_drift_correction_used;
              log->rx.is_data = frame.fcf.frame_type == FRAME802154_DATAFRAME;
              log->rx.sec_level = frame.aux_hdr.security_control.security_level;
              log->rx.estimated_drift = estimated_drift;

              log->rx.rssi=current_input->rssi; //JSB
              log->rx.delay_prN=delay_prN;
              log->rx.delay_prs=delay_prs;

#if TESLA

              log->rx.num_src_tx=frame.num_I_tx;

#endif

            );
            /*TSCH_LOG_ADD(tsch_log_message,
                  snprintf(log->message, sizeof(log->message),
                  "Rx_(Succ)"));*/
            //success_detect(current_link->slotframe_handle);

          } //For me
          else{

            /*TSCH_LOG_ADD(tsch_log_message,
                  snprintf(log->message, sizeof(log->message),
                  "Rx_(Not for me)"));*/

            //success_detect(current_link->slotframe_handle);
          }
          

          /* Poll process for processing of pending input and logs */
          process_poll(&tsch_pending_events_process);
        }//Vaild 
        else {
          
          /*TSCH_LOG_ADD(tsch_log_message,
                  snprintf(log->message, sizeof(log->message),
                  "Rx_(Not valid)"));*/

          //collision_detect(current_link->slotframe_handle);

        }
      } //End if(NETSTACK_RADIO.pending_packet())
      else
      {
        /*TSCH_LOG_ADD(tsch_log_message,
                  snprintf(log->message, sizeof(log->message),
                  "Rx_(Not pending)"));*/
        //collision_detect(current_link->slotframe_handle);
      }

      tsch_radio_off(TSCH_RADIO_CMD_OFF_END_OF_TIMESLOT);
    } //end of else (!packet_seen)

    if(input_queue_drop != 0) {
      TSCH_LOG_ADD(tsch_log_message,
          snprintf(log->message, sizeof(log->message),
              "!queue full skipped %u", input_queue_drop);
      );
      input_queue_drop = 0;
    }

    
  } //if(input_index == -1)

  #if PROPOSED && RESIDUAL_ALLOC
    if(current_link->slotframe_handle > SSQ_SCHEDULE_HANDLE_OFFSET)
    {
      remove_matching_slot();
    }
  #endif  

  TSCH_DEBUG_RX_EVENT();

  PT_END(pt);
}
/*---------------------------------------------------------------------------*/
/* Protothread for slot operation, called from rtimer interrupt
 * and scheduled from tsch_schedule_slot_operation */
static
PT_THREAD(tsch_slot_operation(struct rtimer *t, void *ptr))
{
  TSCH_DEBUG_INTERRUPT();
  PT_BEGIN(&slot_operation_pt);
  /* Loop over all active slots */
  while(tsch_is_associated) {
    //printf("c2\n");
    if(current_link == NULL || tsch_lock_requested) { /* Skip slot operation if there is no link
                                                          or if there is a pending request for getting the lock */
      /* Issue a log whenever skipping a slot */
      TSCH_LOG_ADD(tsch_log_message,
                      snprintf(log->message, sizeof(log->message),
                          "!skipped slot %u %u %u",
                            tsch_locked,
                            tsch_lock_requested,
                            current_link == NULL);
      );
#if PROPOSED & RESIDUAL_ALLOC
      if(exist_matching_slot(&tsch_current_asn))
      {
        remove_matching_slot();
      }
#endif 

    } else {
      int is_active_slot;
      TSCH_DEBUG_SLOT_START();
      tsch_in_slot_operation = 1;
      /* Reset drift correction */
      drift_correction = 0;
      is_drift_correction_used = 0;
      //printf("c3\n");
      /* Get a packet ready to be sent */
      current_packet = get_packet_and_neighbor_for_link(current_link, &current_neighbor);

#if PROPOSED && RESIDUAL_ALLOC
      if(current_link->slotframe_handle > SSQ_SCHEDULE_HANDLE_OFFSET && current_link->link_options == LINK_OPTION_TX)
      {
        //printf("c3\n");
        if(current_packet==NULL)
        {
          if(tsch_is_locked())
          {
            //tsch-schedule is being changing, so locked
            //skip this slot
            TSCH_LOG_ADD(tsch_log_message,
                      snprintf(log->message, sizeof(log->message),
                          "skip slot: locked 1");
            );
            remove_matching_slot();

          }
          else
          {
            printf("ERROR: ssq Tx schedule, but no packets to Tx\n");
          }
        }
      }
#endif      

      /* There is no packet to send, and this link does not have Rx flag. Instead of doing
       * nothing, switch to the backup link (has Rx flag) if any. */
      if(current_packet == NULL && !(current_link->link_options & LINK_OPTION_RX) && backup_link != NULL) {
        current_link = backup_link;
        current_packet = get_packet_and_neighbor_for_link(current_link, &current_neighbor);
      }
/*JSB: mod1*/
      else if(current_packet == NULL && (current_link->link_options & LINK_OPTION_RX) && backup_link != NULL)
      {
        
        /*TSCH_LOG_ADD(tsch_log_message,
                        snprintf(log->message, sizeof(log->message),
                            "current1 H:%u T:%u R:%u,  backup1 H:%u T:%u R:%u",
                              current_link->slotframe_handle,
                              current_link->link_options & LINK_OPTION_TX,
                              current_link->link_options & LINK_OPTION_RX,
                              backup_link->slotframe_handle,
                              backup_link->link_options & LINK_OPTION_TX,
                              backup_link->link_options & LINK_OPTION_RX);
        );*/

        if(current_link->slotframe_handle > backup_link->slotframe_handle)
        {
           current_link = backup_link;
           current_packet = get_packet_and_neighbor_for_link(current_link, &current_neighbor); //There could be Tx option in backup link
        }
        else
        {
          //Use current_link
        }

        /*TSCH_LOG_ADD(tsch_log_message,
                        snprintf(log->message, sizeof(log->message),
                            "current2 H:%u T:%u R:%u,  backup2 H:%u T:%u R:%u",
                              current_link->slotframe_handle,
                              current_link->link_options & LINK_OPTION_TX,
                              current_link->link_options & LINK_OPTION_RX,
                              backup_link->slotframe_handle,
                              backup_link->link_options & LINK_OPTION_TX,
                              backup_link->link_options & LINK_OPTION_RX);
        );*/
      }
/*JSB: mod1*/


      is_active_slot = current_packet != NULL || (current_link->link_options & LINK_OPTION_RX);
      if(is_active_slot) {
        /* Hop channel */
#if PROPOSED
  #if MULTI_CHANNEL
          uint16_t rx_id;

          if(current_link->slotframe_handle>=3 && current_link->slotframe_handle <= SSQ_SCHEDULE_HANDLE_OFFSET) 
          {
            if(current_link->link_options & LINK_OPTION_TX)
            {
               rx_id = get_id_from_tx_sf_handle(current_link->slotframe_handle);

               if(current_packet == NULL)
               {
                printf("ERROR: multi_channel 3\n");
               }
            }
            else if(current_link->link_options & LINK_OPTION_RX)
            {
              rx_id = node_id;
            }
            else
            {
              printf("ERROR: multi_channel 1\n");
            }

            struct tsch_slotframe *sf= tsch_schedule_get_slotframe_by_handle(current_link->slotframe_handle);
            if(sf!=NULL)
            {
              uint64_t ASN=  (uint64_t)(tsch_current_asn.ls4b) + ((uint64_t)(tsch_current_asn.ms1b) << 32);
              if(ASN % ORCHESTRA_CONF_COMMON_SHARED_PERIOD==0)
              {
                //Shared slotframe should have been prioritized
                printf("ERROR: multi_channel 4\n");
              }
              uint64_t ASFN =  ASN / sf->size.val;
              //printf("rx_id %u, ASFN %lu, ASN %u.%u\n",rx_id,ASFN,tsch_current_asn.ms1b,tsch_current_asn.ls4b);
              uint16_t hash_input= (uint16_t)(rx_id + ASFN);
              uint16_t minus_c_offset= hash_ftn(hash_input,2) ; //0 or 1
              current_link->channel_offset=3; //default
              current_link->channel_offset = current_link->channel_offset - minus_c_offset; // 3-0 or 3-1

            }
            else
            {
              printf("sf removed just before, %x.%lx %u\n", tsch_current_asn.ms1b,tsch_current_asn.ls4b,current_link->slotframe_handle);
              goto donothing;
            }
            


          }

    #if RESIDUAL_ALLOC 

          else if (current_link->slotframe_handle > SSQ_SCHEDULE_HANDLE_OFFSET)
          {
            if(current_link->link_options & LINK_OPTION_TX)
            {
               rx_id = (current_link->slotframe_handle - SSQ_SCHEDULE_HANDLE_OFFSET - 1) / 2;

               if(current_packet == NULL)
               {
                printf("ERROR: multi_channel 4\n");
               }
            }
            else if(current_link->link_options & LINK_OPTION_RX)
            {
              rx_id = node_id;
            }
            else
            {
              printf("ERROR: multi_channel 5\n");
            }

          
            
            uint64_t ASN=  (uint64_t)(tsch_current_asn.ls4b) + ((uint64_t)(tsch_current_asn.ms1b) << 32);
            if(ASN % ORCHESTRA_CONF_COMMON_SHARED_PERIOD==0)
            {
              //Shared slotframe should have not been overlapped.
              //Of course, not allowed to overlaped with the other slotframes 
              printf("ERROR: multi_channel 6\n");
            }

            uint16_t hash_input= rx_id + (uint16_t)ASN;
            uint16_t minus_c_offset= hash_ftn(hash_input,2) ; //0 or 1
            current_link->channel_offset=3; //default
            current_link->channel_offset = current_link->channel_offset - minus_c_offset; // 3-0 or 3-1

            
          }

    #endif
  #endif
#endif        
        current_channel = tsch_calculate_channel(&tsch_current_asn, current_link->channel_offset);
        NETSTACK_RADIO.set_value(RADIO_PARAM_CHANNEL, current_channel);
        /* Turn the radio on already here if configured so; necessary for radios with slow startup */
        tsch_radio_on(TSCH_RADIO_CMD_ON_START_OF_TIMESLOT);
        /* Decide whether it is a TX/RX/IDLE or OFF slot */
        /* Actual slot operation */
        if(current_packet != NULL) {
          /* We have something to transmit, do the following:
           * 1. send
           * 2. update_backoff_state(current_neighbor)
           * 3. post tx callback
           **/
          static struct pt slot_tx_pt;
          PT_SPAWN(&slot_operation_pt, &slot_tx_pt, tsch_tx_slot(&slot_tx_pt, t));
        } else {
          /* Listen */
          static struct pt slot_rx_pt;
          PT_SPAWN(&slot_operation_pt, &slot_rx_pt, tsch_rx_slot(&slot_rx_pt, t));
        }
      } // if(is_active_slot)


      donothing:
      TSCH_DEBUG_SLOT_END();
    }

    /* End of slot operation, schedule next slot or resynchronize */

/*#if PROPOSED && RESIDUAL_ALLOC
      if(current_link->slotframe_handle > SSQ_SCHEDULE_HANDLE_OFFSET)
      {
        remove_matching_slot(); // This made simulation stop even when no code in this function.
      }
#endif*/      

    /* Do we need to resynchronize? i.e., wait for EB again */
    if(!tsch_is_coordinator && (TSCH_ASN_DIFF(tsch_current_asn, last_sync_asn) >
        (100 * TSCH_CLOCK_TO_SLOTS(TSCH_DESYNC_THRESHOLD / 100, tsch_timing[tsch_ts_timeslot_length])))) {
      TSCH_LOG_ADD(tsch_log_message,
            snprintf(log->message, sizeof(log->message),
                "ERROR: ! leaving the network, last sync %u",
                          (unsigned)TSCH_ASN_DIFF(tsch_current_asn, last_sync_asn));
      );
      last_timesource_neighbor = NULL;
      tsch_disassociate();
    } else {
      /* backup of drift correction for printing debug messages */
      /* int32_t drift_correction_backup = drift_correction; */
      uint16_t timeslot_diff = 0;
      rtimer_clock_t prev_slot_start;
      /* Time to next wake up */
      rtimer_clock_t time_to_next_active_slot;
      /* Schedule next wakeup skipping slots if missed deadline */

      uint8_t aaa=0;
      do {
        if(current_link != NULL
            && current_link->link_options & LINK_OPTION_TX
            && current_link->link_options & LINK_OPTION_SHARED) {
          /* Decrement the backoff window for all neighbors able to transmit over
           * this Tx, Shared link. */
          tsch_queue_update_all_backoff_windows(&current_link->addr);
          aaa=1;
        }

        /* Get next active link */
        current_link = tsch_schedule_get_next_active_link(&tsch_current_asn, &timeslot_diff, &backup_link);
        //printf("time to next slot %u\n",timeslot_diff);
        if(current_link == NULL) {
          /* There is no next link. Fall back to default
           * behavior: wake up at the next slot. */
          //printf("c3\n");
          timeslot_diff = 1;

        }
        else if(aaa==1)
        {
          if(current_packet!=NULL)
          {
            if(current_packet->ret==MAC_TX_NOACK)
            {
              //printf("tts %u (h %u, %x.%lx)\n",timeslot_diff,current_link->slotframe_handle,tsch_current_asn.ms1b,tsch_current_asn.ls4b);
            }
          }
          
        }

        //printf("tts %u\n",timeslot_diff);

        /* Update ASN */
        TSCH_ASN_INC(tsch_current_asn, timeslot_diff);

#if PROPOSED & RESIDUAL_ALLOC          
        if(current_link == NULL && tsch_is_locked() && exist_matching_slot(&tsch_current_asn)) ////tsch-schedule is being changing, so locked
        {
            TSCH_LOG_ADD(tsch_log_message,
            snprintf(log->message, sizeof(log->message),
                "skip slot: locked 2");
            );
          remove_matching_slot();
        }
#endif        
        /* Time to next wake up */
        time_to_next_active_slot = timeslot_diff * tsch_timing[tsch_ts_timeslot_length] + drift_correction;
        time_to_next_active_slot += tsch_timesync_adaptive_compensate(time_to_next_active_slot);
        drift_correction = 0;
        is_drift_correction_used = 0;
        /* Update current slot start */
        prev_slot_start = current_slot_start;
        current_slot_start += time_to_next_active_slot;
      } while(!tsch_schedule_slot_operation(t, prev_slot_start, time_to_next_active_slot, "main"));
    }
    //printf("c1\n");
    tsch_in_slot_operation = 0;
    PT_YIELD(&slot_operation_pt);
  }

  PT_END(&slot_operation_pt);
}
/*--------------------------------------JSB----------------------------------*/

#if TESLA

uint8_t 
find_index(uint16_t sf_size)
{
  uint8_t i;

  for(i=0;i<NUM_PRIME_NUMBERS;i++)
  {
    if(sf_size==prime_numbers[i])
    {
      //printf("find_index: %u\n",i);
      return i;
    }
  }

  printf("ERROR: Fail find_index\n");
  return 0;

}



static void slotframe_size_adaptation (void * ptr)
{

  uint16_t size_new;
  uint8_t index_new;

  uip_ds6_nbr_t *nbr;
#if PRINT_SELECT_1
  printf("sf_size_adapt: starts %u\n",my_sf_size);
#endif


  rtimer_clock_t busy_wait_time;
  int busy_wait = 0; /* Flag used for logging purposes */
  /* Make sure no new slot operation will start */
  //tsch_lock_requested = 1;
  /* Wait for the end of current slot operation. */
  if(tsch_in_slot_operation) {
    busy_wait = 1;
    busy_wait_time = RTIMER_NOW();
    while(tsch_in_slot_operation) {
#if CONTIKI_TARGET_COOJA || CONTIKI_TARGET_COOJA_IP64
      simProcessRunValue = 1;
      cooja_mt_yield();
#endif /* CONTIKI_TARGET_COOJA || CONTIKI_TARGET_COOJA_IP64 */
    }
    busy_wait_time = RTIMER_NOW() - busy_wait_time;
  }
  
  if(busy_wait) {
    /* Issue a log whenever we had to busy wait until getting the lock */
    TSCH_LOG_ADD(tsch_log_message,
        snprintf(log->message, sizeof(log->message),
            "!get sf_size_adapt delay %u", (unsigned)busy_wait_time);
    );
  }
  


  /*printf("sf_size_adapt: ");
  printf("%u %u %u, %u=%u, %u%%\n", num_idle_rx, num_succ_rx, num_coll_rx,
               num_idle_rx+num_succ_rx+num_coll_rx, num_total_rx,
               ratio_coll_succ_over_total);*/


  if(t_last_check==0)
  {
    t_last_check=clock_seconds();
  }

  if(t_last_update==0)
  {
    t_last_update=clock_seconds();
  }


  t_now=clock_seconds();


/***************************JSB algorithm***********************************/
#if PRINT_SELECT_1
  printf("W %lu, time %lu\n",num_total_rx, t_now-t_last_check);
#endif

  if((num_total_rx>W_th) || ((t_now-t_last_check)>MAX_SF_SIZE_UPDATE_INTERVAL))
  {
    size_new = my_sf_size;
  

    uint32_t W= num_total_rx;


    nbr = nbr_table_head(ds6_neighbors);

/***********************sf_size decrease*********************/
#if PRINT_SELECT_1
    printf("sf_size dec\n");
#endif

    uint32_t W_new;

    while(nbr != NULL) {
      if(nbr->sf_size!=0){  //nbr->sf_size!=0 means nbr is child or parent 
        uint16_t nbr_id=((nbr->ipaddr.u8[14]) << 8) | (nbr->ipaddr.u8[15]); 
        #if PRINT_SELECT_1
        printf("nbr %u: %u/(%u-%u)\n",nbr_id, nbr->num_I_rx, nbr->num_nbr_tx,nbr->num_nbr_tx_offset);
        #endif
        if((int)(nbr->num_nbr_tx-nbr->num_nbr_tx_offset)<0)
        {
          #if PRINT_SELECT_1
          printf("ERROR: num_nbr_tx - num_nbr_tx_offset < 0\n");
          #endif
          goto abc;
        }

        int prr_o=prr_observed(nbr);
        int prr_c= prr_contention(nbr,W);

        if(prr_o!=-1 && prr_c!=-1)
        {
          #if PRINT_SELECT_1
          printf("nbr %u: prr_o %d, prr_c %d\n",nbr_id, prr_o, prr_c);
          #endif
          
          int clb=prr_o-prr_c;
        
          index_new=find_index(size_new);

          while(index_new>0)
          {
            W_new=W*my_sf_size/size_new;

            int prr_c_new=prr_contention(nbr,W_new);

            if(prr_c_new > PRR_LOWER)
            {
              #if PRINT_SELECT_1
              printf("nbr %u: W_n %lu, prr_o_n %d, prr_c_n %d\n",nbr_id, W_new, prr_c_new+clb, prr_c_new);
              #endif
  
              break;
            }

            index_new--;
            size_new=prime_numbers[index_new];

            if(node_id%ORCHESTRA_CONF_COMMON_SHARED_PERIOD==0 && size_new==ORCHESTRA_CONF_COMMON_SHARED_PERIOD)
            {
              #if PRINT_SELECT_1
              printf("Avoid overlap with ss\n");
              #endif
              index_new--;
              size_new=prime_numbers[index_new];
            }
            #if PRINT_SELECT_1
            printf("nbr %u: size_new %u\n",nbr_id,size_new);
            #endif

          }
          
        }
      }
      nbr = nbr_table_next(ds6_neighbors, nbr);
    }//while(nbr != NULL) {

    /*if(node_id==24 && tsch_current_asn.ls4b > 350*1000/10){
      printf("stop\n");
      return ;
    }*/


    if(W_new!=0)  
    {
      while((100*load_sum()/W_new)>LOAD_UPPER && index_new>0)
      {
        index_new--;
        size_new=prime_numbers[index_new];
        W_new=W*my_sf_size/size_new;
        #if PRINT_SELECT_1
        printf("Still high load: size_new %u\n",size_new);
        #endif
      }  
    }
    else
    {
      #if PRINT_SELECT_1
      printf("W_new=0\n");
      #endif
    }



/***************************END******************************/



/***********************sf_size increase*********************/

    if(size_new==my_sf_size)    //Not heavy traffic load
    {
      #if PRINT_SELECT_1
      printf("sf_size inc\n");
      #endif

      index_new=find_index(my_sf_size);

      if(index_new==NUM_PRIME_NUMBERS)
      {
        #if PRINT_SELECT_1
        printf("Already MAX\n");
        #endif

      }
      
    
      while(index_new<NUM_PRIME_NUMBERS-1)
      {
        if(size_new==0)
        {
          printf("ERORR: size_new=0\n");
          break;
        }


        W_new=W*my_sf_size/size_new;

        if(W_new==0)
        {
          printf("ERORR: W_new=0\n");
          break;
        }


        //printf("W_new %lu, Load ratio %lu\n",W_new,100*load_sum()/W_new);


        if((100*load_sum()/W_new)<LOAD_UPPER)   //Light traffic
        {
          uint8_t satisfy_all=1;

          nbr = nbr_table_head(ds6_neighbors);

          while(nbr != NULL) {
            uint16_t nbr_id=((nbr->ipaddr.u8[14]) << 8) | (nbr->ipaddr.u8[15]); 
            int prr_c_new=prr_contention(nbr,W_new);

            if(prr_c_new<PRR_UPPER)
            {
              #if PRINT_SELECT_1
              printf("nbr %u: prr_c_n %d (no satisfy)\n",nbr_id,prr_c_new);
              #endif
              satisfy_all=0;
              break;
            }

            nbr = nbr_table_next(ds6_neighbors, nbr);
          }
        

          if(satisfy_all==1)
          {
            if(++consecutive_inc_decision < THRES_CONSECUTIVE_INC_DECISION)
            {
              #if PRINT_SELECT_1
              printf("sf_size_adapt: No inc, consecutive %u\n",consecutive_inc_decision);
              #endif
              break;
            }

            index_new++;
          
            size_new=prime_numbers[index_new];

            if(node_id%ORCHESTRA_CONF_COMMON_SHARED_PERIOD==0 && size_new==ORCHESTRA_CONF_COMMON_SHARED_PERIOD)
            {
              #if PRINT_SELECT_1
              printf("Avoid overlap with ss\n");
              #endif
              index_new++;
              size_new=prime_numbers[index_new];
            }
            #if PRINT_SELECT_1
            printf("size_new %u\n",size_new);
            #endif

            if(size_new>SF_INC_LIMIT*my_sf_size)
            {
              #if PRINT_SELECT_1
              printf("sf_size_adapt: SF_INC_LIMIT\n");
              #endif
              break;
            }

          }
          else
          { 
            break;
          }

        }//if((100*load_sum()/W_new)<UPPER)
        else
        {
          #if PRINT_SELECT_1
          printf("Load ratio %lu\n",(100*load_sum()/W_new));
          #endif
          break;
        }

      } //while(index_new<NUM_PRIME_NUMBERS-1)
      
    }//if(size_new==my_sf_size)

/***************************END******************************/


/************************sf_size change**********************/

    if(size_new!=my_sf_size)
    {
      if(my_sf_size_version==255)
      {
        my_sf_size_version=1;
      }
      else{
        my_sf_size_version++;
      }

      #if PRINT_SELECT_1
      printf("sf_size_adapt: %u->%u, version:%u\n",
          my_sf_size,size_new,my_sf_size_version);
      #else
        TSCH_LOG_ADD(tsch_log_message,
            snprintf(log->message, sizeof(log->message),
            "sf_size_adapt: %u->%u, version:%u", my_sf_size,size_new,my_sf_size_version));
      #endif

      my_sf_size=size_new;

      consecutive_inc_decision=0;

      inform_sf_size_eb(1);

      inform_sf_size_dao();

      orchestra_adjust_sf_size();


      uip_ds6_nbr_t *nbr = nbr_table_head(ds6_neighbors);
      while(nbr != NULL) {
        uint16_t nbr_id=((nbr->ipaddr.u8[14]) << 8) | (nbr->ipaddr.u8[15]);
#if PRINT_SELECT
        printf("num_I_rx/num_nbr_tx/offset set 0 (%u)\n",nbr_id);
#endif
        nbr->num_nbr_tx_offset=0;
        nbr->num_nbr_tx=0;        
        nbr->num_I_rx=0;
        nbr = nbr_table_next(ds6_neighbors, nbr);
      }

      t_last_update=clock_seconds();
      reset_num_rx();

    }

/***************************END******************************/

    t_last_check=clock_seconds();

  } //if(num_total_rx>W_th || t_now-t_last_check>MAX_SF_SIZE_UPDATE_INTERVAL)


  abc:

  if(t_now-t_last_update >RENEW_MAX_INTERVAL_NBR_OFFSET)
  {
    #if PRINT_SELECT_1
    printf("Renew offset\n");
    printf("num_I_rx set 0 (all)\n");
    #endif

    uip_ds6_nbr_t *nbr = nbr_table_head(ds6_neighbors);
    while(nbr != NULL) {
      //uint16_t nbr_id=((nbr->ipaddr.u8[14]) << 8) | (nbr->ipaddr.u8[15]);
      //printf("Renew offset\n");
      //printf("num_I_rx set 0 (%u)\n",nbr_id);
      nbr->num_nbr_tx_offset=nbr->num_nbr_tx;        
      nbr->num_I_rx=0;
      nbr = nbr_table_next(ds6_neighbors, nbr);
    }
    t_last_update=clock_seconds();

    reset_num_rx();
  }

/*************************JSB algorithm END********************************/


  ctimer_set(&periodic_timer, SF_SIZE_CHECK_PERIOD, slotframe_size_adaptation, NULL); //JSB
     
}


void inform_sf_size_eb(uint8_t sf_size_change)
{
  //Send EB shortly

  /* Enqueue EB only if there isn't already one in queue */
  if( (sf_size_change==0 && tsch_queue_packet_count(&tsch_eb_address) == 0) ||
    (sf_size_change==1) ) {

    if(sf_size_change==1 && tsch_queue_packet_count(&tsch_eb_address) != 0)
    {
      struct tsch_neighbor * n =tsch_queue_get_nbr(&tsch_eb_address);

      tsch_queue_flush_nbr_queue(n);
      #if PRINT_SELECT_1
      printf("inform_sf_size_eb: sf_size changed, flush EB queue\n");
      #endif
    }

    int eb_len;
    uint8_t hdr_len = 0;
    uint8_t tsch_sync_ie_offset;

    packetbuf_clear();
    packetbuf_set_attr(PACKETBUF_ATTR_FRAME_TYPE, FRAME802154_BEACONFRAME);

    eb_len = tsch_packet_create_eb(packetbuf_dataptr(), PACKETBUF_SIZE,
              &hdr_len, &tsch_sync_ie_offset);

    if(eb_len > 0) {
      struct tsch_packet *p;
      packetbuf_set_datalen(eb_len);
      /* Enqueue EB packet */
      if(!(p = tsch_queue_add_packet(&tsch_eb_address, NULL, NULL))) {
        printf("ERROR: inform_sf_size_eb: EB fail\n");
      } else {
        #if PRINT_SELECT_1
        printf("inform_sf_size_eb: enqueue EB packet %u %u\n", eb_len, hdr_len);
        #endif
        p->tsch_sync_ie_offset = tsch_sync_ie_offset;
        //printf("inform_sf_size_eb: tsch_sync_ie_offset %u\n",tsch_sync_ie_offset);
        p->header_len = hdr_len;
      }
    }
  }
  else{
    #if PRINT_SELECT_1
    printf("inform_sf_size_eb: another EB is already queued\n");
    #endif
  }
}


#endif
/*--------------------------------------END----------------------------------*/

/* Set global time before starting slot operation,
 * with a rtimer time and an ASN */
void
tsch_slot_operation_start(void)
{
  static struct rtimer slot_operation_timer;
  rtimer_clock_t time_to_next_active_slot;
  rtimer_clock_t prev_slot_start;
  TSCH_DEBUG_INIT();

  //ctimer_set(&reset_num_rx_timer, (NO_DATA_PERIOD+2*UPLINK_PERIOD)*CLOCK_SECOND, reset_num_rx, NULL); //JSB

  do {
    uint16_t timeslot_diff;
    /* Get next active link */
    current_link = tsch_schedule_get_next_active_link(&tsch_current_asn, &timeslot_diff, &backup_link);
    if(current_link == NULL) {
      /* There is no next link. Fall back to default
       * behavior: wake up at the next slot. */
      timeslot_diff = 1;
    }
    /* Update ASN */
    TSCH_ASN_INC(tsch_current_asn, timeslot_diff);
    /* Time to next wake up */
    time_to_next_active_slot = timeslot_diff * tsch_timing[tsch_ts_timeslot_length];
    /* Update current slot start */
    prev_slot_start = current_slot_start;
    current_slot_start += time_to_next_active_slot;
  } while(!tsch_schedule_slot_operation(&slot_operation_timer, prev_slot_start, time_to_next_active_slot, "association"));
}
/*---------------------------------------------------------------------------*/
/* Start actual slot operation */
void
tsch_slot_operation_sync(rtimer_clock_t next_slot_start,
    struct tsch_asn_t *next_slot_asn)
{
  current_slot_start = next_slot_start;
  tsch_current_asn = *next_slot_asn;
  last_sync_asn = tsch_current_asn;
  current_link = NULL;
}
/*---------------------------------------------------------------------------*/
