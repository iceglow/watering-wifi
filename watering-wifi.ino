#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WiFiClient.h>
#include <Hash.h>
#include <FS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>

#define SSID_FILE "/etc/ssid"
#define PASSWORD_FILE "/etc/pass"
#define HOSTNAME_FILE "/etc/hostname"
#define MQTTURI_FILE "/etc/mqtt"

#define CONNECT_TIMEOUT_SECS 30

#define AP_SSID "watering"

#define MAX_BUFFER 8192

#define MQTT_MAX_PACKET_SIZE 512;

typedef struct {
  char* pinName;
  int pinNumber;
  int state;
} pinsMap;

const pinsMap pins[] {
    {"D0", 16, LOW},
    {"D1", 5, LOW},
    {"D2", 4, LOW},
    {"D3", 0, LOW},
    {"D4", 2, LOW},
    {"D5", 14, LOW},
    {"D6", 12, LOW},
    {"D7", 13, LOW},
    {"D8", 15, LOW},
};

pinsMap AREA1 = pins[0], AREA2 = pins[1], AREA3 = pins[2], AREA4 = pins[5], AREA5 = pins[6], AREA6 = pins[7];
pinsMap AREAS[] = {AREA1, AREA2, AREA3, AREA4, AREA5, AREA6};
const int AREAS_SIZE = 6;

String HOSTNAME = "watering-wifi";

WiFiClient client;
int bufferSize = 0;
uint8_t currentClient = 0;
uint8_t serialBuffer[8193];
ESP8266WebServer server (80);
ESP8266WebServer updateServer(82);
ESP8266HTTPUpdateServer httpUpdater;
PubSubClient mqttClient(client);

void mqttEventSubscribe(char* topic, byte* payload, unsigned int length) {
  StaticJsonDocument<200> doc;
  deserializeJson(doc, payload);
  const char* target = doc["target"];
  if ((HOSTNAME.equals(target) || String("*").equals(target)) && doc["command"] != NULL) {
    const int area = doc["command"]["area"].as<int>();
    const boolean stat = doc["command"]["status"].as<boolean>();

    Serial.println("Command: area " + String(area) + " " + (stat ? "ON":"OFF"));
    if (area < AREAS_SIZE) {
      if (stat) {
        AREAS[area].state = LOW;
      } else {
        AREAS[area].state = HIGH;
      }
      digitalWrite(AREAS[area].pinNumber, AREAS[area].state);
    }
  }

  sendStatus();
}

void sendStatus() {
  DynamicJsonDocument doc(1024);
  JsonObject jsonHeader = doc.createNestedObject("header");
  jsonHeader["ts"] = String(millis());
  jsonHeader["type"] = "watering";
  jsonHeader["title"] = HOSTNAME;
  JsonObject jsonPayload = doc.createNestedObject("payload");

  for (int i = 0; i < AREAS_SIZE; i++) {
    jsonPayload["valve"] = i+1;
    jsonPayload["state"] = AREAS[i].state == 1;
    
    String buffer;
    serializeJson(doc, buffer);
    mqttClient.publish("watering/out", (char *) buffer.c_str());
  }
}

void serverEvent() {
  // just a very simple websocket terminal, feel free to use a custom one
  server.send(200, "text/html", "<!DOCTYPE html><meta charset='utf-8' /><style>p{white-space:pre;word-wrap:break-word;font-family:monospace;}</style><title>Neato Console</title><script language='javascript' type='text/javascript'>var b='ws://'+location.hostname+':81/',c,d,e;function g(){d=new WebSocket(b);d.onopen=function(){h('[connected]')};d.onclose=function(){h('[disconnected]')};d.onmessage=function(a){h('<span style=\"color: blue;\">[response] '+a.data+'</span>')};d.onerror=function(a){h('<span style=\"color: red;\">[error] </span> '+a.data)}}\nfunction k(a){if(13==a.keyCode){a=e.value;if('/disconnect'==a)d.close();else if('/clear'==a)for(;c.firstChild;)c.removeChild(c.firstChild);else''!=a&&(h('[sent] '+a),d.send(a));e.value='';e.focus()}}function h(a){var f=document.createElement('p');f.innerHTML=a;c.appendChild(f);window.scrollTo(0,document.body.scrollHeight)}\nwindow.addEventListener('load',function(){c=document.getElementById('c');e=document.getElementById('i');g();document.getElementById('i').addEventListener('keyup',k,!1);e.focus()},!1);</script><h2>Neato Console</h2><div id='c'></div><input type='text' id='i' style=\"width:100%;font-family:monospace;\">\n");
}

void setupEvent() {
  char ssid[256];
  File ssid_file = SPIFFS.open(SSID_FILE, "r");
  if(!ssid_file) {
    strcpy(ssid, "XXX");
  }
  else {
    ssid_file.readString().toCharArray(ssid, 256);
    ssid_file.close();
  }

  char passwd[256];
  File passwd_file = SPIFFS.open(PASSWORD_FILE, "r");
  if(!passwd_file) {
    strcpy(passwd, "XXX");
  }
  else {
    passwd_file.readString().toCharArray(passwd, 256);
    passwd_file.close();
  }

  String host;
  File host_file = SPIFFS.open(HOSTNAME_FILE, "r");
  if(!host_file) {
    host = HOSTNAME;
  }
  else {
    host = host_file.readString();
    host_file.close();
  }

  char mqtturi[256];
  File mqtturi_file = SPIFFS.open(MQTTURI_FILE, "r");
  if(!mqtturi_file) {
    strcpy(mqtturi, "[username][:password]@host.domain[:port]");
  }
  else {
    mqtturi_file.readString().toCharArray(mqtturi, 256);
    mqtturi_file.close();
  }

  server.send(200, "text/html", String() + 
  "<!DOCTYPE html><html> <body>" +
  "<form action=\"\" method=\"post\" style=\"display: inline;\">" +
  "Access Point SSID:<br />" +
  "<input type=\"text\" name=\"ssid\" value=\"" + ssid + "\"> <br />" +
  "WPA2 Password:<br />" +
  "<input type=\"text\" name=\"password\" value=\"" + passwd + "\"> <br />" +
  "<br />" +
  "Hostname:<br />" +
  "<input type=\"text\" name=\"host\" value=\"" + host + "\"> <br />" +
  "<br />" +
  "MQTT server URI (mqtt[s]://[username][:password]@host.domain[:port]):<br />" +
  "mqtt[s]://<input type=\"text\" name=\"mqtturi\" value=\"" + mqtturi + "\"> <br />" +
  "<br />" +
  "<input type=\"submit\" value=\"Submit\"> </form>" +
  "<form action=\"http://" + HOSTNAME + ".local/reboot\" style=\"display: inline;\">" +
  "<input type=\"submit\" value=\"Reboot\" />" +
  "</form>" +
  "<p>Enter the details for your access point. After you submit, the controller will reboot to apply the settings.</p>" +
  "<p><a href=\"http://" + HOSTNAME + ".local:82/update\">Update Firmware</a></p>" +
  "</body></html>\n");
}

void saveEvent() {
  String user_ssid = server.arg("ssid");
  String user_password = server.arg("password");
  String host = server.arg("host");
  String mqtturi = server.arg("mqtturi");

  SPIFFS.format();

  if(user_ssid != "" && user_password != "" && host != "" && mqtturi != "") {
    SPIFFS.begin();
    File ssid_file = SPIFFS.open(SSID_FILE, "w");
    if (!ssid_file) {
      server.send(200, "text/html", "<!DOCTYPE html><html> <body> Setting Access Point SSID failed!</body> </html>");
      return;
    }
    ssid_file.print(user_ssid);
    ssid_file.close();

    File passwd_file = SPIFFS.open(PASSWORD_FILE, "w");
    if (!passwd_file) {
      server.send(200, "text/html", "<!DOCTYPE html><html> <body> Setting Access Point password failed!</body> </html>");
      return;
    }
    passwd_file.print(user_password);
    passwd_file.close();

    File hostname_file = SPIFFS.open(HOSTNAME_FILE, "w");
    if (!hostname_file) {
      server.send(200, "text/html", "<!DOCTYPE html><html> <body> Setting hostname failed!</body> </html>");
      return;
    }
    hostname_file.print(host);
    hostname_file.close();

    File mqtturi_file = SPIFFS.open(MQTTURI_FILE, "w");
    if (!mqtturi_file) {
      server.send(200, "text/html", "<!DOCTYPE html><html> <body> Setting MQTT server URI failed!</body> </html>");
      return;
    }
    mqtturi_file.print(mqtturi);
    mqtturi_file.close();

    server.send(200, "text/html", String() + 
    "<!DOCTYPE html><html> <body>" +
    "Setup was successful! <br />" +
    "<br />SSID was set to \"" + user_ssid + "\" with the password \"" + user_password + "\". <br />" +
    "<br />Hostname was set to \"" + host + "\". <br />" +
    "<br />MQTT server URI was set to \"" + mqtturi + "\". <br />" +
    "<br />The controller will now reboot. Please re-connect to your Wi-Fi network.<br />" +
    "If the SSID or password was incorrect, the controller will return to Access Point mode." +
    "</body> </html>");

    delay(1000);
    ESP.reset();
  }
}

void rebootEvent() {
  server.send(200, "text/html", String() + 
  "<!DOCTYPE html><html> <body>" +
  "The controller will now reboot.<br />" +
  "If the SSID or password is set but is incorrect, the controller will return to Access Point mode." +
  "</body> </html>");
  ESP.reset();
}

void setup(void) {
  Serial.begin(115200);
  Serial.println("Setup");

  // Set area pins in OUTPUT mode and turn them off
  for (int i = 0; i < AREAS_SIZE; i++) {
    digitalWrite(AREAS[i].pinNumber, HIGH);
    pinMode(AREAS[i].pinNumber, OUTPUT);
  }

  //try to mount the filesystem. if that fails, format the filesystem and try again.
  if(!SPIFFS.begin()) {
    SPIFFS.format();
    SPIFFS.begin();
  }

  if(SPIFFS.exists(SSID_FILE) && SPIFFS.exists(PASSWORD_FILE)) {
    File ssid_file = SPIFFS.open(SSID_FILE, "r");
    char ssid[256];
    ssid_file.readString().toCharArray(ssid, 256);
    ssid_file.close();
    File passwd_file = SPIFFS.open(PASSWORD_FILE, "r");
    char passwd[256];
    passwd_file.readString().toCharArray(passwd, 256);
    passwd_file.close();

    if(SPIFFS.exists(HOSTNAME_FILE)) {
      File hostname_file = SPIFFS.open(HOSTNAME_FILE, "r");
      HOSTNAME = hostname_file.readString();
      hostname_file.close();
    }

    // attempt station connection
    WiFi.disconnect();
    WiFi.mode(WIFI_STA);
    WiFi.hostname(HOSTNAME);
    WiFi.begin(ssid, passwd);
    for(int i = 0; i < CONNECT_TIMEOUT_SECS * 20 && WiFi.status() != WL_CONNECTED; i++) {
      delay(50);
    }
  }

  //start AP mode if either the AP / password do not exist, or cannot be connected to within CONNECT_TIMEOUT_SECS seconds.
  if(WiFi.status() != WL_CONNECTED) {
    WiFi.disconnect();
    WiFi.mode(WIFI_AP);
    if(! WiFi.softAP(AP_SSID)) {
      ESP.reset(); //reset because there's no good reason for setting up an AP to fail
    }
  }

  // start webserver
  server.on("/console", serverEvent);
  server.on("/", HTTP_POST, saveEvent);
  server.on("/", HTTP_GET, setupEvent);
  server.on("/reboot", HTTP_GET, rebootEvent);
  server.onNotFound(serverEvent);

  if (!MDNS.begin(HOSTNAME)) {
    while (1) {
      delay(1000);
    }
  }

  // Start TCP (HTTP) server
  server.begin();

  //OTA update hooks
  ArduinoOTA.onStart([]() {
    SPIFFS.end();
    //TODO: send MQTT message: "ESP-12x: OTA Update Starting\n"
  });

  ArduinoOTA.onEnd([]() {
    SPIFFS.begin();
    //TODO: send MQTT message: "ESP-12x: OTA Update Complete\n"
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    //TODO: send MQTT message: "ESP-12x: OTA Progress: %u%%\r", (progress / (total / 100)
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("ESP-12x: OTA Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("ESP-12x: OTA Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("ESP-12x: OTA Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("ESP-12x: OTA Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("ESP-12x: OTA End Failed");
  });

  ArduinoOTA.begin();
  httpUpdater.setup(&updateServer);
  updateServer.begin();

  // Add service to MDNS-SD
  MDNS.addService("http", "tcp", 80);
  MDNS.addService("ws", "tcp", 81);
  MDNS.addService("http", "tcp", 82);

  if(SPIFFS.exists(MQTTURI_FILE)) {
    File mqtturi_file = SPIFFS.open(MQTTURI_FILE, "r");
    String mqtturi = mqtturi_file.readString();
    mqtturi_file.close();

    int pos_host = mqtturi.indexOf("@");
    int pos_port = mqtturi.indexOf(":", pos_host == -1 ? 0 : pos_host);

    String mqtt_user = "";
    String mqtt_pass = "";

    if (pos_host >= 0) { // User & pass used
      int pos_pass = mqtturi.indexOf(":");

      if (pos_pass > pos_host) { // No pass, we found port
        pos_pass = -1;
      }
      
      mqtt_user = mqtturi.substring(0, pos_pass == -1 ? pos_host : pos_pass);
      mqtt_pass = pos_pass == -1 ? "" : mqtturi.substring(pos_pass+1, pos_host);
    } else { // No user or pass
      pos_host = 0; // Host at beginning
    }

    String mqtt_host = mqtturi.substring(pos_host, pos_port == -1 ? mqtturi.length()-1 : pos_port);
    int mqtt_port = pos_port == -1 ? 1883 : mqtturi.substring(pos_port+1).toInt();
  
    mqttClient.setServer((char *) mqtt_host.c_str(), mqtt_port);
    mqttClient.setCallback(mqttEventSubscribe);
    
    for (int tries = 0; !mqttClient.connected() && tries < 5; tries++) {
      Serial.println("Connecting to MQTT...");
   
      if (mqttClient.connect((char *) HOSTNAME.c_str(), (char *) mqtt_user.c_str(), (char *) mqtt_pass.c_str())) {
   
        Serial.println("connected");
   
      } else {
   
        Serial.print("failed with state ");
        Serial.print(mqttClient.state());
        delay(2000);
   
      }
    }
  
    mqttClient.subscribe("watering/in");
  }
}

void loop(void) {
  mqttClient.loop();

  server.handleClient();

  ArduinoOTA.handle();

  updateServer.handleClient();
  MDNS.update();
}
