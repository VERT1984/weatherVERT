#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Stream.h>
#include "UkraineAlarmDistricts.h"

inline bool uaBackupNamesMatch(const char* left, const char* right) {
  char normalizedLeft[96];
  char normalizedRight[96];
  uaNormalizeAreaName(left, normalizedLeft, sizeof(normalizedLeft));
  uaNormalizeAreaName(right, normalizedRight, sizeof(normalizedRight));
  return strcmp(normalizedLeft, normalizedRight) == 0;
}

// UkraineAlarm /api/v3/alerts повертає перелік локацій, де є активні тривоги.
// Успішний коректний масив без вибраної локації означає відсутність активної
// тривоги в ній. Визначаємо саме AIR; інші типи не запускають повітряний сигнал.
inline bool uaBackupParseActiveAlerts(HTTPClient& http,
                                      const char* target0, uint8_t kind0,
                                      const char* target1, uint8_t kind1,
                                      int levels[2]) {
  // Повний JSON nationwide легко >32 КБ — без фільтра парсер обривався саме
  // тоді, коли резерв найбільш потрібен (багато активних тривог).
  JsonDocument filter;
  filter[0]["regionName"] = true;
  filter[0]["regionType"] = true;
  filter[0]["activeAlerts"][0]["type"] = true;

  JsonDocument doc;
  Stream& networkBody = http.getStream();
  UaHttpBodyStream body(http, networkBody, 8000, 98304, 18000);
  DeserializationError error = deserializeJson(doc, body, DeserializationOption::Filter(filter));
  if (error) {
    Serial.printf("[ukrainealarm] JSON: %s; received=%lu B; heap=%u\n",
                  error.c_str(), (unsigned long)body.bytesRead(), (unsigned)ESP.getFreeHeap());
    return false;
  }

  JsonArray regions = doc.as<JsonArray>();
  if (regions.isNull()) return false;

  levels[0] = 0;
  levels[1] = 0;
  for (JsonObject region : regions) {
    const char* name = region["regionName"] | "";
    const char* type = region["regionType"] | "";
    JsonArray alerts = region["activeAlerts"].as<JsonArray>();
    if (!name[0] || alerts.isNull()) continue;

    for (int slot = 0; slot < 2; slot++) {
      const char* target = slot == 0 ? target0 : target1;
      uint8_t kind = slot == 0 ? kind0 : kind1;
      if (!target || !target[0] || kind == 0) continue;

      bool expectedType = (kind == 1 && strcmp(type, "State") == 0) ||
                          (kind == 2 && (strcmp(type, "District") == 0 ||
                                         strcmp(type, "State") == 0));
      if (!expectedType || !uaBackupNamesMatch(name, target)) continue;

      for (JsonObject alert : alerts) {
        const char* alertType = alert["type"] | "";
        if (strcmp(alertType, "AIR") == 0) {
          levels[slot] = 2;
          break;
        }
      }
    }
  }
  return true;
}
