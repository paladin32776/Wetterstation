/*
Library to get time from a NTP time server
Author: Guenter Fattinger
*/

#include "Arduino.h"
#include "GF_NTPtime.h"

GF_NTPtime::GF_NTPtime() 
{  
}

void GF_NTPtime::init()
{
  Udp.begin(localPort);
}

void GF_NTPtime::sendNTPpacket(char* address)  // send an NTP request to the time server at the given address
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  //Serial.println("beginPacket");
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  //Serial.println("write");
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  //Serial.println("endPacket");
  Udp.endPacket();
}

unsigned long GF_NTPtime::GetNTPtime()
{
  //Serial.print("Sending NTP packet ... ");
  sendNTPpacket("time.metrologie.at"); // send an NTP packet to a time server
  //Serial.println("done.");
  // wait to see if a reply is available
  //Serial.print("Waiting ... ");
  delay(1000);
  //Serial.println("done.");
  int packetSize = Udp.parsePacket();
  if (packetSize) 
  {
    //Serial.print("Received packet of size ");
    //Serial.println(packetSize);
    //Serial.print("Parsing UDP packet ... ");
    // We've received a packet, read the data from it
    Udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer
    //Serial.println("done.");
    // the timestamp starts at byte 40 of the received packet and is four bytes,
    // or two words, long. First, extract the two words:

    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
    // combine the four bytes (two words) into a long integer
    // this is NTP time (seconds since Jan 1 1900):
    unsigned long secsSince1900 = highWord << 16 | lowWord;
    //Serial.print("Seconds since Jan 1 1900 = ");
    //Serial.println(secsSince1900);

    // now convert NTP time into everyday time:
    //Serial.print("Unix time = ");
    // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
    const unsigned long seventyYears = 2208988800UL;
    // subtract seventy years:
    epoch = secsSince1900 - seventyYears;
    // print Unix time:
    //Serial.println(epoch);
  }
  return epoch;
}


