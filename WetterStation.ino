/*
Program: WetterStation 
Version: 4
Last change: 2017-08-15, 14:25
Last changes by: Guenter Fattinger
Changes:
- SDintervall von 10 auf 60 Sek.
- Webseit intervall auf 5 Minuten
- Energiedosis Berechnungsfaktor von 0.15 auf 0.06 geändert
*/
#include <Ethernet.h>
#include <Time.h>
#include <TimeLib.h>
#include <avr/pgmspace.h>
#include "DHT.h"
#include <DS3231.h>
#include <Wire.h>
#include <SFE_BMP180.h>
#include "GF_NTPtime.h".;
#include <SPI.h>
#include <SD.h>
#include <math.h>
#include "TimeFrameCount.h"
#include "ExternerText.h";

#define SDintervall 300             // Intervall für SD Karten Speicherung in Sekunden
#define DHTPIN 5                    // Feuchtesensor 
#define DHTTYPE DHT22               // DHT 22  (AM2302) Feuchtesensor
#define HOEHE 644.0                 // Wetterstation über NN
#define Windgeschwindigkeitspin 3   // --Interrupt 1
#define Wippe 2                     // --Interrupt 0  Wippe des Regensensors 1 x um = 0.2794 mm
#define Windrichtungspin A0
#define Radioaktivitaetspin A2
#define UVpin A1
#define REF_3V3 A3                //3.3V power on the Arduino board
#define Regensensor 8               //Erkennt Regentropfen
#define SEP " "
#define Regenfaktor 0.2794
#define FS(x) (__FlashStringHelper*)(x)

byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
byte ip[] = { 10, 0, 0, 150 };
byte mydns[] = { 10, 0, 0, 138 };
byte gateway[] = { 10, 0, 0, 138 };
byte subnet[] = { 255, 255, 255, 0 };

EthernetServer server(80);

DS3231 rtc;                         // Objekt für Uhr (I2C adress 68h or 1101000b)
Date dt;                            // Objekt für Datum
SFE_BMP180 pressure;                // Objekt für Drucksensor (I2C adress 77h or 1110111b)
DHT dht(DHTPIN, DHTTYPE);           // Objekt für Feuchtesensor
GF_NTPtime ntime;                   // Objekt für Zeitserver
TimeFrameCount tfc;                 // Objekt für Regenpulszähler/speicher
TimeFrameCount tfc_Tag;

double TemperaturBMP180, LuftdruckBMP180, absolut;
float LuftfeuchteDHT, TemperaturDHT, Temp;
float Windgeschwindigkeit, sumWind, vavgWind;
float Regenmenge, Regenmenge_Tag;
float UVIndex;
float TempMax = -30;
float TempMin = 40;
float Energiedosis;
float Taupunkt;
bool Regenstatus;                 // Trocken oder Regen
unsigned long UnixTime, LastTime, LastSDwrite;
long LastWindPuls, navg, lastaverageWind;
char Windrichtung[3];
String dataString;
char TimeStr[12];
char DateStr[12];

void setup()
{
  pinMode(4, OUTPUT);
  digitalWrite(4, HIGH);
  Serial.begin (9600);
  Wire.begin();
  tfc.begin(3600, 100);
  tfc_Tag.begin(3600 * 24, 200);
  rtc.init();
  dht.begin();
  pressure.begin();

  pinMode(Windgeschwindigkeitspin, INPUT);
  pinMode(Windrichtungspin, INPUT);
  pinMode(UVpin, INPUT);
  pinMode(REF_3V3, INPUT);
  pinMode(Wippe, INPUT_PULLUP);
  pinMode(Regensensor, INPUT);
  digitalWrite(Regensensor, HIGH);

  Serial.print("Ethernet initialisieren... ");
  Ethernet.begin(mac, ip, mydns, gateway, subnet);
  delay(50);
  server.begin();
  delay(50);
  Serial.println(Ethernet.localIP());

  ntime.init();
  UnixTime = ntime.GetNTPtime();

  rtc.setDateTime(year(UnixTime) - 2000, month(UnixTime), day(UnixTime), (UnixTime % 86400L) / 3600 , (UnixTime % 3600) / 60, UnixTime % 60);
  dt = rtc.getDate();

  Serial.print("Datum: ");Serial.println(dt.getDateString());
  Serial.print("Zeit:  ");Serial.println(dt.getTimeString());

  Serial.print("SD-Karte initialisieren...");

  // see if the card is present and can be initialized:
  if (!SD.begin(4))
  {
    Serial.println("Sd-Karte nicht vorhanden oder defekt!");
  }
  else
  {
    Serial.println("SD-Karte initialsiert!");
    Serial.print("Speicherintervall ist ");Serial.print(SDintervall);Serial.println(" Sekunden");
    File dataFile = SD.open("datalog.txt", FILE_WRITE);     //File öffnen auf SD-Card
    dataFile.close();
  }

  LastWindPuls = -1;
  navg = 0;
  sumWind = 0;
  vavgWind = 0;
  lastaverageWind = now();
  attachInterrupt(0, regenpulscallback, RISING);
  attachInterrupt(1, windpulscallback, RISING);
  LastSDwrite = now();
}

time_t CEDST()
{
  int m=dt.getMonth();
  int d=dt.getDay();
  int h=dt.getHour();
  unsigned long ts=dt.getTimestamp();
  int dow=weekday(ts)-1;
  //int dow=((ts / 86400) + 4) % 7;
  if ((m>3 && m<10) || (m==3 && d-dow>24) || (m==10 && d-dow<=24))
    ts=ts+2*3600;
  else
    ts=ts+1*3600;
  sprintf(TimeStr,"%02d:%02d:%02d",hour(ts),minute(ts),second(ts));
  sprintf(DateStr,"%02d-%02d-%02d",year(ts),month(ts),day(ts));
  return ts;
}

void regenpulscallback()  //Interrupt 0
{
  tfc.count();
  tfc_Tag.count();
}

void windpulscallback() //Interrupt 1
{
  long Zeit = micros();
  if (LastWindPuls < 0)
    Windgeschwindigkeit = 0;
  else
  {
    Windgeschwindigkeit = 1 / ((float)(Zeit - LastWindPuls)) * 2.4e6;
  }
  LastWindPuls = Zeit;
}


void calcTempDruck(double &T, double &P0, double &Pa)
{
  double P;
  char status;

  status = pressure.startTemperature();

  if (status != 0)
  {
    delay(status);
    status = pressure.getTemperature(T);
    if (status != 0)
    {
      status = pressure.startPressure(3);
      if (status != 0)
      {
        delay(status);
        status = pressure.getPressure(Pa, T);
        if (status != 0)
        {
          P0 = pressure.sealevel(Pa, HOEHE); P0 = P0 + 3;
        }
      }
    }
  }
}


float calcTaupunkt(float H, float T)
{
  float TP;
  float a = 272.186;
  float b = 22.4433;
  float c = 6107.85384;
  TP = (((a * log(H / 100) + (c * T) / (a + T)) / ((b - log(H / 100) - (b * T) / a + T))));
  return TP;
}

void stringWind(char*s)
{
  const char*sWR[16] = {"N  ", "NNO", "NO ", "OON", "O  ", "OOS", "SO ", "SSO", "S  ", "SSW", "SW ", "WWS", "W  ", "WWN", "NW ", "NNW"};
  const float minWR[16] = {3.740, 1.78, 2.15, 0.47, 0.5, 0.405, 0.8, 0.59, 1.38, 1.1, 2.98, 2.83, 4.5, 3.94, 4.21, 3.33};
  const float maxWR[16] = {3.94, 2.08, 2.35, 0.5, 0.56, 0.452, 1.0, 0.71, 1.58, 1.35, 3.18, 3.03, 4.64, 4.14, 4.41, 3.53};
  float U = analogRead(Windrichtungspin);
  strcpy(s, "---");
  U = (U * 5) / 1024;
  for (int n = 0; n < 16; n++)
  {
    if (U > minWR[n] && U < maxWR[n])
      strcpy(s, sWR[n]);
  }
}


int averageAnalogRead(int pinToRead) //Funktion für UV-Index
{
  byte numberOfReadings = 8;
  unsigned int runningValue = 0; 

  for(int x = 0 ; x < numberOfReadings ; x++)
    runningValue += analogRead(pinToRead);
  runningValue /= numberOfReadings;

  return(runningValue);  
}

//The Arduino Map function but for floats
//From: http://forum.arduino.cc/index.php?topic=3922.0
float mapfloat(float x, float in_min, float in_max, float out_min, float out_max)
{
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}


float calcRadioaktivitaet()
{
  int sensorRadioaktivitaet = analogRead(Radioaktivitaetspin);
  delay(1);
  double hochzahl = (sensorRadioaktivitaet * 0.0109729087);
  double Potenz = pow(2.71828, hochzahl);
  float Energiedosis = (Potenz * 1.018203343);
  Energiedosis = Energiedosis * 0.02;
  return Energiedosis;
}


void loop()
{
  delay(100);

  if (now() - lastaverageWind > 3)
  {
    vavgWind = sumWind / navg;
    sumWind = 0;
    navg = 0;
    lastaverageWind = now();
  }

  if (micros() - LastWindPuls > 1e6)
  {
    LastWindPuls = -1;
    Windgeschwindigkeit = 0;
  }

  sumWind += Windgeschwindigkeit;
  navg++;

  LuftfeuchteDHT = dht.readHumidity();                // Luftfeuchte von DHT
  TemperaturDHT = dht.readTemperature();              // Temperatur von DHT
  calcTempDruck(TemperaturBMP180, LuftdruckBMP180, absolut); // Luftdruck auf Meereshöhe
  Regenstatus = digitalRead(Regensensor);             // Trocken oder Regen
  Regenmenge = tfc.read() * Regenfaktor;              // Regenmenge berechnen für Stunde
  Regenmenge_Tag = tfc_Tag.read() * Regenfaktor;      // Regenmenge berechnen für Tag
  Temp = ((TemperaturBMP180 + TemperaturDHT) / 2);    // Temperatur von Drucksensor und Feuchtesensor
  
  TempMax = max(Temp, TempMax);                       // MAX/Min - Temp
  TempMin = min(Temp, TempMin);
  Taupunkt = calcTaupunkt(LuftfeuchteDHT, Temp);      // nach Formel aus Temp und Feuchte
  stringWind(Windrichtung);                           // Windrichtung
  dt = rtc.getDate();                                 // Uhrzeit
  int uvLevel = averageAnalogRead(UVpin);             // UV-Intensität
  int refLevel = averageAnalogRead(REF_3V3);
  float outputVoltage = 3.3 / refLevel * uvLevel;
  float uvIntensity = mapfloat(outputVoltage, 0.99, 2.8, 0.0, 15.0); //Spannung auf UV-Intensität konvertieren
  Energiedosis = calcRadioaktivitaet();               // Zählrohr für Ortsdosis
  absolut = absolut + 5.5;                            // Luftdruck Station
  time_t ts=CEDST();
  if (hour(ts) == 18 && minute(ts) == 0 && second(ts) == 0)
  {
    TempMax = Temp;
    TempMin = Temp;
  }

  dataString = "    ";                                //String für die Speicherkarte erzeugen
  dataString = String(DateStr) + SEP + String(TimeStr) + SEP + Temp + SEP + LuftdruckBMP180 + SEP + LuftfeuchteDHT + SEP + vavgWind + SEP + Regenmenge + SEP + Regenmenge_Tag + SEP + Energiedosis + SEP + uvIntensity;
  
    
    if (now() - LastSDwrite >= SDintervall)
    {
      File dataFile = SD.open("datalog.txt", FILE_WRITE);     //File öffnen auf SD-Card
    
      if (dataFile)
      {
        float UV_Vout;
        dataFile.println(dataString);
        dataFile.close();
        Serial.println();
        Serial.println(dataString);
      }
      else
      {
        Serial.println("Kann keine Datei datalog.txt öffnen!!");
      }
        LastSDwrite = now();
    }

  
  Serial.print(".");
  Ethernet.maintain();                                  //Im Ethernet auf die HP schreiben
  EthernetClient client = server.available();
  if (client)
  {
    boolean currentLineIsBlank = true;
    char line[64];  // Buffer (64 characters long) for recording http request lines.
    char* line2;    // Pointer to a a character (or first character of a string)
    byte nc=0;      // Counting variable for writing into buffer letter-by-letter
    char document[64];
    while (client.connected())
    {
      if (client.available())
      {
        char c = client.read(); // Read next available character
        if (c!=10 && nc<63) // If the character is not a newline (ASCII code 10) and the buffer is not full:
        {
          line[nc]=c;       // Copy the character to current position in buffer, indicated by variable nc
          nc++;             // Increase nc by one to point to next position in buffer
        }
        else if (c==10)  // If the character is a newline:
        {
          line[nc]=0;   // Write a terminating 0 to the current position in the buffer 
          nc=0;         // Reset buffer position variable to beginning of buffer (zero)
        }
        if (nc==0)      // If buffer positin counter is at zero, we are ready to look at what has been read:
        {
          line2=strchr(line,32);  // Find the first space (ASCII 32) in the buffer (returns a pointer to it)
          line2[0]=0;             // Replace space with 0 (to terminate string there)
          line2++;                // Move pointer forward to point to the next character after; here the 2nd substring starts!
          if (strcmp(line,"GET")==0)  // check if first sub string up to the 0 we just wrote is "GET". If so:
          {  
            *strchr(line2,32)=0;      // Find first space (ASCII 32) in 2nd substring and replace it by 0 to terminate string there 
            strcpy(document,line2);
          } 
        }
          
        
        if (c == '\n' && currentLineIsBlank)
        {
          if (strcmp(document,"/")==0)
          {
            Serial.println();
            Serial.print("Sending ");
            Serial.println(document);
            // send a standard http response header
            client.println(F("HTTP/1.1 200 OK"));
            client.println(F("Content-Type: text/html"));
            client.println(F("Connection: close"));          // the connection will be closed after completion of the response
            client.println(F("Refresh: 60"));                 // refresh the page automatically every 10 sec
            client.println();
            client.println(FS(header));

            client.print("<font color=#F5BCA9><h2>&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbspDatum:&nbsp");
            client.print(DateStr);
            client.print(F("&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp"));
  
            client.print(F("Zeit:&nbsp "));
            client.print(TimeStr);
            client.println(F("<br>"));
            client.println(F("<br>"));
  
            client.print(F("<font color=#A9BCF5>Temperatur: (</font>"));
            client.print(F("<font color=red>Max</font>")); client.print(F("<font color=#A9BCF5>/</font>")); client.print(F("<font color=cyan>Min</font>")); client.print(F("<font color=#A9BCF5>):&nbsp&nbsp"));
            client.print(F("<font color=#F5DEB3>")); client.print(Temp, 1); client.print(F("</font>")); client.print(F("     (")); client.print(F("<font color=red>")); client.print(TempMax, 1); client.print(F("</font>")); client.print(F("  / ")); client.print(F("<font color=cyan>")); client.print(TempMin, 1); client.print(F("</font>")); client.print(F(")"));
            client.println(F(" &deg;C")); client.print(F("&nbsp&nbsp&nbsp&nbsp&nbsp"));
  
            client.print(F("Windgeschwindigk.:&nbsp"));
            client.print(vavgWind, 1);
            client.print(F(" km/h"));
            client.println(F("<br />"));
  
            client.print(F("Luftdruck (NN):&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp"));
            client.print(LuftdruckBMP180, 1);
            client.println(F("&nbsphPa")); client.print(F("&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp"));
  
  
            client.print(F("Windrichtung:&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp"));
            client.print(Windrichtung);
            client.println(F("<br />"));
  
            client.print(F("Luftdruck (Station):&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp")); client.print(absolut, 1); client.println(F("&nbsphPa&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp"));
  
            client.print(F("<font color=#A9BCF5>Regenmenge&nbsp(h/d):&nbsp&nbsp&nbsp</font>"));
            client.print(Regenmenge, 1);
            client.print(F(" mm / "));
            client.print(Regenmenge_Tag, 1);
            client.print(F(" mm"));
            client.println(F("<br />"));
  
  
            client.print(F("Luftfeuchte:&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp"));
            client.print(LuftfeuchteDHT, 0);
            client.println(F(" %")); client.print(F("&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp"));
  
  
  
            client.print(F("Taupunkt:&nbsp &nbsp &nbsp &nbsp &nbsp &nbsp &nbsp&nbsp&nbsp&nbsp&nbsp "));
            client.println(Taupunkt, 1);
            client.println(F(" &deg;C"));
            client.println(F("<br />"));
  
            client.print(F("Radioaktivität:&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp"));
            client.print(Energiedosis, 2); client.print(F("&nbsp µGy&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp"));
  
            client.print(F("UVindex: &nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp "));
            client.println(uvIntensity,1); client.print(F("mW/cm^2 &nbsp"));
            client.println(F("<br />"));
  
            if (Regenstatus == false)
            {
              client.println(F("<font color=aqua>Regen</font>&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp"));
            }
            else
            {
              client.println(F("<font color=#F5DEB3>Trocken</font>&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp&nbsp"));
            }
            client.println(F("<br><br><a href='data' download='data'>Download Data</a>"));
            
            client.print("<br><br><small>Last build: ");
            client.println(__DATE__);
            client.print(", ");
            client.println(__TIME__);
            client.print("</small><br>");
            
            client.println(FS(legend));
          }
          else if (strcmp(document,"/data")==0)
          {
            Serial.print("Sending ");
            Serial.println(document);
            
            File dataFile = SD.open("datalog.txt", FILE_READ);     //File öffnen auf SD-Card
            if (dataFile)
            {
              while (dataFile.available()) 
              {
                client.write(dataFile.read());
              }
              dataFile.close();
            }
            else
            {
              Serial.println("Kann keine Datei datalog.txt öffnen!!");
            }
           
          }
          break;
        }
        if (c == '\n')                // you're starting a new line
          currentLineIsBlank = true;
        else if (c != '\r')           // you've gotten a character on the current line
          currentLineIsBlank = false;
      }
    }
    delay(100);

    client.stop();
    Serial.println("Client stopped!");
  }

}



