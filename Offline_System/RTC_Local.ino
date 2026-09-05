#include <esp_now.h>
#include <WiFi.h>
#include <RTClib.h>
#include <Wire.h>

// مک آدرس کلاینت
uint8_t clientAddress[] = {0x24, 0xD7, 0xEB, 0x10, 0x23, 0xCC};

// پین‌های I2C برای DS3231
#define SDA_PIN 21
#define SCL_PIN 22

// کلید درخواست
const uint8_t requestKey = 47;

// ساختار درخواست
typedef struct request_message {
  uint8_t key;
} request_message;

// ساختار داده RTC
typedef struct rtc_data {
  char datetime[25]; // تاریخ و زمان (مثل 2025-09-02 08:07:14)
} rtc_data;

// شیء RTC
RTC_DS3231 rtc;

// متغیر
esp_now_peer_info_t peerInfo;

void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  request_message req;
  memcpy(&req, incomingData, sizeof(req));

  if (req.key == requestKey && memcmp(info->src_addr, clientAddress, 6) == 0) {
    rtc_data myData;
    
    // خواندن زمان از DS3231
    DateTime now = rtc.now();
    snprintf(myData.datetime, sizeof(myData.datetime), "%04d-%02d-%02d %02d:%02d:%02d",
             now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());

    Serial.print("RTC Node: Sending datetime = ");
    Serial.println(myData.datetime);

    // ارسال داده به کلاینت
    esp_err_t result = esp_now_send(clientAddress, (uint8_t *)&myData, sizeof(myData));
    if (result == ESP_OK) {
      Serial.println("RTC Node: Data sent successfully!");
    } else {
      Serial.println("RTC Node: Failed to send data!");
    }
  }
}

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);

  // راه‌اندازی I2C و DS3231
  Wire.begin(SDA_PIN, SCL_PIN);
  if (!rtc.begin()) {
    Serial.println("RTC Node: Failed to initialize DS3231!");
    while (1); // توقف در صورت خطا
  }
  Serial.println("RTC Node: DS3231 initialized successfully");

  // بررسی باتری DS3231
  if (rtc.lostPower()) {
    Serial.println("RTC Node: RTC lost power, please set time!");
  }

  // راه‌اندازی ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("RTC Node: Error initializing ESP-NOW");
    return;
  }
  esp_now_register_recv_cb(OnDataRecv);

  // افزودن کلاینت به عنوان peer
  memcpy(peerInfo.peer_addr, clientAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("RTC Node: Failed to add peer");
    return;
  }
  Serial.println("RTC Node: Setup completed");
}

void loop() {
  // خالی، چون همه چیز در callback انجام می‌شود
}