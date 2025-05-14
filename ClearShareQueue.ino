#include <Preferences.h>
#include <Arduino.h>

Preferences prefs;

void setup() {
  Serial.begin(115200);
  delay(2000);  // Wait for serial to initialize

  Serial.println("🧹 Clearing all stored data...");

  // Clear SharedQueue data
  prefs.begin("rfid-patients", false);  // false = read-write
  prefs.clear();
  prefs.end();
  Serial.println("✅ Cleared 'rfid-patients' queue data.");

  // Clear UID-to-number mapping
  prefs.begin("rfidMap", false);
  prefs.clear();
  prefs.end();
  Serial.println("✅ Cleared 'rfidMap' UID assignments.");

  Serial.println("🔄 All data cleared. You can now restart or upload a new sketch.");
}

void loop() {
  // Do nothing
}
