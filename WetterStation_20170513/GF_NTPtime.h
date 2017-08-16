/*
Library to get time from a NTP time server
Author: Guenter Fattinger
*/
#ifndef GF_NTPtime_h
#define GF_NTPtime_h

#include "Arduino.h"
#include <EthernetUdp.h>

#define NTP_PACKET_SIZE 48

class GF_NTPtime
{
  public:
    GF_NTPtime();
    void init();
    void sendNTPpacket(char* address);
    unsigned long GetNTPtime();
    
  private:
    EthernetUDP Udp;
    //const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message
    byte packetBuffer[ NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets
    unsigned int localPort = 8888;       // local port to listen for UDP packets  
    unsigned long epoch;
};

#endif
