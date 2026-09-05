#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include "LittleFS.h"
#include <esp_wifi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <esp_now.h>
#include <SD.h>
#include <SPI.h>

// پین‌های SPI برای SD
#define SD_CS 13
#define SD_SCK 14
#define SD_MISO 2
#define SD_MOSI 15

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

// File paths in LittleFS
const char* ssidPath = "/ssid.txt";
const char* passPath = "/pass.txt";
const char* channelPath = "/chnow.txt";
const char* oldChannelPath = "/oldchnow.txt";
const char* ntpSyncPath = "/ntpsync.txt";

// File path for SD card
const char* csvPath = "/data.csv";

// Parameters for form
const char* PARAM_INPUT_1 = "ssid";
const char* PARAM_INPUT_2 = "pass";

// Variables
String ssid;
String pass;
int currentChannel = 1;
bool wasConnected = false;
bool manualDisconnect = false;
bool ntpSynced = false;
bool serverRestarted = false; // Track server restart
bool debugServer = true; // Enable server debug messages
unsigned long previousMillis = 0;
const long interval = 10000;
unsigned long lastReconnectAttempt = 0;
const long reconnectInterval = 5000;
unsigned long lastAPCheck = 0;
const long apCheckInterval = 3000;
unsigned long lastCycle = 0;
const long cycleInterval = 60000; // 1 minute
unsigned long lastNodeCheck = 0;
const long nodeCheckInterval = 120000; // 2 minutes

// NTP
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 12600, 60000);

// ESP-NOW
uint8_t rtcNodeMac[] = {0xAC, 0x0B, 0xFB, 0x18, 0x88, 0xD8};
uint8_t masterMac[] = {0x24, 0xD7, 0xEB, 0x10, 0x23, 0xCC};
bool rtcNodeConnected = false;
bool channelAckReceived = false;
bool ntpAckReceived = false;

typedef struct struct_message {
  uint8_t type; // 1: channel, 2: NTP, 3: time request, 4: sync done, 5: time response
  int channel;
  time_t epochTime;
  char datetime[25];
} struct_message;
struct_message outgoingMsg;
struct_message incomingMsg;

// Update peer channel
void updatePeerChannel() {
  esp_now_del_peer(rtcNodeMac);
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, rtcNodeMac, 6);
  peerInfo.channel = currentChannel;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to update RTC node peer channel");
  } else {
    Serial.println("RTC node peer channel updated to " + String(currentChannel));
  }
}

// Callback for ESP-NOW send status
void OnDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
  Serial.print("Last Packet Send Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
  if (status == ESP_NOW_SEND_SUCCESS && outgoingMsg.type == 1) {
    channelAckReceived = true;
  } else if (status == ESP_NOW_SEND_SUCCESS && outgoingMsg.type == 2) {
    ntpAckReceived = true;
  }
}

// Callback for ESP-NOW receive
void OnDataRecv(const esp_now_recv_info *recv_info, const uint8_t *incomingData, int len) {
  memcpy(&incomingMsg, incomingData, sizeof(incomingMsg));
  if (incomingMsg.type == 4) {
    Serial.println("Received sync done from RTC node");
    ntpSynced = true;
    writeFile(LittleFS, ntpSyncPath, "1");
    rtcNodeConnected = true;
  } else if (incomingMsg.type == 5) {
    Serial.println("Received time from RTC: " + String(incomingMsg.datetime));
    saveToSD(incomingMsg.datetime);
    rtcNodeConnected = true;
  }
}

// Initialize LittleFS
void initLittleFS() {
  if (!LittleFS.begin(true)) {
    Serial.println("Error mounting LittleFS");
    return;
  }
  Serial.println("LittleFS mounted");
  Serial.println("Files in LittleFS:");
  File root = LittleFS.open("/");
  File file = root.openNextFile();
  while (file) {
    Serial.println(file.name());
    file = root.openNextFile();
  }
  // Initialize ntpsync.txt if not exists
  if (!LittleFS.exists(ntpSyncPath)) {
    writeFile(LittleFS, ntpSyncPath, "0");
  }
}

// Initialize SD card
void initSD() {
  Serial.println("Initializing SPI for SD card...");
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  Serial.println("Initializing SD card...");
  if (!SD.begin(SD_CS)) {
    Serial.println("Card Mount Failed. Check SD card or connections.");
    return;
  }
  Serial.println("SD Card initialized successfully.");
  
  // Check SD card type
  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("No SD card attached.");
    return;
  }
  Serial.print("SD Card Type: ");
  if (cardType == CARD_MMC) Serial.println("MMC");
  else if (cardType == CARD_SD) Serial.println("SDSC");
  else if (cardType == CARD_SDHC) Serial.println("SDHC");
  else Serial.println("UNKNOWN");

  // Initialize CSV file
  if (SD.exists(csvPath)) {
    Serial.println("CSV file exists.");
  } else {
    Serial.println("CSV file does not exist. Creating new file...");
    File file = SD.open(csvPath, FILE_WRITE);
    if (file) {
      file.println("ID,datetime,mood,ta,ha,hs,li,rf,rh,cycle1,websync,cycle2,cycletotal");
      file.close();
      Serial.println("CSV file created with header.");
    } else {
      Serial.println("Error creating CSV file.");
    }
  }
}

// Read file from LittleFS
String readFile(fs::FS &fs, const char * path) {
  File file = fs.open(path);
  if (!file) {
    Serial.println("Failed to open " + String(path));
    return String();
  }
  String content = file.readStringUntil('\n');
  file.close();
  return content;
}

// Write file to LittleFS
void writeFile(fs::FS &fs, const char * path, const char * message) {
  File file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open " + String(path));
    return;
  }
  if (file.print(message)) {
    Serial.println("Wrote to " + String(path));
  } else {
    Serial.println("Write failed to " + String(path));
  }
  file.close();
}

// Get last ID from CSV
int getLastID() {
  File file = SD.open(csvPath, FILE_READ);
  if (!file) {
    Serial.println("Failed to open " + String(csvPath) + " for reading last ID");
    return 0;
  }
  int lastID = 0;
  // Skip header
  if (file.available()) {
    file.readStringUntil('\n');
  }
  // Read each line
  while (file.available()) {
    String line = file.readStringUntil('\n');
    if (line.length() > 0) {
      int commaIndex = line.indexOf(',');
      if (commaIndex != -1) {
        int id = line.substring(0, commaIndex).toInt();
        if (id > lastID) {
          lastID = id;
        }
      }
    }
  }
  file.close();
  Serial.println("Last ID read from CSV: " + String(lastID));
  return lastID;
}

// Save to SD card
void saveToSD(const char* datetime) {
  static int id = getLastID(); // Initialize with last ID from CSV
  File file = SD.open(csvPath, FILE_APPEND);
  if (file) {
    String data = String(++id) + "," + String(datetime) + ",na,na,na,na,na,na,na,0,pend,0,0";
    file.println(data);
    Serial.println("Saved to SD: " + data);
    file.close();
  } else {
    Serial.println("Failed to open " + String(csvPath));
  }
}

// Find channel for SSID
int getChannelForSSID(const String& targetSSID) {
  WiFi.mode(WIFI_STA);
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; ++i) {
    if (WiFi.SSID(i) == targetSSID) {
      int channel = WiFi.channel(i);
      Serial.printf("Found SSID %s on channel %d\n", targetSSID.c_str(), channel);
      return channel;
    }
  }
  Serial.println("SSID not found, can't get channel");
  return -1;
}

// Set WiFi channel
void setWiFiChannel(int channel) {
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
  Serial.printf("Set channel to %d\n", channel);
}

// Ensure AP is running
void ensureAPRunning() {
  if (WiFi.getMode() != WIFI_AP_STA) {
    WiFi.mode(WIFI_AP_STA);
    Serial.println("Re-enabled WIFI_AP_STA mode");
  }
  WiFi.softAP("ESP-WIFI-MANAGER", NULL, currentChannel);
  Serial.println("Started AP: ESP-WIFI-MANAGER at 192.168.4.1");
  Serial.println("AP status: " + String(WiFi.softAPgetStationNum()) + " clients connected");
}

// Send channel to RTC node
bool sendChannelToNode(int newChannel) {
  channelAckReceived = false;
  outgoingMsg.type = 1;
  outgoingMsg.channel = newChannel;
  esp_err_t result = esp_now_send(rtcNodeMac, (uint8_t *) &outgoingMsg, sizeof(outgoingMsg));
  if (result != ESP_OK) {
    Serial.println("Failed to send channel to RTC node: " + String(esp_err_to_name(result)));
    return false;
  }
  Serial.printf("Sending channel %d to RTC node\n", newChannel);
  
  unsigned long start = millis();
  while (!channelAckReceived && millis() - start < 5000) {
    delay(100);
  }
  if (channelAckReceived) {
    currentChannel = newChannel;
    setWiFiChannel(currentChannel);
    updatePeerChannel();
  }
  return channelAckReceived;
}

// Send NTP time to RTC node
bool sendNTPToNode() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected for NTP");
    return false;
  }
  if (!timeClient.update()) {
    Serial.println("Failed to update NTP");
    return false;
  }
  ntpAckReceived = false;
  outgoingMsg.type = 2;
  outgoingMsg.epochTime = timeClient.getEpochTime();
  esp_err_t result = esp_now_send(rtcNodeMac, (uint8_t *) &outgoingMsg, sizeof(outgoingMsg));
  if (result != ESP_OK) {
    Serial.println("Failed to send NTP to RTC node: " + String(esp_err_to_name(result)));
    return false;
  }
  Serial.println("Sent NTP time to RTC node");
  
  unsigned long start = millis();
  while (!ntpAckReceived && millis() - start < 60000) {
    delay(100);
  }
  return ntpAckReceived;
}

// Request time from RTC node
void requestTimeFromRTC() {
  outgoingMsg.type = 3;
  esp_err_t result = esp_now_send(rtcNodeMac, (uint8_t *) &outgoingMsg, sizeof(outgoingMsg));
  if (result != ESP_OK) {
    Serial.println("Failed to request time from RTC node: " + String(esp_err_to_name(result)));
    rtcNodeConnected = false;
  } else {
    Serial.println("Requested time from RTC node");
  }
}

// Initialize WiFi with channel management
bool initWiFi() {
  if (ssid == "" || pass == "") {
    Serial.println("Undefined SSID or Password");
    return false;
  }

  int oldChannel = WiFi.channel() > 0 ? WiFi.channel() : currentChannel;
  writeFile(LittleFS, oldChannelPath, String(oldChannel).c_str());

  int newChannel = getChannelForSSID(ssid);
  if (newChannel == -1) {
    Serial.println("SSID not found, can't get channel");
    return false;
  }

  if (newChannel != oldChannel) {
    if (!sendChannelToNode(newChannel)) {
      Serial.println("Failed to notify RTC node of new channel");
      return false;
    }
  } else {
    currentChannel = newChannel;
    setWiFiChannel(currentChannel);
    updatePeerChannel();
  }

  ensureAPRunning();
  WiFi.setAutoReconnect(false);
  WiFi.begin(ssid.c_str(), pass.c_str());
  Serial.println("Connecting to " + ssid);

  unsigned long currentMillis = millis();
  previousMillis = currentMillis;

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    if (currentMillis - previousMillis >= interval) {
      Serial.println("Connection failed");
      WiFi.disconnect(true);
      sendChannelToNode(oldChannel);
      return false;
    }
    currentMillis = millis();
  }

  writeFile(LittleFS, channelPath, String(newChannel).c_str());
  Serial.println("Connected to WiFi: " + WiFi.localIP().toString());
  timeClient.begin();
  wasConnected = true;
  manualDisconnect = false;
  ensureAPRunning();
  return true;
}

// Processor for HTML placeholders
String processor(const String& var) {
  if (var == "SSID") return ssid;
  if (var == "PASS") return pass;
  if (var == "RTC_STATUS") return ntpSynced ? "RTC Synced" : "RTC Node need to sync";
  return String();
}

// Initialize ESP-NOW with retry
bool initESPNOW() {
  WiFi.mode(WIFI_AP_STA);
  setWiFiChannel(currentChannel);
  delay(100);
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return false;
  }
  Serial.println("ESP-NOW initialized");
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);

  // Register RTC node
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, rtcNodeMac, 6);
  peerInfo.channel = currentChannel;
  peerInfo.encrypt = false;
  for (int i = 0; i < 3; i++) {
    if (esp_now_add_peer(&peerInfo) == ESP_OK) {
      Serial.println("RTC node added successfully");
      return true;
    }
    Serial.println("Failed to add RTC node, retrying...");
    delay(100);
  }
  Serial.println("Failed to add RTC node after retries");
  return false;
}

void setup() {
  Serial.begin(115200);

  // Initialize LittleFS and SD
  initLittleFS();
  initSD();

  // Load values
  ssid = readFile(LittleFS, ssidPath);
  pass = readFile(LittleFS, passPath);
  currentChannel = readFile(LittleFS, channelPath).toInt();
  if (currentChannel == 0) currentChannel = 1;
  ntpSynced = readFile(LittleFS, ntpSyncPath) == "1";

  // Initialize ESP-NOW
  if (!initESPNOW()) {
    Serial.println("ESP-NOW setup failed, restarting...");
    delay(1000);
    ESP.restart();
  }

  // Register all handlers
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    Serial.println("Serving /index.html");
    if (debugServer) {
      Serial.println("Request received from client IP: " + request->client()->remoteIP().toString());
    }
    request->send(LittleFS, "/index.html", "text/html", false, processor);
  });

  server.on("/config", HTTP_GET, [](AsyncWebServerRequest *request){
    Serial.println("Serving /wifimanager.html for /config");
    if (debugServer) {
      Serial.println("Request received from client IP: " + request->client()->remoteIP().toString());
    }
    request->send(LittleFS, "/wifimanager.html", "text/html", false, processor);
  });

  server.on("/config", HTTP_POST, [](AsyncWebServerRequest *request){
    Serial.println("Received POST to /config");
    if (debugServer) {
      Serial.println("Request received from client IP: " + request->client()->remoteIP().toString());
    }
    if (request->hasParam(PARAM_INPUT_1, true) && request->hasParam(PARAM_INPUT_2, true)) {
      String newSsid = request->getParam(PARAM_INPUT_1, true)->value();
      String newPass = request->getParam(PARAM_INPUT_2, true)->value();
      
      Serial.println("Saving new WiFi settings");
      writeFile(LittleFS, ssidPath, newSsid.c_str());
      writeFile(LittleFS, passPath, newPass.c_str());
      manualDisconnect = false;
      
      request->send(200, "text/plain", "Saved. Rebooting...");
      Serial.println("Rebooting...");
      delay(1000);
      ESP.restart();
    } else {
      Serial.println("Invalid POST input");
      request->send(400, "text/plain", "Invalid input");
    }
  });

  server.on("/ntp", HTTP_GET, [](AsyncWebServerRequest *request){
    Serial.println("Serving /ntp");
    if (debugServer) {
      Serial.println("Request received from client IP: " + request->client()->remoteIP().toString());
    }
    if (!ntpSynced) {
      Serial.println("RTC not synced");
      request->send(400, "text/plain", "RTC not synced");
      return;
    }
    requestTimeFromRTC();
    request->send(200, "text/plain", "Time requested from RTC");
  });

  server.on("/disconnect", HTTP_GET, [](AsyncWebServerRequest *request){
    Serial.println("Serving /disconnect");
    if (debugServer) {
      Serial.println("Request received from client IP: " + request->client()->remoteIP().toString());
    }
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi already disconnected");
      request->send(400, "text/plain", "WiFi already disconnected");
      return;
    }
    manualDisconnect = true;
    Serial.println("Manual disconnect triggered");
    WiFi.disconnect(true);
    wasConnected = false;
    ensureAPRunning();
    delay(100); // Give AP time to stabilize
    if (debugServer) {
      Serial.println("Server is listening on http://192.168.4.1");
    }
    request->send(200, "text/plain", "Disconnected");
  });

  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request){
    Serial.println("Serving /status");
    if (debugServer) {
      Serial.println("Request received from client IP: " + request->client()->remoteIP().toString());
    }
    String status = WiFi.status() == WL_CONNECTED ? "Online" : "Offline";
    String response = "{\"status\":\"" + status + "\",\"ssid\":\"" + ssid + "\"}";
    request->send(200, "application/json", response);
  });

  server.on("/rtc_status", HTTP_GET, [](AsyncWebServerRequest *request){
    Serial.println("Serving /rtc_status");
    if (debugServer) {
      Serial.println("Request received from client IP: " + request->client()->remoteIP().toString());
    }
    String response = "{\"rtc_status\":\"" + String(ntpSynced ? "RTC Synced" : "RTC Node need to sync") + "\"}";
    request->send(200, "application/json", response);
  });

  server.on("/nodes", HTTP_GET, [](AsyncWebServerRequest *request){
    Serial.println("Serving /nodes");
    if (debugServer) {
      Serial.println("Request received from client IP: " + request->client()->remoteIP().toString());
    }
    String response = "{\"rtc\":\"" + String(rtcNodeConnected ? "Connected" : "Disconnected") + "\"}";
    request->send(200, "application/json", response);
  });

  server.on("/test", HTTP_GET, [](AsyncWebServerRequest *request){
    Serial.println("Serving /test");
    if (debugServer) {
      Serial.println("Request received from client IP: " + request->client()->remoteIP().toString());
    }
    request->send(200, "text/plain", "Server is working!");
  });

  server.serveStatic("/", LittleFS, "/");

  // Try to connect
  if (!initWiFi()) {
    ensureAPRunning();
  }

  // Sync NTP if needed
  if (!ntpSynced && WiFi.status() == WL_CONNECTED) {
    updatePeerChannel();
    sendNTPToNode();
  }

  Serial.println("Server started");
  if (debugServer) {
    Serial.println("Server is listening on http://192.168.4.1");
  }
  server.begin();
}

void loop() {
  unsigned long currentMillis = millis();

  // Check if WiFi is disconnected
  if (WiFi.status() != WL_CONNECTED && currentMillis - previousMillis >= interval && !manualDisconnect) {
    Serial.println("WiFi disconnected. Switching to AP mode...");
    WiFi.disconnect(true);
    ensureAPRunning();
    if (!serverRestarted) {
      server.end();
      server.begin();
      Serial.println("Server restarted after WiFi disconnected");
      if (debugServer) {
        Serial.println("Server is listening on http://192.168.4.1");
      }
      serverRestarted = true;
    }
    previousMillis = currentMillis;
    wasConnected = false;
  }

  // Reset serverRestarted when WiFi reconnects
  if (WiFi.status() == WL_CONNECTED && serverRestarted) {
    serverRestarted = false;
  }

  // Check AP status periodically
  if (currentMillis - lastAPCheck >= apCheckInterval) {
    if (WiFi.softAPgetStationNum() == 0 && WiFi.getMode() != WIFI_AP_STA) {
      Serial.println("AP not running, restarting...");
      ensureAPRunning();
      if (!serverRestarted) {
        server.end();
        server.begin();
        Serial.println("Server restarted after AP restart");
        if (debugServer) {
          Serial.println("Server is listening on http://192.168.4.1");
        }
        serverRestarted = true;
      }
    }
    lastAPCheck = currentMillis;
  }

  // Attempt to reconnect
  if (!wasConnected && !manualDisconnect && ssid != "" && pass != "" && currentMillis - lastReconnectAttempt >= reconnectInterval) {
    Serial.println("Attempting to reconnect to " + ssid);
    
    int oldChannel = WiFi.channel() > 0 ? WiFi.channel() : currentChannel;
    writeFile(LittleFS, oldChannelPath, String(oldChannel).c_str());

    int newChannel = getChannelForSSID(ssid);
    if (newChannel != -1) {
      if (newChannel != oldChannel) {
        if (!sendChannelToNode(newChannel)) {
          Serial.println("Failed to notify RTC node of new channel");
          lastReconnectAttempt = currentMillis;
          return;
        }
      } else {
        currentChannel = newChannel;
        setWiFiChannel(currentChannel);
        updatePeerChannel();
      }

      WiFi.setAutoReconnect(false);
      WiFi.begin(ssid.c_str(), pass.c_str());
      Serial.println("Connecting to " + ssid);

      unsigned long connectStart = millis();
      while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        if (millis() - connectStart >= interval) {
          Serial.println("Reconnection failed");
          WiFi.disconnect(true);
          sendChannelToNode(oldChannel);
          lastReconnectAttempt = currentMillis;
          return;
        }
      }

      writeFile(LittleFS, channelPath, String(newChannel).c_str());
      Serial.println("Reconnected to WiFi: " + WiFi.localIP().toString());
      timeClient.begin();
      wasConnected = true;
      manualDisconnect = false;
      ensureAPRunning();
      server.end();
      server.begin();
      Serial.println("Server restarted after reconnect");
      if (debugServer) {
        Serial.println("Server is listening on http://192.168.4.1");
      }
      serverRestarted = false;

      // Sync NTP if needed
      if (!ntpSynced) {
        updatePeerChannel();
        sendNTPToNode();
      }
    } else {
      Serial.println("SSID not found, staying in AP mode");
      ensureAPRunning();
      if (!serverRestarted) {
        server.end();
        server.begin();
        Serial.println("Server restarted after failed reconnect");
        if (debugServer) {
          Serial.println("Server is listening on http://192.168.4.1");
        }
        serverRestarted = true;
      }
    }

    lastReconnectAttempt = currentMillis;
  }

  // Check node status
  if (currentMillis - lastNodeCheck >= nodeCheckInterval) {
    if (!rtcNodeConnected) {
      Serial.println("RTC node disconnected");
    }
    lastNodeCheck = currentMillis;
  }

  // Request time every minute
  if (ntpSynced && currentMillis - lastCycle >= cycleInterval) {
    requestTimeFromRTC();
    lastCycle = currentMillis;
  }
}