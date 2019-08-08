/*
 * Copyright (c) 2013, Swedish Institute of Computer Science.
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
 *
 */

/**
 * \addtogroup uip6
 * @{
 */

/**
 * \file
 *    IPv6 Neighbor cache (link-layer/IPv6 address mapping)
 * \author Mathilde Durvy <mdurvy@cisco.com>
 * \author Julien Abeille <jabeille@cisco.com>
 * \author Simon Duquennoy <simonduq@sics.se>
 *
 */

#ifndef UIP_DS6_NEIGHBOR_H_
#define UIP_DS6_NEIGHBOR_H_

#include "net/ip/uip.h"
#include "net/nbr-table.h"
#include "sys/stimer.h"
#include "net/ipv6/uip-ds6.h"
#if UIP_CONF_IPV6_QUEUE_PKT
#include "net/ip/uip-packetqueue.h"
#endif                          /*UIP_CONF_QUEUE_PKT */

/*--------------------------------------------------*/
/** \brief Possible states for the nbr cache entries */
#define  NBR_INCOMPLETE 0
#define  NBR_REACHABLE 1
#define  NBR_STALE 2
#define  NBR_DELAY 3
#define  NBR_PROBE 4

NBR_TABLE_DECLARE(ds6_neighbors);

/** \brief An entry in the nbr cache */
typedef struct uip_ds6_nbr {
  uip_ipaddr_t ipaddr;
  uint8_t isrouter;
  uint8_t state;
#if UIP_ND6_SEND_NS || UIP_ND6_SEND_RA
  struct stimer reachable;
  struct stimer sendns;
  uint8_t nscount;
#endif /* UIP_ND6_SEND_NS || UIP_ND6_SEND_RA */
#if UIP_CONF_IPV6_QUEUE_PKT
  struct uip_packetqueue_handle packethandle;
#define UIP_DS6_NBR_PACKET_LIFETIME CLOCK_SECOND * 4
#endif                          /*UIP_CONF_QUEUE_PKT */
#if TESLA
  uint16_t sf_size;
  uint8_t sf_size_version;


  uint16_t num_I_tx; //Update whenever I txed for nbr
                           // It is piggybacked.
                           // It is initialized when I remove tx_sf for nbr

  uint16_t num_nbr_tx;  //Update whenever I rx the piggybacked packet.
                              

  uint16_t num_I_rx; //Update whenever I rxed from nbr successfully
                            // It is initialized when I change my sf_size

  uint16_t num_nbr_tx_offset;
#endif 

#if PROPOSED
  uint16_t my_N; //For Tx    //1. determined by me
  uint16_t my_t_offset;      //2. determined by nbr
  uint8_t my_uninstallable;

  uint16_t nbr_N; //For Rx   //1. determined by nbr
  uint16_t nbr_t_offset;     //2. determined by me

  uint16_t num_tx; //network-layer

  uint8_t new_add;

  uint8_t rx_no_path; // will be delete soon. When it is set, r_nbr and slotframe could not be matched.


  uint8_t my_low_prr;
  uint16_t num_tx_mac;
  uint16_t num_tx_succ_mac;
  uint16_t num_consecutive_tx_fail_mac;
  uint16_t consecutive_my_N_inc;

  uint8_t consecutive_new_tx_request;
#endif  

} uip_ds6_nbr_t;

void uip_ds6_neighbors_init(void);

/** \brief Neighbor Cache basic routines */
uip_ds6_nbr_t *uip_ds6_nbr_add(const uip_ipaddr_t *ipaddr,
                               const uip_lladdr_t *lladdr,
                               uint8_t isrouter, uint8_t state,
                               nbr_table_reason_t reason, void *data);
int uip_ds6_nbr_rm(uip_ds6_nbr_t *nbr);
const uip_lladdr_t *uip_ds6_nbr_get_ll(const uip_ds6_nbr_t *nbr);
const uip_ipaddr_t *uip_ds6_nbr_get_ipaddr(const uip_ds6_nbr_t *nbr);
uip_ds6_nbr_t *uip_ds6_nbr_lookup(const uip_ipaddr_t *ipaddr);
uip_ds6_nbr_t *uip_ds6_nbr_ll_lookup(const uip_lladdr_t *lladdr);
uip_ipaddr_t *uip_ds6_nbr_ipaddr_from_lladdr(const uip_lladdr_t *lladdr);
const uip_lladdr_t *uip_ds6_nbr_lladdr_from_ipaddr(const uip_ipaddr_t *ipaddr);
void uip_ds6_link_neighbor_callback(int status, int numtx);
void uip_ds6_neighbor_periodic(void);
int uip_ds6_nbr_num(void);

#if UIP_ND6_SEND_NS
/**
 * \brief Refresh the reachable state of a neighbor. This function
 * may be called when a node receives an IPv6 message that confirms the
 * reachability of a neighbor.
 * \param ipaddr pointer to the IPv6 address whose neighbor reachability state
 * should be refreshed.
 */
void uip_ds6_nbr_refresh_reachable_state(const uip_ipaddr_t *ipaddr);
#endif /* UIP_ND6_SEND_NS */

/**
 * \brief
 *    This searches inside the neighbor table for the neighbor that is about to
 *    expire the next.
 *
 * \return
 *    A reference to the neighbor about to expire the next or NULL if
 *    table is empty.
 */
uip_ds6_nbr_t *uip_ds6_get_least_lifetime_neighbor(void);

#endif /* UIP_DS6_NEIGHBOR_H_ */
/** @} */
