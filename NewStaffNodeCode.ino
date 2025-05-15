// Modified Staff/Doctor Node Code
#include <SPI.h>
#include <MFRC522.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include <esp_now.h>
#include <WiFi.h>

#define RST_PIN  5
#define SS_PIN   4
#define GREEN_LED_PIN 15
#define RED_LED_PIN   2
#define BUTTON_PIN  14

MFRC522 mfrc522(SS_PIN, RST_PIN);
Preferences preferences;

uint8_t arrivalMAC[] = {0x08, 0xD1, 0xF9, 0xD7, 0x50, 0x98};
uint8_t arrivalMAC1[] = {0x08, 0xD1, 0xF9, 0xD7, 0x50, 0x98};
uint8_t displayMAC[] = {0x68, 0xC6, 0x3A, 0xFC, 0x61, 0x3E};

struct QueueItem {
  char uid[20];
  char timestamp[25];
  int number;
  bool removeFromQueue;
  bool reserved;
};

struct HandshakeMsg {
  char type[15];  // "HELLO", "ATTENDED", "NOT_ATTENDED"
  char uid[20];
};

QueueItem currentPatient = {};
bool hasPatient = false;

void onDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  if (len == sizeof(QueueItem)) {
    memcpy(&currentPatient, incomingData, sizeof(currentPatient));
    hasPatient = true;
    Serial.print("📥 Assigned Patient: "); Serial.println(currentPatient.number);
    esp_now_send(displayMAC, (uint8_t*)&currentPatient.number, sizeof(currentPatient.number));
  }
}

void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("📤 Send Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivered ✅" : "Failed ❌");
}

void setup() {
  Serial.begin(115200);
  SPI.begin();
  mfrc522.PCD_Init();
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  digitalWrite(GREEN_LED_PIN, HIGH);
  digitalWrite(RED_LED_PIN, HIGH);

  WiFi.mode(WIFI_STA);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  if (esp_now_init() != ESP_OK) {
    Serial.println("❌ ESP-NOW Init Failed");
    return;
  }

  esp_now_register_recv_cb(onDataRecv);
  esp_now_register_send_cb(onDataSent);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, arrivalMAC, 6);
  peerInfo.channel = 1;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);

  memcpy(peerInfo.peer_addr, displayMAC, 6);
  esp_now_add_peer(&peerInfo);

  // Send handshake
  HandshakeMsg hello;
  strcpy(hello.type, "HELLO");
  esp_now_send(arrivalMAC, (uint8_t*)&hello, sizeof(hello));

  Serial.println("👨‍⚕️ Staff Node Ready");
}

void loop() {
  if (!hasPatient) return;

  if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) {
    // Check for request next button
    if (digitalRead(BUTTON_PIN) == LOW) {
      HandshakeMsg msg;
      strcpy(msg.type, "NOT_ATTENDED");
      strncpy(msg.uid, currentPatient.uid, sizeof(msg.uid));
      esp_now_send(arrivalMAC, (uint8_t*)&msg, sizeof(msg));
      hasPatient = false;
      delay(500);
    }
    return;
  }

  String scannedUID = getUIDString(mfrc522.uid.uidByte, mfrc522.uid.size);
  if (String(currentPatient.uid) == scannedUID) {
    HandshakeMsg msg;
    strcpy(msg.type, "ATTENDED");
    strncpy(msg.uid, currentPatient.uid, sizeof(msg.uid));
    esp_now_send(arrivalMAC, (uint8_t*)&msg, sizeof(msg));
    Serial.print("✅ Patient Served: "); Serial.println(currentPatient.number);
    blinkLED(GREEN_LED_PIN);
    hasPatient = false;
  } else {
    Serial.println("❌ Invalid UID");
    blinkLED(RED_LED_PIN);
  }

  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
  delay(1500);
}

String getUIDString(byte *buffer, byte bufferSize) {
  String uid = "";
  for (byte i = 0; i < bufferSize; i++) {
    if (buffer[i] < 0x10) uid += "0";
    uid += String(buffer[i], HEX);
  }
  uid.toUpperCase();
  return uid;
}

void blinkLED(int pin) {
  digitalWrite(pin, LOW);
  delay(500);
  digitalWrite(pin, HIGH);
}
