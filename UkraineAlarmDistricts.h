#pragma once
 
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <string.h>
 
#ifndef UA_MAP_REGION_COUNT
#error Include UkraineAlertMap.h before UkraineAlarmDistricts.h
#endif
 
class UaHttpBodyStream : public Stream {
 public:
  UaHttpBodyStream(HTTPClient& http, Stream& client, uint32_t idleTimeoutMs,
                   size_t maxBytes = 32768, uint32_t maxDurationMs = 20000)
      : _http(http), _client(client), _idleTimeoutMs(idleTimeoutMs), _maxBytes(maxBytes),
        _maxDurationMs(maxDurationMs), _startedAt(millis()) {}
 
  int available() override { return _client.available(); }
 
  int read() override {
    if (!waitForByte()) return -1;
    int value = _client.read();
    if (value >= 0) {
      if (_bytesRead == 0) _firstByte = value;
      _lastByte = value;
      _bytesRead++;
    }
    return value;
  }
 
  int peek() override {
    return waitForByte() ? _client.peek() : -1;
  }
 
  void flush() override {}
  size_t write(uint8_t value) override { return _client.write(value); }
 
  size_t bytesRead() const { return _bytesRead; }
  int firstByte() const { return _firstByte; }
  int lastByte() const { return _lastByte; }
 
 private:
  bool waitForByte() {
    if (_bytesRead >= _maxBytes) return false;
    const unsigned long waitStarted = millis();
    while (_client.available() <= 0) {
      if (!_http.connected()) return false;
      if (_maxDurationMs && millis() - _startedAt >= _maxDurationMs) return false;
      if (millis() - waitStarted >= _idleTimeoutMs) return false;
      delay(1);
    }
    return true;
  }
 
  HTTPClient& _http;
  Stream& _client;
  uint32_t _idleTimeoutMs;
  size_t _maxBytes;
  uint32_t _maxDurationMs;
  uint32_t _startedAt;
  size_t _bytesRead = 0;
  int _firstByte = -1;
  int _lastByte = -1;
};
 
inline uint16_t uaMapRgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
 
inline uint16_t uaDistrictMapColor(uint8_t region, const char* statuses, bool fresh) {
  if (!fresh || region >= UA_MAP_REGION_COUNT || statuses[region] == '\0')
    return uaMapRgb565(105, 108, 115);
  if (statuses[region] == 'R' || statuses[region] == 'A') return uaMapRgb565(220, 35, 35);
  if (statuses[region] == 'Y' || statuses[region] == 'P') return uaMapRgb565(245, 185, 0);
  if (statuses[region] == 'N') return uaMapRgb565(25, 155, 65);
  return uaMapRgb565(105, 108, 115);
}
 
inline bool uaHasPrefix(const char* text, const char* prefix) {
  return strncmp(text, prefix, strlen(prefix)) == 0;
}
 
inline bool uaHasSuffix(const char* text, const char* suffix) {
  const size_t textLength = strlen(text);
  const size_t suffixLength = strlen(suffix);
  return textLength >= suffixLength && strcmp(text + textLength - suffixLength, suffix) == 0;
}
 
inline void uaNormalizeAreaName(const char* source, char* output, size_t capacity) {
  if (capacity == 0) return;
  output[0] = '\0';
  if (!source) return;
 
  while (*source == ' ' || *source == '\t') source++;
  if (uaHasPrefix(source, "м. ")) source += strlen("м. ");
  else if (uaHasPrefix(source, "місто ")) source += strlen("місто ");
  else if (uaHasPrefix(source, "АР ")) source += strlen("АР ");
  else if (uaHasPrefix(source, "Автономна Республіка "))
    source += strlen("Автономна Республіка ");
 
  size_t length = strlen(source);
  if (length >= capacity) length = capacity - 1;
  memcpy(output, source, length);
  output[length] = '\0';
 
  // Єдиний апостроф: API надсилає ' (ASCII), а в назвах карти стоїть ’ (U+2019).
  // Без цього райони Кам'янський, Кам'янець-Подільський, Куп'янський не знаходились на карті.
  {
    char* w = output;
    for (const char* r = output; *r;) {
      const unsigned char a = (unsigned char)r[0], b = (unsigned char)r[1], c = (unsigned char)r[2];
      if (a == 0xE2 && b == 0x80 && (c == 0x99 || c == 0x98)) {  // ’ ‘
        *w++ = '\'';
        r += 3;
      } else if (a == 0xCA && b == 0xBC) {  // ʼ
        *w++ = '\'';
        r += 2;
      } else {
        *w++ = *r++;
      }
    }
    *w = '\0';
    length = strlen(output);
  }
 
  while (length > 0 && (output[length - 1] == ' ' || output[length - 1] == '\t'))
    output[--length] = '\0';
  const char* suffixes[] = { " район", " область", " обл." };
  for (const char* suffix : suffixes) {
    if (uaHasSuffix(output, suffix)) {
      output[strlen(output) - strlen(suffix)] = '\0';
      break;
    }
  }
  length = strlen(output);
  while (length > 0 && (output[length - 1] == ' ' || output[length - 1] == '\t'))
    output[--length] = '\0';
  for (size_t i = 0; i < length; i++) {
    if (output[i] >= 'A' && output[i] <= 'Z') output[i] += 'a' - 'A';
  }
}
 
inline int uaFindMapName(const char* name, const char* const* names, size_t count) {
  char wanted[96];
  uaNormalizeAreaName(name, wanted, sizeof(wanted));
  for (size_t i = 0; i < count; i++) {
    char candidate[96];
    uaNormalizeAreaName(names[i], candidate, sizeof(candidate));
    if (strcmp(wanted, candidate) == 0) return (int)i;
  }
 
  static const char* const legacyNames[][2] = {
    { "Володимир-Волинський", "Володимирський" },
    { "Сєвєродонецький", "Сіверськодонецький" },
    { "Красноградський", "Берестинський" },
    { "Куп'янський", "Куп’янський" },
  };
  for (const auto& alias : legacyNames) {
    if (strcmp(wanted, alias[0]) != 0) continue;
    for (size_t i = 0; i < count; i++) {
      char candidate[96];
      uaNormalizeAreaName(names[i], candidate, sizeof(candidate));
      if (strcmp(alias[1], candidate) == 0) return (int)i;
    }
  }
  return -1;
}
 
inline uint8_t uaAlertLevelRank(char level) {
  if (level == 'R' || level == 'A') return 3;
  if (level == 'Y' || level == 'P') return 2;
  if (level == '?') return 1;
  return 0;
}
 
inline void uaMergeAlertLevel(char& target, char incoming) {
  if (uaAlertLevelRank(incoming) > uaAlertLevelRank(target)) target = incoming;
}
 
// The map has district polygons, so city-only alerts use their containing district.
inline int uaFindCityDistrict(const char* city) {
  static const char* const cityNames[] = {
    "Запоріжжя", "Кривий Ріг", "Нікополь", "Харків", "Світловодськ", "Дніпро",
  };
  static const char* const districtNames[] = {
    "Запорізький район", "Криворізький район", "Нікопольський район", "Харківський район",
    "Олександрійський район", "Дніпровський район",
  };
 
  const int citySlot = uaFindMapName(city, cityNames, sizeof(cityNames) / sizeof(cityNames[0]));
  if (citySlot < 0) return -1;
  return uaFindMapName(districtNames[citySlot], uaMapRegionNames, UA_MAP_REGION_COUNT);
}
 
inline bool uaParseActiveAlerts(HTTPClient& http, char* statuses, size_t capacity) {
  if (!statuses || capacity < UA_MAP_REGION_COUNT + 1) return false;
 
  JsonDocument filter;
  filter["alerts"][0]["alert_type"] = true;
  filter["alerts"][0]["alert_level"] = true;
  filter["alerts"][0]["location_type"] = true;
  filter["alerts"][0]["location_title"] = true;
  filter["alerts"][0]["location_oblast"] = true;
  filter["alerts"][0]["location_raion"] = true;
 
  JsonDocument doc;
  Stream& networkBody = http.getStream();
  UaHttpBodyStream body(http, networkBody, 12000, 65536, 20000);
  DeserializationError error = deserializeJson(
      doc, body, DeserializationOption::Filter(filter));
  if (error || !doc["alerts"].is<JsonArray>()) {
    Serial.printf("[alert-map] JSON: %s; received=%lu B; heap=%u\n",
                  error ? error.c_str() : "у відповіді немає alerts",
                  (unsigned long)body.bytesRead(), (unsigned)ESP.getFreeHeap());
    return false;
  }
 
  char next[UA_MAP_REGION_COUNT + 1];
  char oblastLevels[UA_MAP_STATE_COUNT];
  memset(next, 'N', UA_MAP_REGION_COUNT);
  memset(oblastLevels, 'N', sizeof(oblastLevels));
  next[UA_MAP_REGION_COUNT] = '\0';
  uint16_t redCount = 0, yellowCount = 0, unmatched = 0;
 
  for (JsonObject alert : doc["alerts"].as<JsonArray>()) {
    const char* type = alert["alert_type"] | "";
    if (strcmp(type, "air_raid") != 0) continue;
 
    const char* rawLevel = alert["alert_level"] | "";
    char level = strcmp(rawLevel, "red") == 0 ? 'R'
                 : strcmp(rawLevel, "yellow") == 0 ? 'Y'
                                                    : '?';
    if (level == 'R') redCount++;
    else if (level == 'Y') yellowCount++;
 
    const char* title = alert["location_title"] | "";
    const char* locationType = alert["location_type"] | "";
    const char* oblastName = alert["location_oblast"] | "";
    const char* raionName = alert["location_raion"] | "";
 
    int stateSlot = uaFindMapName(oblastName, uaMapStateNames, UA_MAP_STATE_COUNT);
    if (stateSlot < 0 && strcmp(locationType, "oblast") == 0)
      stateSlot = uaFindMapName(title, uaMapStateNames, UA_MAP_STATE_COUNT);
 
    if (strcmp(locationType, "oblast") == 0 && stateSlot >= 0) {
      uaMergeAlertLevel(oblastLevels[stateSlot], level);
      continue;
    }
 
    int mapSlot = -1;
    if (strcmp(locationType, "raion") == 0)
      mapSlot = uaFindMapName(title, uaMapRegionNames, UA_MAP_REGION_COUNT);
    if (mapSlot < 0 && raionName[0])
      mapSlot = uaFindMapName(raionName, uaMapRegionNames, UA_MAP_REGION_COUNT);
    if (mapSlot < 0)
      mapSlot = uaFindMapName(title, uaMapRegionNames, UA_MAP_REGION_COUNT);
    if (mapSlot < 0 && strcmp(locationType, "city") == 0)
      mapSlot = uaFindCityDistrict(title);
 
    if (mapSlot >= 0) {
      uaMergeAlertLevel(next[mapSlot], level);
    } else {
      Serial.printf("[alert-map] без полігону: type='%s', title='%s', oblast='%s', raion='%s'\n",
                    locationType, title, oblastName, raionName);
      unmatched++;
    }
  }
 
  for (uint8_t i = 0; i < UA_MAP_REGION_COUNT; i++) {
    const uint8_t stateSlot = pgm_read_byte(&uaMapRegionState[i]);
    uaMergeAlertLevel(next[i], oblastLevels[stateSlot]);
  }
 
  if (unmatched) Serial.printf("[alert-map] локацій без відповідника на карті: %u\n", unmatched);
  Serial.printf("[alert-map] активних: червоних %u, жовтих %u\n", redCount, yellowCount);
  memcpy(statuses, next, UA_MAP_REGION_COUNT + 1);
  return true;
}
 
inline int uaKyivOblastAirLevel(const char* statuses) {
  const int kyivOblast = uaFindMapName("Київська область", uaMapStateNames,
                                       UA_MAP_STATE_COUNT);
  if (kyivOblast < 0) return -1;
  int level = 0;
  for (uint8_t i = 0; i < UA_MAP_REGION_COUNT; i++) {
    if (pgm_read_byte(&uaMapRegionState[i]) != kyivOblast) continue;
    if (statuses[i] == 'R' || statuses[i] == 'A') return 2;
    if ((statuses[i] == 'Y' || statuses[i] == 'P') && level < 1) level = 1;
  }
  return level;
}
 