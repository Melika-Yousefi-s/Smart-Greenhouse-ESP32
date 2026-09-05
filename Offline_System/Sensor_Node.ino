#include <esp_now.h>
#include <WiFi.h>
#include "DHT.h"
#include <BH1750.h>
#include <Wire.h>

// تعریف پین و نوع سنسور DHT
#define DHTPIN 4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// تعریف سنسور BH1750
BH1750 lightMeter;

// پین سنسور رطوبت خاک
#define SOIL_SENSOR_PIN 34  // پین آنالوگ سنسور رطوبت خاک

// مک آدرس کلاینت
uint8_t clientAddress[] = {0x24, 0xD7, 0xEB, 0x10, 0x23, 0xCC};

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

// کلید درخواست
const uint8_t requestKey = 47;

// متغیر
esp_now_peer_info_t peerInfo;

void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  request_message req;
  memcpy(&req, incomingData, sizeof(req));

  if (req.key == requestKey) {
    sensor_data myData;

    // خواندن دما و رطوبت از DHT11
    float temp = dht.readTemperature();
    float hum = dht.readHumidity();
    delay(200);
    if (isnan(temp)) {
      myData.ta = -1;
      Serial.println("Sensor1: Failed to read temperature from DHT11");
    } else {
      myData.ta = temp;
      Serial.print("Sensor1: Temperature = ");
      Serial.print(temp);
      Serial.println(" °C");
    }
    if (isnan(hum)) {
      myData.ha = -1;
      Serial.println("Sensor1: Failed to read humidity from DHT11");
    } else {
      myData.ha = hum;
      Serial.print("Sensor1: Humidity = ");
      Serial.print(hum);
      Serial.println(" %");
    }

    // خواندن نور از BH1750
    float lux = lightMeter.readLightLevel();
    if (isnan(lux) || lux < 0) {
      myData.lux = -1;
      Serial.println("Sensor1: Failed to read light level from BH1750");
    } else {
      myData.lux = lux;
      Serial.print("Sensor1: Light = ");
      Serial.print(lux);
      Serial.println(" lx");
    }

    // خواندن رطوبت خاک
    int soilMoistureValue = analogRead(SOIL_SENSOR_PIN);
    float soilMoisturePercent = map(soilMoistureValue, 3150, 2530, 0, 100);
    soilMoisturePercent = constrain(soilMoisturePercent, 0, 100);
    myData.hs = soilMoisturePercent;
    Serial.print("Sensor1: Soil Moisture = ");
    Serial.print(soilMoisturePercent);
    Serial.println(" %");

    // ارسال داده به کلاینت
    esp_err_t result = esp_now_send(clientAddress, (uint8_t *)&myData, sizeof(myData));
    if (result == ESP_OK) {
      Serial.println("Sensor1: Data sent successfully!");
    } else {
      Serial.println("Sensor1: Failed to send data!");
    }
  }
}

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  Wire.begin();

  // راه‌اندازی سنسورها
  dht.begin();
  delay(1100);
  if (!lightMeter.begin()) {
    Serial.println("Sensor1: Failed to initialize BH1750!");
  } else {
    Serial.println("Sensor1: BH1750 initialized successfully");
  }

  // راه‌اندازی پین سنسور رطوبت خاک
  pinMode(SOIL_SENSOR_PIN, INPUT);

  // راه‌اندازی ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Sensor1: Error initializing ESP-NOW");
    return;
  }
  esp_now_register_recv_cb(OnDataRecv);

  // افزودن کلاینت به عنوان peer
  memcpy(peerInfo.peer_addr, clientAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Sensor1: Failed to add peer");
    return;
  }
  Serial.println("Sensor1: Setup completed");
}

void loop() {
  // خالی، چون همه چیز در callback انجام می‌شود
}