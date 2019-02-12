
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include "WS2812FX.h"
#include <Adafruit_NeoPixel.h>

extern const char index_html[];
extern const char main_js[];

#define WIFI_SSID "TeslaLED"
#define WIFI_PASSWORD "NateMcComb"



IPAddress local_IP(10,10,10,10);
IPAddress gateway(10,10,10,1);
IPAddress subnet(255,255,255,0);

// QUICKFIX...See https://github.com/esp8266/Arduino/issues/263
#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))

#define LED_PIN D1                       // 0 = GPIO0, 2=GPIO2
#define LED_COUNT 200 //151

//#define WIFI_TIMEOUT 30000              // checks WiFi every ...ms. Reset after this time, if WiFi cannot reconnect.
#define HTTP_PORT 80

#define DEFAULT_COLOR 0xFF00FF
#define DEFAULT_BRIGHTNESS 255
#define DEFAULT_SPEED 3000
//#define DEFAULT_MODE FX_MODE_STATIC
#define DEFAULT_MODE 0


unsigned long auto_last_change = 0;
unsigned long last_wifi_check_time = 0;
String modes = "";
uint8_t myModes[] = {}; // *** optionally create a custom list of effect/mode numbers
boolean auto_cycle = true;
boolean auto_cycle_state = false; // used to detect going in and out of auto

int BlueStat_Blink = 1100;  // ms
unsigned long last_BlueStat_Blink = 0;
const short int BlueStat_LED = 2; //GPIO2
int WebConnections = 0;

WS2812FX ws2812fx = WS2812FX(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
ESP8266WebServer server(HTTP_PORT);

void setup(){
  pinMode(BlueStat_LED, OUTPUT); // Initialize the BlueStat_LED pin as an output
  Serial.begin(115200);
  Serial.println();
  Serial.println();
  Serial.println("Starting...");

  modes.reserve(5000);
  modes_setup();

  Serial.println("WS2812FX setup");
  ws2812fx.init();
  ws2812fx.setMode(DEFAULT_MODE);
  ws2812fx.setColor(DEFAULT_COLOR);
  ws2812fx.setSpeed(DEFAULT_SPEED);
  ws2812fx.setBrightness(DEFAULT_BRIGHTNESS);
  ws2812fx.start();

  Serial.println("Wifi setup");
  wifi_setup();
 
  Serial.println("HTTP server setup");
  server.on("/", srv_handle_index_html);
  server.on("/main.js", srv_handle_main_js);
  server.on("/modes", srv_handle_modes);
  server.on("/set", srv_handle_set);
  server.onNotFound(srv_handle_not_found);
  server.begin();
  Serial.println("HTTP server started.");

  Serial.println("ready!");
}


void loop() {
  unsigned long now = millis();
  if (auto_cycle_state != auto_cycle){
    if (auto_cycle == false){Serial.println("Manual Mode");} else {Serial.println("Auto Mode");}
    auto_cycle_state = auto_cycle;
  }

  if (now - BlueStat_Blink > last_BlueStat_Blink){ 
    digitalWrite(BlueStat_LED, !digitalRead(BlueStat_LED));
    last_BlueStat_Blink = now;
  }
  
  server.handleClient();
  delay(1);
  ws2812fx.service();
            
  //if(now - last_wifi_check_time > WIFI_TIMEOUT) {
  if(WebConnections != WiFi.softAPgetStationNum()){
    Serial.print("Checking WiFi connections... ");
    WebConnections = WiFi.softAPgetStationNum();
    Serial.printf("Stations connected to soft-AP = %d\n", WebConnections);
    if(WebConnections == 0){
      BlueStat_Blink = 800;
      WiFi.disconnect();
      wifi_setup();
    } else {
      BlueStat_Blink = 50;
    }
    last_wifi_check_time = now;
  }
           
  if(auto_cycle && (now - auto_last_change > 90000)) { // cycle effect mode every 30 seconds
    //uint8_t next_mode = (ws2812fx.getMode() + 1 + random(0,15) ) % ws2812fx.getModeCount(); //
    uint8_t next_mode = (random(0,59) ) % ws2812fx.getModeCount(); // 
    if(next_mode > 59){ // disable custom
      next_mode = 0;
    }
    if(sizeof(myModes) > 0) { // if custom list of modes exists
      for(uint8_t i=0; i < sizeof(myModes); i++) {
        if(myModes[i] == ws2812fx.getMode()) {
          next_mode = ((i + 1) < sizeof(myModes) - 4) ? myModes[i + 1] : myModes[0];
          break;
        }
      }
    }
    ws2812fx.setMode(next_mode);
    //ws2812fx.resetSegments();
    ws2812fx.setNumSegments(1);
    ws2812fx.setLength(LED_COUNT);
    if(ws2812fx.getMode() <= 9){
      ws2812fx.setSpeed(1000);
    }else if(ws2812fx.getMode() == 10){
      ws2812fx.setSpeed(300);
    }else if(ws2812fx.getMode() <= 15){
      ws2812fx.setSpeed(1000);
    }else if(ws2812fx.getMode() <= 17){
      ws2812fx.setSpeed(6000);
    }else if(ws2812fx.getMode() == 18){
      ws2812fx.setSpeed(1000);
    }else if(ws2812fx.getMode() <= 20){
      ws2812fx.setSpeed(700);
    }else if(ws2812fx.getMode() <= 22){
      ws2812fx.setSpeed(100);
    }else if(ws2812fx.getMode() == 23){
      ws2812fx.setSpeed(400);
    }else if(ws2812fx.getMode() <= 25){
      ws2812fx.setSpeed(100);
    }else if(ws2812fx.getMode() <= 27){
      ws2812fx.setSpeed(400);
    }else if(ws2812fx.getMode() == 28){
      ws2812fx.setSpeed(2000);
    }else if(ws2812fx.getMode() <= 33){
      ws2812fx.setSpeed(100);
    }else if(ws2812fx.getMode() <= 35){
      ws2812fx.setSpeed(50);
    }else if(ws2812fx.getMode() <= 39){
      ws2812fx.setSpeed(1000);
    }else if(ws2812fx.getMode() <= 42){
      ws2812fx.setSpeed(6000);
    }else if(ws2812fx.getMode() == 43){
      ws2812fx.setSpeed(1000);
    }else if(ws2812fx.getMode() == 44){
      ws2812fx.setSpeed(600);
    }else if(ws2812fx.getMode() <= 46){
      ws2812fx.setSpeed(1000);
    }else if(ws2812fx.getMode() <= 49){
      ws2812fx.setSpeed(8000);
    }else if(ws2812fx.getMode() <= 52){
      ws2812fx.setSpeed(8000);
    }else if(ws2812fx.getMode() == 53){
      ws2812fx.setSpeed(1000);
    }else if(ws2812fx.getMode() == 54){
      ws2812fx.setSpeed(5000);
    }else if(ws2812fx.getMode() == 55){
      ws2812fx.setSpeed(50);
    }else{
      ws2812fx.setSpeed(1000);
    }
    //ws2812fx.setSpeed(max(ws2812fx.getSpeed(), 5) * 1.2);

    
    if (auto_cycle == false){Serial.print("Manual ");} else {Serial.print("Auto ");}
    Serial.print("mode is:"); Serial.print(ws2812fx.getMode());Serial.print(" - "); Serial.print(ws2812fx.getModeName(ws2812fx.getMode()));
    Serial.print(" & speed is:"); Serial.println(ws2812fx.getSpeed());
    auto_last_change = now;
  }
}



/*
 * Connect to WiFi. If no connection is made within WIFI_TIMEOUT, ESP gets resettet.
 */
void wifi_setup() {
  Serial.println();
  Serial.print("Configuring AP: ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_AP);
  WiFi.persistent(false); //new
  //WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  //#ifdef STATIC_IP  
  //  WiFi.softAPConfig(ip, gateway, subnet);
  //#endif
  Serial.print("Setting soft-AP configuration ... ");
  Serial.println(WiFi.softAPConfig(local_IP, gateway, subnet) ? "Ready" : "Failed!");

  Serial.print("Setting soft-AP ... ");
  //Serial.println(WiFi.softAP(WIFI_SSID) ? "Ready" : "Failed!"); //WiFi.softAP(ssid.c_str(), password.c_str(), wifi_channel, hidden);
  Serial.println(WiFi.softAP(WIFI_SSID, WIFI_PASSWORD) ? "Ready" : "Failed!");

  Serial.print("Soft-AP IP address = ");
  Serial.println(WiFi.softAPIP());

  
  //WiFi.softAPConfig(local_IP, gateway, subnet);
  //WiFi.softAP(WIFI_SSID, WIFI_PASSWORD);

  IPAddress myIP = WiFi.softAPIP();
  Serial.println("");
  Serial.println("WiFi AP Online");  
  Serial.print("AP IP address:");
  Serial.println(myIP);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.println();
}


/*
 * Build <li> string for all modes.
 */
void modes_setup() {
  modes = "";
  uint8_t num_modes = sizeof(myModes) > 0 ? sizeof(myModes) : ws2812fx.getModeCount();
  for(uint8_t i=0; i < num_modes; i++) {
    uint8_t m = sizeof(myModes) > 0 ? myModes[i] : i;
    modes += "<li><a href='#' class='m' id='";
    modes += m;
    modes += "'>";
    modes += ws2812fx.getModeName(m);
    modes += "</a></li>";
  }
}

/* #####################################################
#  Webserver Functions
##################################################### */

void srv_handle_not_found() {
  server.send(404, "text/plain", "File Not Found");
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
      uint32_t tmp = (uint32_t) strtol(&server.arg(i)[0], NULL, 16);
      if(tmp >= 0x000000 && tmp <= 0xFFFFFF) {
        ws2812fx.setColor(tmp);
      }
    }

    if(server.argName(i) == "m") {
      uint8_t tmp = (uint8_t) strtol(&server.arg(i)[0], NULL, 10);
      auto_cycle = false;
      ws2812fx.setMode(tmp % ws2812fx.getModeCount());
      Serial.print("mode is "); Serial.println(ws2812fx.getModeName(ws2812fx.getMode()));
    }

    if(server.argName(i) == "b") {
      if(server.arg(i)[0] == '-') {
        ws2812fx.setBrightness(ws2812fx.getBrightness() * 0.8);
      } else if(server.arg(i)[0] == ' ') {
        ws2812fx.setBrightness(min(max(ws2812fx.getBrightness(), 5) * 1.2, 255));
      } else { // set brightness directly
        uint8_t tmp = (uint8_t) strtol(&server.arg(i)[0], NULL, 10);
        ws2812fx.setBrightness(tmp);
      }
      Serial.print("brightness is "); Serial.println(ws2812fx.getBrightness());
    }

    if(server.argName(i) == "s") {
      if(server.arg(i)[0] == '-') {
        ws2812fx.setSpeed(max(ws2812fx.getSpeed(), 5) * 1.2);
      } else {
        ws2812fx.setSpeed(ws2812fx.getSpeed() * 0.8);
      }
      Serial.print("speed is "); Serial.println(ws2812fx.getSpeed());
    }

    if(server.argName(i) == "a") {
      if(server.arg(i)[0] == '-') {
        auto_cycle = false;
      } else {
        auto_cycle = true;
        auto_last_change = 0;
      }
    }
  }
  server.send(200, "text/plain", "OK");
}
