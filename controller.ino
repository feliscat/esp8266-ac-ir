#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <WebSerial.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <ir_Gree.h>
#include <PubSubClient.h>

#ifndef STASSID
#define STASSID "ssid"
#define STAPSK  "password"
#define MQTTSERVER "mqttip"
#endif

const uint16_t kIrLed = 4;  // ESP8266 GPIO pin to use.
IRGreeAC ac(kIrLed);  // Set the GPIO to be used for sending messages.

WiFiClient espClient;
PubSubClient client(espClient);

AsyncWebServer server(80); // Webserver for WebSerial

const char* ssid = STASSID;
const char* password = STAPSK;
const char* mqtt_server = MQTTSERVER;
float roomTemp = 0;
float acTemp = 0;
int loopcnt = 0;
bool acOn = false;
const char* mqttInit = "{\"name\":\"AC Controller\", \"unique_id\":\"livingroomaccontroller\", \"temp_cmd_t\":\"homeassistant/climate/ac-controller/set\", \"curr_temp_t\":\"homeassistant/climate/ac-controller/temp\",\"min_temp\":\"61\", \"max_temp\":\"90\",\"temp_step\":\"1\",\"modes\":[\"off\",\"cool\"],\"mode_stat_t\":\"homeassistant/climate/ac-controller/state\",\"mode_stat_tpl\":\"{{ value_json.mode }}\"}";

void callback(char* topic, byte* payload, unsigned int length) {
  WebSerial.print("Message arrived [");
  WebSerial.print(topic);
  WebSerial.print("] ");
  payload[length] = '\0'; // Make payload a string by NULL terminating it.

  if(topic[4]=='-') { // hackjob If the 5th char is a - then we're getting a message about the room temp otherwise we're getting an instruction from HA
    roomTemp = atof((char *)payload); // oh right I hate C... crazy conversion methods 
    WebSerial.print(roomTemp);
  } else {
    acTemp = atof((char *)payload);
    WebSerial.print(acTemp);
  }
  
  WebSerial.println();
}


void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    WebSerial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP8266Client-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str())) {
      WebSerial.println("connected");
      client.subscribe("home-assistant/livingroom/sensor/temperature/state");
      client.subscribe("homeassistant/climate/ac-controller/set/#");
    } else {
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void setup() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    delay(5000);
    ESP.restart(); // If we fail to connect to wifi, reboot after 5 seconds and try again
  }

  ArduinoOTA.setHostname("ACArduino.controller");
  ArduinoOTA.setPassword("password");
  ArduinoOTA.onStart([]() {
    WebSerial.println("Start updating");
  });
  ArduinoOTA.onEnd([]() {
    WebSerial.println("\nEnd Updating");
  });
  ArduinoOTA.onError([](ota_error_t error) {
    WebSerial.print("Error: ");
    WebSerial.println(error);
    if (error == OTA_AUTH_ERROR) {
      WebSerial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      WebSerial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      WebSerial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      WebSerial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      WebSerial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
  
  WebSerial.begin(&server);
  server.begin();
  
  ac.begin();
  ac.off();
  ac.setMode(kGreeCool);
  ac.setFan(kGreeFanMin);
  ac.setTemp(61, true); // 2nd parameter is true for farenheight

  // Connect to MQTT server
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  // Announce ourselves to HA
  hiHomeAssistant();
  
  WebSerial.println("Device booted");
}

void hiHomeAssistant() {
  WebSerial.print("Saying Hello to HA ");
  WebSerial.print(mqttInit);
  WebSerial.println();
  client.publish("homeassistant/climate/livingroom-ac-controller/config", mqttInit);
}

void loop() {
  delay(1000);
  
  ArduinoOTA.handle();

  // Reconnect to mqtt server if we've lost connection
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // Every 10 minutes, re-publish the fact that we exist to HA
  if(loopcnt++>600) {
    loopcnt = 0;
    hiHomeAssistant();
  }

  if(!acOn && (acTemp+1<roomTemp)) {
    acOn = true;
    ac.on();
    ac.send();
    WebSerial.println("Turning on AC");
    client.publish("homeassistant/climate/ac-controller/state", "{\"mode\":\"cool\"}");
  }
  if(acOn && (acTemp>roomTemp+2)) {
    acOn = false;
    ac.off();
    ac.send();
    client.publish("homeassistant/climate/ac-controller/state", "{\"mode\":\"off\"}");
    WebSerial.println("Turning off AC");
  }
  
}
