#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Wire.h>
#include <RTClib.h>

RTC_DS3231 rtc;
uint8_t masterMac[] = {0x24, 0xD7, 0xEB, 0x10, 0x23, 0xCC};
int currentChannel = 1;
unsigned long lastScan = 0;
unsigned long lastMasterResponse = 0;
const long scanInterval = 120000; // 2 minutes
const long masterTimeout = 120000; // 2 minutes

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
  esp_now_del_peer(masterMac);
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, masterMac, 6);
  peerInfo.channel = currentChannel;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to update master peer channel");
  } else {
    Serial.println("Master peer channel updated to " + String(currentChannel));
  }
}

// Callback for receiving data
void OnDataRecv(const esp_now_recv_info *recv_info, const uint8_t *incomingData, int len) {
  memcpy(&incomingMsg, incomingData, sizeof(incomingMsg));
  lastMasterResponse = millis(); // Update last response time
  if (incomingMsg.type == 1) {
    Serial.printf("Received channel %d from master\n", incomingMsg.channel);
    currentChannel = incomingMsg.channel;
    setWiFiChannel(currentChannel);
    updatePeerChannel(); // Update master peer channel
    // Send ACK
    outgoingMsg.type = 1;
    outgoingMsg.channel = currentChannel;
    esp_err_t result = esp_now_send(masterMac, (uint8_t *) &outgoingMsg, sizeof(outgoingMsg));
    if (result != ESP_OK) {
      Serial.println("Failed to send ACK to master: " + String(esp_err_to_name(result)));
    } else {
      Serial.println("Sent ACK to master");
    }
  } else if (incomingMsg.type == 2) {
    Serial.println("Received NTP time from master");
    rtc.adjust(DateTime(incomingMsg.epochTime));
    // Send sync done
    outgoingMsg.type = 4;
    esp_err_t result = esp_now_send(masterMac, (uint8_t *) &outgoingMsg, sizeof(outgoingMsg));
    if (result != ESP_OK) {
      Serial.println("Failed to send sync done to master: " + String(esp_err_to_name(result)));
    } else {
      Serial.println("Sent sync done to master");
    }
  } else if (incomingMsg.type == 3) {
    Serial.println("Received time request from master");
    DateTime now = rtc.now();
    char datetime[25];
    snprintf(datetime, sizeof(datetime), "%04d-%02d-%02d %02d:%02d:%02d",
             now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());
    outgoingMsg.type = 5;
    strcpy(outgoingMsg.datetime, datetime);
    esp_err_t result = esp_now_send(masterMac, (uint8_t *) &outgoingMsg, sizeof(outgoingMsg));
    if (result != ESP_OK) {
      Serial.println("Failed to send time to master: " + String(esp_err_to_name(result)));
    } else {
      Serial.println("Sent time: " + String(datetime));
    }
  }
}

// Scan for master
void scanForMaster() {
  Serial.println("Scanning for master...");
  WiFi.mode(WIFI_STA);
  for (int channel = 1; channel <= 13; channel++) {
    setWiFiChannel(channel);
    // Re-register peer with current channel
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, masterMac, 6);
    peerInfo.channel = channel;
    peerInfo.encrypt = false;
    for (int i = 0; i < 3; i++) { // Retry adding peer
      if (esp_now_add_peer(&peerInfo) == ESP_OK) {
        Serial.println("Master added for channel " + String(channel));
        break;
      }
      Serial.println("Failed to add master for channel " + String(channel) + ", retrying...");
      delay(100);
    }
    // Send test message
    outgoingMsg.type = 1;
    outgoingMsg.channel = channel;
    esp_err_t result = esp_now_send(masterMac, (uint8_t *) &outgoingMsg, sizeof(outgoingMsg));
    if (result != ESP_OK) {
      Serial.println("Failed to send to master on channel " + String(channel) + ": " + String(esp_err_to_name(result)));
    }
    delay(100); // Wait for response
  }
  // Restore peer on current channel
  currentChannel = 1; // Reset to default channel
  setWiFiChannel(currentChannel);
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, masterMac, 6);
  peerInfo.channel = currentChannel;
  peerInfo.encrypt = false;
  for (int i = 0; i < 3; i++) { // Retry restoring peer
    if (esp_now_add_peer(&peerInfo) == ESP_OK) {
      Serial.println("Master peer restored on channel " + String(currentChannel));
      break;
    }
    Serial.println("Failed to restore master peer on channel " + String(currentChannel) + ", retrying...");
    delay(100);
  }
}

bool initESPNOW() {
  WiFi.mode(WIFI_STA);
  setWiFiChannel(currentChannel);
  delay(100);
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return false;
  }
  Serial.println("ESP-NOW initialized");
  esp_now_register_recv_cb(OnDataRecv);

  // Register master
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, masterMac, 6);
  peerInfo.channel = currentChannel;
  peerInfo.encrypt = false;
  for (int i = 0; i < 3; i++) {
    if (esp_now_add_peer(&peerInfo) == ESP_OK) {
      Serial.println("Master added successfully");
      return true;
    }
    Serial.println("Failed to add master, retrying...");
    delay(100);
  }
  Serial.println("Failed to add master after retries");
  return false;
}

void setWiFiChannel(int channel) {
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
  Serial.printf("Set channel to %d\n", channel);
}

void setup() {
  Serial.begin(115200);
  
  // Initialize RTC
  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC");
    while (1);
  }
  
  // Initialize ESP-NOW
  if (!initESPNOW()) {
    Serial.println("ESP-NOW setup failed, restarting...");
    delay(1000);
    ESP.restart();
  }
}

void loop() {
  unsigned long currentMillis = millis();
  
  // Scan for master if no response received for 2 minutes
  if (currentMillis - lastMasterResponse >= masterTimeout && currentMillis - lastScan >= scanInterval) {
    scanForMaster();
    lastScan = currentMillis;
  }
}