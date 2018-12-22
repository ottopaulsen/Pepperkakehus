#include <Adafruit_NeoPixel.h>

#include <PubSubClient.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <EEPROM.h>

#include "secrets.h"
/*
 * Secrets are: 
 *   #define WIFIPASSWORD "secret"
 *   #define MQTTPASSWORD "secret"
 *   #define UPDATEOTAPASSWORD "secret"
 */

// NeoPixel
#define PIN 2
#define NUM_LEDS 8
Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUM_LEDS, PIN, NEO_GRB + NEO_KHZ800);
//int numLeds = 4; // Initial number of lit leds
//int r = 50, g = 50, b = 50; // Initial color settings
//int brightness = 50; // Initial brightness


// WiFi
const char* wifiSsid = "Xtreme";
const char* wifiPassword = WIFIPASSWORD;
WiFiClient  wifiClient;

// MQTT
const char* mqttServer = "10.0.0.15";
const char* mqttUser = "ottomq";
const char* mqttPassword = MQTTPASSWORD;
const long mqttPort = 1883;
const char* mqttClientId = "pepperkakehus-1";

PubSubClient mqttClient(wifiClient);

// MQTT Topics
#define MQTT_TOPIC "pepperkakehus/1"
const char* mqttTopicInSetR = MQTT_TOPIC "/r"; // Red
const char* mqttTopicInSetG = MQTT_TOPIC "/g"; // Green
const char* mqttTopicInSetB = MQTT_TOPIC "/b"; // Blue
const char* mqttTopicInSetN = MQTT_TOPIC "/n"; // Number of leds to lit
const char* mqttTopicInSetL = MQTT_TOPIC "/l"; // Brightness
const char* mqttTopicLiveCounter = MQTT_TOPIC "/alive";

const char* mqttTopicOutSettingsR = MQTT_TOPIC "/settings/r";
const char* mqttTopicOutSettingsG = MQTT_TOPIC "/settings/g";
const char* mqttTopicOutSettingsB = MQTT_TOPIC "/settings/b";
const char* mqttTopicOutSettingsN = MQTT_TOPIC "/settings/n";
const char* mqttTopicOutSettingsL = MQTT_TOPIC "/settings/l";
const char* mqttTopicOutMsg = MQTT_TOPIC "/msg";


long liveCounter = 0;
long liveCounterInterval = 30000;
long lastLiveCounterTime = 0;


// OTA Update
// http://esp8266.github.io/Arduino/versions/2.0.0/doc/ota_updates/ota_updates.html
const char* host = "pepperkakehus-update";
const char* update_path = "/firmware";
const char* update_username = "admin";
const char* update_password = UPDATEOTAPASSWORD;
ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;

// EEPROM Settings
#define SETTINGS_VERSION 100
#define DEFAULT_R 90
#define DEFAULT_G 90
#define DEFAULT_B 0
#define DEFAULT_N 8
#define DEFAULT_L 75
struct Settings {
  int version; // Figure ot if data has been saved before
  int r;
  int g;
  int b;
  int numLeds;
  int brightness;
};
Settings settings;



void setup() {

  // Serial
  Serial.begin(115200);

  EEPROM.begin(256);
  readSettingsFromEEPROM();

  // WiFi
  connectWifi();

  // MQTT
  Serial.println("Connecting MQTT");
  mqttClient.setServer(mqttServer, mqttPort);
  mqttClient.setCallback(mqttCallback);
  randomSeed(micros());

  // OTA Update
  Serial.println("Setting up OTA Update");
  MDNS.begin("pepperkakehus");
  httpUpdater.setup(&httpServer, update_path, update_username, update_password);
  httpServer.begin();
  MDNS.addService("http", "tcp", 80);
  Serial.printf("HTTPUpdateServer ready! Open http://%s.local%s in your browser and login with username '%s' and password '%s'\n", host, update_path, update_username, update_password);

  Serial.println("Strip begin");
  
  strip.begin();
  strip.show();

  lastLiveCounterTime = millis();

  Serial.println("Trying the first reconnect");
  reconnectMqtt();
  Serial.println("Trying the first publish");
  mqttClient.publish("pepperkakehus/1/alive", String(liveCounter).c_str());
  Serial.println("Setup done");

  writeMqttSettings();

  
}

void loop() {

  // OTA Update
  httpServer.handleClient();

  // NeoPixel
  for(int i = 0; i < settings.numLeds; i++){
    strip.setPixelColor(i, map(settings.r, 0, 100, 0, 255), map(settings.g, 0, 100, 0, 255), map(settings.b, 0, 100, 0, 255));
  }
  for(int i = settings.numLeds; i < NUM_LEDS; i++){
    strip.setPixelColor(i, 0, 0, 0);
  }

  strip.setBrightness(map(settings.brightness, 0, 100, 0, 255));
  strip.show();

  if(WiFi.status() != WL_CONNECTED) connectWifi();

  // MQTT
  reconnectMqtt();
  if(millis() >= lastLiveCounterTime + liveCounterInterval){
    liveCounter++;
    Serial.print("Sending live counter: ");
    Serial.print(liveCounter);
    Serial.println("");
    mqttClient.publish(mqttTopicLiveCounter, String(liveCounter).c_str());
    lastLiveCounterTime = millis();
  }

  mqttClient.loop();
  
}

void connectWifi() {
  // WiFi
  delay(10);
  Serial.print("Connecting wifi");
  WiFi.begin(wifiSsid, wifiPassword);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());  
}


void reconnectMqtt() {
  /* 
   * This code is copied from an example.
   * Not sure it is the optimal usage of the client id.
   */

  // Loop until we're reconnected
  if (!mqttClient.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    //String clientId = "pepper-";
    //clientId += String(random(0xffff), HEX);
    // Attempt to connect
    //if (mqttClient.connect(clientId.c_str(), mqttUser, mqttPassword)) {
    if (mqttClient.connect(mqttClientId, mqttUser, mqttPassword)) {
      Serial.println("connected");

      // ... and resubscribe
      char* subTopic = MQTT_TOPIC "/#";
      Serial.print("Subscribing to topic: ");
      Serial.println(subTopic);
      if(mqttClient.subscribe(subTopic, 1)){
        Serial.println("Subscribe ok");
      } else {
        Serial.println("Subscribe failed");
      }
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" try again next time");
    }
  }
}

void mqttSend(const char* topic, const char* msg){
  reconnectMqtt();
  mqttClient.publish(topic, msg); 
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  /*
   * Callback for subscribed messages
   */
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println("");

  // Set status to turn on green led if ack is received
  if(strcmp(topic, mqttTopicInSetR) == 0) {
    settings.r = constrain(payloadToInt(payload, length, settings.r), 0, 100);
    writeSettingsToEEPROM();
  } else if(strcmp(topic, mqttTopicInSetG) == 0) {
    settings.g = constrain(payloadToInt(payload, length, settings.g), 0, 100);
    writeSettingsToEEPROM();
  } else if(strcmp(topic, mqttTopicInSetB) == 0) {
    settings.b = constrain(payloadToInt(payload, length, settings.b), 0, 100);
    writeSettingsToEEPROM();
  } else if(strcmp(topic, mqttTopicInSetN) == 0) {
    settings.numLeds = constrain(payloadToInt(payload, length, settings.numLeds), 0, NUM_LEDS);
    writeSettingsToEEPROM();
  } else if(strcmp(topic, mqttTopicInSetL) == 0) {
    settings.brightness = constrain(payloadToInt(payload, length, settings.brightness), 0, 100);
    writeSettingsToEEPROM();
  }
}


int payloadToInt(byte* payload, unsigned int length, int defaultValue){
  payload[length] = '\0';
  String s = String((char*)payload);
  int f = s.toInt();
  if (f == 0 && payload[0] != '0') return defaultValue;
  else return f;
}

void readSettingsFromEEPROM(){
  int eeAddress = 0;
  mqttSend(mqttTopicOutMsg, String("Reading EEPROM").c_str());
  EEPROM.get(eeAddress, settings);
  
  if(settings.version != SETTINGS_VERSION){
    mqttSend(mqttTopicOutMsg, String("EEPROM - Not found version").c_str());
    settings.version = SETTINGS_VERSION;
    settings.r = DEFAULT_R;
    settings.g = DEFAULT_G;
    settings.b = DEFAULT_B;
    settings.numLeds = DEFAULT_N;
    settings.brightness = DEFAULT_L;
    EEPROM.put(0, settings);
  }
}

void writeSettingsToEEPROM(){
  int eeAddress = 0;
  mqttSend(mqttTopicOutMsg, String("Writing EEPROM").c_str());
  EEPROM.put(eeAddress, settings);
  EEPROM.commit();
}

void writeMqttSettings(){
  reconnectMqtt();

  mqttClient.publish(mqttTopicOutSettingsR, String(settings.r).c_str());
  mqttClient.publish(mqttTopicOutSettingsG, String(settings.g).c_str());
  mqttClient.publish(mqttTopicOutSettingsB, String(settings.b).c_str());
  mqttClient.publish(mqttTopicOutSettingsN, String(settings.numLeds).c_str());
  mqttClient.publish(mqttTopicOutSettingsL, String(settings.brightness).c_str());
}




