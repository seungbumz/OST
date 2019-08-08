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
 *         IEEE 802.15.4 TSCH MAC schedule manager.
 * \author
 *         Simon Duquennoy <simonduq@sics.se>
 *         Beshr Al Nahas <beshr@sics.se>
 */

#include "contiki.h"
#include "dev/leds.h"
#include "lib/memb.h"
#include "net/nbr-table.h"
#include "net/packetbuf.h"
#include "net/queuebuf.h"
#include "net/mac/tsch/tsch.h"
#include "net/mac/tsch/tsch-queue.h"
#include "net/mac/tsch/tsch-private.h"
#include "net/mac/tsch/tsch-packet.h"
#include "net/mac/tsch/tsch-schedule.h"
#include "net/mac/tsch/tsch-log.h"
#include "net/mac/frame802154.h"
#include "sys/process.h"
#include "sys/rtimer.h"
#include <string.h>

#if PROPOSED && RESIDUAL_ALLOC
    #include "orchestra.h"
#endif
#if TSCH_LOG_LEVEL >= 1
#define DEBUG DEBUG_NONE
#else /* TSCH_LOG_LEVEL */
#define DEBUG DEBUG_NONE
#endif /* TSCH_LOG_LEVEL */
#include "net/net-debug.h"

/* Pre-allocated space for links */
MEMB(link_memb, struct tsch_link, TSCH_SCHEDULE_MAX_LINKS);
/* Pre-allocated space for slotframes */
MEMB(slotframe_memb, struct tsch_slotframe, TSCH_SCHEDULE_MAX_SLOTFRAMES);
/* List of slotframes (each slotframe holds its own list of links) */
LIST(slotframe_list);

/* Adds and returns a slotframe (NULL if failure) */
struct tsch_slotframe *
tsch_schedule_add_slotframe(uint16_t handle, uint16_t size)
{
  if(size == 0) {
    return NULL;
  }

  if(tsch_schedule_get_slotframe_by_handle(handle)) {
    /* A slotframe with this handle already exists */
    PRINTF("TSCH-schedule: add_slotframe already exists\n");
    return NULL;
  }

  if(tsch_get_lock()) {
    struct tsch_slotframe *sf = memb_alloc(&slotframe_memb);
    if(sf != NULL) {
      /* Initialize the slotframe */
      sf->handle = handle;
      TSCH_ASN_DIVISOR_INIT(sf->size, size);
      LIST_STRUCT_INIT(sf, links_list);
      /* Add the slotframe to the global list */
      list_add(slotframe_list, sf);
    }
    else{
      PRINTF("ERROR: add_slotframe fail\n");
    }
    PRINTF("TSCH-schedule: add_slotframe %u %u\n",
           handle, size);
    tsch_release_lock();
    return sf;
  }
  return NULL;
}
/*---------------------------------------------------------------------------*/
/* Removes all slotframes, resulting in an empty schedule */
int
tsch_schedule_remove_all_slotframes(void)
{
  struct tsch_slotframe *sf;
  while((sf = list_head(slotframe_list))) {
    if(tsch_schedule_remove_slotframe(sf) == 0) {
      return 0;
    }
  }
  return 1;
}
/*---------------------------------------------------------------------------*/
/* Removes a slotframe Return 1 if success, 0 if failure */
int
tsch_schedule_remove_slotframe(struct tsch_slotframe *slotframe)
{
  if(slotframe != NULL) {
    /* Remove all links belonging to this slotframe */
    struct tsch_link *l;
    uint8_t result;
    while((l = list_head(slotframe->links_list))) {
      result=tsch_schedule_remove_link(slotframe, l);
      if(result==0)
      {
        printf("ERROR: tsch_schedule_remove_link fail\n");
        //printf("c4: %u %u %u\n",slotframe != NULL, l != NULL, l->slotframe_handle == slotframe->handle);
        //printf("c5: %u %u\n",l->slotframe_handle, slotframe->handle);
        return 0;
      }
    }

    /* Now that the slotframe has no links, remove it. */
    if(tsch_get_lock()) {
      PRINTF("TSCH-schedule: remove_slotframe %u %u\n", slotframe->handle, slotframe->size.val);
      memb_free(&slotframe_memb, slotframe);
      list_remove(slotframe_list, slotframe);
      tsch_release_lock();
      return 1;
    }
  }
  return 0;
}
/*---------------------------------------------------------------------------*/
#if TESLA
int
tsch_schedule_change_slotframe_link_handle(struct tsch_slotframe *slotframe, uint16_t handle)
{
  if(tsch_schedule_get_slotframe_by_handle(handle)) {
    /* A slotframe with this handle already exists */
    return 0;
  }

  if(!tsch_is_locked()) {
    slotframe->handle = handle; //slotframe handle change

    struct tsch_link *l = list_head(slotframe->links_list);
    while(l != NULL) {
        l->slotframe_handle = handle; //link handle change
        l = list_item_next(l);
    }
    return 1;
  }

  return 0;
}
#endif
/*---------------------------------------------------------------------------*/
/* Looks for a slotframe from a handle */
struct tsch_slotframe *
tsch_schedule_get_slotframe_by_handle(uint16_t handle)
{
  if(!tsch_is_locked()) {
    struct tsch_slotframe *sf = list_head(slotframe_list);
    while(sf != NULL) {
      if(sf->handle == handle) {
        return sf;
      }
      sf = list_item_next(sf);
    }
  }
  return NULL;
}
/*---------------------------------------------------------------------------*/
/* Looks for a link from a handle */
struct tsch_link *
tsch_schedule_get_link_by_handle(uint16_t handle)
{
  if(!tsch_is_locked()) {
    struct tsch_slotframe *sf = list_head(slotframe_list);
    while(sf != NULL) {
      struct tsch_link *l = list_head(sf->links_list);
      /* Loop over all items. Assume there is max one link per timeslot */
      while(l != NULL) {
        if(l->slotframe_handle == handle) {
          return l;
        }
        l = list_item_next(l);
      }
      sf = list_item_next(sf);
    }
  }
  return NULL;
}
/*---------------------------------------------------------------------------*/
/* Adds a link to a slotframe, return a pointer to it (NULL if failure) */
struct tsch_link *
tsch_schedule_add_link(struct tsch_slotframe *slotframe,
                       uint8_t link_options, enum link_type link_type, const linkaddr_t *address,
                       uint16_t timeslot, uint16_t channel_offset)
{
  struct tsch_link *l = NULL;
  if(slotframe != NULL) {
    /* We currently support only one link per timeslot in a given slotframe. */
    /* Start with removing the link currently installed at this timeslot (needed
     * to keep neighbor state in sync with link options etc.) */
    tsch_schedule_remove_link_by_timeslot(slotframe, timeslot);
    if(!tsch_get_lock()) {
      PRINTF("TSCH-schedule:! add_link memb_alloc couldn't take lock\n");
    } else {
      l = memb_alloc(&link_memb);
      if(l == NULL) {
        PRINTF("TSCH-schedule:! add_link memb_alloc failed\n");
        tsch_release_lock();
      } else {
        static int current_link_handle = 0;
        struct tsch_neighbor *n;
        /* Add the link to the slotframe */
        list_add(slotframe->links_list, l);
        /* Initialize link */
        l->handle = current_link_handle++;
        l->link_options = link_options;
        l->link_type = link_type;
        l->slotframe_handle = slotframe->handle;
        l->timeslot = timeslot;
        l->channel_offset = channel_offset;
        l->data = NULL;
        if(address == NULL) {
          address = &linkaddr_null;
        }
        linkaddr_copy(&l->addr, address);

        PRINTF("TSCH-schedule: add_link %u %u %u %u %u %u\n",
               slotframe->handle, link_options, link_type, timeslot, channel_offset, TSCH_LOG_ID_FROM_LINKADDR(address));

        if(timeslot>=slotframe->size.val)
        {
          printf("ERROR: too big timeslot %u %u %u (tsch_schedule_add_link)\n",slotframe->handle, timeslot, slotframe->size.val);
        }
        
        /* Release the lock before we update the neighbor (will take the lock) */
        tsch_release_lock();

        if(l->link_options & LINK_OPTION_TX) {
          n = tsch_queue_add_nbr(&l->addr);
          /* We have a tx link to this neighbor, update counters */
          if(n != NULL) {
            n->tx_links_count++;
            PRINTF("TSCH-schedule: link to %u, tx_links_count %u\n"
              ,TSCH_LOG_ID_FROM_LINKADDR(&(n->addr)),n->tx_links_count);
            if(!(l->link_options & LINK_OPTION_SHARED)) {
              n->dedicated_tx_links_count++;
            }
          }
        }

      }
    }
  }
  return l;
}

/*---------------------------------------------------------------------------*/
/* Removes a link from slotframe. Return 1 if success, 0 if failure */
int
tsch_schedule_remove_link(struct tsch_slotframe *slotframe, struct tsch_link *l)
{
  if(slotframe != NULL && l != NULL && l->slotframe_handle == slotframe->handle) {
    if(tsch_get_lock()) {
      uint8_t link_options;
      linkaddr_t addr;

      /* Save link option and addr in local variables as we need them
       * after freeing the link */
      link_options = l->link_options;
      linkaddr_copy(&addr, &l->addr);


      /* The link to be removed is scheduled as next, set it to NULL
       * to abort the next link operation */
      if(l == current_link) {
        current_link = NULL;
      }
      PRINTF("TSCH-schedule: remove_link %u %u %u %u %u\n",
             slotframe->handle, l->link_options, l->timeslot, l->channel_offset,
             TSCH_LOG_ID_FROM_LINKADDR(&l->addr));

      list_remove(slotframe->links_list, l);
      memb_free(&link_memb, l);

      /* Release the lock before we update the neighbor (will take the lock) */
      tsch_release_lock();

      /* This was a tx link to this neighbor, update counters */
      if(link_options & LINK_OPTION_TX) {
        struct tsch_neighbor *n = tsch_queue_add_nbr(&addr);
        if(n != NULL) {
          n->tx_links_count--;
          if(!(link_options & LINK_OPTION_SHARED)) {
            n->dedicated_tx_links_count--;
          }
        }
      }

      return 1;
    } else {
      PRINTF("TSCH-schedule:! remove_link memb_alloc couldn't take lock\n");
    }
  }

  return 0;
}
/*---------------------------------------------------------------------------*/
/* Removes a link from slotframe and timeslot. Return a 1 if success, 0 if failure */
int
tsch_schedule_remove_link_by_timeslot(struct tsch_slotframe *slotframe, uint16_t timeslot)
{  
  return slotframe != NULL &&
         tsch_schedule_remove_link(slotframe, tsch_schedule_get_link_by_timeslot(slotframe, timeslot));
}
/*---------------------------------------------------------------------------*/
/* Looks within a slotframe for a link with a given timeslot */
struct tsch_link *
tsch_schedule_get_link_by_timeslot(struct tsch_slotframe *slotframe, uint16_t timeslot)
{
  if(!tsch_is_locked()) {
    if(slotframe != NULL) {
      struct tsch_link *l = list_head(slotframe->links_list);
      /* Loop over all items. Assume there is max one link per timeslot */
      while(l != NULL) {
        if(l->timeslot == timeslot) {
          return l;
        }
        l = list_item_next(l);
      }
      return l;
    }
  }
  return NULL;
}
/*---------------------------------------------------------------------------*/
#if PROPOSED
#if RESIDUAL_ALLOC
uint16_t
tsch_schedule_get_subsequent_schedule(struct tsch_asn_t *asn)
{
  uint16_t ssq_schedule=0;

  uint8_t used[16]; //0~15th slot is used
  
  //check slotframe schedule 
  PRINTF("ssq_schedule: used by slotframe ");
  if(!tsch_is_locked()) { 
   struct tsch_slotframe *sf = list_head(slotframe_list);
   while(sf != NULL) {
      uint16_t timeslot = TSCH_ASN_MOD(*asn, sf->size);
      struct tsch_link *l = list_head(sf->links_list);

      while(l != NULL) {

        uint16_t time_to_timeslot =
          l->timeslot > timeslot ?
          l->timeslot - timeslot :
          sf->size.val + l->timeslot - timeslot;

        if((time_to_timeslot-1)<16)
        {
          used[time_to_timeslot-1]=1;
          PRINTF("%u ",time_to_timeslot);
        }

        l = list_item_next(l);

      }
     sf = list_item_next(sf);
   }

  }
  PRINTF("\n");

  //check matching slot schedule
  uint8_t i;
  uint64_t curr_ASN= (uint64_t)(asn->ls4b) + ((uint64_t)(asn->ms1b) << 32);
  uint64_t ssq_ASN;
  PRINTF("ssq_schedule: used by ssq ");
  for(i=0; i<16; i++)
  {
    if(ssq_schedule_list[i].asn.ls4b==0 && ssq_schedule_list[i].asn.ms1b==0)
    {

    }
    else
    {
      ssq_ASN= (uint64_t)(ssq_schedule_list[i].asn.ls4b) + ((uint64_t)(ssq_schedule_list[i].asn.ms1b) << 32);
      uint64_t time_to_timeslot=ssq_ASN-curr_ASN;
      if(time_to_timeslot==0)
      {
        PRINTF("curr ");
      }
      else if(0<time_to_timeslot && time_to_timeslot<=16)
      {
        PRINTF("%lu ",time_to_timeslot);
        used[time_to_timeslot-1]=1;
      }
      else
      {
        printf("ERROR: ssq time_to_timeslot is more than 16\n");
      }


    }
  }
  PRINTF("\n");


  for(i=0;i<16;i++)
  {
    if(used[i]==1)
    {
      ssq_schedule = ssq_schedule | (1 << i);
    }
  }

  //printf("ssq_schedule: %d\n",ssq_schedule);
 return ssq_schedule;
}

uint8_t
earlier_ssq_schedule_list(uint16_t * time_to_orig_schedule, struct tsch_link ** link)
{
  uint8_t i;
  uint8_t earliest_i;
  uint64_t earliest_ASN=0;
  struct tsch_link* earliest_link;
  uint64_t ssq_ASN;
  uint64_t curr_ASN= (uint64_t)(tsch_current_asn.ls4b) + ((uint64_t)(tsch_current_asn.ms1b) << 32);
  //First, choose the earliest ssq schedule
  for(i=0; i<16; i++)
  {
    if(ssq_schedule_list[i].asn.ls4b==0 && ssq_schedule_list[i].asn.ms1b==0)
    {
      
    }
    else
    {
      ssq_ASN= (uint64_t)(ssq_schedule_list[i].asn.ls4b) + ((uint64_t)(ssq_schedule_list[i].asn.ms1b) << 32);
      if(earliest_ASN==0 || ssq_ASN < earliest_ASN)
      {
        earliest_ASN= ssq_ASN;
        earliest_link= &(ssq_schedule_list[i].link);
        earliest_i=i;
        //printf("temp earliest link\n");
        if(earliest_link == NULL)
        {
          printf("ERROR: Null temp earliest_link\n");
        }

      }


      if(ssq_ASN <= curr_ASN)
      {
        printf("ERROR: ssq_ASN(%x.%lx) <= curr_ASN(%x.%lx)\n",
          ssq_schedule_list[i].asn.ms1b, ssq_schedule_list[i].asn.ls4b, tsch_current_asn.ms1b, tsch_current_asn.ls4b);
        
        ssq_schedule_list[i].asn.ls4b=0;
        ssq_schedule_list[i].asn.ms1b=0; 

        return 0; 
      }
    }

  }

  if(earliest_ASN==0)
  {
    //No pending ssq schedule
    return 0;
  }
  else
  {
    uint64_t time_to_earliest = earliest_ASN-curr_ASN;

    if(time_to_earliest < *time_to_orig_schedule)
    {
      PRINTF("Earlier ssq exists %u\n",time_to_earliest);
      *time_to_orig_schedule=(uint16_t)time_to_earliest;
      *link= earliest_link;
      if(link == NULL)
      {
        printf("ERROR: Null earliest_link\n");
      }
      return 1;
    }
    else if(time_to_earliest==*time_to_orig_schedule)
    {
      //printf("ERROR: ssq overlap with orig schedule %x.%lx %x.%lx %u\n",ssq_schedule_list[earliest_i].asn.ms1b, ssq_schedule_list[earliest_i].asn.ls4b, 
        //tsch_current_asn.ms1b,tsch_current_asn.ls4b,
        //ssq_schedule_list[earliest_i].link.slotframe_handle);
      ssq_schedule_list[earliest_i].asn.ms1b=0;
      ssq_schedule_list[earliest_i].asn.ls4b=0;

      return 0;
    }
    else
    {
      return 0;
    }
  }

}

#endif
#endif

/*---------------------------------------------------------------------------*/
/* Returns the next active link after a given ASN, and a backup link (for the same ASN, with Rx flag) */
struct tsch_link *
tsch_schedule_get_next_active_link(struct tsch_asn_t *asn, uint16_t *time_offset,
    struct tsch_link **backup_link)
{
  uint16_t time_to_curr_best = 0;
  struct tsch_link *curr_best = NULL;
  struct tsch_link *curr_backup = NULL; /* Keep a back link in case the current link
  turns out useless when the time comes. For instance, for a Tx-only link, if there is
  no outgoing packet in queue. In that case, run the backup link instead. The backup link
  must have Rx flag set. */
  if(!tsch_is_locked()) {
    struct tsch_slotframe *sf = list_head(slotframe_list);
    /* For each slotframe, look for the earliest occurring link */
    while(sf != NULL) {
      /* Get timeslot from ASN, given the slotframe length */
      uint16_t timeslot = TSCH_ASN_MOD(*asn, sf->size);
      struct tsch_link *l = list_head(sf->links_list);
      while(l != NULL) {
        uint16_t time_to_timeslot =
          l->timeslot > timeslot ?
          l->timeslot - timeslot :
          sf->size.val + l->timeslot - timeslot;
        if(curr_best == NULL || time_to_timeslot < time_to_curr_best) {
          time_to_curr_best = time_to_timeslot;
          curr_best = l;
          curr_backup = NULL;
        } else if(time_to_timeslot == time_to_curr_best) {
          struct tsch_link *new_best = NULL;
          /* Two links are overlapping, we need to select one of them.
           * By standard: prioritize Tx links first, second by lowest handle */
          if((curr_best->link_options & LINK_OPTION_TX) == (l->link_options & LINK_OPTION_TX)) {
            /* Both or neither links have Tx, select the one with lowest handle */
            if(l->slotframe_handle < curr_best->slotframe_handle) {
              new_best = l;
            }
#if PROPOSED
            if( (curr_best->slotframe_handle==1) && (curr_best->link_options & LINK_OPTION_TX) &&  (l->slotframe_handle==2) )
            { //Prevent Autonomous unicast Tx from interfere Autonomous broadcast Tx/Rx (They share the same c_offset in PROPOSED) 
              //Prioritize Autonomous broadcast Tx/Rx to Autonomous unicast Tx
              //printf("AU Tx < AB\n");
              new_best = l;
            }
#endif

#if TESLA
            if( ((curr_best->link_options & LINK_OPTION_TX)==1) && ((l->link_options & LINK_OPTION_TX)==1) //Both Tx option
                && (curr_best->slotframe_handle > 3) && (l->slotframe_handle > 3) //Both tx_sf //NOTE1
            ) {

              uint16_t id_curr_best=curr_best->slotframe_handle - 3; //NOTE1
              uint16_t id_l=l->slotframe_handle-3; //NOTE1
              struct tsch_neighbor *n_curr_best = tsch_queue_get_nbr_from_id(id_curr_best); //NOTE1
              struct tsch_neighbor *n_l= tsch_queue_get_nbr_from_id(id_l);//NOTE1

              uint8_t q_num_curr_best;
              uint8_t q_num_l;

              //printf("Tx link overlap: ID %u %u\n",id_curr_best,id_l);

              if(n_curr_best==NULL)
              {
                q_num_curr_best=0; //child and queue is empty
              }
              else
              {
                q_num_curr_best=ringbufindex_elements(&n_curr_best->tx_ringbuf);
              }

              if(n_l==NULL)
              {
                q_num_l=0; //child and queue is empty
              }
              else
              {
                q_num_l=ringbufindex_elements(&n_l->tx_ringbuf);
              }

              //printf("Tx link overlap: Queue size %u %u\n",q_num_curr_best,q_num_l);
              if(q_num_l>q_num_curr_best)
              {
                new_best = l;
              }
              else
              {
                new_best=curr_best;
              }

            }
#endif
            
          } else {
            /* Select the link that has the Tx option */
            if(l->link_options & LINK_OPTION_TX) {
              new_best = l;
            }
          }

//#if TESLA
          //Give the highest priority to EB (even if rx link)
          if(l->slotframe_handle==0)
          {
            new_best = l;
          }
          else if(curr_best->slotframe_handle==0)
          {
            new_best = curr_best;
          }
//#endif

          /* Maintain backup_link */
          if(curr_backup == NULL) {
            /* Check if 'l' best can be used as backup */
            if(new_best != l && (l->link_options & LINK_OPTION_RX)) { /* Does 'l' have Rx flag? */
              curr_backup = l;
            }
            /* Check if curr_best can be used as backup */
            if(new_best != curr_best && (curr_best->link_options & LINK_OPTION_RX)) { /* Does curr_best have Rx flag? */
              curr_backup = curr_best;
            }
          }
          
#if TESLA
          else{ //curr_backup!=NULL   //backup link update

            if(l!=NULL)
            {

              if(new_best != l && (l->link_options & LINK_OPTION_RX)) { /* Does 'l' have Rx flag? */

                  if(curr_backup->slotframe_handle > l->slotframe_handle)
                  {
                    //printf("Backup link update 1\n");
                    curr_backup = l;
                  }

              }
            }
            else
            {
              //printf("l=NULL!\n");
            }

            if(curr_best!=NULL){
              /* Check if curr_best can be used as backup */
              if(new_best != curr_best && (curr_best->link_options & LINK_OPTION_RX)) { /* Does curr_best have Rx flag? */
                
                  if(curr_backup->slotframe_handle > curr_best->slotframe_handle)
                  {
                    //printf("Backup link update 2\n");
                    curr_backup = curr_best;
                  }
              }
            }
            else
            {
              //printf("curr_best=NULL!\n");
            }


          }
#endif


          /* Maintain curr_best */
          if(new_best != NULL) {
            curr_best = new_best;
          }
        }

        l = list_item_next(l);
      }
      sf = list_item_next(sf);
    }

    if(time_offset != NULL) {
      *time_offset = time_to_curr_best;
      //printf("orig time_offset %u\n",*time_offset);
    }


#if PROPOSED && RESIDUAL_ALLOC
    struct tsch_link * ssq_link =NULL;
    uint16_t new_time_offset=*time_offset; //initialize
    if(earlier_ssq_schedule_list(&new_time_offset,&ssq_link))
    {
      if(ssq_link!=NULL)
      {
        //printf("changed time_offset %u\n",new_time_offset);
        *time_offset = new_time_offset;
        *backup_link=NULL;
        return ssq_link;
        
      }
      else
      {
        printf("ERROR: ssq_link is NULL\n");
      }  
      
    }
#endif

  } //if(!tsch_is_locked())

  if(backup_link != NULL) {
    *backup_link = curr_backup;
  }


  return curr_best;
}
/*---------------------------------------------------------------------------*/
/* Module initialization, call only once at startup. Returns 1 is success, 0 if failure. */
int
tsch_schedule_init(void)
{
  if(tsch_get_lock()) {
    memb_init(&link_memb);
    memb_init(&slotframe_memb);
    list_init(slotframe_list);
    tsch_release_lock();
    return 1;
  } else {
    return 0;
  }
}
/*---------------------------------------------------------------------------*/
/* Create a 6TiSCH minimal schedule */
void
tsch_schedule_create_minimal(void)
{
  struct tsch_slotframe *sf_min;
  /* First, empty current schedule */
  tsch_schedule_remove_all_slotframes();
  /* Build 6TiSCH minimal schedule.
   * We pick a slotframe length of TSCH_SCHEDULE_DEFAULT_LENGTH */
  sf_min = tsch_schedule_add_slotframe(0, TSCH_SCHEDULE_DEFAULT_LENGTH);
  /* Add a single Tx|Rx|Shared slot using broadcast address (i.e. usable for unicast and broadcast).
   * We set the link type to advertising, which is not compliant with 6TiSCH minimal schedule
   * but is required according to 802.15.4e if also used for EB transmission.
   * Timeslot: 0, channel offset: 0. */
  tsch_schedule_add_link(sf_min,
      LINK_OPTION_RX | LINK_OPTION_TX | LINK_OPTION_SHARED | LINK_OPTION_TIME_KEEPING,
      LINK_TYPE_ADVERTISING, &tsch_broadcast_address,
      0, 0);
}
/*---------------------------------------------------------------------------*/
/* Prints out the current schedule (all slotframes and links) */
void
tsch_schedule_print(void)
{
  if(!tsch_is_locked()) {
    struct tsch_slotframe *sf = list_head(slotframe_list);

    printf("Schedule: slotframe list\n");

    while(sf != NULL) {
      struct tsch_link *l = list_head(sf->links_list);

      printf("[Slotframe] Handle %u, size %u\n", sf->handle, sf->size.val);
      printf("List of links:\n");

      while(l != NULL) {
        printf("[Link] Options %02x, type %u, timeslot %u, channel offset %u, address %u\n",
               l->link_options, l->link_type, l->timeslot, l->channel_offset, l->addr.u8[7]);
        l = list_item_next(l);
      }

      sf = list_item_next(sf);
    }

    printf("Schedule: end of slotframe list\n");
  }
}
/*---------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------*/
/* Prints out the current schedule (all slotframes and links) */
#if PROPOSED
void
tsch_schedule_print_proposed(void)
{
  if(!tsch_is_locked()) {
    struct tsch_slotframe *sf = list_head(slotframe_list);

    printf("[SLOTFRAMES] Opt / Size / Timeslot\n");

    while(sf != NULL) {
      if(sf->handle>2){

        if(sf->handle%2==0){
          printf("[ID:%u] Rx / %u / ",sf->handle/2-1,sf->size.val);
        }
        else{
          printf("[ID:%u] Tx / %u / ",sf->handle/2,sf->size.val);
        }


        struct tsch_link *l = list_head(sf->links_list);

        //printf("[Slotframe] Handle %u, size %u\n", sf->handle, sf->size.val);
        //printf("List of links:\n");

        while(l != NULL) {
          printf("%u\n", l->timeslot);
          l = list_item_next(l);
        }
      }

      sf = list_item_next(sf);
    }

    printf("\n");
  }
}
struct tsch_slotframe *
tsch_schedule_get_slotframe_head(void)
{

  struct tsch_slotframe *sf = list_head(slotframe_list);
  
  return sf;

}
#endif
/*---------------------------------------------------------------------------*/
