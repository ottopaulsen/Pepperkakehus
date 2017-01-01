#include <Adafruit_NeoPixel.h>

#include <PubSubClient.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>

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
int numLeds = 4; // Initial number of lit leds
int r = 50, g = 50, b = 50; // Initial color settings
int brightness = 50; // Initial brightness


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


void setup() {

  // Serial
  Serial.begin(115200);

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
  
}

void loop() {

  // OTA Update
  httpServer.handleClient();

  // NeoPixel
  for(int i = 0; i < numLeds; i++){
    strip.setPixelColor(i, map(r, 0, 100, 0, 255), map(g, 0, 100, 0, 255), map(b, 0, 100, 0, 255));
  }
  for(int i = numLeds; i < NUM_LEDS; i++){
    strip.setPixelColor(i, 0, 0, 0);
  }

  strip.setBrightness(map(brightness, 0, 100, 0, 255));
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
    r = constrain(payloadToInt(payload, length, r), 0, 100);
  } else if(strcmp(topic, mqttTopicInSetG) == 0) {
    g = constrain(payloadToInt(payload, length, g), 0, 100);
  } else if(strcmp(topic, mqttTopicInSetB) == 0) {
    b = constrain(payloadToInt(payload, length, b), 0, 100);
  } else if(strcmp(topic, mqttTopicInSetN) == 0) {
    numLeds = constrain(payloadToInt(payload, length, numLeds), 0, NUM_LEDS);
  } else if(strcmp(topic, mqttTopicInSetL) == 0) {
    brightness = constrain(payloadToInt(payload, length, brightness), 0, 100);
  }
}


int payloadToInt(byte* payload, unsigned int length, int defaultValue){
  payload[length] = '\0';
  String s = String((char*)payload);
  int f = s.toInt();
  if (f == 0 && payload[0] != '0') return defaultValue;
  else return f;
}



