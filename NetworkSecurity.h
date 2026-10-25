#pragma once

#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <WiFi.h>
#include "TlsCertificates.h"

// Один TLS на весь пристрій: погода (loop) і тривоги (окреме ядро) інакше
// одночасно тримають два рукостискання, рвуть Wi-Fi і з'їдають heap.
inline SemaphoreHandle_t httpsMux = nullptr;

inline void httpsLockInit() {
  if (!httpsMux) httpsMux = xSemaphoreCreateMutex();
}

inline bool httpsLock(uint32_t waitMs) {
  if (!httpsMux) return false;
  return xSemaphoreTake(httpsMux, pdMS_TO_TICKS(waitMs)) == pdTRUE;
}

inline void httpsUnlock() {
  if (httpsMux) xSemaphoreGive(httpsMux);
}

inline void tlsClose(NetworkClientSecure& client, HTTPClient& http) {
  http.setReuse(false);
  http.end();
  client.stop();
}

// Увімкнути перевірку сертифіката та обмежити час TLS рукостискання.
// Час на ESP32 має бути синхронізований через NTP до першого HTTPS-запиту.
inline void configureTrustedTls(NetworkClientSecure& client) {
  client.setCACert(TLS_ROOT_CA_BUNDLE);
  client.setHandshakeTimeout(6);
}

inline bool tlsHeapAvailable(const char* service) {
  const uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t largestBlock = ESP.getMaxAllocHeap();
  // Два одночасні TLS більше не запускаємо; 45 КБ вистачає на одне з'єднання.
  if (freeHeap >= 45000 && largestBlock >= 16000) return true;
  Serial.printf("[https] %s пропущено: мало heap (вiльно %u, найбiльший блок %u)\n",
                service, (unsigned)freeHeap, (unsigned)largestBlock);
  return false;
}

// HTTPClient інакше повертає однаковий код -1 для DNS та помилки TCP.
// Перевіряємо DNS окремо, щоб журнал точно показував, де обірвався запит.
inline bool tlsResolveHost(const char* host, char* error, size_t errorSize) {
  IPAddress address;
  if (WiFi.hostByName(host, address)) return true;
  snprintf(error, errorSize, "DNS не знайшов %s", host);
  return false;
}
