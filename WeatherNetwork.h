#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include "NetworkSecurity.h"

// Спільний захищений HTTP GET для двох OpenWeatherMap запитів.
// Клієнт і HTTPClient передає головний скетч, щоб кожен запит завершувався
// його звичним http.end() і не залишав сокет відкритим.
inline bool weatherHttpsGet(HTTPClient& http, NetworkClientSecure& tls,
                            const String& url, bool clockReady) {
  if (!clockReady) {
    Serial.println("[https] запит пропущено: очікування синхронізації часу NTP");
    return false;
  }
  if (!tlsHeapAvailable("OpenWeatherMap")) return false;
  http.setReuse(false);
  http.begin(tls, url);
  http.useHTTP10(true);
  http.setTimeout(5000);
  http.setConnectTimeout(3000);
  int code = http.GET();
  if (code != 200) {
    Serial.printf("[https] OpenWeatherMap HTTP %d (%s)\n", code,
                  HTTPClient::errorToString(code).c_str());
  }
  return code == 200;
}
