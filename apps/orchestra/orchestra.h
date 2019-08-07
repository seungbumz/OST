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
 *         Orchestra header file
 *
 * \author Simon Duquennoy <simonduq@sics.se>
 */

#ifndef __ORCHESTRA_H__
#define __ORCHESTRA_H__

#include "net/mac/tsch/tsch.h"
#include "net/mac/tsch/tsch-conf.h"
#include "net/mac/tsch/tsch-schedule.h"
#include "orchestra-conf.h"
#if PROPOSED
  #include "net/ipv6/uip-ds6-nbr.h"

#endif

/* The structure of an Orchestra rule */
struct orchestra_rule {
  void (* init)(uint16_t slotframe_handle);
  void (* new_time_source)(const struct tsch_neighbor *old, const struct tsch_neighbor *new);
  int  (* select_packet)(uint16_t *slotframe, uint16_t *timeslot);
  void (* child_added)(const linkaddr_t *addr);
  void (* child_removed)(const linkaddr_t *addr);
#if TESLA
  void (* adjust_sf_size)(void);
  void (* adjust_tx_sf_size)(const uip_ipaddr_t* from);

#endif
};

struct orchestra_rule eb_per_time_source;
struct orchestra_rule unicast_per_neighbor_rpl_storing;
struct orchestra_rule unicast_per_neighbor_rpl_ns;
struct orchestra_rule default_common;


extern linkaddr_t orchestra_parent_linkaddr;
extern int orchestra_parent_knows_us;
extern uint8_t bootstrap_period;

#if PROPOSED & RESIDUAL_ALLOC

extern struct ssq_schedule_t ssq_schedule_list[16];

#endif

#if TESLA
  extern uint16_t my_sf_size;
  extern uint8_t my_sf_size_version;

  extern uint8_t is_waiting_eack;
  extern struct ctimer wait_eack_timer;

  extern uint8_t is_waiting_eack2;
  extern struct ctimer wait_eack_timer2;



  extern uint8_t allow_rm_tx_sf;

  extern uint8_t l_l_ts;

#endif

  extern uint32_t num_total_rx_accum;

#if PROPOSED 
  extern uint32_t num_total_auto_rx_accum;
#endif

/* Call from application to start Orchestra */
void orchestra_init(void);
/* Callbacks requied for Orchestra to operate */
/* Set with #define TSCH_CALLBACK_PACKET_READY orchestra_callback_packet_ready */
void orchestra_callback_packet_ready(void);
/* Set with #define TSCH_CALLBACK_NEW_TIME_SOURCE orchestra_callback_new_time_source */
void orchestra_callback_new_time_source(const struct tsch_neighbor *old, const struct tsch_neighbor *new);
/* Set with #define NETSTACK_CONF_ROUTING_NEIGHBOR_ADDED_CALLBACK orchestra_callback_child_added */
void orchestra_callback_child_added(const linkaddr_t *addr);
/* Set with #define NETSTACK_CONF_ROUTING_NEIGHBOR_REMOVED_CALLBACK orchestra_callback_child_removed */
void orchestra_callback_child_removed(const linkaddr_t *addr);

#if TESLA
void orchestra_adjust_sf_size(void);
void orchestra_adjust_tx_sf_size(const uip_ipaddr_t* from);
void inform_sf_size_eb(uint8_t sf_size_change);
uint16_t get_tx_sf_handle_from_id(const uint16_t id);
uint16_t get_tx_sf_handle_from_linkaddr(const linkaddr_t *addr);
void remove_tx_sf(const linkaddr_t *linkaddr);
void change_attr_in_tx_queue(const linkaddr_t * dest, uint8_t is_adjust_tx_sf_size,  uint8_t only_first_packet);
void check_queued_packet(struct tsch_neighbor *n);
void shared_tx_expired (void* ptr);
#endif

#if PROPOSED
void reset_nbr(const linkaddr_t *addr, uint8_t new_add, uint8_t rx_no_path);
uint16_t get_tx_sf_handle_from_id(const uint16_t id);
uint16_t get_rx_sf_handle_from_id(const uint16_t id);
uint16_t get_id_from_tx_sf_handle(const uint16_t handle);
uint16_t get_id_from_rx_sf_handle(const uint16_t handle);
uint8_t is_routing_nbr(uip_ds6_nbr_t *nbr);
void print_nbr(void);
void remove_tx(uint16_t id);
void remove_rx(uint16_t id);
void change_queue_select_packet(uint16_t id, uint16_t handle, uint16_t timeslot);
void change_queue_N_update(uint16_t nbr_id, uint16_t updated_N);
uint8_t get_todo_no_resource();
uint8_t get_todo_consecutive_new_tx_request();

#endif
int neighbor_has_uc_link(const linkaddr_t *linkaddr);

#endif /* __ORCHESTRA_H__ */
