/*
 * Neopixel and wifi for backlight effects.
 * 1) on boot, look if we find a wifi we know if yes:
 * 2) If not
 *    a) Scan for wifi
 *    b) go into Access point mode and let the use select one
 *    c) or allow then to control the color in ap mode
 *    d) if they selected a wifi get password and reboot
 *    
 */

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <FS.h>
#include <Adafruit_NeoPixel.h>
#include <WiFiUdp.h>
#include <Time.h>
#include <TimeLib.h>
#include <TimeAlarms.h>
#include "click_js.h"
#define PIXEL_PIN    4    // Digital IO pin connected to the NeoPixels.
#define PIXEL_COUNT 16
#define MYTZ "EST5EDT"

#ifndef APSSID
#define APSSID "Sasquash"
#define APPSK  "iamayeti"
#endif
const char *apssid = APSSID;
const char *appsk = APPSK;
int numNetworks;
unsigned int localPort = 2390;      // local port to listen for UDP packets
// prototypes for alarm functions
void squashOn(void);
void squashOff(void);
byte retRGB[3] = {0,0,0};

/* Don't hardwire the IP address or we won't get the benefits of the pool.
 *  Lookup the IP address for the host name instead */
IPAddress timeServerIP; // time.nist.gov NTP server address
const char* ntpServerName = "1.north-america.pool.ntp.org";
const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message

byte packetBuffer[ NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets
time_t seconds;
time_t ltime;
time_t diff_time;
struct tm * timeinfo;
struct tm * gmtt;
time_t initialTimes[] = {AlarmHMS(07,00,00), AlarmHMS(21,00,00)};
AlarmId AlarmIds[2];
String alarmNames[] = {"On", "Off"};
OnTick_t almFunc[] = {squashOn, squashOff};

/* 
 *  We will have a web server either as an AP or once on wifi as a client
 */
ESP8266WebServer server(80);
// A UDP instance to let us send and receive packets over UDP
WiFiUDP udp;

// Parameter 1 = number of pixels in strip,  neopixel stick has 8
// Parameter 2 = pin number (most are valid)
// Parameter 3 = pixel type flags, add together as needed:
//   NEO_RGB     Pixels are wired for RGB bitstream
//   NEO_GRB     Pixels are wired for GRB bitstream, correct for neopixel stick
//   NEO_KHZ400  400 KHz bitstream (e.g. FLORA pixels)
//   NEO_KHZ800  800 KHz bitstream (e.g. High Density LED strip), correct for neopixel stick
Adafruit_NeoPixel strip = Adafruit_NeoPixel(PIXEL_COUNT, PIXEL_PIN, NEO_GRB + NEO_KHZ800);
enum modes {constant, rainbow, cycle, chase};
bool on;
/*------------------------------------------------------------
 structs for data that will be safed on the spiffs file system
 information    c type     structure   File
 -------------+----------+-----------+------------
 network data : netdata_t, netdata  -> network.dat
 led data     : leddata_t, leddata  -> led.dat
 timer data   : timedata_t, timedata -> time.dat
 --------------------------------------------------------------*/
typedef struct {
  char ssid[32]="";
  char psk[32]="";
  char hostname[32]="";
} netdata_t;
 netdata_t netdata;
typedef struct {
  int num_leds;
  enum modes mode;
  uint32_t ledOnColor[128]; // max pixel count
} leddata_t;
leddata_t leddata;
typedef struct {
  char tz[8] = "";
  int dst = 0;
  time_t almtime[2];
  bool enable[2];
} timedata_t;
timedata_t timedata;
void setup() {
  int r;
  int l=0;
  Serial.begin(115200);
  SPIFFS.begin();
  Serial.println(F("Loading config"));
  // set default incase no file loaded
  // timedata aka time.dat
  timedata.dst=0;
  strncpy(timedata.tz,"EST5EDT",7);
  timedata.tz[7] = 0;
  timedata.almtime[0] = initialTimes[0];
  timedata.almtime[1] = initialTimes[1];
  timedata.enable[0] = false;
  timedata.enable[1] = false;
  // leddata aka led.dat
  leddata.num_leds = 16;
  leddata.mode = rainbow;
  for(r=0; r<leddata.num_leds; r++) {
    leddata.ledOnColor[r]=63<<((r%3)*8);
  }
  //start to try file loading
  File f = SPIFFS.open("/led.dat", "r");
  if(!f) { // file does not exist
    Serial.println("No led file, use defaults");
  } else {
    f.read((byte*)&leddata,sizeof(leddata));
    f.close();
    strip.updateLength(leddata.num_leds);
  }
  strip.begin();
  strip.clear();
  f = SPIFFS.open("/network.dat", "r");
  if(!f) { // file does not exist
    netdata.ssid[0]=0;
    netdata.psk[0]=0;
    strncpy(netdata.hostname,"sasquash",31);
    netdata.hostname[31] = 0;
    Serial.println("No network file, will be an AP");
  } else {
    f.read((byte*)&netdata,sizeof(netdata));
    f.close();
    f = SPIFFS.open("/time.dat", "r");
    if(!f) { // file does not exist
      Serial.println("No timedata file, Data default");
    } else {
      f.read((byte*)&timedata,sizeof(timedata));
      f.close();
      Serial.print("read file, will connect to: '");
      Serial.print(netdata.ssid);
      Serial.print("' using PSK of '");
      Serial.print(netdata.psk);
      Serial.println("'.");
    }
    strip.setPixelColor(0, 100,0,0); // switch to green after scan
    strip.show();
    setenv("TZ", timedata.tz, timedata.dst);
    tzset();
  } // else no wifi data
  // Set WiFi to station mode and disconnect from an AP if it was previously connected
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  Serial.println("Setup done");
  r = wifiScan();
  strip.setPixelColor(0, 0,100,0); // switch to green after scan
  strip.show(); // update
  if(r!=-1) {
    Serial.print("Connecting to ");
    Serial.print(netdata.ssid);
    Serial.print(" as ");
    Serial.println(netdata.hostname);
    WiFi.disconnect();
    WiFi.scanDelete();
    WiFi.hostname(netdata.hostname);
    WiFi.begin(netdata.ssid, netdata.psk);
    r=10;
    while ((WiFi.status() != WL_CONNECTED) && (r > -1)) {
      strip.setPixelColor((11-r), 100,0,0); // light a red light for each loop tyring to connect
      strip.show(); // update
      delay(1000);
      Serial.print(r);
      r=r-1;
    }
  }
  Serial.println("");
  if(r!=-1) {
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.println("Starting UDP");
    udp.begin(localPort);
    Serial.print("Local port: ");
    Serial.println(udp.localPort());
    Serial.println("Trying to get initial time");
    while(! seconds) {
      seconds = getNtpTime();
      if (seconds) {
	setTime(seconds);
	Serial.println("Set initial time");
	timeinfo = localtime (&seconds);
	Serial.println(asctime(timeinfo));
      } else {
	Serial.println("unable to get initial time re-trying");
      }
      // fiddle with leds to show loop
      strip.setPixelColor(l,0,0,0);
      l = (l+1) % PIXEL_COUNT;
      strip.setPixelColor(l,0,20,0);
      strip.show();
      delay(1000); // wait a second
    }
  }
  if(r==-1) {
    //strip.clear();
    strip.setPixelColor(0, 0,0,100); // switch to blue if unconnected
    strip.show(); // update
    runAp();
  } else {
    for (uint8_t a = 0; a < 2; a++) {
      AlarmIds[a] = Alarm.alarmRepeat(loc2gm(initialTimes[a]),almFunc[a]);
      Serial.print(alarmNames[a]);
      Serial.print(" alarm id: ");
      Serial.println(AlarmIds[a]);
      Alarm.disable(AlarmIds[a]); // disable default
    }
  }
  Serial.println(" Alarm Setup Done.");
  squashOn();
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  // HTML pages
  server.on("/", handle_root);
  server.on("/configWifi",handle_configWifi);
  server.on("/configLed",handle_configLed);
  server.on("/configTime",handle_configTime);
  // settings callback aka CGI
  server.on("/setcolor",handle_setcolor);
  server.on("/setleddata",handle_leddataForm); 
  server.on("/settimedata",handle_timedataForm);
  server.on("/setwifi",handle_setwifi);
  //when all else fails
  server.onNotFound(handle_NotFound);
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  // update internal timer used for web headers.
  seconds = now();
  timeinfo = localtime (&seconds);
  server.handleClient();
  if(on==false) {
    Alarm.delay(10);
  } else if (leddata.mode==rainbow) {
    runRainbow();
  } else if (leddata.mode==cycle) {
    cycleRainbow();
  } else if (leddata.mode==chase) {
    chaseRainbow();
  } else {
    Alarm.delay(10);
  }
  //delay(10);
}
void wDelay(int ms) {
  for(int w = 0; w<ms; w=w+10) {
    seconds = now();
    timeinfo = localtime (&seconds);
    server.handleClient();
    Alarm.delay(10);
  }
}
int wifiScan() {
   Serial.println("scan start");
  int r =-1;
  // WiFi.scanNetworks will return the number of networks found
  numNetworks = WiFi.scanNetworks();
  Serial.println("scan done");
  if (numNetworks == 0) {
    Serial.println("no networks found");
  } else {
    Serial.print(numNetworks);
    Serial.println(" networks found");
    for (int i = 0; i < numNetworks; ++i) {
      // Print SSID and RSSI for each network found
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.print(WiFi.SSID(i));
      Serial.print(" (");
      Serial.print(WiFi.RSSI(i));
      Serial.print(")");
      Serial.println((WiFi.encryptionType(i) == ENC_TYPE_NONE) ? " " : "*");
      if(strcmp(WiFi.SSID(i).c_str(),netdata.ssid) ==0) {
        Serial.println("this matched my eeprom data");
        r=i;
      }
    }
  }
  Serial.println("");
  return(r);
}

 void handle_NotFound(){
   if (!handleFileRead(server.uri()))                  // send it if it exists
     server.send(404, "text/plain", "Not found");      // othewise error
}

void handleApRoot() {
  server.send(200, "text/html", genAPRootHTML());
}
void handle_root() {
  server.send(200, "text/html", genRootHTML());
}
void handle_configTime() {
  server.send(200, "text/html", genTimeHTML());
}
void handle_configLed() {
  server.send(200, "text/html", genLedHTML());
}

void handle_configWifi() {
  server.send(200, "text/html", genWifiHTML());
}

String getContentType(String filename) { // convert the file extension to the MIME type
  if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css")) return "text/css";
  else if (filename.endsWith(".js")) return "application/javascript";
  else if (filename.endsWith(".ico")) return "image/x-icon";
  return "text/plain";
}

bool handleFileRead(String path) { // send the right file to the client (if it exists)
  Serial.println("handleFileRead: " + path);
  if (path.endsWith("/")) path += "index.html";         // If a folder is requested, send the index file
  String contentType = getContentType(path);            // Get the MIME type
  if (SPIFFS.exists(path)) {                            // If the file exists
    File file = SPIFFS.open(path, "r");                 // Open it
    size_t sent = server.streamFile(file, contentType); // And send it to the client
    file.close();                                       // Then close the file again
    return true;
  }
  Serial.println("\tFile Not Found");
  return false;                                         // If the file doesn't exist, return false
}

void runAp() {
 Serial.print("Configuring access point...");
  /* You can remove the password parameter if you want the AP to be open. */
  if(WiFi.softAP(apssid, appsk)) {
    Serial.println("Soft AP OK");
  } else {
    Serial.println("Soft AP startup error");
  }

  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(myIP);
  server.on("/", handleApRoot);
  server.on("/setwifi",handle_setwifi);
  server.on("/setcolor",handle_setcolor);
  server.onNotFound(handle_NotFound);
  server.begin();
  Serial.println("HTTP server started");
  while(1) {
    server.handleClient();
    delay(10);
  }
}

void squashOn() {
  uint16 i;
  on = true;
  if(leddata.mode==constant) {
    for(i=0; i<strip.numPixels(); i++) {
      strip.setPixelColor(i, leddata.ledOnColor[i]);
    }
    strip.show();
  }
}

void squashOff() {
  on = false;
  strip.clear();
  strip.show();
}

void reboot() {
  ESP.restart();
}

// send an NTP request to the time server at the given address
unsigned long sendNTPpacket(IPAddress& address)
{
  Serial.println("sending NTP packet...");
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
  udp.beginPacket(address, 123); //NTP requests are to port 123
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
}

time_t getNtpTime(void) {
  WiFi.hostByName(ntpServerName, timeServerIP); 

  sendNTPpacket(timeServerIP); // send an NTP packet to a time server
  // wait to see if a reply is available
  delay(1000);
  
  int cb = udp.parsePacket();
  if (cb) {
    // We've received a packet, read the data from it
    udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer
    //the timestamp starts at byte 40 of the received packet and is four bytes,
    // or two words, long. First, extract the two words:
    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
    // combine the four bytes (two words) into a long integer
    // this is NTP time (seconds since Jan 1 1900):
    unsigned long secsSince1900 = highWord << 16 | lowWord;
    // now convert NTP time into everyday time:
    // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
    const unsigned long seventyYears = 2208988800UL;
    // subtract seventy years:
    unsigned long epoch = secsSince1900 - seventyYears;
    return(epoch);
  }
  return(0);
}

char * ascTimeStatus(void) {
  timeStatus_t s = timeStatus();
  if(s==timeNotSet) {
    return("Not Set");
  } else if(s==timeNeedsSync) {
    return("Need Sync");
  } else if(s==timeSet) {
    return("Time OK");
  } else {
    return("No status");
  }
}
// helper functions for TimeAlarms since they work in GMT and missing access method
time_t loc2gm(time_t itime) {
  return( (SECS_PER_DAY + (itime-diff_time))%SECS_PER_DAY);
}

time_t gm2loc(time_t itime) {
  return( (SECS_PER_DAY + (itime+diff_time))%SECS_PER_DAY);
}
String genAPRootHTML(){
  String ptr = "<!DOCTYPE html> <html>\n"; 
  ptr +="<html>\n";
  ptr+="    <head>\n";
  ptr+="      <title>Set WIFI</title>\n";
  ptr+=style_js;
  ptr+="     </head>\n";
  ptr+="    <body  onload=\"clickColor(false);\">\n";
  ptr+="      <h1>Wifi settings</h1>\n";
  ptr+="      <form action=\"/setwifi\" method=\"post\">\n";
  ptr+="	<label for=\"network\">Select Network<br></label>\n";
  ptr+="  <select id=\"network\" name=\"network\">\n";
  if (numNetworks == 0) {
    ptr+="	  <option value=\"None\">None</option>\n";
  } else {
    for (int i = 0; i < numNetworks; ++i) {
      // Print SSID and RSSI for each network found
      ptr+="	  <option value=\"";
      ptr+=WiFi.SSID(i);
      ptr+="\">";
      ptr+=WiFi.SSID(i);
      ptr+="</option>\n";
    }
    ptr+="  </select><br><br>\n";
    ptr+="	<label for=\"psk\">Network Key<br></label>\n";
    ptr+="	<input type=text id=\"psk\" name=\"psk\"><br><br>\n";
    ptr+="	<label for=\"hname\">host name<br></label>\n";
    ptr+="	<input type=text id=\"hname\" name=\"hname\" value=\"";
    ptr+=netdata.hostname;
    ptr+="\"><br><br>\n";
    ptr+="	<input type=submit value=Save>\n";
    ptr+="      </form>\n";
    ptr+="      <p>Click the 'Save' button to set the network paramaters and reboot.</p>\n";
    ptr+="      <!-- see https://www.w3schools.com/colors/colors_picker.asp -->\n";
    ptr+=script_js;
    ptr+="      \n";
    ptr+=control_html;
    if(on) {
      ptr+="checked=\checked\"";
    }
    ptr+=control_html1;
    ptr+=genModeSelect();
    ptr+=control_html2;
    ptr+=String(leddata.num_leds);
    ptr+=control_html3;
    ptr+="      <br>\n";
    ptr+=footer_links;
    ptr+="    </body>\n";
    ptr+="</html>\n";
    ptr+="\n";
    return ptr;
  }
}
String genWifiHTML(){
  String ptr = "<!DOCTYPE html> <html>\n"; 
  ptr +="<html>\n";
  ptr+="    <head>\n";
  ptr+="      <title>Set WIFI</title>\n";
  ptr+=style_js;
  ptr+="     </head>\n";
  ptr+="    <body  onload=\"clickColor(false);\">\n";
  ptr+="      <h1>Wifi settings</h1>\n";
  ptr+="      <form action=\"/setwifi\" method=\"post\">\n";
  ptr+="  <label for=\"network\">Select Network<br></label>\n";
  ptr+="  <input type=text id=\"network\" name=\"network\" value=\"";
  ptr+=netdata.ssid;
  ptr+="\"><br><br>\n";
  ptr+="  <label for=\"psk\">Network Key<br></label>\n";
  ptr+="  <input type=text id=\"psk\" name=\"psk\"><br><br>\n";
  ptr+="  <label for=\"hname\">host name<br></label>\n";
  ptr+="  <input type=text id=\"hname\" name=\"hname\" value=\"";
  ptr+=netdata.hostname;
  ptr+="\"><br><br>\n";
  ptr+="  <input type=submit value=Save>\n";
  ptr+="      </form>\n";
  ptr+="      <p>Click the 'Save' button to set the network paramaters and reboot.</p>\n";
  ptr+="      <!-- see https://www.w3schools.com/colors/colors_picker.asp -->\n";
  ptr+=script_js;
  ptr+="      \n";
  ptr+=control_html;
  if(on) {
    ptr+="checked=\checked\"";
  }
  ptr+=control_html1;
  ptr+=genModeSelect();
  ptr+=control_html2;
  ptr+=String(leddata.num_leds);
  ptr+=control_html3;
  ptr+="      <br>\n";
  ptr+=footer_links;
  ptr+="    </body>\n";
  ptr+="</html>\n";
  ptr+="\n";
  return ptr;
}

String genRootHTML(){
  String ptr = "<!DOCTYPE html> <html>\n"; 
  ptr +="<html>\n";
  ptr+="    <head>\n";
  ptr+="      <title>Set Color</title>\n";
  ptr+=style_js;
  ptr+="     </head>\n";
  ptr+="    <body  onload=\"clickColor(false);\">\n";
  ptr+="      <h1>Led setting</h1>\n";
  ptr+="Changes made here take place immediatly, but are saved if power is lost. Click the button below to save LED configuration<br>\n";
  ptr+=script_js;
  ptr+="      \n";
  ptr+=control_html;
  if(on) {
    ptr+="checked=\checked\"";
  }
  ptr+=control_html1;
  ptr+=genModeSelect();
  ptr+=control_html2;
  ptr+=String(leddata.num_leds);
  ptr+=control_html3;
  ptr+="      <br>\n";
  ptr+=footer_links;
  ptr+="    </body>\n";
  ptr+="</html>\n";
  return ptr;
}

String genTimeHTML(){
  time_t curAlmTime;
  String ptr = "<!DOCTYPE html> <html>\n"; 
  ptr +="<html>\n";
  ptr+="    <head>\n";
  ptr+="      <title>Set Time Zone and alarms</title>\n";
  ptr+=style_js;
  ptr+="     </head>\n";
  ptr+="    <body>\n";
  ptr+="<br><br><hr><br><br>";
  ptr += "<h1>Set On and off time</h1>\n";
  ptr +="<h3>";
  ptr +=asctime(timeinfo);
  ptr +=" ";
  ptr +=ascTimeStatus();
  ptr +="</h3>\n";
  ptr +="<form action=\"/settimedata\" method=\"post\">\n";
  for (uint8_t i = 0; i < 2; i++) {
    curAlmTime = gm2loc(Alarm.read(AlarmIds[i]));
    ptr +="<label for=\"alm" + String(i) + "\"> " + alarmNames[i] + " time: </label>\n";
    ptr +="<input id=\"alm" + String(i) + "\" type=\"time\" name=\"alm" + String(i) + "\" value=\"";
    if(hour(curAlmTime)<10) {
      ptr += "0";
    }
    ptr += String(hour(curAlmTime)) + ":";
    if(minute(curAlmTime)<10) {
      ptr += "0";
    }
    ptr += String(minute(curAlmTime)) + "\" required>\n";
    ptr +="<input type=\"checkbox\" name=\"enable" + String(i) + "value=\"enabled\" ";
    if(Alarm.isEnabled(AlarmIds[i])) {
      ptr += "checked";
    }
    ptr += "> Enable?";
    ptr +="<br />\n";
  }
  ptr+="<br><br><hr><br><br>";
  ptr+=tz_form;
  ptr+="	      <input type=\"submit\" value=\"Save\">\n";
  ptr+="	      </form>\n";
  ptr+="	      <p>Click the \"Save\" button to save alarms and zone information.</p>\n";
  ptr+="<br><br><hr><br><br>";
  ptr+=footer_links;
  ptr+="    </body>\n";
  ptr+="</html>\n";
  return ptr;
}
String genLedHTML(){
  String ptr = "<!DOCTYPE html> <html>\n"; 
  ptr +="<html>\n";
  ptr+="    <head>\n";
  ptr+="      <title>Set and save LED configuration </title>\n";
  ptr+=style_js;
  ptr+="     </head>\n";
  ptr+="    <body  onload=\"clickColor(false);\">\n";
  ptr+="      <h1>Led setting</h1>\n";
  ptr+="Changes made here take place immediatly, but can be saved Click the save button below to save LED configuration<br>\n";
  ptr+=script_js;
  ptr+="      \n";
  ptr+=control_html;
  if(on) {
    ptr+="checked=\checked\"";
  }
  ptr+=control_html1;
  ptr+=genModeSelect();
  ptr+=control_html2;
  ptr+=String(leddata.num_leds);
  ptr+=control_html3;
  ptr+="<form action=\"/setleddata\" method=\"post\">\n";
  //ptr+="  	    <div class=\"input-group\" id=\"numLedDIV\">\n";
  ptr+="  	      <label for=\"num_leds\">Number of Leds</label>\n";
  ptr+="	      <input id=\"num_leds\" name=\"num_leds\" type=number min=1 max=255 value=";
  ptr+=String(leddata.num_leds);
  ptr+=">\n";
  //ptr+="	    </div>\n";
  ptr+="	      <input type=\"submit\" value=\"Save\">\n";
  ptr+="	      </form>\n";
  ptr+="	      <p>Click the \"Save\" button to save LED configuration.</p>\n";
  ptr+="      <br>\n";
  ptr+=footer_links;
  ptr+="    </body>\n";
  ptr+="</html>\n";
  return ptr;
}

void handle_setwifi() {
  if (server.method() != HTTP_POST) {
    String message = "Non post POST method: ";
    message += String(server.method()) +"\n";
    server.send(405, "text/plain", message);
  } else {
    String ptr = "<!DOCTYPE html> <html>\n";
    ptr +="<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";
    ptr +="<title>Set WiFi</title>\n";
    ptr+=style_js;
    ptr +="</head>\n";
    ptr +="<body>\n";
    ptr +="<h1>Set WiFi</h1>\n";
    ptr +="<h3> server args</h3>";
    for (uint8_t i = 0; i < server.args(); i++) {
      ptr += " (" + String(i) +") "+ server.argName(i) + ": " + server.arg(i) + " -- <br>";
      if(server.argName(i).substring(0,7).equals("network")) {
        strncpy(netdata.ssid,server.arg(i).c_str(),31);
      }
      if(server.argName(i).substring(0,3).equals("psk")) {
        strncpy(netdata.psk,server.arg(i).c_str(),31);
      }
      if(server.argName(i).substring(0,5).equals("hname")) {
        strncpy(netdata.hostname,server.arg(i).c_str(),31);
      }
    }
    if(netdata.hostname!="") {
      ptr +="<br> Saving<br>\n";
      File f = SPIFFS.open("/network.dat", "w");
      if(f && f.write((byte*)&netdata,sizeof(netdata)) ) {
        f.close();
       ptr +="<br> Success<br>\n";
     }
    }
    ptr +="<br> Rebooting in 30 seconds<br>\n";
    ptr +=footer_links;
    ptr +="</body>\n";
    ptr +="</html>\n";
    server.send(200, "text/html", ptr);
    delay(30000);
    reboot();
  }
}
void handle_leddataForm() {
  int r;
  if (server.method() != HTTP_POST) {
    String message = "Non post POST method: ";
    message += String(server.method()) +"\n";
    server.send(405, "text/plain", message);
  } else {
    String ptr = "<!DOCTYPE html> <html>\n";
    ptr +="<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";
    ptr +="<title>Sasquash Time Control</title>\n";
    ptr+=style_js;
    ptr +="</head>\n";
    ptr +="<body>\n";
    ptr +="<h1>Sasquash LED Setting</h1>\n";
    ptr +="<h3>";
    ptr +=asctime(timeinfo);
    ptr +=" ";
    ptr +=ascTimeStatus();
    ptr +="</h3>\n";
    ptr +="<h3> server args</h3>";
    for (uint8_t i = 0; i < server.args(); i++) {
      ptr += " (" + String(i) +") "+ server.argName(i) + ": " + server.arg(i) + " -- <br>";
      if(server.argName(i).substring(0,8).equals("num_leds")) {
        r=server.arg(i).toInt();
	      leddata.num_leds=r;
	      strip.updateLength(leddata.num_leds);
      }
    }
    if(leddata.mode==constant) {
      for(r=0; r<leddata.num_leds; r++) {
        leddata.ledOnColor[r]=strip.getPixelColor(r);
      }
    }
    ptr +="<br> Saving<br>\n";
      File f = SPIFFS.open("/led.dat", "w");
      if(f && f.write((byte*)&leddata,sizeof(leddata)) ) {
        f.close();
       ptr +="<br> Success<br>\n";
     }
    ptr +=footer_links;
    ptr +="</body>\n";
    ptr +="</html>\n";
    server.send(200, "text/html", ptr);
  }
}

  
void handle_timedataForm() {
  bool e;
  time_t newt, oldt;
  int t,hr,min;
  if (server.method() != HTTP_POST) {
    String message = "Non post POST method: ";
    message += String(server.method()) +"\n";
    server.send(405, "text/plain", message);
  } else {
    String ptr = "<!DOCTYPE html> <html>\n";
    ptr +="<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";
    ptr +="<title>Sasquash Time Control</title>\n";
    ptr+=style_js;
    ptr +="</head>\n";
    ptr +="<body>\n";
    ptr +="<h1>Sasquash Time Control Setting</h1>\n";
    ptr +="<h3>";
    ptr +=asctime(timeinfo);
    ptr +=" ";
    ptr +=ascTimeStatus();
    ptr +="</h3>\n";
    ptr +="<h3> server args</h3>";
    timedata.dst=0; // default if box is unchecked
    for (uint8_t i = 0; i < server.args(); i++) {
      ptr += " (" + String(i) +") "+ server.argName(i) + ": " + server.arg(i) + " -- <br>";
      if(server.argName(i).substring(0,3).equals("alm")) {
        t = server.argName(i).substring(3,4).toInt();
        ptr += alarmNames[t] + ": ";
        hr = server.arg(i).substring(0,2).toInt();
        min = server.arg(i).substring(3,5).toInt();
        newt = loc2gm(AlarmHMS(hr,min,0));
        oldt = Alarm.read(AlarmIds[t]);
        if(newt == oldt) {
          ptr += "Unchanged, ";
        } else {
	  ptr += "Update timer, ";
	  Alarm.write(AlarmIds[t],newt);
        }
	timedata.almtime[t] = newt;
        if(server.argName(i+1).substring(0,6).equals("enable")) {
          e = true;
          ptr += "Enabled, ";
	  Alarm.enable(AlarmIds[t]); // explicitly enable, helps with midnight gmt.
        } else {
          e = false;
          ptr += "Disabled, ";
	  Alarm.disable(AlarmIds[t]);
        }
	timedata.enable[t] = e;
        if(e == Alarm.isEnabled(AlarmIds[t])) {
          ptr += "Same Enable state.<br />\n";
        } else {
          ptr += "Different Enable state.<br >\n";
        }
      }
      if(server.argName(i).substring(0,2).equals("tz")) {
        strncpy(timedata.tz,server.arg(i).c_str(),8);
      }
      if(server.argName(i).substring(0,3).equals("dst")) {
        if(server.arg(i).substring(0,4).equals("true")) {
	  timedata.dst=1;
	}
      }
    }
    ptr +="<br> Saving<br>\n";
    File f = SPIFFS.open("/timedata.dat", "w");
    if(f && f.write((byte*)&timedata,sizeof(timedata)) ) {
      f.close();
      ptr +="<br> Success<br>\n";
     }
    ptr +="<br> Rebooting in 30 seconds<br>\n";
    ptr +=footer_links;
    ptr +="</body>\n";
    ptr +="</html>\n";
    server.send(200, "text/html", ptr);
    delay(30000);
    reboot();
  }
}
void handle_setcolor() {
  uint8_t r,g,b,all;
  int num;
  if (server.method() != HTTP_POST) {
    Serial.println("Non post POST method: ");
    String message = "Non post POST method: ";
    message += String(server.method()) +"\n";
    server.send(405, "text/plain", message);
  } else {
    server.send(200, "text/plain", "revieved post query");
   Serial.println("POST method: ");
    for (uint8_t i = 0; i < server.args(); i++) {
      Serial.print("(" + String(i) +") "+ server.argName(i) + ": " + server.arg(i) + " \n");
      if(server.argName(i).substring(0,1).equals("r")) {
        r=server.arg(i).toInt();
        //Serial.print("got r=" + String(r));
      }
       if(server.argName(i).substring(0,1).equals("g")) {
        g=server.arg(i).toInt();
        //Serial.print("got g=" + String(g));
      }
      if(server.argName(i).substring(0,1).equals("b")) {
        b=server.arg(i).toInt();
         //Serial.print("got b=" + String(b));
      }
      if(server.argName(i).substring(0,3).equals("num")) {
        num=server.arg(i).toInt();
      }
      if(server.argName(i).substring(0,2).equals("on")) {
        if(server.arg(i).substring(0,4).equals("true")) {
	  on=true;
	}else{
	  on=false;
	  strip.clear();
	  strip.show();
	}
      }
      if(server.argName(i).substring(0,3).equals("all")) {
        if(server.arg(i).substring(0,4).equals("true")) {
	  all=1;
	}else{
	  all=0;
	}
      }
      if(server.argName(i).substring(0,4).equals("mode")) {
        if(server.arg(i).substring(0,8).equals("Constant")) {
	  leddata.mode=constant;
	}
        if(server.arg(i).substring(0,7).equals("Rainbow")) {
	  leddata.mode=rainbow;
	}
        if(server.arg(i).substring(0,13).equals("Chase Rainbow")) {
	  leddata.mode=chase;
	}
        if(server.arg(i).substring(0,13).equals("Cycle Rainbow")) {
	  leddata.mode=cycle;
	}
      }
    }
    if(on && leddata.mode == constant) {
      if(all == 1) {
	for (int j = 0; j < leddata.num_leds; j++) {
	  strip.setPixelColor(j, r,g,b); // set all pixels
	}
      } else {
	  strip.setPixelColor(num, r,g,b); // set this pixel
      }
      strip.show(); // update
    }
  }
}

void wheel(int pos){
    // Generate rainbow colors across 0-255 positions.
    if(pos < 85) {
        retRGB[0] = pos * 3;
        retRGB[1] = 255 - pos * 3;
        retRGB[2] = 0;
        return;
    } else if( pos < 170) {
        pos -= 85;
        retRGB[2] = pos * 3;
        retRGB[0] = 255 - pos * 3;
        retRGB[1] = 0;
        return;
    } else {
        pos -= 170;
        retRGB[1] = pos * 3;
        retRGB[2] = 255 - pos * 3;
        retRGB[0] = 0;
        return;
    }
}
void runRainbow(void){
byte i;
int l;
    //Draw rainbow that fades across all pixels at once.
    for(l =0; l<(256 * 1); l++){
        for(i=0; i<leddata.num_leds; i++){
            wheel((i+l) & 255);
            strip.setPixelColor(i, retRGB[0],retRGB[1],retRGB[2]);
        }
        if(!on) {
          squashOff();
          return;
        }
        strip.show();
        wDelay(50);
    }
}

void cycleRainbow(void) {
byte i;
int l;
    //Draw rainbow that uniformly distributes itself across all pixels.
    for(l=0; l <(256*5); l++) {
        for(i=0; i<leddata.num_leds; i++){
            wheel((int(i * 256 / leddata.num_leds) + l) & 255);
            strip.setPixelColor(i, retRGB[0],retRGB[1],retRGB[2]);
        }
        if(!on) {
          squashOff();
          return;
        }
        strip.show();
        wDelay(50);
    }
}

void chaseRainbow(void){
byte i,j;
int l;
    //Rainbow movie theater light style chaser animation.
    for(l = 0 ; l<256; l++){
     for(j=0; j<10; j++){
      for(i=0; i<leddata.num_leds; i++){
                wheel((i+j) % 255); 
                strip.setPixelColor(i, retRGB[0],retRGB[1],retRGB[2]);
            }
        if(!on) {
          squashOff();
          return;
        }
            strip.show();
            wDelay(100);
            for(i=0; i<leddata.num_leds; i++){
                strip.setPixelColor(i, 0,0,0);
            }
     }
    }
}
String genModeSelect(){
  String ptr="  		<option value=\"Constant\"";
  if(leddata.mode==constant) {ptr += " selected";}
  ptr+=">Constant</option>\n";
  ptr+="		<option value=\"Rainbow\"";
  if(leddata.mode==rainbow) {ptr += " selected";}
  ptr+=">Rainbow</option>\n";
  ptr+="		<option value=\"Cycle Rainbow\"";
  if(leddata.mode==cycle) {ptr += " selected";}
  ptr+=">Cycle Rainbow</option>\n";
  ptr+="		<option value=\"Chase Rainbow\"";
  if(leddata.mode==chase) {ptr += " selected";}
  ptr+=">Chase Rainbow</option>\n";
  return(ptr);
}
