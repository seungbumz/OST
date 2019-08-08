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
 */
/**
 * \file
 *         A RPL+TSCH node able to act as either a simple node (6ln),
 *         DAG Root (6dr) or DAG Root with security (6dr-sec)
 *         Press use button at startup to configure.
 *
 * \author Simon Duquennoy <simonduq@sics.se>
 */

#include "contiki.h"
#include "node-id.h"
#include "net/rpl/rpl.h"
#include "net/ipv6/uip-ds6-route.h"
#include "net/mac/tsch/tsch.h"
#include "net/rpl/rpl-private.h"
#if WITH_ORCHESTRA
#include "orchestra.h"
#endif /* WITH_ORCHESTRA */

//#include "powertrace.h"

#include "lib/random.h"
#include "simple-udp.h"

#include "net/ip/uip.h"

#define DEBUG DEBUG_PRINT
#include "net/ip/uip-debug.h"

#define CONFIG_VIA_BUTTON PLATFORM_HAS_BUTTON //JSB:1
#if CONFIG_VIA_BUTTON
#include "button-sensor.h"
#endif /* CONFIG_VIA_BUTTON */

#include "../../../apps/powertrace/powertrace.h" //JSB

#define UP_SEND_INTERVAL   (uint32_t)(UPLINK_PERIOD * CLOCK_SECOND)
#define DOWN_SEND_INTERVAL   (uint32_t)(DOWNLINK_PERIOD * CLOCK_SECOND)


#define UDP_PORT 1234

#define UIP_IP_BUF   ((struct uip_ip_hdr *)&uip_buf[UIP_LLH_LEN])

static struct simple_udp_connection unicast_connection;

uint32_t num_recv[TESTBED_SIZE]; 

static uint16_t target_id=ROOT_ID; 
//static uint16_t target_id=0; //JUMP_ID

static uint32_t seq_per_node=1;

static uip_ipaddr_t server_ipaddr;

static uint32_t route_loss=0;

uint8_t bootstrap_period;

uint8_t get_dest_ipaddr(uip_ipaddr_t* dest, uint16_t id);

/*---------------------------------------------------------------------------*/
PROCESS(node_process, "RPL Node");
#if CONFIG_VIA_BUTTON
AUTOSTART_PROCESSES(&node_process, &sensors_process);
#else /* CONFIG_VIA_BUTTON */
AUTOSTART_PROCESSES(&node_process);
#endif /* CONFIG_VIA_BUTTON */

/*---------------------------------------------------------------------------*/
static void
print_network_status(void)
{
  int i;
  uint8_t state;
  uip_ds6_defrt_t *default_route;
#if RPL_WITH_STORING
  uip_ds6_route_t *route;
#endif /* RPL_WITH_STORING */
#if RPL_WITH_NON_STORING
  rpl_ns_node_t *link;
#endif /* RPL_WITH_NON_STORING */

  PRINTF("--- Network status ---\n");

  /* Our IPv6 addresses */
  PRINTF("- Server IPv6 addresses:\n");
  for(i = 0; i < UIP_DS6_ADDR_NB; i++) {
    state = uip_ds6_if.addr_list[i].state;
    if(uip_ds6_if.addr_list[i].isused &&
       (state == ADDR_TENTATIVE || state == ADDR_PREFERRED)) {
      PRINTF("-- ");
      PRINT6ADDR(&uip_ds6_if.addr_list[i].ipaddr);
      PRINTF("\n");
    }
  }

  /* Our default route */
  PRINTF("- Default route:\n");
  default_route = uip_ds6_defrt_lookup(uip_ds6_defrt_choose());
  if(default_route != NULL) {
    PRINTF("-- ");
    PRINT6ADDR(&default_route->ipaddr);
    //PRINTF(" (lifetime: %lu seconds)\n", (unsigned long)default_route->lifetime.interval);
    PRINTF("\n");
  } else {
    PRINTF("-- None\n");
  }

#if RPL_WITH_STORING
  /* Our routing entries */
  PRINTF("- Routing entries (%u in total):\n", uip_ds6_route_num_routes());
  route = uip_ds6_route_head();
  while(route != NULL) {

    uint16_t dest_id=((route->ipaddr.u8[14]) << 8) | (route->ipaddr.u8[15]);
    uint16_t nexthop_id=((uip_ds6_route_nexthop(route)->u8[14]) << 8) | (uip_ds6_route_nexthop(route)->u8[15]);


    //PRINTF("-- ");
    //PRINT6ADDR(&route->ipaddr);
    //PRINTF("%u", dest_id);
    //PRINTF(" via ");
    //PRINT6ADDR(uip_ds6_route_nexthop(route));
    //PRINTF("%u", nexthop_id);

    //PRINTF(" (%lu sec)\n", (unsigned long)route->state.lifetime);
    route = uip_ds6_route_next(route);
  }

  //PRINTF("- 1-hop children entries\n"); //exactly exiting next-hop
  nbr_table_item_t *item = nbr_table_head(nbr_routes);
  while(item != NULL) {

      linkaddr_t *addr = nbr_table_get_lladdr(nbr_routes, item);

      uint16_t child_id=(((addr)->u8[LINKADDR_SIZE - 2]) << 8) | ((addr)->u8[LINKADDR_SIZE - 1]);

      //PRINTF("-- ");
      //PRINTF("%u ",child_id);
      //PRINTF("\n");
      item = nbr_table_next(nbr_routes, item);
    }

#if TESLA
  PRINTF("- Neighbors (my sf size: %u)\n",my_sf_size);
#else
  //PRINTF("- Neighbors\n");
#endif  

  uip_ds6_nbr_t *nbr = nbr_table_head(ds6_neighbors);
  
  while(nbr != NULL) {

    uint16_t nbr_id=((nbr->ipaddr.u8[14]) << 8) | (nbr->ipaddr.u8[15]);

    //PRINTF("-- ");
    //PRINTF("%u ", nbr_id);
#if TESLA    
    //PRINTF(" (sf size: %u(%u), %u %u %u)", nbr->sf_size, nbr->sf_size_version, nbr->num_I_tx, nbr->num_nbr_tx, nbr->num_I_rx);
#endif
    //PRINTF("\n");
    
    nbr = nbr_table_next(ds6_neighbors, nbr);
  }
      
#endif

#if RPL_WITH_NON_STORING
  /* Our routing links */
  PRINTF("- Routing links (%u in total):\n", rpl_ns_num_nodes());
  link = rpl_ns_node_head();
  while(link != NULL) {
    uip_ipaddr_t child_ipaddr;
    uip_ipaddr_t parent_ipaddr;
    rpl_ns_get_node_global_addr(&child_ipaddr, link);
    rpl_ns_get_node_global_addr(&parent_ipaddr, link->parent);
    PRINTF("-- ");
    PRINT6ADDR(&child_ipaddr);
    if(link->parent == NULL) {
      memset(&parent_ipaddr, 0, sizeof(parent_ipaddr));
      PRINTF(" --- DODAG root ");
    } else {
      PRINTF(" to ");
      PRINT6ADDR(&parent_ipaddr);
    }
    PRINTF(" (lifetime: %lu seconds)\n", (unsigned long)link->lifetime);
    link = rpl_ns_node_next(link);
  }
#endif

  PRINTF("----------------------\n");
}
/*---------------------------------------------------------------------------*/
static void
net_init(uip_ipaddr_t *br_prefix)
{
  uip_ipaddr_t global_ipaddr;

  if(br_prefix) { /* We are RPL root. Will be set automatically
                     as TSCH pan coordinator via the tsch-rpl module */
    memcpy(&global_ipaddr, br_prefix, 16);
    uip_ds6_set_addr_iid(&global_ipaddr, &uip_lladdr);
    uip_ds6_addr_add(&global_ipaddr, 0, ADDR_AUTOCONF);
    rpl_set_root(RPL_DEFAULT_INSTANCE, &global_ipaddr);
    rpl_set_prefix(rpl_get_any_dag(), br_prefix, 64);
    rpl_repair_root(RPL_DEFAULT_INSTANCE);
  }

  NETSTACK_MAC.on();
}
/*---------------------------------------------------------------------------*/
static void
receiver(struct simple_udp_connection *c,
         const uip_ipaddr_t *sender_addr,
         uint16_t sender_port,
         const uip_ipaddr_t *receiver_addr,
         uint16_t receiver_port,
         const uint8_t *data,
         uint16_t datalen)
{
  uint16_t src = ((struct app_data *)data)->src;
  uint32_t seqno = ((struct app_data *)data)->seqno - ((uint32_t)src << 16) ;
  uint8_t hop = ((struct app_data *)data)->hop;
  uint32_t seq_per_node1 = ((struct app_data *)data)->dummy1;
  
  if(ROOT_ID<=src && src<=ROOT_ID+TESTBED_SIZE-1)
  {  
    num_recv[src-ROOT_ID]++;
    //ORPL_LOG_FROM_APPDATAPTR((struct app_data *)data, "App: received");
    PRINTF("D Rx from %u", src);
    if(node_id==ROOT_ID)
    {
      PRINTF(", %lu/%lu",num_recv[src-ROOT_ID],seqno+1);  //up
    }
    else
    {
      PRINTF(", %lu/%lu",num_recv[src-ROOT_ID],seq_per_node1*NUM_BURST_DOWN); //down
    }
    hop = uip_ds6_if.cur_hop_limit - UIP_IP_BUF->ttl + 1;
    PRINTF(", H: %d", hop);
    PRINTF("\n");
  }
  else
  {
    PRINTF("ERROR: Wrong src\n");
  }
}

/*---------------------------------------------------------------------------*/
void down_app_send_to(uint16_t id) {

  static uint32_t cnt; //JSB add
  //static unsigned int cnt; //JSB delete

  struct app_data data;
  uip_ipaddr_t dest_ipaddr;
  
  data.magic= APP_DATA_MAGIC;
  data.seqno = ((uint32_t)node_id << 16) + cnt;
  data.src = node_id;
  data.dest = id;
  data.hop = 0;
  data.fpcount = 0;

  data.dummy1=seq_per_node;  //JSB add, ref.orpl-log.h
  
  int i;
  for(i=0;i<10;i++)
  {
    data.dummy2[i]= random_rand()%100000; 
  }

#if !DOWNLINK_DISABLE
  //ORPL_LOG_FROM_APPDATAPTR(&data, "App: sending");
  PRINTF("DATA send to %u ",id); 
  PRINTF(" seq=%lu", seq_per_node);
  PRINTF("\n");
  
  if(!(get_dest_ipaddr(&dest_ipaddr,id)))
  {
    PRINTF("ERROR: No dest id %u\n",id);

    route_loss++;

    printf("R_Loss=%lu\n",route_loss);

    
  }
  else{
    printf("app data size:%d\n",sizeof(data));
    simple_udp_sendto(&unicast_connection, &data, sizeof(data), &dest_ipaddr);
  }

  cnt++;
  /*if(id==TESTBED_SIZE+ROOT_ID-1)
  //if(id==200)//JUMP_ID
  {
   seq_per_node++;
  }*/
#endif
  
}
/*---------------------------------------------------------------------------*/
void up_app_send_to(uint16_t id) {

  static uint32_t cnt; //JSB add
  //static unsigned int cnt; //JSB delete

  struct app_data data;

  /*#if FORWARDER_EXIST
  //if(node_id==1 || node_id==3 || node_id==4 || node_id==5 ||
  //   node_id==6 || node_id==7)
    if(node_id!=1 && node_id!=31)
    {
       return ;
    }
  #endif*/
 
  data.magic= APP_DATA_MAGIC;
  data.seqno = ((uint32_t)node_id << 16) + cnt;
  data.src = node_id;
  data.dest = id;
  data.hop = 0;
  data.fpcount = 0;
  //ORPL_LOG_FROM_APPDATAPTR(&data, "App: sending");

  int i;
  for(i=0;i<10;i++)
  {
    data.dummy2[i]= random_rand()%100000; 
  }

#if !UPLINK_DISABLE
  PRINTF("DATA send %lu", cnt+1);
  PRINTF("\n");


  //PRINTF("app data size:%d\n",sizeof(data));

  simple_udp_sendto(&unicast_connection, &data, sizeof(data), &server_ipaddr);

  cnt++;
#endif
  
}
/*---------------------------------------------------------------------------*/
uint8_t get_dest_ipaddr(uip_ipaddr_t* dest, uint16_t id)
{
  uip_ds6_route_t *route;
  uip_ipaddr_t * addr;
  route = uip_ds6_route_head();
  while(route != NULL) {
    
    addr= &(route->ipaddr);
    //PRINT6ADDR(addr);
    //PRINTF("searching id=%u %u\n",id,addr->u8[15]);
    
    uint16_t addr_id= ((addr->u8[14]) << 8) | (addr->u8[15]);
    //PRINTF("searching id=%u(%04x)\n",addr_id,addr_id);
    if(addr_id==id)
    {
      memcpy(dest,addr,16);
      return 1;
    }
    route = uip_ds6_route_next(route);
  }
  return 0;
}
/*---------------------------------------------------------------------------*/

PROCESS_THREAD(node_process, ev, data)
{
  //static struct etimer et;
  static struct etimer et1;
  static struct etimer periodic_timer;
  static struct etimer send_timer;
  static struct etimer orchestra_timer;

  PROCESS_BEGIN();

  
  /* 3 possible roles:
   * - role_6ln: simple node, will join any network, secured or not
   * - role_6dr: DAG root, will advertise (unsecured) beacons
   * - role_6dr_sec: DAG root, will advertise secured beacons
   * */
  static int is_coordinator = 0;
  static enum { role_6ln, role_6dr, role_6dr_sec } node_role;
  node_role = role_6ln;

  int coordinator_candidate = 0;

  PRINTF("UIP_CONF_MAX_ROUTES=%u\n",UIP_CONF_MAX_ROUTES);
  PRINTF("NBR_TABLE_CONF_MAX_NEIGHBORS=%u\n",NBR_TABLE_CONF_MAX_NEIGHBORS);
  PRINTF("TSCH_CONF_DEFAULT_TIMESLOT_LENGTH=%u\n",TSCH_CONF_DEFAULT_TIMESLOT_LENGTH);

 
#ifdef CONTIKI_TARGET_Z1
  /* Set node with MAC address c1:0c:00:00:00:00:01 as coordinator,
   * convenient in cooja for regression tests using z1 nodes
   * */
  extern unsigned char node_mac[8];
  unsigned char coordinator_mac[8] = { 0xc1, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 };

  coordinator_candidate = (memcmp(node_mac, coordinator_mac, 8) == 0);
#elif CONTIKI_TARGET_COOJA
  coordinator_candidate = (node_id == ROOT_ID);
#endif
  coordinator_candidate = (node_id == ROOT_ID); //JSB add

  
  if(coordinator_candidate) {
    if(LLSEC802154_ENABLED) {
      node_role = role_6dr_sec;
    } else {
      node_role = role_6dr;
      PRINTF("I AM ROOT!\n");
    }
  } else {
    node_role = role_6ln;
#if IOT_LAB_M3
    uip_ip6addr(&server_ipaddr, 0xfd00, 0, 0, 0, 0, 0, 0, ROOT_ID); 
#else
    uip_ip6addr(&server_ipaddr, 0xfd00, 0, 0, 0, 0x0201, 0x0001, 0x0001, ROOT_ID);  //JSB add
#endif
    PRINTF("server_ipaddr: ");
    PRINT6ADDR(&server_ipaddr);
    PRINTF("\n");
  }


#if CONFIG_VIA_BUTTON

  /*{
#define CONFIG_WAIT_TIME 5

    SENSORS_ACTIVATE(button_sensor);
    etimer_set(&et, CLOCK_SECOND * CONFIG_WAIT_TIME);

    while(!etimer_expired(&et)) {
      PRINTF("Init: current role: %s. Will start in %u seconds. Press user button to toggle mode.\n",
             node_role == role_6ln ? "6ln" : (node_role == role_6dr) ? "6dr" : "6dr-sec",
             CONFIG_WAIT_TIME);
      PROCESS_WAIT_EVENT_UNTIL(((ev == sensors_event) &&
                                (data == &button_sensor) && button_sensor.value(0) > 0)
                               || etimer_expired(&et));
      if(ev == sensors_event && data == &button_sensor && button_sensor.value(0) > 0) {
        node_role = (node_role + 1) % 3;
        if(LLSEC802154_ENABLED == 0 && node_role == role_6dr_sec) {
          node_role = (node_role + 1) % 3;
        }
        etimer_restart(&et);
      }
    }
  }*/

#endif /* CONFIG_VIA_BUTTON */

  PRINTF("Init: node starting with role %s\n",
         node_role == role_6ln ? "6ln" : (node_role == role_6dr) ? "6dr" : "6dr-sec");

  tsch_set_pan_secured(LLSEC802154_ENABLED && (node_role == role_6dr_sec));
  is_coordinator = node_role > role_6ln;

  if(is_coordinator) {
    uip_ipaddr_t prefix;
    uip_ip6addr(&prefix, UIP_DS6_DEFAULT_PREFIX, 0, 0, 0, 0, 0, 0, 0);
    net_init(&prefix);
  } else {
    net_init(NULL);
  }



#if WITH_ORCHESTRA
  /***********************JSB add***************************/
  //Too many printf when boot
  etimer_set(&orchestra_timer, 0.5 * CLOCK_SECOND);
  PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&orchestra_timer));
  /***********************JSB end***************************/

  orchestra_init();
#endif /* WITH_ORCHESTRA */




  simple_udp_register(&unicast_connection, UDP_PORT,
                      NULL, UDP_PORT, receiver);
  
  etimer_set(&periodic_timer, NO_DATA_PERIOD * CLOCK_SECOND);
  bootstrap_period=1;
  PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));
  bootstrap_period=0;

#if USE_ENERGEST  
  energest_init();
#endif

  powertrace_start(CLOCK_SECOND*POWERTRACE_INTERVAL); 


  //printf("node.c: c1\n");

  if(node_id == ROOT_ID) {
    etimer_set(&periodic_timer, DOWN_SEND_INTERVAL);

  } else { //node_id != ROOT_ID
#if TRAFFIC_CHANGE_EXP
    etimer_set(&periodic_timer, random_rand() % (UP_SEND_INTERVAL));
#else
    etimer_set(&periodic_timer, UP_SEND_INTERVAL);
#endif
    
  }
  
  //printf("node.c: c2\n");
  etimer_set(&et1, CLOCK_SECOND * 60);

  while(1) {

    if(node_id == ROOT_ID) {

      PROCESS_WAIT_EVENT();

      if(ev == PROCESS_EVENT_TIMER && data == &periodic_timer) {
        etimer_reset(&periodic_timer);
        etimer_set(&send_timer, random_rand() % (DOWN_SEND_INTERVAL));

        
      } 
      else if(ev == PROCESS_EVENT_TIMER && data == &send_timer){

        target_id++;
        //target_id++; //JUMP_ID
        

        if(target_id>TESTBED_SIZE+ROOT_ID-1)
        {
          target_id=ROOT_ID+1; 
          //target_id=3; //JUMP_ID
        }
#if IOT_LAB_M3
#if GRENOBLE1        
        if( target_id==56 || target_id==36 || target_id==37 || target_id==40 || target_id==43 || target_id==46 ||
            target_id==51 || target_id==52 || target_id==57 || target_id==62 ||  target_id==69)

#elif GRENOBLE2
        if( target_id==64 || target_id==2 || target_id==20 || target_id==45 || target_id==59 || target_id==60 || 
            target_id==68 || target_id==76 || 
            target_id==78 || target_id==80 || target_id==61 || target_id==71)

#elif GRENOBLE2_1
        if( target_id==64 || target_id==2 || target_id==20 || target_id==45 || target_id==59 || target_id==60 || 
          target_id==68 || target_id==76 || 
          target_id==78 || target_id==80 || target_id==61 || target_id==71)
          
#elif GRENOBLE2_2
        if( target_id==64 || target_id==2 || target_id==20 || target_id==45 || target_id==59 || target_id==60 || 
          target_id==68 || target_id==76 || 
          target_id==78 || target_id==80 || target_id==61 || target_id==71)

#endif

        {

        }

        else{
          int i;
          for(i=0; i<NUM_BURST_DOWN; i++)
          {
            down_app_send_to(target_id);
          }
        }
#else
        
          int i;
          for(i=0; i<NUM_BURST_DOWN; i++)
          {
            down_app_send_to(target_id);
          }

        
#endif
          if(target_id==TESTBED_SIZE+ROOT_ID-1)
          //if(id==200)//JUMP_ID
          {
           seq_per_node++;
          }

        //down_app_send_to(target_id);
      }
      else if (ev == PROCESS_EVENT_TIMER && data == &et1){
        print_network_status();
        //PROCESS_YIELD_UNTIL(etimer_expired(&et1)); it makes ERROR!!!
        etimer_reset(&et1);

      }
      else{
        PRINTF("ERROR: unexpected event %u, %u,%u,%u,%u,%p",ev 
          ,data==&et1, data==&periodic_timer, data==&send_timer, data==&orchestra_timer, data);
        PRINTF(",%u\n",(((struct etimer *)data)->timer).interval);

        /*int i;
        for(i=0;i<5;i++)
        {
          if((((struct etimer *)data)->p->name)+i!=NULL)
            PRINTF("%c",(((struct etimer *)data)->p->name)[i]);
        }*/
        PRINTF("ERROR: Recovery\n");
        etimer_set(&periodic_timer, DOWN_SEND_INTERVAL);
        etimer_set(&et1, CLOCK_SECOND * 60);
      }
   
    }
    else { //node_id != ROOT_ID    
    
      PROCESS_WAIT_EVENT();

      if(ev == PROCESS_EVENT_TIMER && data == &periodic_timer) {
#if TRAFFIC_CHANGE_EXP
      
        static uint16_t tx_num=0;
        tx_num++;

        if(tx_num<10)
        {
          etimer_set(&periodic_timer, UP_SEND_INTERVAL*2);
        }
        else if(10<tx_num && tx_num<30)
        {
          etimer_set(&periodic_timer, UP_SEND_INTERVAL);
        }
        else
        {
          etimer_set(&periodic_timer, UP_SEND_INTERVAL*2);
        }

        up_app_send_to(ROOT_ID);
#else
        uint16_t rand=random_rand() % (UP_SEND_INTERVAL);
        //printf("node.c: fired %u %u\n",rand/CLOCK_SECOND, CLOCK_SECOND);
        etimer_reset(&periodic_timer);
        etimer_set(&send_timer, rand);

#endif
      } 
      else if(ev == PROCESS_EVENT_TIMER && data == &send_timer){
      
        int j;
        for(j=0; j<NUM_BURST_UP; j++)
        {
          up_app_send_to(ROOT_ID);
        }
        
      }
      else if (ev == PROCESS_EVENT_TIMER && data == &et1){

        print_network_status();
        //PROCESS_YIELD_UNTIL(etimer_expired(&et1)); it makes ERROR!!!
        etimer_reset(&et1);

      }
      else{
        PRINTF("ERROR: unexpected event %u, %u,%u,%u,%u,%p",ev 
          ,data==&et1, data==&periodic_timer, data==&send_timer, data==&orchestra_timer, data);
        PRINTF(",%u\n",(((struct etimer *)data)->timer).interval);

        PRINTF("ERROR: Recovery\n");
        etimer_set(&periodic_timer, UP_SEND_INTERVAL);
        etimer_set(&et1, CLOCK_SECOND * 60);
      }      
    
    }

  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
