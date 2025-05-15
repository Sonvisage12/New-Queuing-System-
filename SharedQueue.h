#ifndef SHARED_QUEUE_H
#define SHARED_QUEUE_H

#include <Preferences.h>
#include <Arduino.h>
#include <vector>

struct QueueItem {
  char uid[20];
  char timestamp[25];
  int number;
  bool removeFromQueue;
  bool reserved;
};

class SharedQueue {
private:
  std::vector<QueueItem> queue;
  Preferences prefs;
  const char* ns;

public:
  SharedQueue(const char* namespaceName) : ns(namespaceName) {}

  void load() {
    prefs.begin(ns, true);
    queue.clear();
    int count = prefs.getUInt("count", 0);
    for (int i = 0; i < count; ++i) {
      String key = "item" + String(i);
      String data = prefs.getString(key.c_str(), "");
      if (data.length() > 0) {
        QueueItem item;
        parseItem(data, item);
        queue.push_back(item);
      }
    }
    prefs.end();
  }

  void save() {
    prefs.begin(ns, false);
    prefs.clear();
    prefs.putUInt("count", queue.size());
    for (size_t i = 0; i < queue.size(); ++i) {
      String key = "item" + String(i);
      prefs.putString(key.c_str(), serializeItem(queue[i]));
    }
    prefs.end();
  }

  void add(String uid, String timestamp, int number) {
    QueueItem item;
    strncpy(item.uid, uid.c_str(), sizeof(item.uid));
    strncpy(item.timestamp, timestamp.c_str(), sizeof(item.timestamp));
    item.number = number;
    item.removeFromQueue = false;
    item.reserved = false;
    queue.push_back(item);
    save();
  }

  void addIfNew(String uid, String timestamp, int number) {
    if (!exists(uid)) add(uid, timestamp, number);
  }

  void removeByUID(String uid) {
    queue.erase(std::remove_if(queue.begin(), queue.end(),
      [&](QueueItem& item) {
        return String(item.uid) == uid;
      }), queue.end());
    save();
  }

  void reserveUID(String uid) {
    for (auto& item : queue) {
      if (String(item.uid) == uid) {
        item.reserved = true;
        break;
      }
    }
    save();
  }

  QueueItem peek() {
    for (const auto& item : queue) {
      if (!item.reserved) {
        return item;
      }
    }
    QueueItem empty = {};
    return empty;
  }

  bool exists(String uid) {
    for (const auto& item : queue) {
      if (String(item.uid) == uid) return true;
    }
    return false;
  }

  void print() {
    Serial.println("---- SharedQueue ----");
    for (const auto& item : queue) {
      Serial.print("UID: "); Serial.print(item.uid);
      Serial.print(", Num: "); Serial.print(item.number);
      Serial.print(", Reserved: "); Serial.println(item.reserved ? "Yes" : "No");
    }
    Serial.println("---------------------");
  }

private:
  String serializeItem(const QueueItem& item) {
    return String(item.uid) + "," + item.timestamp + "," + String(item.number) + "," + (item.reserved ? "1" : "0");
  }

  void parseItem(const String& str, QueueItem& item) {
    int idx1 = str.indexOf(',');
    int idx2 = str.indexOf(',', idx1 + 1);
    int idx3 = str.indexOf(',', idx2 + 1);

    String uid = str.substring(0, idx1);
    String timestamp = str.substring(idx1 + 1, idx2);
    int number = str.substring(idx2 + 1, idx3).toInt();
    bool reserved = str.substring(idx3 + 1) == "1";

    strncpy(item.uid, uid.c_str(), sizeof(item.uid));
    strncpy(item.timestamp, timestamp.c_str(), sizeof(item.timestamp));
    item.number = number;
    item.removeFromQueue = false;
    item.reserved = reserved;
  }
};

#endif
