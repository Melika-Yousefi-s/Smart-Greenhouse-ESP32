#include <esp_now.h>
#include <WiFi.h>
#include <SPI.h>
#include <SD.h>

// پین‌های SD کارت
#define SD_CS 13
#define SD_SCK 14
#define SD_MISO 2
#define SD_MOSI 15

// کلید درخواست
const uint8_t requestKey = 47;

// فاصله زمانی درخواست (10 ثانیه)
const long actionInterval = 10000;

// مک آدرس نودها
uint8_t sensor1MacAddress[] = {0x30, 0xC6, 0xF7, 0xF7, 0xAC, 0x68};
uint8_t relayNodeMacAddress[] = {0xA8, 0x42, 0xE3, 0x91, 0x14, 0x54};
uint8_t rtcNodeMacAddress[] = {0xAC, 0x0B, 0xFB, 0x18, 0x88, 0xD8};

// ساختار درخواست
typedef struct request_message {
  uint8_t key;
} request_message;

// ساختار داده سنسور
typedef struct sensor_data {
  float ta;  // دما
  float ha;  // رطوبت
  float lux; // نور
  float hs;  // رطوبت خاک
} sensor_data;

// ساختار داده برای ارسال به نود رله
typedef struct relay_control_message {
  float ta;  // دما از سنسور 1
  float ha;  // رطوبت از سنسور 1
} relay_control_message;

// ساختار تأییدیه از نود رله
typedef struct relay_confirmation {
  uint8_t status; // 1 = موفقیت، 0 = خطا
  uint8_t rf;     // وضعیت فن
  uint8_t rh;     // وضعیت پد
} relay_confirmation;

// ساختار داده از نود RTC
typedef struct rtc_data {
  char datetime[25]; // تاریخ و زمان (مثل 2025-09-02 11:55:13)
} rtc_data;

// متغیرها
request_message requestData;
sensor_data dataFromSensor1;
relay_control_message relayData;
relay_confirmation relayConfirm;
rtc_data dataFromRTC;
esp_now_peer_info_t peerInfoSensor1;
esp_now_peer_info_t peerInfoRelay;
esp_now_peer_info_t peerInfoRTC;
unsigned long lastActionTime = 0;
bool dataFromSensor1Received = false;
bool relayConfirmReceived = false;
bool dataFromRTCReceived = false;
unsigned long logId = 1;
unsigned long sessionId = 1;
uint8_t mood = 0; // 0 = شب، 1 = روز
unsigned long cycleStartTime = 0; // زمان شروع سیکل
unsigned long cycleTime = 0; // مدت زمان سیکل (ms)

// استخراج ساعت از datetime و تعیین mood
void calculateMood() {
  // فرمت datetime: "YYYY-MM-DD HH:MM:SS"
  String datetimeStr = String(dataFromRTC.datetime);
  int hourStart = datetimeStr.indexOf(' ') + 1; // شروع ساعت
  int hourEnd = datetimeStr.indexOf(':'); // پایان ساعت
  String hourStr = datetimeStr.substring(hourStart, hourEnd);
  int currentHour = hourStr.toInt();
  mood = (currentHour >= 6 && currentHour < 18) ? 1 : 0; // روز: 6 صبح تا 18، شب: بقیه
  Serial.print("Mood: ");
  Serial.println(mood == 1 ? "Day" : "Night");
}

// callback دریافت داده
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  if (memcmp(info->src_addr, sensor1MacAddress, 6) == 0) {
    memcpy(&dataFromSensor1, incomingData, sizeof(dataFromSensor1));
    dataFromSensor1Received = true;
    Serial.println("Data received from Sensor 1:");
    Serial.print("  Temperature = "); Serial.print(dataFromSensor1.ta); Serial.println(" °C");
    Serial.print("  Humidity = "); Serial.print(dataFromSensor1.ha); Serial.println(" %");
    Serial.print("  Light = "); Serial.print(dataFromSensor1.lux); Serial.println(" lx");
    Serial.print("  Soil Moisture = "); Serial.print(dataFromSensor1.hs); Serial.println(" %");
  } else if (memcmp(info->src_addr, relayNodeMacAddress, 6) == 0) {
    memcpy(&relayConfirm, incomingData, sizeof(relayConfirm));
    relayConfirmReceived = true;
    Serial.print("Relay Node Confirmation: ");
    Serial.print(relayConfirm.status == 1 ? "Relay control successful" : "Relay control failed");
    Serial.print(", Fan = "); Serial.print(relayConfirm.rf == 1 ? "ON" : "OFF");
    Serial.print(", Pad = "); Serial.println(relayConfirm.rh == 1 ? "ON" : "OFF");
  } else if (memcmp(info->src_addr, rtcNodeMacAddress, 6) == 0) {
    memcpy(&dataFromRTC, incomingData, sizeof(dataFromRTC));
    dataFromRTCReceived = true;
    Serial.print("RTC Node: Datetime = ");
    Serial.println(dataFromRTC.datetime);
    calculateMood(); // محاسبه mood بعد از دریافت datetime
  }
}

// callback وضعیت ارسال
void OnDataSent(const esp_now_send_info_t *info, esp_now_send_status_t status) {
  Serial.print("Request Send Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

// راه‌اندازی SD
void initSD() {
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  delay(100);
  if (!SD.begin(SD_CS)) {
    Serial.println("SD Card Mount Failed!");
    return;
  }
  Serial.println("SD Card initialized.");
}

// ساخت SessionID و فایل CSV
void initCSV() {
  if (!SD.exists("/session_id.txt")) {
    File file = SD.open("/session_id.txt", FILE_WRITE);
    if (file) {
      file.println("1");
      file.close();
    }
    sessionId = 1;
  } else {
    File file = SD.open("/session_id.txt", FILE_READ);
    if (file) {
      sessionId = file.readStringUntil('\n').toInt();
      file.close();
      sessionId++;
      file = SD.open("/session_id.txt", FILE_WRITE);
      if (file) {
        file.println(String(sessionId));
        file.close();
      }
    }
  }

  if (!SD.exists("/sensor_log.csv")) {
    File file = SD.open("/sensor_log.csv", FILE_WRITE);
    if (file) {
      file.println("ID,SessionID,ta1,ha1,lux1,hs,rf,rh,datetime,mood,cycle1");
      file.close();
    }
    logId = 1;
  } else {
    File file = SD.open("/sensor_log.csv", FILE_READ);
    if (file) {
      String lastLine;
      while (file.available()) {
        lastLine = file.readStringUntil('\n');
      }
      file.close();
      if (lastLine.length() > 0) {
        int commaIndex = lastLine.indexOf(',');
        if (commaIndex != -1) {
          logId = lastLine.substring(0, commaIndex).toInt() + 1;
        }
      }
    }
  }
  Serial.print("Starting logId: ");
  Serial.println(logId);
  Serial.print("SessionID: ");
  Serial.println(sessionId);
}

// ذخیره داده‌ها
void logData() {
  cycleTime = millis() - cycleStartTime; // محاسبه مدت زمان سیکل
  File file = SD.open("/sensor_log.csv", FILE_APPEND);
  if (file) {
    file.print(logId);
    file.print(",");
    file.print(sessionId);
    file.print(",");
    file.print(dataFromSensor1.ta);
    file.print(",");
    file.print(dataFromSensor1.ha);
    file.print(",");
    file.print(dataFromSensor1.lux);
    file.print(",");
    file.print(dataFromSensor1.hs);
    file.print(",");
    file.print(relayConfirm.rf);
    file.print(",");
    file.print(relayConfirm.rh);
    file.print(",");
    file.print("\"");
    file.print(dataFromRTC.datetime);
    file.print("\",");
    file.print(mood);
    file.print(",");
    file.print(cycleTime);
    file.println();
    file.close();
    Serial.print("Cycle Time: ");
    Serial.print(cycleTime);
    Serial.println(" ms");
    Serial.println("Data logged.");
    logId++;
  } else {
    Serial.println("Error opening CSV.");
  }
}

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  esp_now_register_recv_cb(OnDataRecv);
  esp_now_register_send_cb(OnDataSent);

  // افزودن peer نود سنسور 1
  memcpy(peerInfoSensor1.peer_addr, sensor1MacAddress, 6);
  peerInfoSensor1.channel = 0;
  peerInfoSensor1.encrypt = false;
  esp_now_add_peer(&peerInfoSensor1);

  // افزودن peer نود رله
  memcpy(peerInfoRelay.peer_addr, relayNodeMacAddress, 6);
  peerInfoRelay.channel = 0;
  peerInfoRelay.encrypt = false;
  esp_now_add_peer(&peerInfoRelay);

  // افزودن peer نود RTC
  memcpy(peerInfoRTC.peer_addr, rtcNodeMacAddress, 6);
  peerInfoRTC.channel = 0;
  peerInfoRTC.encrypt = false;
  esp_now_add_peer(&peerInfoRTC);

  initSD();
  initCSV();
}

void loop() {
  static unsigned long nextActionTime = 0;
  unsigned long currentTime = millis();

  if (currentTime >= nextActionTime) {
    cycleStartTime = currentTime; // ثبت زمان شروع سیکل
    requestData.key = requestKey;

    // ارسال درخواست به نود سنسور 1
    esp_now_send(sensor1MacAddress, (uint8_t *)&requestData, sizeof(requestData));
    unsigned long waitStart = millis();
    dataFromSensor1Received = false;
    while (millis() - waitStart < 2000 && !dataFromSensor1Received) {
      delay(1); // کاهش تأخیر برای بهبود پاسخ‌گویی
    }

    // ارسال درخواست به نود RTC
    esp_now_send(rtcNodeMacAddress, (uint8_t *)&requestData, sizeof(requestData));
    waitStart = millis();
    dataFromRTCReceived = false;
    while (millis() - waitStart < 2000 && !dataFromRTCReceived) {
      delay(1); // کاهش تأخیر
    }

    if (dataFromSensor1Received && dataFromRTCReceived) {
      // آماده‌سازی داده برای نود رله
      relayData.ta = dataFromSensor1.ta;
      relayData.ha = dataFromSensor1.ha;

      // ارسال داده به نود رله
      esp_now_send(relayNodeMacAddress, (uint8_t *)&relayData, sizeof(relayData));
      waitStart = millis();
      relayConfirmReceived = false;
      while (millis() - waitStart < 2000 && !relayConfirmReceived) {
        delay(1); // کاهش تأخیر
      }

      if (relayConfirmReceived && relayConfirm.status == 1) {
        logData(); // فقط در صورت تأیید موفقیت از نود رله داده‌ها ذخیره شوند
      } else {
        Serial.println("No confirmation from Relay Node or control failed");
      }
    } else {
      if (!dataFromSensor1Received) {
        Serial.println("No data received from Sensor 1");
      }
      if (!dataFromRTCReceived) {
        Serial.println("No data received from RTC Node");
      }
    }
    nextActionTime = currentTime + actionInterval; // زمان‌بندی ثابت برای درخواست بعدی
  }
}