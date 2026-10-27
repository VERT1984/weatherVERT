#pragma once
// =====================================================================
// Settings.h — усі налаштування пристрою в пам'яті NVS (Preferences).
// Нічого секретного в коді немає: ключі вводить користувач
// через веб-сторінку (QR-код на екрані).
// =====================================================================
#include <Arduino.h>
#include <Preferences.h>

struct AppCfg {
  char owmKey[48];        // ключ OpenWeatherMap
  char alertsToken[80];   // токен alerts.in.ua
  char ukAlarmToken[80];  // токен ukrainealarm.com (для резервного джерела, Етап 2)
  char weatherCity[40];   // місто для погоди латиницею, напр. "Kyiv,UA"
  char weatherTitle[24];  // як підписати погоду на екрані
  uint8_t locKind[2];     // 0 — не задано, 1 — область, 2 — район
  int16_t locIdx[2];      // номер у uaMapStateNames (область) або uaMapRegionNames (район)
  uint32_t cityUid[2];    // необов'язково: UID міста в alerts.in.ua (точніший статус міста), 0 — немає
  char locTitle[2][52];   // підпис локації на екрані
  char alarmText[64];     // текст на весь екран при тривозі
  char pin[8];            // PIN доступу до веб-сторінки (є в QR-коді)
  uint8_t pressMm;        // 1 — тиск у мм рт. ст., 0 — у гПа
  uint8_t nightOn;        // 1 — нічний режим увімкнено (22:00–07:00)
  uint8_t nightBr;        // яскравість вночі, %
  uint8_t autoBr;         // 1 — автояскравість за датчиком світла
  uint8_t ledMode;        // червоний при тривозі: 0 — вимк., 1 — блимає, 2 — світить
  uint8_t ledCalm;        // 1 — зелений світиться, коли спокійно
  uint8_t minuteSilence;  // щоденний тихий сигнал о 09:00
};

AppCfg cfg;
portMUX_TYPE cfgMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool cfgDirty = false;  // веб-сторінка збереглася → головний цикл оновлює екран

// Безпечне копіювання UTF-8 рядка: не рве кирилицю посередині літери
inline void cfgCopyUtf8(char* dst, size_t cap, const char* src) {
  if (cap == 0) return;
  size_t n = strlen(src);
  if (n >= cap) n = cap - 1;
  while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80) n--;  // src[n] — продовження літери → відступаємо
  memcpy(dst, src, n);
  dst[n] = '\0';
}

// Шрифти u8g2 на екрані не мають української «і»: замінюємо на латинську (як у решті скетча)
inline void cfgScreenText(char* s) {
  char* w = s;
  for (const char* r = s; *r;) {
    unsigned char a = (unsigned char)r[0], b = (unsigned char)r[1];
    if (a == 0xD1 && b == 0x96) { *w++ = 'i'; r += 2; }        // і
    else if (a == 0xD0 && b == 0x86) { *w++ = 'I'; r += 2; }   // І
    else *w++ = *r++;
  }
  *w = '\0';
}

inline void cfgGet(Preferences& p, const char* key, char* dst, size_t cap) {
  String v = p.getString(key, "");
  cfgCopyUtf8(dst, cap, v.c_str());
}

inline void cfgSave(const AppCfg& c) {
  Preferences p;
  if (!p.begin("ws", false)) return;
  p.putString("owm", c.owmKey);
  p.putString("alerts", c.alertsToken);
  p.putString("uk", c.ukAlarmToken);
  p.putString("city", c.weatherCity);
  p.putString("wt", c.weatherTitle);
  p.putString("txt", c.alarmText);
  p.putString("pin", c.pin);
  p.putUChar("pm", c.pressMm);
  p.putUChar("no", c.nightOn);
  p.putUChar("nb", c.nightBr);
  p.putUChar("ab", c.autoBr);
  p.putUChar("lm", c.ledMode);
  p.putUChar("lg", c.ledCalm);
  p.putUChar("ms", c.minuteSilence);
  for (int n = 0; n < 2; n++) {
    char k[8];
    snprintf(k, sizeof(k), "k%d", n);
    p.putUChar(k, c.locKind[n]);
    snprintf(k, sizeof(k), "i%d", n);
    p.putShort(k, c.locIdx[n]);
    snprintf(k, sizeof(k), "u%d", n);
    p.putUInt(k, c.cityUid[n]);
    snprintf(k, sizeof(k), "t%d", n);
    p.putString(k, c.locTitle[n]);
  }
  p.end();
}

inline void cfgLoad() {
  memset(&cfg, 0, sizeof(cfg));
  cfg.pressMm = 1;  // значення за замовчуванням
  cfg.nightOn = 1;
  cfg.nightBr = 15;
  cfg.autoBr = 0;
  cfg.ledMode = 1;
  cfg.ledCalm = 1;
  cfg.minuteSilence = 1;
  Preferences p;
  if (p.begin("ws", false)) {
    cfgGet(p, "owm", cfg.owmKey, sizeof(cfg.owmKey));
    cfgGet(p, "alerts", cfg.alertsToken, sizeof(cfg.alertsToken));
    cfgGet(p, "uk", cfg.ukAlarmToken, sizeof(cfg.ukAlarmToken));
    cfgGet(p, "city", cfg.weatherCity, sizeof(cfg.weatherCity));
    cfgGet(p, "wt", cfg.weatherTitle, sizeof(cfg.weatherTitle));
    cfgGet(p, "txt", cfg.alarmText, sizeof(cfg.alarmText));
    cfgGet(p, "pin", cfg.pin, sizeof(cfg.pin));
    cfg.pressMm = p.getUChar("pm", cfg.pressMm);
    cfg.nightOn = p.getUChar("no", cfg.nightOn);
    cfg.nightBr = constrain((int)p.getUChar("nb", cfg.nightBr), 3, 100);
    cfg.autoBr = p.getUChar("ab", cfg.autoBr);
    cfg.ledMode = p.getUChar("lm", cfg.ledMode);
    cfg.ledCalm = p.getUChar("lg", cfg.ledCalm);
    cfg.minuteSilence = p.getUChar("ms", cfg.minuteSilence);
    for (int n = 0; n < 2; n++) {
      char k[8];
      snprintf(k, sizeof(k), "k%d", n);
      cfg.locKind[n] = p.getUChar(k, 0);
      snprintf(k, sizeof(k), "i%d", n);
      cfg.locIdx[n] = p.getShort(k, 0);
      snprintf(k, sizeof(k), "u%d", n);
      cfg.cityUid[n] = p.getUInt(k, 0);
      snprintf(k, sizeof(k), "t%d", n);
      cfgGet(p, k, cfg.locTitle[n], sizeof(cfg.locTitle[n]));
    }
    p.end();
  }
  if (cfg.alarmText[0] == '\0') strlcpy(cfg.alarmText, "ТРИВОГА!", sizeof(cfg.alarmText));
  if (cfg.pin[0] == '\0') {  // перший запуск: випадковий PIN із 6 цифр
    snprintf(cfg.pin, sizeof(cfg.pin), "%06lu", (unsigned long)(esp_random() % 1000000UL));
    cfgSave(cfg);
  }
}

// Мінімум для роботи: ключі + хоч одна локація тривоги
inline bool cfgComplete() {
  return cfg.owmKey[0] && cfg.alertsToken[0] && cfg.locKind[0] != 0;
}

// ---------- яскравість і фон (замість bright.txt / bg.txt) ----------
inline void cfgLoadUi(int& brightness, int& r, int& g, int& b) {
  Preferences p;
  if (!p.begin("ws", false)) return;
  int v = p.getInt("br", brightness);
  if (v >= 10 && v <= 100) brightness = v;
  r = constrain(p.getInt("bgr", r), 0, 255);
  g = constrain(p.getInt("bgg", g), 0, 255);
  b = constrain(p.getInt("bgb", b), 0, 255);
  p.end();
}

inline void cfgSaveBrightness(int brightness) {
  Preferences p;
  if (!p.begin("ws", false)) return;
  p.putInt("br", brightness);
  p.end();
}

inline void cfgSaveBg(int r, int g, int b) {
  Preferences p;
  if (!p.begin("ws", false)) return;
  p.putInt("bgr", r);
  p.putInt("bgg", g);
  p.putInt("bgb", b);
  p.end();
}

// Скидання до заводських: стирає всі налаштування (ключі, локації, яскравість, фон)
inline void cfgFactoryReset() {
  Preferences p;
  if (p.begin("ws", false)) {
    p.clear();
    p.end();
  }
}
