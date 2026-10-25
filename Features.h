#pragma once
// =====================================================================
// Features.h — нічний режим, автояскравість (датчик світла), світлодіод тривоги.
//
// ПІДКЛЮЧЕННЯ (допаяти ):
//
//  1) ДАТЧИК СВІТЛА (фоторезистор GL5528 або подібний) — GPIO34
//        3.3V ──[ фоторезистор ]──┬── GPIO34
//                                 └──[ резистор 10 кОм ]── GND
//     (за бажанням конденсатор 100 нФ між GPIO34 і GND — менше шуму)
//     У налаштуваннях на сторінці телефона потім увімкніть "Автояскравість".
//
//  2) ДВОКОЛІРНИЙ СВІТЛОДІОД (червоний + зелений, 3 виводи) — GPIO27 і GPIO32
//        ЗАГАЛЬНИЙ вивід (зазвичай середній) ──────────────── GND
//        вивід "червоний" ──[ 0.5 кОм ]── GPIO27
//        вивід "зелений"  ──[ 0.5 кОм ]── GPIO32
//     Якщо діод "із загальним анодом" (світиться, коли загальний вивід на +3.3V, а решта через
//     резистори на GND), загальний вивід підключіть до 3.3V і поставте LED_COMMON_ANODE 1 у скетчі.
//     Логіка: тривога — червоний (блимає/світить), спокійно — зелений, жовтий рівень — обидва
//     (жовтий/бурштиновий), немає даних — вимкнено. Вночі зелений приглушується.
//     
//     Безпечна розкладка, якщо збирати на S3:
// Що	Було	Для S3
// TFT SCLK / MISO / MOSI	18 -19, / 23-12, / 13 -11
// TFT CS / DC / RST	5 / 22 / 4	10 / 9 / 8
// Тач CS	17 на	14
// Підсвітка	26 на	15
// Зумер	25 на	16
// Світлодіод червоний / зелений	27 / 32 та	17 / 18
// Датчик світла	34 на	4
// =====================================================================
#include <Arduino.h>
#include "Settings.h"

#define NIGHT_FROM_H 22  // нічний режим з 22:00
#define NIGHT_TO_H 7     // до 07:00
#define AUTO_BR_MIN 5   // мінімальна яскравість при автояскравості, %

extern int brightness;        // яскравість із меню (%), визначена в скетчі
extern long cachedTZ;         // зсув часового поясу, с
extern volatile int alertState;
extern bool alarmActive;
extern int alarmKind;

static bool featNight = false;
static int featLdr = 2000;
static int featAppliedBr = -1;
static unsigned long featNightAt = 0;
static bool featForce = false;

inline bool featuresIsNight() {
  return featNight;
}

// Яскравість з урахуванням автояскравості і нічного режиму
inline int effectiveBrightness() {
  int b = brightness;
  if (cfg.autoBr) {
    int v = constrain(featLdr, 150, 3500);
    b = map(v, 150, 3500, AUTO_BR_MIN, max(brightness, AUTO_BR_MIN));
  }
  if (featNight && cfg.nightBr < b) b = cfg.nightBr;
  return constrain(b, 3, 100);
}

void applyBrightness() {
  int b = effectiveBrightness();
  featAppliedBr = b;
  int duty = map(b, 0, 100, 0, 255);
  ledcWrite(BL_PIN, BL_INVERT ? 255 - duty : duty);
}

// Яскравість каналу світлодіода: duty 0–255 (ШІМ). Для загального анода рівень інвертується.
inline void ledWriteDuty(int pin, int duty) {
  ledcWrite(pin, LED_COMMON_ANODE ? 255 - duty : duty);
}

inline void featuresBegin() {
  ledcAttach(LED_PIN, 1000, 8);
  ledcAttach(LED_G_PIN, 1000, 8);
  ledWriteDuty(LED_PIN, 0);
  ledWriteDuty(LED_G_PIN, 0);
  analogSetPinAttenuation(LDR_PIN, ADC_11db);  // діапазон 0–3.3 В
}

// Викликати після зміни налаштувань: нічний режим перераховується одразу
inline void featuresRecheck() {
  featNightAt = 0;
  featForce = true;  // застосувати яскравість одразу, навіть якщо різниця мала
}

inline void featuresTick(unsigned long now) {
  // --- двоколірний світлодіод: тривога — червоний, спокійно — зелений ---
  int rDuty = 0, gDuty = 0;
  int st = alertState;
  bool alarmNow = (st == 2) || (alarmActive && alarmKind == 1);
  if (alarmNow) {
    if (cfg.ledMode == 2) rDuty = 255;
    else if (cfg.ledMode == 1) rDuty = ((now / 500UL) % 2 == 0) ? 255 : 0;
  } else if (st == 3) {  // жовтий рівень: червоний + частина зеленого = бурштиновий
    rDuty = 230;
    gDuty = 250;
  } else if (st == 1 && cfg.ledCalm) {  // спокійно
    gDuty = featNight ? 25 : 255;
  }
  static int rLast = -1, gLast = -1;
  if (rDuty != rLast) {
    ledWriteDuty(LED_PIN, rDuty);
    rLast = rDuty;
  }
  if (gDuty != gLast) {
    ledWriteDuty(LED_G_PIN, gDuty);
    gLast = gDuty;
  }

  bool changed = false;

  // --- датчик світла: двічі на секунду, зі згладжуванням ---
  static unsigned long ldrAt = 0;
  if (cfg.autoBr && now - ldrAt >= 500UL) {
    ldrAt = now;
    featLdr = (featLdr * 7 + analogRead(LDR_PIN)) / 8;
    changed = true;
  }

  // --- нічний режим: перевірка раз на 20 с ---
  if (featNightAt == 0 || now - featNightAt >= 20000UL) {
    featNightAt = now ? now : 1;
    time_t t = time(nullptr);
    if (t > 1577836800L) {  // час синхронізований
      t += cachedTZ;
      tm* p = gmtime(&t);
      bool n = cfg.nightOn && (p->tm_hour >= NIGHT_FROM_H || p->tm_hour < NIGHT_TO_H);
      if (n != featNight) {
        featNight = n;
        changed = true;
      }
    }
  }
  if (!cfg.nightOn && featNight) {
    featNight = false;
    changed = true;
  }

  // не чіпаємо підсвітку під час червоного екрана тривоги (там 100%)
  if (!alarmActive && (featForce || (changed && abs(effectiveBrightness() - featAppliedBr) >= 2))) {
    featForce = false;
    applyBrightness();
  }
}
