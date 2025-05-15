// ========================== INCLUDES & DEFINITIONS ==========================
#include <SPI.h>
#include <MFRC522.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include <esp_now.h>
#include <WiFi.h>
#include <vector>
#include <algorithm>
#include "SharedQueue.h"

#define RST_PIN  5
#define SS_PIN   4
#define GREEN_LED_PIN 15
#define RED_LED_PIN   2

MFRC522 mfrc522(SS_PIN, RST_PIN);
Preferences prefs;
SharedQueue sharedQueue("rfid-patients");

// ========================== STRUCTURES ==========================

struct HandshakeMsg {
  char type[10]; // should be "HELLO"
};

// ========================== MAC ADDRESSES ==========================
uint8_t arrivalMAC[] = {0x08, 0xD1, 0xF9, 0xD7, 0x50, 0x98};
uint8_t doctorMAC1[] = {0x78, 0x42, 0x1C, 0x6C, 0xA8, 0x3C};
uint8_t doctorMAC2[]  = {0x78, 0x42, 0x1C, 0x6C, 0xE4, 0x9C};
std::vector<uint8_t*> doctorPeers = {doctorMAC1, doctorMAC2};

int registeredStaff = 0;
bool handshakeComplete = false;

// ========================== CALLBACKS ==========================
void onDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
  if (len == sizeof(HandshakeMsg)) {
    HandshakeMsg msg;
    memcpy(&msg, incomingData, sizeof(msg));
    if (strcmp(msg.type, "HELLO") == 0) {
      registeredStaff++;
      Serial.print("\u2709 Handshake received. Total staff online: ");
      Serial.println(registeredStaff);
    }
    return;
  }

  if (len == sizeof(QueueItem)) {
    QueueItem item;
    memcpy(&item, incomingData, sizeof(item));

    if (item.removeFromQueue) {
      sharedQueue.removeByUID(String(item.uid));
    } else {
      sharedQueue.addIfNew(String(item.uid), String(item.timestamp), item.number);
    }
    sharedQueue.print();
  }
}

void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("\xF0\x9F\x93\xA4 Send Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivered" : "Failed");
}

// ========================== SETUP ==========================
void setup() {
  Serial.begin(115200);

  // Setup hardware
  prefs.begin("rfidMap", false);
  prefs.end();
  SPI.begin();
  WiFi.mode(WIFI_STA);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  Serial.print("WiFi MAC Address: ");
  Serial.println(WiFi.macAddress());

  mfrc522.PCD_Init();
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(RED_LED_PIN, OUTPUT);
  digitalWrite(GREEN_LED_PIN, HIGH);
  digitalWrite(RED_LED_PIN, HIGH);

  if (esp_now_init() != ESP_OK) {
    Serial.println("\u274C Error initializing ESP-NOW");
    return;
  }

  for (auto peer : doctorPeers) {
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, peer, 6);
    peerInfo.channel = 1;
    peerInfo.encrypt = false;
    if (!esp_now_is_peer_exist(peer)) {
      esp_now_add_peer(&peerInfo);
    }
  }

  esp_now_register_send_cb(onDataSent);
  esp_now_register_recv_cb(onDataRecv);
  sharedQueue.load();

  // Send handshake
  HandshakeMsg hello;
  strcpy(hello.type, "HELLO");
  for (auto peer : doctorPeers) {
    esp_now_send(peer, (uint8_t*)&hello, sizeof(hello));
  }

  // Wait for handshakes (you can also use a timeout)
  delay(2000);
  handshakeComplete = true;

  Serial.println("\xF0\x9F\x93\x8D Arrival Node Ready.");
  sharedQueue.print();
}

// ========================== MAIN LOOP ==========================
void loop() {
  if (!handshakeComplete) return;

  if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) return;

  String uid = getUIDString(mfrc522.uid.uidByte, mfrc522.uid.size);
  Serial.print("Card UID detected: "); Serial.println(uid);

  if (sharedQueue.exists(uid)) {
    Serial.println("Already in queue");
    blinkLED(RED_LED_PIN);
  } else {
    int pid = getPermanentNumber(uid);
    if (pid == -1) {
      blinkLED(RED_LED_PIN);
      return;
    }

    String timeStr = String(millis());
    sharedQueue.add(uid, timeStr, pid);

    QueueItem item;
    strncpy(item.uid, uid.c_str(), sizeof(item.uid));
    strncpy(item.timestamp, timeStr.c_str(), sizeof(item.timestamp));
    item.number = pid;
    item.removeFromQueue = false;

    for (auto peer : doctorPeers) {
      esp_now_send(peer, (uint8_t *)&item, sizeof(item));
    }

    Serial.print("Registered: "); Serial.println(pid);
    blinkLED(GREEN_LED_PIN);
    sharedQueue.print();
  }

  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
  delay(1500);
}

// ========================== UTILS ==========================
String getUIDString(byte *buffer, byte bufferSize) {
  String uidString = "";
  for (byte i = 0; i < bufferSize; i++) {
    if (buffer[i] < 0x10) uidString += "0";
    uidString += String(buffer[i], HEX);
  }
  uidString.toUpperCase();
  return uidString;
}

int getPermanentNumber(String uid) {
  prefs.begin("rfidMap", true);
  int pid = -1;
  if (prefs.isKey(uid.c_str())) {
    pid = prefs.getUInt(uid.c_str(), -1);
    Serial.print("Known UID: "); Serial.print(uid);
    Serial.print(" -> Assigned Number: "); Serial.println(pid);
  } else {
    Serial.print("Unknown UID: "); Serial.println(uid);
  }
  prefs.end();
  return pid;
}

void blinkLED(int pin) {
  digitalWrite(pin, LOW);
  delay(500);
  digitalWrite(pin, HIGH);
}
