#pragma once
// =====================================================================
// WifiPortal.h — власний портал підключення до Wi-Fi.
// Точка доступу з паролем (він у QR-коді) + DNS "усе на 192.168.4.1" + проста сторінка:
// телефон сам відкриває її як "вхід у мережу".
// =====================================================================
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>

#define WP_AP_NAME "ESP32-WeatherStation"
#define WP_TRY_TIMEOUT_MS 20000UL

enum { WP_IDLE = 0, WP_TRYING = 1, WP_OK = 2, WP_FAIL = 3 };

static DNSServer wpDns;
static WebServer wpServer(80);
static bool wpRoutesSet = false;
static volatile int wpState = WP_IDLE;
static unsigned long wpTryStart = 0;
static String wpNets[14];
static int wpNetCount = 0;
static bool wpScanPending = false;
static unsigned long wpLastScanStart = 0;
static void wpStartScan() {
  if (wpScanPending || wpState == WP_TRYING) return;
  WiFi.scanDelete();
  int result = WiFi.scanNetworks(true);
  wpLastScanStart = millis();
  wpScanPending = (result == WIFI_SCAN_RUNNING || result >= 0);
}

static String wpHtmlEsc(const String& s) {
  String o;
  for (unsigned i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '&') o += "&amp;";
    else if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '"') o += "&quot;";
    else o += c;
  }
  return o;
}

// Результати фонового сканування (перевіряємо з wpLoop, нічого не блокує)
static void wpCollectScan() {
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return;
  wpScanPending = false;
  if (n < 0) {
    WiFi.scanDelete();
    Serial.printf("[wifi] scan failed: %d\n", n);
    return;  // Повторна спроба запускається з wpLoop.
  }
  wpNetCount = 0;
  for (int i = 0; i < n && wpNetCount < 14; i++) {
    String s = WiFi.SSID(i);
    if (s.length() == 0) continue;
    bool dup = false;
    for (int j = 0; j < wpNetCount; j++)
      if (wpNets[j] == s) dup = true;
    if (!dup) wpNets[wpNetCount++] = s;
  }
  WiFi.scanDelete();
}

static void wpRedirect() {
  String url = String("http://") + WiFi.softAPIP().toString() + "/";
  wpServer.sendHeader("Location", url, true);
  wpServer.sendHeader("Cache-Control", "no-store");
  wpServer.send(302, "text/plain", "");
}

static const char WP_HEAD[] PROGMEM =
  "<!doctype html><html lang=uk><head><meta charset=utf-8>"
  "<meta name=viewport content='width=device-width,initial-scale=1'>"
  "<title>Wi-Fi</title><style>"
  "body{font:17px system-ui,sans-serif;background:#0b1020;color:#e8ecf5;margin:0 auto;padding:18px;max-width:520px}"
  "h1{font-size:21px}label{display:block;margin:14px 0 5px;font-size:13px;color:#9aa7c7}"
  "input,select{width:100%;box-sizing:border-box;padding:13px;border-radius:10px;border:1px solid #2a3558;background:#121a33;color:#fff;font-size:17px}"
  "button{margin-top:24px;width:100%;padding:15px;border:0;border-radius:12px;background:#2f6bff;color:#fff;font-size:18px;font-weight:600}"
  "a{color:#8fb4ff}</style></head><body>";

static void wpRoot() {
  if (wpServer.hasArg("scan")) wpStartScan();
  String p;
  p.reserve(2600);
  String head = FPSTR(WP_HEAD);
  if (wpScanPending) head.replace("<title>", "<meta http-equiv=refresh content=3><title>");
  p += head;
  p += "<h1>Підключення до Wi-Fi</h1><form method=post action=/connect>";
  p += "<label>Оберіть вашу мережу</label><select name=s>";
  if (wpNetCount == 0) p += "<option value=\"\">&#1052;&#1077;&#1088;&#1077;&#1078;&#1110; &#1097;&#1077; &#1085;&#1077; &#1079;&#1085;&#1072;&#1081;&#1076;&#1077;&#1085;&#1086;</option>";
  for (int i = 0; i < wpNetCount; i++) {
    String e = wpHtmlEsc(wpNets[i]);
    p += "<option value=\"" + e + "\">" + e + "</option>";
  }
  p += "<option value=\"\">— інша мережа (впишіть нижче) —</option></select>";
  p += "<p><a href='/?scan=1'>&#1054;&#1085;&#1086;&#1074;&#1080;&#1090;&#1080; &#1089;&#1087;&#1080;&#1089;&#1086;&#1082; &#1084;&#1077;&#1088;&#1077;&#1078;</a></p>";
  p += "<label>Назва мережі, якщо її немає у списку</label><input name=m autocapitalize=off>";
  p += "<label>Пароль Wi-Fi</label><input name=p type=password autocapitalize=off>";
  p += "<button type=submit>Підключити</button></form></body></html>";
  wpServer.send(200, "text/html; charset=utf-8", p);
}

static void wpConnect() {
  String ssid = wpServer.arg("m");
  ssid.trim();
  if (ssid.length() == 0) ssid = wpServer.arg("s");
  String pwd = wpServer.arg("p");
  if (ssid.length() == 0 || ssid.length() > 32 || pwd.length() > 63) {
    wpRedirect();
    return;
  }
  WiFi.begin(ssid.c_str(), pwd.c_str());  // дані самі збережуться в пам'яті ESP32
  wpState = WP_TRYING;
  wpTryStart = millis();

  String p;
  p += FPSTR(WP_HEAD);
  p += "<h1 id=t>Підключаю…</h1><p id=m>Зачекайте до 20 секунд.</p>"
       "<script>function f(t,e){document.getElementById('t').textContent=t;"
       "document.getElementById('m').innerHTML=e;}"
       "function q(){fetch('/state').then(r=>r.json()).then(d=>{"
       "if(d.s==2)f('Підключено ✓','Дивіться на екран пристрою — там з\\'явиться QR для налаштувань.');"
       "else if(d.s==3)f('Не вдалось','Перевірте пароль. <a href=/>Спробувати ще раз</a>');"
       "else setTimeout(q,1500);}).catch(()=>f('Готово ✓','Схоже, пристрій уже підключився. Дивіться на його екран.'));}"
       "setTimeout(q,2000);</script></body></html>";
  wpServer.send(200, "text/html; charset=utf-8", p);
}

static void wpStateJson() {
  String s = "{\"s\":";
  s += (int)wpState;
  s += "}";
  wpServer.send(200, "application/json", s);
}

inline void wpStart(const char* apPass) {
  WiFi.mode(WIFI_AP_STA);
  wpNetCount = 0;
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  WiFi.softAP(WP_AP_NAME, apPass);
  delay(200);

  wpDns.setErrorReplyCode(DNSReplyCode::NoError);
  wpDns.start(53, "*", WiFi.softAPIP());

  if (!wpRoutesSet) {
    wpServer.on("/", HTTP_GET, wpRoot);
    wpServer.on("/connect", HTTP_POST, wpConnect);
    wpServer.on("/state", HTTP_GET, wpStateJson);
    // адреси, якими телефони та комп'ютери перевіряють "чи є інтернет" → показуємо нашу сторінку
    const char* probes[] = { "/generate_204", "/gen_204", "/hotspot-detect.html", "/library/test/success.html",
                             "/connecttest.txt", "/ncsi.txt", "/redirect", "/fwlink", "/canonical.html", "/success.txt" };
    for (const char* u : probes) wpServer.on(u, HTTP_ANY, wpRedirect);
    wpServer.onNotFound(wpRedirect);
    wpRoutesSet = true;
  }
  wpServer.begin();
  wpState = WP_IDLE;
  wpStartScan();  // фонове сканування: екран одразу реагує на дотик
  Serial.printf("[wifi] портал запущено: %s, IP %s, мереж у списку: %d\n",
                WP_AP_NAME, WiFi.softAPIP().toString().c_str(), wpNetCount);
}

// Викликати часто (з loop). Повертає поточний стан підключення.
inline int wpLoop() {
  wpDns.processNextRequest();
  wpServer.handleClient();
  if (wpScanPending) wpCollectScan();
  else if (millis() - wpLastScanStart >= 15000UL) wpStartScan();
  if (wpState == WP_TRYING) {
    if (WiFi.status() == WL_CONNECTED) {
      wpState = WP_OK;
      Serial.printf("[wifi] підключено до %s, IP %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
    } else if (millis() - wpTryStart > WP_TRY_TIMEOUT_MS) {
      wpState = WP_FAIL;
      WiFi.disconnect(false);
      Serial.println("[wifi] підключення не вдалось (пароль?)");
    }
  }
  return wpState;
}

inline void wpStop() {
  wpScanPending = false;
  WiFi.scanDelete();
  wpServer.stop();
  wpDns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  if (WiFi.status() != WL_CONNECTED && wpState != WP_FAIL) WiFi.reconnect();
}
