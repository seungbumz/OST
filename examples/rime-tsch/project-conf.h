/*
 * Copyright (c) 2016, Inria.
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
 *         Project config file
 * \author
 *         Simon Duquennoy <simon.duquennoy@inria.fr>
 *
 */

#ifndef __PROJECT_CONF_H__
#define __PROJECT_CONF_H__


#define IOT_LAB_M3 0 

#if IOT_LAB_M3
#define RF2XX_WITH_TSCH 1
#define RF2XX_TX_POWER  PHY_POWER_3dBm
#define ROOT_ID 1
#define TESTBED_SIZE 25
#define LILLE_M3_UID {0, 36980, 42896, 5458, 14425, 5207, 5460, 10328, 8273, 1377, 0, 9301, 42384, 4693, 2135, 13396, 4437, 4193, 39027, 45682, 47219, 41843, 14168, 39283, 46194, 10072, 6226, 41328, 46960, 4695, 41865, 6484, 13144, 12632, 34675, 13141, 38263, 10068, 14424, 8530, 45683, 9554, 43145, 5204, 9809, 46449, 46962, 6228, 2390, 8537, 45705, 47475, 13912, 5713, 12625, 46961, 41587, 4694, 34420, 45681, 1889, 12884, 9049, 49264, 5976, 46195, 6231, 6487, 10325, 42633, 41072, 5716, 0, 6486, 6481, 35187, 10581, 5974, 8784, 0, 10064, 5462, 8534, 42121, 37744, 38771, 46193, 8533, 49267, 43408, 12369, 4449, 14690, 13395, 12626, 12633, 43152, 8277, 5969, 2393, 39305, 47216, 6233, 2392, 4953, 13400, 43121, 1881, 5720, 34419, 34163, 8791, 46704, 0, 8536, 37232, 8528, 33652, 4952, 12629, 10073, 50291, 9298, 34164, 12888, 4951, 9560, 5970, 9808, 38775, 12373, 12889, 5203, 35188, 4181, 4440, 13397, 9813, 9045, 13139, 9810, 4184, 5718, 14434, 5977, 45939, 45449, 8529, 45936, 5975, 4183, 41104, 41331, 46450, 45169, 45938, 14169, 10069, 8532, 5719, 42611, 41872, 10324, 34676, 41079, 9552, 12370, 8790, 45193, 5209, 46705, 8792, 13652, 49520, 37491, 9305, 9557, 36976, 42099, 1632, 13657, 45426, 10323, 41584, 12628, 47218, 46216, 4436, 9558, 4950, 1880, 33907, 38256, 13656, 4441, 45960, 9297, 2391, 6230, 36979, 37488, 41353, 39031, 49779, 47474, 45427, 10320, 13145, 9040, 8272, 6229, 5972, 50035, 4949, 10577, 42865, 4438, 47472, 45961, 33908, 43123, 12377, 37235, 41609, 42128, 42355, 38259, 9561, 5205, 38768, 10065, 8274, 13913, 34931, 4692, 45680, 9042, 2136, 37747, 49776, 45704, 45168, 9296, 45171, 4696, 10321, 8785, 9553, 12627, 5465, 9817, 9300, 9304, 47217, 9048, 45937, 9556, 2137, 43377, 12372}
//Curent Suspected list {10+113+119+127+162+164+167-168+183+192+212+232+237+246-247+249+251}
//0 --> 0

#else
#define RF_TX_POWER -15 //dBm cf)cc2420.c /ieee-mode.c
#define ROOT_ID 1
#define TESTBED_SIZE 25
#endif


#define NO_DATA_PERIOD 300
#define UPLINK_PERIOD  2//DOWNLINK_PERIOD*TESTBED_SIZE


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

#undef TSCH_CONF_AUTOSELECT_TIME_SOURCE
#define TSCH_CONF_AUTOSELECT_TIME_SOURCE 1

/* Needed for cc2420 platforms only */
/* Disable DCO calibration (uses timerB) */
#undef DCOSYNCH_CONF_ENABLED
#define DCOSYNCH_CONF_ENABLED            0
/* Enable SFD timestamps (uses timerB) */
#undef CC2420_CONF_SFD_TIMESTAMPS
#define CC2420_CONF_SFD_TIMESTAMPS       1

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
#undef TSCH_SCHEDULE_CONF_DEFAULT_LENGTH
#define TSCH_SCHEDULE_CONF_DEFAULT_LENGTH 7

#undef TSCH_LOG_CONF_ID_FROM_LINKADDR
#define TSCH_LOG_CONF_ID_FROM_LINKADDR(addr) ((addr) ? (addr)->u8[LINKADDR_SIZE - 2] : 0)

#if CONTIKI_TARGET_COOJA
#define COOJA_CONF_SIMULATE_TURNAROUND 0
#endif /* CONTIKI_TARGET_COOJA */


#endif /* __PROJECT_CONF_H__ */
