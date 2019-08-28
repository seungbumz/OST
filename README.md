# OST + TESLA
This is the open source code of "TESLA: Traffic-aware Elstic Slotframe Adjustment in TSCH Networks" and "OST: On-Demand TSCH Scheduling with Traffic-awareness". Furthermore, it also can be used for 6TiSCH minimal scheduling (https://tools.ietf.org/html/draft-ietf-6tisch-minimal-21#section-4.1) and Orchestra (https://dl.acm.org/citation.cfm?id=2809714).

TESLA will be published in IEEE Access soon, while OST is under review process.



Main application files can be found in OST/examples/ipv6/rpl-tsch

1. node.c: initializes RPL/TSCH netwrok and generates bidirectional traffic. It contains operations of both the RPL root and non-roots nodes.

2. project-conf.h: introduces a lot of experimental parameters and settings.
  
  - IOT_LAB_M3: When it is set, the code is for testbed experiments (default: IoT-LAB M3 devices). Otherwise, it is for Cooja simulation.
  
  There are four important parameters to configure each of TSCH scheduling scheme.
  - PROPOSED: It is set for "OST" experiment with the others unset.
  - TESLA: It is set for "TESLA" experiment with the others unset.
  - USE_6TISCH_MINIMAL: It is set for "6TiSCH minimal scheduling" experiment with the others unset.
  - ORCHESTRA_CONF_UNICAST_SENDER_BASED: It is set for "sender-based Orchestra" with the others unset.
  Lastly, if all of them are unset, that is "receiver-based Orchestra".
  
  - M3_UID: Devices' UIDs of target nodes in IoT-LAB testbed (https://www.iot-lab.info/testbed/status).
  
  
