
#include <WiFi.h>
#include <WebServer.h>
#include "WS2812FX.h"
#include <Adafruit_NeoPixel.h>
#include "DNSServer.h"
#include <WiFiUdp.h>
#include <TinyGPS++.h>
//#include <SoftwareSerial.h>
#include "ESP32_RMT_Driver.h"


#define WIFI_SSID "TeslaLED"
#define WIFI_PASSWORD ""

#define STATIC_IP                       // uncomment for static IP, set IP below

#ifdef STATIC_IP
  IPAddress ip(10,10,10,10);
  IPAddress gateway(10,10,10,1);
  IPAddress subnet(255,255,255,0);
#endif

WiFiUDP Udp;
unsigned int localUdpPort = 4210;  // local port to listen on
char incomingPacket[255];  // buffer for incoming packets
char  replyPacket[] = "OK";  // a reply string to send back

const byte DNS_PORT = 53;
DNSServer dnsServer;

TaskHandle_t LEDTask;
TaskHandle_t CommTask;

extern const char index_html[];
extern const char main_js[];


// QUICKFIX...See https://github.com/esp8266/Arduino/issues/263
#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))

                                    //Out 1-12, 2-13, 3-14
#define OUT1_LED_PIN 12 //D1                       // 0 = GPIO0, 2=GPIO2
#define OUT1_LED_COUNT 40//200 //151

#define OUT2_LED_PIN 13 //D1                       // 0 = GPIO0, 2=GPIO2
#define OUT2_LED_COUNT 24//200 //151

#define WIFI_TIMEOUT 30000              // checks WiFi every ...ms. Reset after this time, if WiFi cannot reconnect.
#define HTTP_PORT 80

#define DEFAULT_COLOR 0x5F00FF
#define DEFAULT_BRIGHTNESS 255 //10
#define DEFAULT_SPEED 3000
//#define DEFAULT_MODE FX_MODE_STATIC
#define DEFAULT_MODE 0
#define auto_cycle_timedelay 90000

unsigned long auto_last_change = 0;
unsigned long last_wifi_check_time = 0;
unsigned long wifi_check_time = 5000;
String modes = "";
uint8_t myModes[] = {}; // *** optionally create a custom list of effect/mode numbers
boolean auto_cycle = true;
boolean auto_cycle_state = false; // used to detect going in and out of auto

int BlueStat_Blink = 2000;  // ms
unsigned long last_BlueStat_Blink = 0;
const short int BlueStat_LED = 2; //GPIO2
boolean WiFIConnected = false;

WS2812FX ws2812fx1 = WS2812FX(OUT1_LED_COUNT, OUT1_LED_PIN, NEO_RGB + NEO_KHZ800);
WS2812FX ws2812fx2 = WS2812FX(OUT2_LED_COUNT, OUT2_LED_PIN, NEO_GRB + NEO_KHZ800);
WebServer server(HTTP_PORT);

int PatternsCount = 25; 
int PatternsPlayed[25] = {};

TinyGPSPlus gps;  // The TinyGPS++ object
//SoftwareSerial ss(D5, D6); // The serial connection to the GPS device
int print_GPS ,GPScount, GPSage = 0;
String MPHspeed_str , GPScount_str;
float MPHspeed = 0;

void setup(){
  pinMode(BlueStat_LED, OUTPUT); // Initialize the BlueStat_LED pin as an output
  Serial.begin(115200); //115200 or 500000
  Serial.println();
  Serial.println();
  Serial.println("Starting...");
  //ESP.wdtEnable(4000); // Enable software WDT for 4 Sec timeout
  //ESP.wdtDisable();

  modes.reserve(5000);
  modes_setup();

  Serial.println("WS2812FX setup");
  ws2812fx1.init();
  ws2812fx1.setMode(DEFAULT_MODE);
  ws2812fx1.setColor(DEFAULT_COLOR);
  ws2812fx1.setSpeed(DEFAULT_SPEED);
  ws2812fx1.setBrightness(DEFAULT_BRIGHTNESS);
  rmt_tx_int(RMT_CHANNEL_0, ws2812fx1.getPin()); // assign ws2812fx to RMT channel 0
  ws2812fx1.setCustomShow(ws2812fx1OUTPUT);
  ws2812fx1.start();
  Serial.printf("Output 1 on GPIO %d - STARTED\n",ws2812fx1.getPin());
  
  ws2812fx2.init();
  ws2812fx2.setMode(DEFAULT_MODE);
  ws2812fx2.setColor(DEFAULT_COLOR);
  ws2812fx2.setSpeed(DEFAULT_SPEED);
  ws2812fx2.setBrightness(DEFAULT_BRIGHTNESS);
  rmt_tx_int(RMT_CHANNEL_1, ws2812fx2.getPin()); // assign ws2812fx to RMT channel 0
  ws2812fx2.setCustomShow(ws2812fx2OUTPUT);
  ws2812fx2.start();
  Serial.printf("Output 2 on GPIO %d - STARTED\n",ws2812fx2.getPin());
  
  Serial.printf("Number of modes:%d\n",ws2812fx1.getModeCount());
  
  Serial.println("Wifi setup....");
  wifi_setup();
 
  Serial.println("HTTP server setup....");
  server.on("/", srv_handle_index_html);
  server.on("/main.js", srv_handle_main_js);
  server.on("/modes", srv_handle_modes);
  server.on("/set", srv_handle_set);
  server.onNotFound(srv_handle_not_found);
  server.begin();
  Serial.println("HTTP server started!");
  
  Udp.begin(localUdpPort);
  Serial.printf("UDP listening at IP %s:%d\n", WiFi.localIP().toString().c_str(), localUdpPort);

  Serial2.begin(9600);
  print_GPS = millis() + 800;
  Serial.println("GPS Serial Input Started....");
  Serial.printf("setup() running on core %d\n",xPortGetCoreID());
  
  //create a task that will be executed in the Task1code() function, with priority 1 and executed on core 0
  Serial.println("LEDTaskCode Starting....");    
  xTaskCreatePinnedToCore(
                    LEDTaskCode,   /* Task function. */
                    "LEDTask",     /* name of task. */
                    10000,       /* Stack size of task */
                    NULL,        /* parameter of the task */
                    100,           /* priority of the task */
                    &LEDTask,      /* Task handle to keep track of created task */
                    1);          /* pin task to core 0 */   
             
  delay(50); 
  Serial.println("CommTaskCode Starting....");
  xTaskCreatePinnedToCore(
                    CommTaskCode,   /* Task function. */
                    "CommTask",     /* name of task. */
                    10000,       /* Stack size of task */
                    NULL,        /* parameter of the task */
                    1,           /* priority of the task */
                    &CommTask,      /* Task handle to keep track of created task */
                    0);          /* pin task to core 0 */                  
  delay(50); 
  Serial.println("Running!");
}

void loop() {
  
}

int SpeedFadeVal = 255;
int LEDTick;
void LEDTaskCode( void * pvParameters ){
  Serial.printf("LEDTaskCode running on core %d\n",xPortGetCoreID());
  for(;;){
    //unsigned long tick = micros();
    //if(tick - LEDTick > 100) {
      //LEDTick = tick;
      //ws2812fx1.setMPH(MPHspeed);
      ws2812fx1.service(SpeedFadeVal);
      ws2812fx2.service(255);
    //} 
  }
}

bool Driving;
int CommTick;
int SendUDPTick;
String inString = "";    // string to hold serial input
void CommTaskCode( void * pvParameters ){
  Serial.printf("CommTaskCode running on core %d\n",xPortGetCoreID());
  for(;;){
    //ESP.wdtFeed(); // Reset WDT 
    unsigned long tick = millis();
    if(tick - CommTick > 100) {
      CommTick = tick;
      if (GPSage > 25 && GPSage < 1500){
        CommTick += 20;
      }
      unsigned long now = millis();
      if (auto_cycle_state != auto_cycle){
        if (auto_cycle == false){Serial.println("Manual Mode");} else {Serial.println("Auto Mode");}
        auto_cycle_state = auto_cycle;
      }
    
      if (now - BlueStat_Blink > last_BlueStat_Blink){ 
        digitalWrite(BlueStat_LED, !digitalRead(BlueStat_LED));
        last_BlueStat_Blink = now;
      }

      while (Serial.available() > 0) {
        int inChar = Serial.read();
        if (isDigit(inChar)) {
          // convert the incoming byte to a char and add it to the string:
          inString += (char)inChar;
        }
        // if you get a newline, print the string, then the string's value:
        if (inChar == '\n') {
          if(inString.toInt() < 60){
            Serial.print("Setting show to:");
            Serial.println(inString.toInt());
            SetShow(inString.toInt());
            UpdateShowSettings();
            auto_cycle = false;
            if (auto_cycle == false){Serial.print("Manual ");} else {Serial.print("Auto ");}
            Serial.print("Mode- Show:"); Serial.print(ws2812fx1.getMode());Serial.print(" - "); Serial.print(ws2812fx1.getModeName(ws2812fx1.getMode()));
            Serial.print(" & Speed is:"); Serial.println(ws2812fx1.getSpeed());
          }
          else{
            auto_cycle = true;
          }
          inString = "";
        }
      }
      
      dnsServer.processNextRequest();
      server.handleClient();
      delay(2);
      int packetSize = Udp.parsePacket();
      if (packetSize)
      {
        // receive incoming UDP packets
        
        //Serial.printf("Received %d bytes from %s, port %d\n", packetSize, Udp.remoteIP().toString().c_str(), Udp.remotePort());
        int len = Udp.read(incomingPacket, 255);
        if (len > 0)
        {
          incomingPacket[len] = 0;
        }
        //Serial.printf("UDP packet contents: %s\n", incomingPacket);
        Serial.printf("%s: %s\n", Udp.remoteIP().toString().c_str(), incomingPacket);
        
        if(incomingPacket != replyPacket){
        // send back a reply, to the IP address and port we got the packet from
        //Udp.beginPacket(Udp.remoteIP(), Udp.remotePort());
        //Udp.write(replyPacket);
        //Udp.endPacket();
        }
      }
      delay(2);
      if (Serial2.available() > 0){
       // Serial.println(ss.available());
      }
      while (Serial2.available() > 0){
        //Serial.write(ss.read());
        if (gps.encode(Serial2.read())){
          if (gps.speed.isValid()){
            MPHspeed = gps.speed.mph();
          }
          if (gps.satellites.isValid()){
            GPScount = gps.satellites.value();
          }
          if (GPScount < 4){
            MPHspeed = 0;
          }
          MPHspeed_str = String(MPHspeed);
          GPScount_str = String(GPScount);
          
          if (GPSage >= 0 || GPSage <= 5000){ // -1 or 5000+ means the GPS is off-line
            if (MPHspeed < 5){ // Speed less then to start fading up
              if (SpeedFadeVal < 255){
                SpeedFadeVal +=1;
              }
              else{
                SpeedFadeVal = 255;
                Driving = false;
              }
            }
            else{ // Speed is over 2 mph, start fading down selected LEDS
              SpeedFadeVal -= MPHspeed / 10;
              if (SpeedFadeVal < 0){
                SpeedFadeVal = 0;
                if (!Driving){
                  Driving = true;
                  Serial.println(" Drving Detected - Starting Show: 0");
                  SetShow(0);
                }
              }
            }
          }
          else{//GPS off-line so bypass driving settings
            SpeedFadeVal = 255;
          }
          
        }
      }
      if(now > print_GPS + 1000){
        print_GPS = now;
        GPSage = gps.satellites.age();
        //Serial.print(String(GPSage));
        if (GPSage < 0 || GPSage >= 5000){
          Serial.printf("~~GPS OFF-LINE~~ Age(sec.):%d\n",GPSage/1000);
          MPHspeed = 0;
          GPScount = 0;
          SpeedFadeVal = 255;
          print_GPS += 3000;
        }
        else
        { 
          if (GPSage > 25 && GPSage < 1500){
            print_GPS -= 20;
          }
          Serial.print(" MPH:");
          Serial.print(String(MPHspeed_str));
          Serial.print(" Sat#:");
          Serial.print(String(GPScount_str));
          Serial.print(" Age(ms):");
          Serial.print(String(GPSage));
          Serial.print(" FadeVal:");
          Serial.print(String(SpeedFadeVal));
          if (Driving){
            Serial.print(" Driving..");
          }
          Serial.println(" ");
          
        }
        
      }
      if(now > SendUDPTick + 500){
        SendUDPTick = now;
        SendShowInfo();
      }
      delay(2);
      
      //ws2812fx1.service();
                
      if(now - last_wifi_check_time > wifi_check_time) {
        last_wifi_check_time = now;
        if (WiFi.status() == WL_CONNECTED){
          BlueStat_Blink = 50;
          Udp.beginPacket({255,255,255,255}, localUdpPort);
          //Udp.write("Online");
          //data will be sent to server
          uint8_t buffer[50] = "Online";
          Udp.write(buffer, 11);
          Udp.endPacket();
          if(WiFIConnected == false){
            WiFIConnected = true;
            Serial.println("WiFi connected");
          }
        }else {
          BlueStat_Blink = 800;
          if(WiFIConnected == true){
            WiFIConnected = false;
            Serial.println("WiFi disconnected");
          }
        }
      }
          
      if(auto_cycle && (now - auto_last_change > auto_cycle_timedelay) && !Driving) { // cycle effect mode every 30 seconds
        //uint8_t next_mode = (ws2812fx.getMode() + 1 + random(0,15) ) % ws2812fx.getModeCount(); //
        boolean NewShow = false;
    
        while (NewShow == false){
          boolean Found = false;
          uint8_t next_mode = (random(0,58) ) % ws2812fx1.getModeCount(); // 
          if(next_mode > 58){ // disable custom
            next_mode = 0;
          }
          Serial.print("|");
          for( int i = 0; i < PatternsCount; i++){ //Seach PatternsPlayed for new RandNum
            Serial.print(PatternsPlayed[i]);
            if(PatternsPlayed[i] == next_mode){
              Found = true;
              Serial.println("<- has already played, retrying...");
              break; // RandNum show has already played, Pick a new RandNum show
            }
            else{
              Serial.print("|");
            }
          }
          if(Found == false){
            for( int i = 0; i < PatternsCount; i++){ //Remove first show num. from list
              PatternsPlayed[i] = PatternsPlayed[i+1];
            }
            NewShow = true;
            PatternsPlayed[PatternsCount - 1] = next_mode ; //add new RandNum to end of list
            SetShow(next_mode);
          }
        }
        
        UpdateShowSettings();
               
        if (auto_cycle == false){Serial.print("Manual ");} else {Serial.print("Auto ");}
        Serial.print("Mode- Show:"); Serial.print(ws2812fx1.getMode());Serial.print(" - "); Serial.print(ws2812fx1.getModeName(ws2812fx1.getMode()));
        Serial.print(" & Speed is:"); Serial.println(ws2812fx1.getSpeed());
    
        
        
        auto_last_change = now;
      }
    }
  }
}

void SetShow(int next_mode){
  ws2812fx1.setNumSegments(1);
  ws2812fx1.setLength(OUT1_LED_COUNT);
  ws2812fx2.setNumSegments(1);
  ws2812fx2.setLength(OUT2_LED_COUNT);
            
  ws2812fx1.setMode(next_mode);
  ws2812fx2.setMode(next_mode);
}
int SlaveSpeed;
void UpdateShowSettings(){
  //ws2812fx.resetSegments();
  
  if(ws2812fx1.getMode() <= 9){
    ws2812fx1.setSpeed(3000);
    SlaveSpeed = 1000;
  }else if(ws2812fx1.getMode() == 10){
    ws2812fx1.setSpeed(600);
    SlaveSpeed = 300;
  }else if(ws2812fx1.getMode() <= 15){
    ws2812fx1.setSpeed(1500);
    SlaveSpeed = 1000;
  }else if(ws2812fx1.getMode() <= 17){
    ws2812fx1.setSpeed(12000);
    SlaveSpeed = 6000;
  }else if(ws2812fx1.getMode() == 18){
    ws2812fx1.setSpeed(1500);
    SlaveSpeed = 1000;
  }else if(ws2812fx1.getMode() <= 20){
    ws2812fx1.setSpeed(1200);
    SlaveSpeed = 700;
  }else if(ws2812fx1.getMode() <= 22){
    ws2812fx1.setSpeed(100);
    SlaveSpeed = 100;
  }else if(ws2812fx1.getMode() == 23){
    ws2812fx1.setSpeed(800);
    SlaveSpeed = 400;
  }else if(ws2812fx1.getMode() <= 25){
    ws2812fx1.setSpeed(50);
    SlaveSpeed = 100;
  }else if(ws2812fx1.getMode() <= 27){
    ws2812fx1.setSpeed(800);
    SlaveSpeed = 400;
  }else if(ws2812fx1.getMode() == 28){
    ws2812fx1.setSpeed(5000);
    SlaveSpeed = 2000;
  }else if(ws2812fx1.getMode() <= 33){
    ws2812fx1.setSpeed(2000);
    SlaveSpeed = 100;
  }else if(ws2812fx1.getMode() <= 35){
    ws2812fx1.setSpeed(50);
    SlaveSpeed = 1000;
  }else if(ws2812fx1.getMode() <= 39){
    ws2812fx1.setSpeed(1000);
    SlaveSpeed = 1000;
  }else if(ws2812fx1.getMode() <= 42){
    ws2812fx1.setSpeed(9000);
    SlaveSpeed = 16000;
  }else if(ws2812fx1.getMode() == 43){
    ws2812fx1.setSpeed(1000);
    SlaveSpeed = 1000;
  }else if(ws2812fx1.getMode() == 44){
    ws2812fx1.setSpeed(1200);
    SlaveSpeed = 600;
  }else if(ws2812fx1.getMode() <= 46){
    ws2812fx1.setSpeed(1000);
    SlaveSpeed = 1000;
  }else if(ws2812fx1.getMode() <= 49){
    ws2812fx1.setSpeed(4000);
    SlaveSpeed = 8000;
  }else if(ws2812fx1.getMode() <= 52){
    ws2812fx1.setSpeed(4000);
    SlaveSpeed = 8000;
  }else if(ws2812fx1.getMode() == 53){
    ws2812fx1.setSpeed(1000);
    SlaveSpeed = 1000;
  }else if(ws2812fx1.getMode() == 54){
    ws2812fx1.setSpeed(2000);
    SlaveSpeed = 5000;
  }else if(ws2812fx1.getMode() == 55){
    ws2812fx1.setSpeed(100);
    SlaveSpeed = 100;
  }else{
    ws2812fx1.setSpeed(1000);
    SlaveSpeed = 1000;
  }
  ws2812fx2.setSpeed(ws2812fx1.getSpeed());
  //SendShowInfo();
}
void SendShowInfo(){
  char msg[20];
  //uint8_t buffer[50] = "Online";
  //        Udp.write(buffer, 11);
  //        Udp.endPacket();
  Udp.beginPacket({255,255,255,255}, localUdpPort);
  sprintf(msg, "MODE=%d", ws2812fx1.getMode());
  Udp.print(msg);
  Udp.endPacket();
  
  delay(1);
  Udp.beginPacket({255,255,255,255}, localUdpPort);
  sprintf(msg, "SPEED=%d", SlaveSpeed);
  Udp.print(msg);
  Udp.endPacket();
}


/*
 * Connect to WiFi. If no connection is made within WIFI_TIMEOUT, ESP gets resettet.
 */
void wifi_setup() {
    //Serial.println();
  Serial.print("Connecting to ");
  Serial.println(WIFI_SSID);
  WiFi.setHostname("TeslaLED");
  if (WIFI_PASSWORD == ""){
    WiFi.begin(WIFI_SSID);
  }else{
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
  WiFi.mode(WIFI_STA);
  #ifdef STATIC_IP  
    WiFi.config(ip, gateway, subnet);
  #endif

  /*unsigned long connect_start = millis();
  while(WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");

    if(millis() - connect_start > WIFI_TIMEOUT) {
      Serial.println();
      Serial.print("Tried ");
      Serial.print(WIFI_TIMEOUT);
      Serial.print("ms. Resetting ESP now.");
      ESP.reset();
    }
  }
*/
  delay(500);
  if(WiFi.status() == WL_CONNECTED){
    Serial.println("");
    Serial.printf("WiFi connected to %s\n",WIFI_SSID);  
    Serial.printf("IP address: %s\n",WiFi.localIP());
    //Serial.println();
    //Serial.println();
  }else{
    Serial.println("WiFi NOT connected");
  }
  Serial.println(dnsServer.start(DNS_PORT, "*", WiFi.localIP()) ? "DNS - Ready!" : "DNS - Failed!");
  
}


void modes_setup() {
  modes = "";
  uint8_t num_modes = sizeof(myModes) > 0 ? sizeof(myModes) : ws2812fx1.getModeCount();
  for(uint8_t i=0; i < num_modes -1; i++) {
    uint8_t m = sizeof(myModes) > 0 ? myModes[i] : i;
    modes += "<li><a href='#' class='m' id='";
    modes += m;
    modes += "'>";
    modes += ws2812fx1.getModeName(m);
    modes += "</a></li>";
  }
}

/* #####################################################
#  Webserver Functions
##################################################### */
String responseHTML = ""                   
                      "<!DOCTYPE HTML><html lang='en-US'><head><meta charset='UTF-8'>"
                      "<meta http-equiv='refresh' content='0; url=http://10.10.10.10'>"
                      "<script type='text/javascript'>window.location.href = 'http://10.10.10.10'</script>"
                      "<title>Page Redirection</title></head><body>If you are not redirected automatically, follow this "
                      "<a href='http://10.10.10.10'>link to #PurpleModel3</a></body></html>";

void srv_handle_not_found() {
  server.send(200, "text/html", responseHTML);
  Serial.println("Web Page Not Found - Redirecting");
}

void srv_handle_index_html() {
  server.send_P(200,"text/html", index_html);
}

void srv_handle_main_js() {
  server.send_P(200,"application/javascript", main_js);
}

void srv_handle_modes() {
  server.send(200,"text/plain", modes);
}

void srv_handle_set() {
  for (uint8_t i=0; i < server.args(); i++){
    if(server.argName(i) == "c") {
      uint32_t tmp = (uint32_t) strtol(&server.arg(i).c_str()[0], NULL, 16);
      if(tmp >= 0x000000 && tmp <= 0xFFFFFF) {
        ws2812fx1.setColor(tmp);
        ws2812fx2.setColor(tmp);
        Serial.print("Color Changed to "); Serial.println(ws2812fx1.getColor(), HEX);
      }
    }

    if(server.argName(i) == "m") {
      uint8_t tmp = (uint8_t) strtol(&server.arg(i).c_str()[0], NULL, 10);
      auto_cycle = false;
      Serial.println("Auto Mode is: OFF");
      ws2812fx1.setNumSegments(1);
      ws2812fx1.setLength(OUT1_LED_COUNT);
      ws2812fx2.setNumSegments(1);
      ws2812fx2.setLength(OUT2_LED_COUNT);
      ws2812fx1.setMode(tmp % ws2812fx1.getModeCount());
      UpdateShowSettings();
      ws2812fx2.setMode(tmp % ws2812fx1.getModeCount());
      Serial.print("Show is "); Serial.println(ws2812fx1.getModeName(ws2812fx1.getMode()));
    }

    if(server.argName(i) == "b") {
      if(server.arg(i)[0] == '-') {
        ws2812fx1.setBrightness(ws2812fx1.getBrightness() * 0.8);
      } else if(server.arg(i)[0] == ' ') {
        ws2812fx1.setBrightness(min(max(ws2812fx1.getBrightness(), 5) * 1.2, 255));
      } else { // set brightness directly
        uint8_t tmp = (uint8_t) strtol(&server.arg(i).c_str()[0], NULL, 10);
        ws2812fx1.setBrightness(tmp);
      }
      ws2812fx2.setBrightness(ws2812fx1.getBrightness());
      Serial.print("brightness is "); Serial.println(ws2812fx1.getBrightness());
    }

    if(server.argName(i) == "s") {
      if(server.arg(i)[0] == '-') {
        ws2812fx1.setSpeed(max(ws2812fx1.getSpeed(), 5) * 1.2);
      } else {
        ws2812fx1.setSpeed(ws2812fx1.getSpeed() * 0.8);
      }
      ws2812fx2.setSpeed(ws2812fx1.getSpeed());
      Serial.print("speed is "); Serial.println(ws2812fx1.getSpeed());
    }

    if(server.argName(i) == "a") {
      if(server.arg(i)[0] == '-') {
        auto_cycle = false;
      } else {
        auto_cycle = true;
        auto_last_change = millis() + 120000;
        
      }
    }
  }
  server.send(200, "text/plain", "OK");
  //delay(10);
}
void ws2812fx1OUTPUT(void) {
  uint8_t *pixels = ws2812fx1.getPixels();
  // numBytes is one more then the size of the ws2812fx1's *pixels array.
  // the extra byte is used by the driver to insert the LED reset pulse at the end.
  uint16_t numBytes = ws2812fx1.getNumBytes() + 1;
  rmt_write_sample(RMT_CHANNEL_0, pixels, numBytes, false); // channel 0
}
void ws2812fx2OUTPUT(void) {
  uint8_t *pixels = ws2812fx2.getPixels();
  // numBytes is one more then the size of the ws2812fx1's *pixels array.
  // the extra byte is used by the driver to insert the LED reset pulse at the end.
  uint16_t numBytes = ws2812fx2.getNumBytes() + 1;
  rmt_write_sample(RMT_CHANNEL_1, pixels, numBytes, false); // channel 1
}
