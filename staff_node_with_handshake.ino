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
Preferences preferences;
SharedQueue sharedQueue("rfid-patients");

// ========================== STRUCTURES ==========================
struct QueueItem {
  char uid[20];
  char timestamp[25];
  int number;
  bool removeFromQueue;
};

struct SyncRequest {
  char type[10]; // "SYNC_REQ"
};

struct StaffHandshake {
  char msg[6]; // "HELLO"
};

struct StaffResponse {
  char msg[3]; // "ID"
  uint8_t staffId;
};

// ========================== MAC ADDRESSES ==========================
uint8_t arrivalMAC[] = {0x08, 0xD1, 0xF9, 0xD7, 0x50, 0x98};
uint8_t doctorMAC1[] = {0x78, 0x42, 0x1C, 0x6C, 0xA8, 0x3C};
uint8_t doctorMAC2[]  = {0x78, 0x42, 0x1C, 0x6C, 0xE4, 0x9C};
uint8_t displayMAC[] = {0x68, 0xC6, 0x3A, 0xFC, 0x61, 0x3E};
std::vector<uint8_t*> peerMACs = { arrivalMAC, doctorMAC1, doctorMAC2, displayMAC };

int patientNum = 0;
int k = 0; int N = 1;
uint8_t staffId = 0;
bool firstServed = false;

std::map<String, QueueItem> queueMap;
std::queue<String> patientOrder;

// ========================== CALLBACKS ==========================
void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  if (len == sizeof(SyncRequest)) {
    SyncRequest req;
    memcpy(&req, incomingData, sizeof(req));
    if (strcmp(req.type, "SYNC_REQ") == 0) {
      Serial.println("🔄 Received sync request.");
      for (auto& entry : queueMap) {
        esp_now_send(info->src_addr, (uint8_t*)&entry.second, sizeof(QueueItem));
      }
      return;
    }
  }
  if (len == sizeof(StaffResponse)) {
    StaffResponse resp;
    memcpy(&resp, incomingData, sizeof(resp));
    if (strncmp(resp.msg, "ID", 2) == 0) {
      staffId = resp.staffId;
      Serial.print("✅ Assigned Staff ID: ");
      Serial.println(staffId);
    }
    return;
  }
  if (len == sizeof(QueueItem)) {
    QueueItem item;
    memcpy(&item, incomingData, sizeof(item));
    String uid = String(item.uid);

    if (item.removeFromQueue) {
      if (queueMap.count(uid)) {
        queueMap.erase(uid);
        std::queue<String> temp;
        while (!patientOrder.empty()) {
          if (patientOrder.front() != uid)
            temp.push(patientOrder.front());
          patientOrder.pop();
        }
        patientOrder = temp;
        Serial.print("🗑️ Removed: ");
        Serial.println(uid);
        preferences.begin("queue", false);
        preferences.clear();
        preferences.end();
        displayNextPatient();
      }
    } else {
      if (!queueMap.count(uid)) {
        queueMap[uid] = item;
        patientOrder.push(uid);
        Serial.print("📥 Added Patient ");
        Serial.print(item.number);
        Serial.print(" | UID: ");
        Serial.println(uid);
        displayNextPatient();
      }
    }
  }
}

void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("📤 Send Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivered ✅" : "Failed ❌");
}

// ========================== SETUP ==========================
void setup() {
  Serial.begin(115200);
  SPI.begin();
  mfrc522.PCD_Init();

  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(RED_LED_PIN, OUTPUT);
  digitalWrite(GREEN_LED_PIN, HIGH);
  digitalWrite(RED_LED_PIN, HIGH);

  WiFi.mode(WIFI_STA);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  Serial.print("WiFi MAC Address: ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    Serial.println("❌ ESP-NOW Init Failed");
    return;
  }

  esp_now_register_recv_cb(onDataRecv);
  esp_now_register_send_cb(onDataSent);

  for (auto mac : peerMACs) {
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, mac, 6);
    peerInfo.channel = 1;
    peerInfo.encrypt = false;
    if (!esp_now_is_peer_exist(mac)) {
      esp_now_add_peer(&peerInfo);
    }
  }

  // Send handshake
  StaffHandshake hs;
  strcpy(hs.msg, "HELLO");
  esp_now_send(arrivalMAC, (uint8_t*)&hs, sizeof(hs));

  preferences.begin("queue", true);
  int size = preferences.getUInt("queueSize", 0);
  for (int i = 0; i < size; i++) {
    String key = "q_" + String(i);
    QueueItem item;
    if (preferences.getBytes(key.c_str(), &item, sizeof(QueueItem))) {
      String uid = String(item.uid);
      queueMap[uid] = item;
      patientOrder.push(uid);
    }
  }
  preferences.end();
  Serial.println("👨‍⚕️ Staff Node Ready");
  displayNextPatient();
}

// ========================== MAIN LOOP ==========================
void loop() {
  if (staffId == 0) return;

  if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) return;

  String uid = getUIDString(mfrc522.uid.uidByte, mfrc522.uid.size);
  uid.toUpperCase();

  if (!patientOrder.empty() && uid == patientOrder.front()) {
    if ((staffId == 1) || (staffId == 2 && firstServed)) {
      QueueItem item = queueMap[uid];
      item.removeFromQueue = true;
      for (auto mac : peerMACs) {
        esp_now_send(mac, (uint8_t*)&item, sizeof(item));
      }
      queueMap.erase(uid);
      patientOrder.pop();
      if (staffId == 1) firstServed = true;
      else firstServed = false;
      Serial.print("✅ Attended: ");
      Serial.println(item.number);
      blinkLED(GREEN_LED_PIN);
      displayNextPatient();
    } else {
      Serial.println("🕒 Waiting for turn");
    }
  } else {
    Serial.println("❌ Access Denied");
    blinkLED(RED_LED_PIN);
  }

  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
  delay(1500);
}

// ========================== HELPERS ==========================
void displayNextPatient() {
  while (!patientOrder.empty()) {
    String uid = patientOrder.front();
    if (queueMap.count(uid)) {
      QueueItem item = queueMap[uid];
      for (auto mac : peerMACs) {
        esp_now_send(mac, (uint8_t*)&item, sizeof(item));
      }
      patientNum = item.number;
      if (k == 1 || N == 1) {
        esp_now_send(displayMAC, (uint8_t*)&patientNum, sizeof(patientNum));
        k = 0; N = 0;
      }
      Serial.print("🔔 Next Patient: ");
      Serial.println(patientNum);
      return;
    } else {
      patientOrder.pop();
    }
  }
  patientNum = 0;
  esp_now_send(displayMAC, (uint8_t*)&patientNum, sizeof(patientNum));
  Serial.println("📭 Queue is empty"); N = 1;
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
  delay(1000);
  digitalWrite(pin, HIGH);
}
