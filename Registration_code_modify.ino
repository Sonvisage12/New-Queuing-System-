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

uint8_t arrivalMAC[] = {0x08, 0xD1, 0xF9, 0xD7, 0x50, 0x98};
uint8_t doctorMAC1[] = {0x78, 0x42, 0x1C, 0x6C, 0xA8, 0x3C};
uint8_t doctorMAC[]  = {0x78, 0x42, 0x1C, 0x6C, 0xE4, 0x9C};
uint8_t broadcastAddr[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

void broadcastFullQueue() {
  auto qList = sharedQueue.getQueue();
  for (const auto& entry : qList) {
    QueueItem item;
    memset(&item, 0, sizeof(item));
    strncpy(item.uid, entry.uid.c_str(), sizeof(item.uid));
    strncpy(item.timestamp, entry.timestamp.c_str(), sizeof(item.timestamp));
    item.number = entry.number;
    item.removeFromQueue = false;
    esp_now_send(broadcastAddr, (uint8_t*)&item, sizeof(item));
    delay(10);
  }
  Serial.println("✅ Full queue broadcast complete.");
}

void onDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
  QueueItem item;
  memcpy(&item, incomingData, sizeof(item));

  String uid = String(item.uid);
  if (uid == "SYNC_REQ") {
    Serial.println("🔁 SYNC_REQ received. Broadcasting full queue...");
    broadcastFullQueue();
    return;
  }

  if (item.removeFromQueue) {
    sharedQueue.removeByUID(uid);
  } else {
    sharedQueue.addIfNew(uid, String(item.timestamp), item.number);
  }

  Serial.print("📩 Received from: ");
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  Serial.println(macStr);
  sharedQueue.print();
}

void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("📤 Send Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivered ✅" : "Failed ❌");
}

void setup() {
  Serial.begin(115200);
  prefs.begin("rfidMap", false);
  prefs.putUInt("13B6B1E3", 1);
  prefs.putUInt("13D7ADE3", 2);
  prefs.putUInt("A339D9E3", 3);
  prefs.putUInt("220C1805", 4);
  prefs.putUInt("638348E3", 5);
  prefs.putUInt("A3E9C7E3", 6);
  prefs.putUInt("5373BEE3", 7);
  prefs.putUInt("62EDFF51", 8);
  prefs.putUInt("131DABE3", 9);
  prefs.putUInt("B3D4B0E3", 10);
  prefs.putUInt("23805EE3", 11);
  prefs.putUInt("1310BAE3", 12);
  prefs.putUInt("D38A47E3", 13);
  prefs.putUInt("6307D8E3", 14);
  prefs.putUInt("D35FC4E3", 15);
  prefs.putUInt("C394B9E3", 16);
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
    Serial.println("❌ ESP-NOW Init Failed");
    return;
  }

  std::vector<uint8_t*> peers = {doctorMAC, arrivalMAC, doctorMAC1};
  for (auto peer : peers) {
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, peer, 6);
    peerInfo.channel = 1;
    peerInfo.encrypt = false;
    if (!esp_now_is_peer_exist(peer)) {
      esp_now_add_peer(&peerInfo);
    }
  }

  esp_now_peer_info_t broadcastPeer = {};
  memcpy(broadcastPeer.peer_addr, broadcastAddr, 6);
  broadcastPeer.channel = 1;
  broadcastPeer.encrypt = false;
  esp_now_add_peer(&broadcastPeer);

  esp_now_register_send_cb(onDataSent);
  esp_now_register_recv_cb(onDataRecv);

  sharedQueue.load();
  Serial.println("📚 RFID Arrival Node Ready");
  sharedQueue.print();
}

void loop() {
  if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) return;

  String uid = getUIDString(mfrc522.uid.uidByte, mfrc522.uid.size);
  Serial.print("🆔 Card UID detected: ");
  Serial.println(uid);

  if (sharedQueue.exists(uid)) {
    Serial.println("⏳ Already in queue.");
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

    for (auto peer : std::vector<uint8_t*>{doctorMAC, arrivalMAC, doctorMAC1}) {
      esp_now_send(peer, (uint8_t *)&item, sizeof(item));
    }
    Serial.printf("✅ Registered: %d | %s\n", pid, timeStr.c_str());
    blinkLED(GREEN_LED_PIN);
    sharedQueue.print();
  }
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
  delay(1500);
}

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
    Serial.printf("✅ Known UID: %s -> %d\n", uid.c_str(), pid);
  } else {
    Serial.printf("❌ Unknown UID: %s\n", uid.c_str());
  }
  prefs.end();
  return pid;
}

void blinkLED(int pin) {
  digitalWrite(pin, LOW);
  delay(1000);
  digitalWrite(pin, HIGH);
}
