#include <esp_now.h>
#include <WiFi.h>

// مک آدرس کلاینت
uint8_t clientAddress[] = {0x24, 0xD7, 0xEB, 0x10, 0x23, 0xCC};

// پین‌های رله
#define RF_PIN 25 // پین فن
#define RH_PIN 26 // پین پد

// آستانه‌ها
const float tempHigh = 37.0;  // آستانه بالا دما
const float tempLow = 35.0;   // آستانه پایین دما
const float humHigh = 60.0;   // آستانه بالا رطوبت
const float humLow = 40.0;    // آستانه پایین رطوبت

// وضعیت رله‌ها برای Hysteresis
bool fanOn = false;  // وضعیت فن
bool padOn = false;  // وضعیت پد

// ساختار داده دریافتی از کلاینت
typedef struct relay_control_message {
  float ta;  // دما از سنسور 1
  float ha;  // رطوبت از سنسور 1
} relay_control_message;

// ساختار تأییدیه
typedef struct relay_confirmation {
  uint8_t status; // 1 = موفقیت، 0 = خطا
  uint8_t rf;     // وضعیت فن (1 = روشن، 0 = خاموش)
  uint8_t rh;     // وضعیت پد (1 = روشن، 0 = خاموش)
} relay_confirmation;

// متغیر
esp_now_peer_info_t peerInfo;

void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  relay_control_message receivedData;
  memcpy(&receivedData, incomingData, sizeof(receivedData));

  if (memcmp(info->src_addr, clientAddress, 6) == 0) {
    Serial.println("Received data from Client:");
    Serial.print("  Temperature = "); Serial.print(receivedData.ta); Serial.println(" °C");
    Serial.print("  Humidity = "); Serial.print(receivedData.ha); Serial.println(" %");

    // کنترل فن (بر اساس دما)
    if (receivedData.ta > tempHigh && !fanOn) {
      fanOn = true;
      digitalWrite(RF_PIN, LOW); // فن روشن (Active-Low)
      Serial.println("Fan turned ON");
    }
    if (receivedData.ta < tempLow && fanOn) {
      fanOn = false;
      digitalWrite(RF_PIN, HIGH); // فن خاموش
      Serial.println("Fan turned OFF");
    }

    // کنترل پد (بر اساس رطوبت)
    if (receivedData.ha < humLow && !padOn) {
      padOn = true;
      digitalWrite(RH_PIN, LOW); // پد روشن (Active-Low)
      Serial.println("Pad turned ON");
    }
    if (receivedData.ha > humHigh && padOn) {
      padOn = false;
      digitalWrite(RH_PIN, HIGH); // پد خاموش
      Serial.println("Pad turned OFF");
    }

    // کوپلینگ اضافی: اگر فن روشن و رطوبت مناسب اما دما هنوز بالا، پد رو اولویت بده
    if (fanOn && !padOn && receivedData.ha < 55.0) {
      padOn = true;
      digitalWrite(RH_PIN, LOW); // پد روشن
      Serial.println("Pad turned ON for cooling (coupling)");
    }

    // ارسال تأییدیه به کلاینت
    relay_confirmation confirm;
    confirm.status = 1; // موفقیت
    confirm.rf = fanOn ? 1 : 0;
    confirm.rh = padOn ? 1 : 0;
    esp_err_t result = esp_now_send(clientAddress, (uint8_t *)&confirm, sizeof(confirm));
    if (result == ESP_OK) {
      Serial.println("Confirmation sent to Client");
    } else {
      Serial.println("Failed to send confirmation to Client");
    }
  }
}

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);

  // راه‌اندازی پین‌های رله
  pinMode(RF_PIN, OUTPUT);
  pinMode(RH_PIN, OUTPUT);
  digitalWrite(RF_PIN, HIGH); // خاموش کردن اولیه رله‌ها (Active-Low)
  digitalWrite(RH_PIN, HIGH);
  Serial.println("Relay pins initialized");

  // راه‌اندازی ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  esp_now_register_recv_cb(OnDataRecv);

  // افزودن کلاینت به عنوان peer
  memcpy(peerInfo.peer_addr, clientAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
    return;
  }
  Serial.println("Relay Node: Setup completed");
}

void loop() {
  // خالی، چون همه چیز در callback انجام می‌شود
}