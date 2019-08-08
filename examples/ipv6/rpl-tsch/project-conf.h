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
 * \author Simon Duquennoy <simonduq@sics.se>
 */

#ifndef __PROJECT_CONF_H__
#define __PROJECT_CONF_H__



/******************JSB******************/
struct app_data {
  uint32_t magic;
  uint32_t seqno;
  uint16_t src;
  uint16_t dest;
  uint8_t hop;
  uint8_t ping;
  uint8_t fpcount;
  
  uint32_t dummy1; //JSB: app layer payload sizes need to be same ->used in app-up-down.c
  uint32_t dummy2[10]; //JSB: app layer payload sizes need to be same
};

#define APP_DATA_MAGIC 0xcafebabe

#define NUM_BURST_UP 1
#define NUM_BURST_DOWN 1

//#define UPLINK_PERIOD  DOWNLINK_PERIOD*TESTBED_SIZE  //JUMP_ID
//#define DOWNLINK_PERIOD 0.1 * NUM_BURST_DOWN // 0.2, 0.3, 0.5, 0.7, 1  <-> 5, 3.3, 2, 1.4, 1

#define UPLINK_PERIOD 0.286*TESTBED_SIZE  //0.667
#define DOWNLINK_PERIOD 0.667             //0.286

#define UPLINK_DISABLE 0
#define DOWNLINK_DISABLE 0



#define FORWARDER_EXIST 0
#define NO_DATA_PERIOD 1800   //600
#define POWERTRACE_INTERVAL 60

#define TSCH_CONF_RX_WAIT 800  //guard time
#define TSCH_CONF_RX_ACK_DELAY 800
#define TSCH_CONF_TX_ACK_DELAY 1000
#define TSCH_CONF_MAX_KEEPALIVE_TIMEOUT (30 * CLOCK_SECOND)
#define TSCH_CONF_MAX_EB_PERIOD (16 * CLOCK_SECOND)


#define ORCHESTRA_CONF_EBSF_PERIOD 397
#define ORCHESTRA_CONF_COMMON_SHARED_PERIOD 57 //RB(TESLA):23, SB:11 , PROP:57 
#define ORCHESTRA_CONF_UNICAST_PERIOD 47    //TESLA:7  //PROP: 31:o 79:x, so 47

#define ORCHESTRA_CONF_RULES { &eb_per_time_source, &unicast_per_neighbor_rpl_storing, &default_common }
//If change this order, need to check NOTE1.

#define NUM_CHANNEL sizeof(TSCH_DEFAULT_HOPPING_SEQUENCE)
#define NUM_RM_NEIGHBOR 5
#define RETAIN_TX_LINK_DURATION_BEFORE_RM (30 * CLOCK_SECOND)
#define RX_CCA_ENABLED 0
#define COLLISION_WEIGHT 1
#define TSCH_LOG_CONF_ID_FROM_LINKADDR(addr) ((addr) ? ((((addr)->u8[LINKADDR_SIZE - 2]) << 8) | (addr)->u8[LINKADDR_SIZE - 1]) : 0) 
#define ID_FROM_IPADDR(addr) ((addr) ? ((((addr)->u8[14]) << 8) | (addr)->u8[15]) : 0) //JSB
#define SEQNO_LT(a, b) ((signed char)((a) - (b)) < 0)

/*******************************************************/
#define IOT_LAB_M3 1 //0 when COOJA 

#define PROPOSED 1

#define TESLA 0 //SPECIAL_OFFSET check
#define USE_6TISCH_MINIMAL 0  //sf size check
#define ORCHESTRA_CONF_UNICAST_SENDER_BASED 0 //sf size check. Except SB, 0

/*******************************************************/

#if PROPOSED
  #undef ORCHESTRA_CONF_COMMON_SHARED_PERIOD
  #define ORCHESTRA_CONF_COMMON_SHARED_PERIOD 41 //57

  #undef ORCHESTRA_CONF_UNICAST_PERIOD
  #define ORCHESTRA_CONF_UNICAST_PERIOD 47

#endif

#if TESLA
  #undef ORCHESTRA_CONF_COMMON_SHARED_PERIOD
  #define ORCHESTRA_CONF_COMMON_SHARED_PERIOD 41 

  #undef ORCHESTRA_CONF_UNICAST_PERIOD
  #define ORCHESTRA_CONF_UNICAST_PERIOD 7
#endif

#if !PROPOSED & !TESLA & !USE_6TISCH_MINIMAL & !ORCHESTRA_CONF_UNICAST_SENDER_BASED
//RB
  #undef ORCHESTRA_CONF_COMMON_SHARED_PERIOD
  #define ORCHESTRA_CONF_COMMON_SHARED_PERIOD 41 //57 

  #undef ORCHESTRA_CONF_UNICAST_PERIOD
  #define ORCHESTRA_CONF_UNICAST_PERIOD 13
#endif

#if !PROPOSED & !TESLA & !USE_6TISCH_MINIMAL & ORCHESTRA_CONF_UNICAST_SENDER_BASED
//SB
  #undef ORCHESTRA_CONF_COMMON_SHARED_PERIOD
  #define ORCHESTRA_CONF_COMMON_SHARED_PERIOD 41

  #undef ORCHESTRA_CONF_UNICAST_PERIOD
  #define ORCHESTRA_CONF_UNICAST_PERIOD 13
#endif



#if IOT_LAB_M3

#undef RF2XX_SOFT_PREPARE
#define RF2XX_SOFT_PREPARE 0

#undef RF2XX_WITH_TSCH
#define RF2XX_WITH_TSCH 1

#define USE_ENERGEST 1
#define ENERGEST_CONF_ON 1

#define DIO_FILTER 1
#define THRES_DIO_RSSI -80

#define RF2XX_TX_POWER  PHY_POWER_m17dBm //JUMP_ID //PHY_POWER_m17dBm
#define ROOT_ID 1 //130-161 -> 32

#define GRENOBLE1 0
#define GRENOBLE2 1   //Root corner 1
#define GRENOBLE2_1 0 //Root center
#define GRENOBLE2_2 0 //Root corner 2

#if GRENOBLE1
#define TESTBED_SIZE 83 //JUMP_ID
#elif GRENOBLE2
#define TESTBED_SIZE 84 //JUMP_ID
#elif GRENOBLE2_1
#define TESTBED_SIZE 84 //JUMP_ID
#elif GRENOBLE2_2
#define TESTBED_SIZE 84 //JUMP_ID
#endif

#if GRENOBLE1
#define M3_UID {0, 41849,  36982,  45945,  49281,  34933,  45696,  33906,  38247,  37237,  45929,  39025,  46466,  46209,  8544,  39297,  14432,  46952,  45440,  49256,  45672,  37751,  37507,  45160,  14433,  34681,  41842,  34945,  38017,  9058,  42615,  5474,  41091,  47222,  14177,  33649,  36983,  42616,  38265,  37481,  47233,  43394,  45955,  38275,  42115,  39030,  46464,  49783,  35202,  45416,  5728,  46696,  6240,  42368,  42104,  38019,  42113,  33904,  38261,  42096,  50025,  45442,  38529,  46199,  45952,  43392,  10337,  37497,  43136,  38769,  38018,  49526,  43368,  38784,  45953,  42370,  41603,  46977,  39029,  42371,  43369,  47490,  4450,  50032}
#elif  GRENOBLE2                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    // 184 <-> 185                                                   
#define M3_UID {0, 42864,  46184,  45433,  45161,  45687,  45184,  37249,  43137,  39041,  42869,  46454,  37762,  41074,  33911,  4194,  41073,  37234,  42360,  42609,  43129,  33905,  35177,  8802,  37750,  42088,  42089,  34162,  41344,  49782,  38770,  38514,  45673,  34417,  39296,  38787,  41586,  37737,  41833,  9056,  41065,  5985,  38505,  37745,  45417,  42872,  46211,  34169,  42883,  38777,  49257,  38528,  42600,  37760,  39286,  38531,  46710,  38262,  46953,  45185,  41593,  12640,  6241,  41856,  42881,  41089,  39040,  38274,  34677,  37494,  45697,  38008,  38513,  6242,  33909,  41590,  49270,  37753,  38520,  37761,  41337,  43120,  38258,  12385,  41601}
#elif GRENOBLE2_1
#define M3_UID {0, 9056,  46184,  45433,  45161,  45687,  45184,  37249,  43137,  39041,  42869,  46454,  37762,  41074,  33911,  4194,  41073,  37234,  42360,  42609,  43129,  33905,  35177,  8802,  37750,  42088,  42089,  34162,  41344,  49782,  38770,  38514,  45673,  34417,  39296,  38787,  41586,  37737,  41833,  42864,  41065,  5985,  38505,  37745,  45417,  42872,  46211,  34169,  42883,  38777,  49257,  38528,  42600,  37760,  39286,  38531,  46710,  38262,  46953,  45185,  41593,  12640,  6241,  41856,  42881,  41089,  39040,  38274,  34677,  37494,  45697,  38008,  38513,  6242,  33909,  41590,  49270,  37753,  38520,  37761,  41337,  43120,  38258,  12385,  41601}
#elif  GRENOBLE2_2                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    // 184 <-> 185                                                   
#define M3_UID {0, 41601,  46184,  45433,  45161,  45687,  45184,  37249,  43137,  39041,  42869,  46454,  37762,  41074,  33911,  4194,  41073,  37234,  42360,  42609,  43129,  33905,  35177,  8802,  37750,  42088,  42089,  34162,  41344,  49782,  38770,  38514,  45673,  34417,  39296,  38787,  41586,  37737,  41833,  9056,  41065,  5985,  38505,  37745,  45417,  42872,  46211,  34169,  42883,  38777,  49257,  38528,  42600,  37760,  39286,  38531,  46710,  38262,  46953,  45185,  41593,  12640,  6241,  41856,  42881,  41089,  39040,  38274,  34677,  37494,  45697,  38008,  38513,  6242,  33909,  41590,  49270,  37753,  38520,  37761,  41337,  43120,  38258,  12385,  42864}
#endif
     //grenoble{0, 41849,  36982,  45945,  49281,  34933,  45696,  33906,  38247,  37237,  45929,  39025,  46466,  46209,  8544,  39297,  14432,  46952,  45440,  49256,  45672,  37751,  37507,  45160,  14433,  34681,  41842,  34945,  38017,  9058,  42615,  5474,  41091,  47222,  14177,  33649,  36983,  42616,  38265,  37481,  47233,  43394,  45955,  38275,  42115,  39030,  46464,  49783,  35202,  45416,  5728,  46696,  6240,  42368,  42104,  38019,  42113,  33904,  38261,  42096,  50025,  45442,  38529,  46199,  45952,  43392,  10337,  37497,  43136,  38769,  38018,  49526,  43368,  38784,  45953,  42370,  41603,  46977,  39029,  42371,  43369,  47490,  4450,  50032}
     // Lille_latest{0, 36980, 42896, 5458, 14425, 5207, 5460, 10328, 8273, 1377, 0, 9301, 42384, 4693, 2135, 13396, 4437, 4193, 39027, 45682, 47219, 41843, 14168, 39283, 46194, 10072, 6226, 41328, 46960, 4695, 41865, 6484, 13144, 12632, 34675, 13141, 38263, 10068, 14424, 8530, 45683, 9554, 43145, 5204, 9809, 46449, 46962, 6228, 2390, 8537, 45705, 47475, 13912, 5713, 12625, 46961, 41587, 4694, 34420, 45681, 1889, 12884, 9049, 49264, 5976, 46195, 6231, 6487, 10325, 42633, 41072, 5716, 0, 6486, 6481, 35187, 10581, 5974, 8784, 0, 10064, 5462, 8534, 42121, 37744, 38771, 46193, 8533, 49267, 43408, 12369, 4449, 14690, 13395, 12626, 12633, 43152, 8277, 5969, 2393, 39305, 47216, 6233, 2392, 4953, 13400, 43121, 1881, 5720, 34419, 34163, 8791, 46704, 0, 8536, 37232, 8528, 33652, 4952, 12629, 10073, 50291, 9298, 34164, 12888, 4951, 9560, 5970, 9808, 38775, 12373, 12889, 5203, 35188, 4181, 4440, 13397, 9813, 9045, 13139, 9810, 4184, 5718, 14434, 5977, 45939, 45449, 8529, 45936, 5975, 4183, 41104, 41331, 46450, 45169, 45938, 14169, 10069, 8532, 5719, 42611, 41872, 10324, 34676, 41079, 9552, 12370, 8790, 45193, 5209, 46705, 8792, 13652, 49520, 37491, 9305, 9557, 36976, 42099, 1632, 13657, 45426, 10323, 41584, 46216 ,47218, 9958, 4436, 1880, 4950, 12628, 33907, 38256, 13656, 4441, 45960, 9297, 2391, 6230, 36979, 37488, 41353, 39031, 49779, 47474, 45427, 10320, 13145, 9040, 8272, 6229, 5972, 50035, 4949, 10577, 42865, 4438, 47472, 45961, 33908, 43123, 12377, 37235, 41609, 42128, 42355, 38259, 9561, 5205, 38768, 10065, 8274, 13913, 34931, 4692, 45680, 9042, 2136, 37747, 49776, 45704, 45168, 9296, 45171, 4696, 10321, 8785, 9553, 12627, 5465, 9817, 9300, 9304, 47217, 9048, 45937, 9556, 2137, 43377, 12372}
     //Lille : {0, 36980, 42896, 5458, 14425, 5207, 5460, 10328, 8273, 1377, 0, 9301, 42384, 4693, 2135, 13396, 4437, 4193, 39027, 45682, 47219, 41843, 14168, 39283, 46194, 10072, 6226, 41328, 46960, 4695, 41865, 6484, 13144, 12632, 34675, 13141, 38263, 10068, 14424, 8530, 45683, 9554, 43145, 5204, 9809, 46449, 46962, 6228, 2390, 8537, 45705, 47475, 13912, 5713, 12625, 46961, 41587, 4694, 34420, 45681, 1889, 12884, 9049, 49264, 5976, 46195, 6231, 6487, 10325, 42633, 41072, 5716, 0, 6486, 6481, 35187, 10581, 5974, 8784, 0, 10064, 5462, 8534, 42121, 37744, 38771, 46193, 8533, 49267, 43408, 12369, 4449, 14690, 13395, 12626, 12633, 43152, 8277, 5969, 2393, 39305, 47216, 6233, 2392, 4953, 13400, 43121, 1881, 5720, 34419, 34163, 8791, 46704, 0, 8536, 37232, 8528, 33652, 4952, 12629, 10073, 50291, 9298, 34164, 12888, 4951, 9560, 5970, 9808, 38775, 12373, 12889, 5203, 35188, 4181, 4440, 13397, 9813, 9045, 13139, 9810, 4184, 5718, 14434, 5977, 45939, 45449, 8529, 45936, 5975, 4183, 41104, 41331, 46450, 45169, 45938, 14169, 10069, 8532, 5719, 42611, 41872, 10324, 34676, 41079, 9552, 12370, 8790, 45193, 5209, 46705, 8792, 13652, 49520, 37491, 9305, 9557, 36976, 42099, 1632, 13657, 45426, 10323, 41584, 12628, 47218, 46216, 4436, 9558, 4950, 1880, 33907, 38256, 13656, 4441, 45960, 9297, 2391, 6230, 36979, 37488, 41353, 39031, 49779, 47474, 45427, 10320, 13145, 9040, 8272, 6229, 5972, 50035, 4949, 10577, 42865, 4438, 47472, 45961, 33908, 43123, 12377, 37235, 41609, 42128, 42355, 38259, 9561, 5205, 38768, 10065, 8274, 13913, 34931, 4692, 45680, 9042, 2136, 37747, 49776, 45704, 45168, 9296, 45171, 4696, 10321, 8785, 9553, 12627, 5465, 9817, 9300, 9304, 47217, 9048, 45937, 9556, 2137, 43377, 12372}
//Curent Suspected list {10+113+119+127+162+164+167-168+183+192+212+232+237+246-247+249+251}
//Paris :{0,13666, 45173, 10594, 13410, 13399, 41332, 42612, 41844, 12886, 45941, 13922, 10082, 38003, 12631, 10070, 43380, 16482, 37236, 10582, 46452, 37504, 9570, 14167, 42356, 45940, 16994, 38260, 38515, 14422, 12642, 39284, 45428, 49524, 45684, 43381, 45429, 47477, 37748, 46965, 10338, 12375, 9826, 14423, 47221, 12898, 9815, 41076, 16995, 46709, 42868, 14166, 33651, 13142, 49780, 10327, 45685, 13143, 38772, 34944, 9047, 43124, 35200, 38004, 9559, 38516, 46964, 34688, 47476, 13911}

/*******************************************************/
#else
#define RF_TX_POWER -15 //dBm cf)cc2420.c /ieee-mode.c
#define ROOT_ID 1
#define TESTBED_SIZE 25
#endif
/*******************************************************/


/*******************************************************/
/********* Enable RPL non-storing mode *****************/
/*******************************************************/

//#undef UIP_CONF_MAX_ROUTES
//#define UIP_CONF_MAX_ROUTES 0 /* No need for routes */
//#undef RPL_CONF_MOP
//#define RPL_CONF_MOP RPL_MOP_NON_STORING /* Mode of operation*/
//#undef ORCHESTRA_CONF_RULES
//#define ORCHESTRA_CONF_RULES { &eb_per_time_source, &unicast_per_neighbor_rpl_ns, &default_common } /* Orchestra in non-storing */

/*******************************************************/
/********* Enable RPL storing mode *****************/
// JSB: Initial setting was the upper one (non-storing mode)
// JSB: I made this part
/*******************************************************/
#undef UIP_CONF_MAX_ROUTES
#define UIP_CONF_MAX_ROUTES TESTBED_SIZE /* No need for routes */
#undef NBR_TABLE_CONF_MAX_NEIGHBORS
#define NBR_TABLE_CONF_MAX_NEIGHBORS TESTBED_SIZE/2
#undef RPL_CONF_MOP
#define RPL_CONF_MOP RPL_MOP_STORING_NO_MULTICAST /* Mode of operation*/
#undef QUEUEBUF_CONF_NUM
#define QUEUEBUF_CONF_NUM 16

#define TSCH_CONF_MAX_INCOMING_PACKETS 8

#undef TSCH_CONF_MAC_MAX_FRAME_RETRIES
#define TSCH_CONF_MAC_MAX_FRAME_RETRIES 8

#if TESLA
#define TSCH_CONF_MAC_MAX_BE 4
#else
#define TSCH_CONF_MAC_MAX_BE 5
#endif

#define RPL_CONF_WITH_PROBING 1 //0

//#define RPL_CONF_NOPATH_REMOVAL_DELAY RPL_DEFAULT_LIFETIME*3/4*60 //sec

#define RPL_CONF_WITH_DAO_ACK 1
#define RPL_CONF_DAO_RETRANSMISSION_TIMEOUT 20*CLOCK_SECOND
/*******************************************************/
#define PRINT_SELECT 0
#define PRINT_SELECT_1 0

#if PROPOSED
#define N_SELECTION_PERIOD 15 
//related to N_MAX: Min. traffic load = 1/(N_SELECTION_PERIOD*100) pkt/slot (when num_tx=1). 

#define N_MAX 8 //max t_offset 65535-1, 65535 is used for no-allocation
#define MORE_UNDER_PROVISION 1 //more allocation 2^MORE_UNDER_PROVISION times than under-provision
#define NO_ON_DEMAND_PROVISION 0

#define INC_N_NEW_TX_REQUEST 100
#define MULTI_CHANNEL 1
#define PRR_THRES_TX_CHANGE 70
#define NUM_TX_MAC_THRES_TX_CHANGE 20
#define NUM_TX_FAIL_THRES 5
#define THRES_CONSEQUTIVE_N_INC 3

#define T_OFFSET_ALLOCATION_FAIL ((1<<N_MAX) + 1)
#define T_OFFSET_CONSECUTIVE_NEW_TX_REQUEST ((1<<N_MAX) + 2)

#define THRES_CONSECUTIVE_NEW_TX_REQUEST 10

#define TSCH_SCHEDULE_CONF_MAX_SLOTFRAMES 2*NBR_TABLE_CONF_MAX_NEIGHBORS
#define TSCH_SCHEDULE_CONF_MAX_LINKS 2*NBR_TABLE_CONF_MAX_NEIGHBORS


#define RESIDUAL_ALLOC 1
#define SSQ_SCHEDULE_HANDLE_OFFSET (2*TESTBED_SIZE+2) //Under-provision uses up to 2*TESTBED_SIZE+2

#undef TSCH_CONF_RX_ACK_DELAY
#define TSCH_CONF_RX_ACK_DELAY 1300

#undef TSCH_CONF_TX_ACK_DELAY
#define TSCH_CONF_TX_ACK_DELAY 1500


#endif


#if USE_6TISCH_MINIMAL
  #define TSCH_SCHEDULE_CONF_WITH_6TISCH_MINIMAL 1
  #define WITH_ORCHESTRA 0
  #define TSCH_SCHEDULE_CONF_DEFAULT_LENGTH 2
#endif

#if TESLA

#define TSCH_SCHEDULE_CONF_MAX_SLOTFRAMES NBR_TABLE_CONF_MAX_NEIGHBORS
#define TSCH_SCHEDULE_CONF_MAX_LINKS 2*NBR_TABLE_CONF_MAX_NEIGHBORS

#define TEMP_SF_HANDLE 3 //NOTE1
#define SF_HANDLE_OFFSET 3 //NOTE1

#define SF_SIZE_CHECK_PERIOD (15*CLOCK_SECOND) // larger than RETAIN_RX_SF_DURATION_BEFORE_RM //20
#define MAX_SF_SIZE_UPDATE_INTERVAL 0 //30
#define RENEW_MAX_INTERVAL_NBR_OFFSET 0 //30
#define RETAIN_RX_SF_DURATION_BEFORE_RM (10 * CLOCK_SECOND) //15
#define SHARED_TX_INTERVAL 40*CLOCK_SECOND
#define SF_INC_LIMIT 1.5

#define SPECIAL_OFFSET 1 //0 or 1, 0 means No special channel offset 

#define W_th 0 //50 
#define PRR_LOWER 80//80
#define PRR_UPPER 90//90
#define LOAD_UPPER 50
#define THRES_CONSECUTIVE_INC_DECISION 1 //3

#define MODIFY_LOOP_DETECT 1 //Do not use it when TESLA 0

#define INCLUDE_QUEUE 1

#define TRAFFIC_CHANGE_EXP 0

#define PRIME_NUMBERS \
{ \
2,   3,   5,   7,   11,  13,  17,  19,  23,  29,  \
31,  37,  41,  43,  47,  53,  59,  61,  67,  71,  \
73,  79,  83,  89,  97 \
}  


/*101, 103, 107, 109, 113, \
127, 131, 137, 139, 149, 151, 157, 163, 167, 173, \
179, 181, 191, 193, 197, 199 \
}
*/
/*211, 223, 227, 229, \
233, 239, 241, 251, 257, 263, 269, 271, 277, 281, \
283, 293, 307, 311, 313, 317, 331, 337, 347, 349, \
353, 359, 367, 373, 379, 383, 389, 397, 401, 409, \
419, 421, 431, 433, 439, 443, 449, 457, 461, 463, \
467, 479, 487, 491, 499, 503, 509, 521, 523, 541, \
547, 557, 563, 569, 571, 577, 587, 593, 599, 601, \
607, 613, 617, 619, 631, 641, 643, 647, 653, 659, \
661, 673, 677, 683, 691, 701, 709, 719, 727, 733, \
739, 743, 751, 757, 761, 769, 773, 787, 797, 809, \
811, 821, 823, 827, 829, 839, 853, 857, 859, 863, \
877, 881, 883, 887, 907, 911, 919, 929, 937, 941, \
947, 953, 967, 971, 977, 983, 991, 997  \
}*/

#endif
/******************END******************/


/* Set to run orchestra */
#ifndef WITH_ORCHESTRA
#define WITH_ORCHESTRA 1 //JSB: originally 0
#endif /* WITH_ORCHESTRA */



/* Set to enable TSCH security */
#ifndef WITH_SECURITY
#define WITH_SECURITY 0
#endif /* WITH_SECURITY */



/*******************************************************/
/********************* Enable TSCH *********************/
/*******************************************************/

/* Netstack layers */
#undef NETSTACK_CONF_MAC
#define NETSTACK_CONF_MAC     tschmac_driver
#undef NETSTACK_CONF_RDC
#define NETSTACK_CONF_RDC     nordc_driver
#undef NETSTACK_CONF_FRAMER
#define NETSTACK_CONF_FRAMER  framer_802154


/* IEEE802.15.4 frame version */
#undef FRAME802154_CONF_VERSION
#define FRAME802154_CONF_VERSION FRAME802154_IEEE802154E_2012

/* TSCH and RPL callbacks */
#define RPL_CALLBACK_PARENT_SWITCH tsch_rpl_callback_parent_switch
#define RPL_CALLBACK_NEW_DIO_INTERVAL tsch_rpl_callback_new_dio_interval
#define TSCH_CALLBACK_JOINING_NETWORK tsch_rpl_callback_joining_network
#define TSCH_CALLBACK_LEAVING_NETWORK tsch_rpl_callback_leaving_network

/* Needed for CC2538 platforms only */
/* For TSCH we have to use the more accurate crystal oscillator
 * by default the RC oscillator is activated */
#undef SYS_CTRL_CONF_OSC32K_USE_XTAL
#define SYS_CTRL_CONF_OSC32K_USE_XTAL 1

/* Needed for cc2420 platforms only */
/* Disable DCO calibration (uses timerB) */
#undef DCOSYNCH_CONF_ENABLED
#define DCOSYNCH_CONF_ENABLED 0
/* Enable SFD timestamps (uses timerB) */
#undef CC2420_CONF_SFD_TIMESTAMPS
#define CC2420_CONF_SFD_TIMESTAMPS 1

/*******************************************************/
/******************* Configure TSCH ********************/
/*******************************************************/

/* TSCH logging. 0: disabled. 1: basic log. 2: with delayed
 * log messages from interrupt */
#undef TSCH_LOG_CONF_LEVEL
#define TSCH_LOG_CONF_LEVEL 2

/* IEEE802.15.4 PANID */
#undef IEEE802154_CONF_PANID
#define IEEE802154_CONF_PANID 0xabcd

/* Do not start TSCH at init, wait for NETSTACK_MAC.on() */
#undef TSCH_CONF_AUTOSTART
#define TSCH_CONF_AUTOSTART 0

/* 6TiSCH minimal schedule length.
 * Larger values result in less frequent active slots: reduces capacity and saves energy. */
#if USE_6TISCH_MINIMAL
#else
#undef TSCH_SCHEDULE_CONF_DEFAULT_LENGTH
#define TSCH_SCHEDULE_CONF_DEFAULT_LENGTH 3
#endif

#if WITH_SECURITY

/* Enable security */
#undef LLSEC802154_CONF_ENABLED
#define LLSEC802154_CONF_ENABLED 1
/* TSCH uses explicit keys to identify k1 and k2 */
#undef LLSEC802154_CONF_USES_EXPLICIT_KEYS
#define LLSEC802154_CONF_USES_EXPLICIT_KEYS 1
/* TSCH uses the ASN rather than frame counter to construct the Nonce */
#undef LLSEC802154_CONF_USES_FRAME_COUNTER
#define LLSEC802154_CONF_USES_FRAME_COUNTER 0

#endif /* WITH_SECURITY */

#if WITH_ORCHESTRA

/* See apps/orchestra/README.md for more Orchestra configuration options */
#define TSCH_SCHEDULE_CONF_WITH_6TISCH_MINIMAL 0 /* No 6TiSCH minimal schedule */
#define TSCH_CONF_WITH_LINK_SELECTOR 1 /* Orchestra requires per-packet link selection */
/* Orchestra callbacks */
#define TSCH_CALLBACK_NEW_TIME_SOURCE orchestra_callback_new_time_source
#define TSCH_CALLBACK_PACKET_READY orchestra_callback_packet_ready
#define NETSTACK_CONF_ROUTING_NEIGHBOR_ADDED_CALLBACK orchestra_callback_child_added
#define NETSTACK_CONF_ROUTING_NEIGHBOR_REMOVED_CALLBACK orchestra_callback_child_removed

#endif /* WITH_ORCHESTRA */

/*******************************************************/
/************* Other system configuration **************/
/*******************************************************/

#if CONTIKI_TARGET_Z1
/* Save some space to fit the limited RAM of the z1 */
#undef UIP_CONF_TCP
#define UIP_CONF_TCP 0
#undef QUEUEBUF_CONF_NUM
#define QUEUEBUF_CONF_NUM 3
#undef RPL_NS_CONF_LINK_NUM
#define RPL_NS_CONF_LINK_NUM  8
#undef NBR_TABLE_CONF_MAX_NEIGHBORS
#define NBR_TABLE_CONF_MAX_NEIGHBORS 8
#undef UIP_CONF_ND6_SEND_NS
#define UIP_CONF_ND6_SEND_NS 0
#undef SICSLOWPAN_CONF_FRAG
#define SICSLOWPAN_CONF_FRAG 0

#if WITH_SECURITY
/* Note: on sky or z1 in cooja, crypto operations are done in S/W and
 * cannot be accommodated in normal slots. Use 65ms slots instead, and
 * a very short 6TiSCH minimal schedule length */
#undef TSCH_CONF_DEFAULT_TIMESLOT_LENGTH
#define TSCH_CONF_DEFAULT_TIMESLOT_LENGTH 65000
#undef TSCH_SCHEDULE_CONF_DEFAULT_LENGTH
#define TSCH_SCHEDULE_CONF_DEFAULT_LENGTH 2
/* Reduce log level to make space for security on z1 */
#undef TSCH_LOG_CONF_LEVEL
#define TSCH_LOG_CONF_LEVEL 0
#endif /* WITH_SECURITY */

#endif /* CONTIKI_TARGET_Z1 */

#if CONTIKI_TARGET_CC2538DK || CONTIKI_TARGET_ZOUL || \
  CONTIKI_TARGET_OPENMOTE_CC2538
#define TSCH_CONF_HW_FRAME_FILTERING    0
#endif /* CONTIKI_TARGET_CC2538DK || CONTIKI_TARGET_ZOUL \
       || CONTIKI_TARGET_OPENMOTE_CC2538 */

#if CONTIKI_TARGET_COOJA
#define COOJA_CONF_SIMULATE_TURNAROUND 0
#endif /* CONTIKI_TARGET_COOJA */

#endif /* __PROJECT_CONF_H__ */
