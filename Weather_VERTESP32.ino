// =====================================================================
// ПОГОДНА СТАНЦІЯ — ESP32
// Плата в Arduino IDE: ESP32 Dev Module, схема розділів Huge APP (3MB No OTA)
// Ядро: arduino-esp32 3.x, ArduinoJson 7.x
// Бібліотеки: Adafruit GFX, Adafruit ILI9341, U8g2_for_Adafruit_GFX,
//             XPT2046_Touchscreen (Paul Stoffregen), Time (TimeLib),
//             WiFiManager (tzapu), ArduinoJson, QR-код ESP-IDF з ESP32 core
// У папці скетча: image_data.h, UkraineAlertMap.h, UkraineAlarmDistricts.h, WeatherNetwork.h WifiPortal.h
//                 Settings.h, WebConfig.h, NetworkSecurity.h, TlsCertificates.h, UkraineAlarmBackup.h
// =====================================================================
#include <FS.h>
#include <LittleFS.h>
#include <SPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <esp_task_wdt.h>
#include <DNSServer.h>
#include <WebServer.h>
#include "WifiPortal.h"
#include <qrcode.h>
#include <TimeLib.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <XPT2046_Touchscreen.h>
#include "image_data.h"
#include "UkraineAlertMap.h"
#include "Settings.h"
#include "WebConfig.h"
#include "Notes.h"
#include "NetworkSecurity.h"
#include "WeatherNetwork.h"
#include "UkraineAlarmDistricts.h"
#include "UkraineAlarmBackup.h"

// =====================================================================
// КОНФІГУРАЦІЯ ЧАСУ ТА ПІНІВ
// =====================================================================
#define TZ_SUMMER 10800  // UTC+3
#define TZ_WINTER 7200   // UTC+2

#define TFT_CS 5
#define TFT_RST 4
#define TFT_DC 22
#define TFT_MOSI 23
#define TFT_MISO 19
#define TFT_SCLK 18

#define TOUCH_CS 17

#define ALERT_POLL_MS 10000
#define ALERT_KEEPALIVE 0
#define MAP_FRESH_MS 180000UL
#define BUZZER_PIN 25
#define BUZZER_FREQ 1700
#define BUZZER_ACTIVE 0

#define BL_PIN 26
#define BL_INVERT 0

// ДОДАТКОВІ ПІДКЛЮЧЕННЯ світлодіод
#define LED_PIN 27
#define LED_G_PIN 32
#define LED_COMMON_ANODE 0
#define LDR_PIN 34
#include "Features.h"  // нічний режим 22:00–07:00, автояскравість, світлодіод

// =====================================================================
// РОЗМІРИ ТА ПОЗИЦІЇ
// =====================================================================
#define IMG_WIDTH 60
#define IMG_HEIGHT 60
#define RGB565(r, g, b) ((((r)&0xF8) << 8) | (((g)&0xFC) << 3) | ((b) >> 3))

int bgR = 0, bgG = 0, bgB = 40;
int bgAR = 0, bgAG = 0, bgAB = 40;
uint16_t bgColor = RGB565(0, 0, 40);
#define BACKGROUND_COLOR bgColor

#define TIME_Y 60
#define TEMP_X 10
#define TEMP_Y 10
// Рядки погоди ліворуч
#define WX_Y_SUN 86
#define WX_Y_PRESS 99
#define WX_Y_HUM 112
#define WX_Y_WIND 125
#define WX_Y_AQI 138

#define SCREEN_WIDTH 160
#define SCREEN_HEIGHT 85
#define DAY_WIDTH (SCREEN_WIDTH / 7)
#define DAY_HEIGHT (SCREEN_HEIGHT / 5)

#define FORECAST_Y 160
#define FORECAST_ITEM_W 64

#define WIFI_CONNECT_TIMEOUT 30
#define WIFI_MANUAL_PORTAL_TIMEOUT 180
#define SWIPE_THRESHOLD 300

#define CAL_X1 155
#define CAL_Y1 70
#define CAL_X2 320
#define CAL_Y2 165

#define CLK_X1 95
#define CLK_Y1 20
#define CLK_X2 232
#define CLK_Y2 62

#define MENU_BTN_X 254
#define MENU_BTN_Y 4
#define MENU_BTN_W 62
#define MENU_BTN_H 62

#define MENU_X1 244
#define MENU_Y1 0
#define MENU_X2 320
#define MENU_Y2 70

#define WX_X1 0
#define WX_Y1 0
#define WX_X2 94
#define WX_Y2 78

#define ALERT_X1 0
#define ALERT_Y1 138
#define ALERT_X2 152
#define ALERT_Y2 159

#define TS_MINX 200
#define TS_MAXX 3800
#define TS_MINY 200
#define TS_MAXY 3800
#define TS_INVERT_X 1
#define TS_INVERT_Y 1

// =====================================================================
// КОЛЬОРИ
// =====================================================================
#define BLACK 0x0000
#define WHITE 0xFFFF
#define RED 0xF800
#define GREEN 0x07E0
#define DARKGREEN 0x03E0
#define MAGENTA 0xF81F
#define ORANGE 0xFBE0
#define GREY 0x5AEB
#define CYAN 0x07FF
#define YELLOW 0xFFE0

// =====================================================================
// ОБ'ЄКТИ ТА ЗМІННІ
// =====================================================================
HTTPClient httpClient;
NetworkClientSecure weatherTlsClient;

class SntpClock {
  long offset = 0;
public:
  void begin() {
    configTime(0, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  }
  void setTimeOffset(long o) {
    offset = o;
  }
  bool valid() {
    return time(nullptr) > 1577836800L;
  }
  unsigned long getEpochTime() {
    return (unsigned long)(time(nullptr) + offset);
  }
  bool update() {
    return valid();
  }
  bool forceUpdate() {
    for (int i = 0; i < 150 && !valid(); i++) delay(100);
    return valid();
  }
};

SntpClock timeClient;
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);
U8G2_FOR_ADAFRUIT_GFX u8g2Fonts;
XPT2046_Touchscreen ts(TOUCH_CS);

bool calendarFullscreen = false;
bool clockFullscreen = false;
char cfTime[6] = "", cfSec[3] = "", cfDow[24] = "", cfDate[24] = "";
int menuState = 0;
int brightness = 100;
int shownAlertState = -1;
int calDisplayMonth = 1;
int calDisplayYear = 2026;

bool touchTracking = false;
int touchStartRawX = 0, touchCurrRawX = 0;
int touchStartRawY = 0, touchCurrRawY = 0;

char lastHours[3] = "", lastMinutes[3] = "", lastSeconds[3] = "";
char lastDayOfWeek[24] = "";
char lastTemperature[16] = "";
char lastSunrise[16] = "", lastSunset[16] = "";
char lastPressure[16] = "", lastHumidity[16] = "", lastWindSpeed[16] = "";
char lastWeatherCondition[32] = "";

int currentYear = 0;
int today = 0;

unsigned long lastWeatherUpdate = 0;
unsigned long lastCalendarUpdate = 0;
unsigned long lastNTPUpdate = 0;
unsigned long lastTouchCheck = 0;
unsigned long lastTimeDraw = 0;
unsigned long lastDSTCheck = 0;
long cachedTZ = TZ_WINTER;

void displayCalendar();
void drawMenuButton();
void drawAlertStatus();
void loadBrightness();
void applyBrightness();
void returnToMain();
void restoreAfterAlarm();
void drawSystem();
void loadBg();
void drawBgScreen();
void drawBgSlider(int ch);
void drawBgPreview();
void setBgFromTouch(int sx, int sy);
void drawAlertHist();
void loadAlertLog();
void drawWifiConfigScreen(const char* portalPassword, bool manual);
void drawSettingsQr();
void startAlarmScreen(int kind);
void alarmTick(unsigned long now);
bool httpGetOK(const String& url);
extern bool alarmActive;
char bootPortalPass[12] = "";

// =====================================================================
// ДОПОМІЖНІ ФУНКЦІЇ ДЛЯ ТЕКСТУ З АВТОМАТИЧНИМ ФОНОМ (ОБТІКАННЯМ)
// =====================================================================
void drawText(int x, int y, const char* txt, uint16_t fgColor, uint16_t bgColorOverride = BACKGROUND_COLOR) {
  int tw = u8g2Fonts.getUTF8Width(txt);
  int fontAscent = 12;  // Орієнтовна висота шрифту над базовою лінією
  int fontHeight = 16;
  tft.fillRect(x, y - fontAscent, tw + 2, fontHeight, bgColorOverride);
  u8g2Fonts.setBackgroundColor(bgColorOverride);
  u8g2Fonts.setForegroundColor(fgColor);
  u8g2Fonts.setCursor(x, y);
  u8g2Fonts.print(txt);
}

void drawTextCentered(int cx, int y, const char* txt, uint16_t fgColor, uint16_t bgColorOverride = BACKGROUND_COLOR) {
  int tw = u8g2Fonts.getUTF8Width(txt);
  int x = cx - tw / 2;
  int fontAscent = 12;
  int fontHeight = 16;
  tft.fillRect(x - 2, y - fontAscent, tw + 4, fontHeight, bgColorOverride);
  u8g2Fonts.setBackgroundColor(bgColorOverride);
  u8g2Fonts.setForegroundColor(fgColor);
  u8g2Fonts.setCursor(x, y);
  u8g2Fonts.print(txt);
}

int daysInMonth(int y, int m) {
  if (m == 2) {
    bool leap = (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
    return leap ? 29 : 28;
  }
  if (m == 4 || m == 6 || m == 9 || m == 11) return 30;
  return 31;
}

time_t lastSundayAt1UTC(int y, int m) {
  tmElements_t tm;
  tm.Year = CalendarYrToTm(y);
  tm.Month = m;
  tm.Day = daysInMonth(y, m);
  tm.Hour = 1;
  tm.Minute = 0;
  tm.Second = 0;
  time_t t = makeTime(tm);
  while (weekday(t) != 1) t -= 86400UL;
  return t;
}

bool isUkraineSummerTime(time_t utcNow) {
  int y = year(utcNow);
  time_t springFwd = lastSundayAt1UTC(y, 3);
  time_t fallBack = lastSundayAt1UTC(y, 10);
  return (utcNow >= springFwd && utcNow < fallBack);
}

long currentTZOffset() {
  return cachedTZ;
}

void updateDSTCache() {
  time_t utcNow = (time_t)timeClient.getEpochTime() - cachedTZ;
  long newTZ = isUkraineSummerTime(utcNow) ? TZ_SUMMER : TZ_WINTER;
  if (newTZ != cachedTZ) {
    cachedTZ = newTZ;
    timeClient.setTimeOffset(newTZ);
    setTime((time_t)timeClient.getEpochTime());
    currentYear = year();
    today = day();
    Serial.printf("[DST] зміна офсету -> %lds (%s)\n", newTZ, newTZ == TZ_SUMMER ? "літо" : "зима");
  }
}

void updateNTPTime() {
  timeClient.update();
  timeClient.setTimeOffset(cachedTZ);
  time_t newEpoch = (time_t)timeClient.getEpochTime();
  if (newEpoch < (time_t)(1577836800UL + cachedTZ)) return;
  setTime(newEpoch);
  int prevYear = currentYear;
  int prevToday = today;
  currentYear = year();
  today = day();
  if (currentYear != prevYear || today != prevToday) {
    displayCalendar();
  }
}

static uint16_t iconBuf[IMG_WIDTH * IMG_HEIGHT];
void drawIcon(int x, int y, const uint16_t* img) {
  for (int i = 0; i < IMG_WIDTH * IMG_HEIGHT; i++) {
    uint16_t c = img[i];
    iconBuf[i] = (c == 0x0000) ? BACKGROUND_COLOR : c;
  }
  tft.drawRGBBitmap(x, y, iconBuf, IMG_WIDTH, IMG_HEIGHT);
}

void displayWeatherIcon(const char* cond, int x, int y, bool night = false) {
  if (strcmp(cond, "Clear") == 0) drawIcon(x, y, night ? img_150 : img_100);
  else if (strcmp(cond, "Clouds") == 0) drawIcon(x, y, night ? img_154 : img_102);
  else if (strcmp(cond, "Cloudy Day") == 0) drawIcon(x, y, night ? img_154 : img_104);
  else if (strcmp(cond, "Clear Night") == 0) drawIcon(x, y, img_150);
  else if (strcmp(cond, "Cloudy Night") == 0) drawIcon(x, y, img_154);
  else if (strcmp(cond, "Thunderstorm") == 0) drawIcon(x, y, img_303);
  else if (strcmp(cond, "Rain") == 0) drawIcon(x, y, img_306);
  else if (strcmp(cond, "Shower Rain") == 0) drawIcon(x, y, img_310);
  else if (strcmp(cond, "Hail") == 0) drawIcon(x, y, img_313);
  else if (strcmp(cond, "Snow") == 0) drawIcon(x, y, img_403);
  else if (strcmp(cond, "Drizzle") == 0) drawIcon(x, y, img_404);
  else drawIcon(x, y, img_NA);
}

int condPriority(const char* cond) {
  if (strcmp(cond, "Thunderstorm") == 0) return 7;
  if (strcmp(cond, "Snow") == 0) return 6;
  if (strcmp(cond, "Hail") == 0) return 5;
  if (strcmp(cond, "Rain") == 0) return 4;
  if (strcmp(cond, "Shower Rain") == 0) return 3;
  if (strcmp(cond, "Drizzle") == 0) return 2;
  if (strcmp(cond, "Clouds") == 0) return 1;
  return 0;
}

void drawForecastItem(int index, const char* condition, int tempMin, int tempMax, const char* dayName) {
  int x = index * FORECAST_ITEM_W + 1;
  int y = FORECAST_Y;
  tft.fillRect(x, y, IMG_WIDTH, IMG_HEIGHT + 35, BACKGROUND_COLOR);

  u8g2Fonts.setFont(u8g2_font_6x13_t_cyrillic);
  int tw = u8g2Fonts.getUTF8Width(dayName);
  drawText(x + (IMG_WIDTH - tw) / 2, y + 10, dayName, CYAN, BACKGROUND_COLOR);

  displayWeatherIcon(condition, x, y + 12);

  char buf[12];
  snprintf(buf, sizeof(buf), "%d/%d", tempMin, tempMax);
  tw = u8g2Fonts.getUTF8Width(buf);
  drawText(x + (IMG_WIDTH - tw) / 2, y + IMG_HEIGHT + 18, buf, GREEN, BACKGROUND_COLOR);
}

struct FcItem {
  char cond[16];
  int tmin;
  int tmax;
  char name[16];
};
FcItem fcCache[5];
int fcCount = 0;

struct HrItem {
  char cond[16];
  int temp;
  int hh;
  int mm;
  bool night;
};
HrItem hrCache[5];
int hrCount = 0;
bool hourlyScreen = false;

void displayForecastItem(int index, const char* condition, int tempMin, int tempMax, const char* dayName) {
  if (index >= 0 && index < 5) {
    strlcpy(fcCache[index].cond, condition, sizeof(fcCache[index].cond));
    fcCache[index].tmin = tempMin;
    fcCache[index].tmax = tempMax;
    strlcpy(fcCache[index].name, dayName, sizeof(fcCache[index].name));
    if (index + 1 > fcCount) fcCount = index + 1;
  }
  drawForecastItem(index, condition, tempMin, tempMax, dayName);
}

void displayCalendar() {
  if (currentYear < 2020) return;
  tft.fillRect(160, 80, 160, 80, BACKGROUND_COLOR);

  int m = month();
  int dim = daysInMonth(currentYear, m);
  tmElements_t tm;
  tm.Year = CalendarYrToTm(currentYear);
  tm.Month = m;
  tm.Day = 1;
  tm.Hour = 0;
  tm.Minute = 0;
  tm.Second = 0;

  int fdow = weekday(makeTime(tm));
  fdow = (fdow == 1) ? 7 : fdow - 1;

  int x = 160 + ((fdow - 1) * DAY_WIDTH);
  int y = 85;

  u8g2Fonts.setFont(u8g2_font_6x13_t_cyrillic);

  for (int d = 1; d <= dim; d++) {
    bool isToday = (d == today && m == month() && currentYear == year());
    int col = (fdow - 1 + d - 1) % 7;
    uint16_t fColor = isToday ? GREEN : (col == 6 ? RED : WHITE);

    char dStr[4];
    itoa(d, dStr, 10);
    drawText(x, y, dStr, fColor, BACKGROUND_COLOR);

    x += DAY_WIDTH;
    if ((d + fdow - 1) % 7 == 0) {
      x = 160;
      y += DAY_HEIGHT;
    }
  }
}

void displayCalendarFullscreen(int displayMonth, int displayYear) {
  tft.fillScreen(BACKGROUND_COLOR);
  int dim = daysInMonth(displayYear, displayMonth);
  const char* monthNames[] = {
    "Сiчень", "Лютий", "Березень", "Квiтень", "Травень", "Червень",
    "Липень", "Серпень", "Вересень", "Жовтень", "Листопад", "Грудень"
  };

  u8g2Fonts.setFont(u8g2_font_10x20_t_cyrillic);
  String lbl = String(monthNames[displayMonth - 1]) + " " + String(displayYear);
  drawTextCentered(160, 20, lbl.c_str(), CYAN, BACKGROUND_COLOR);

  u8g2Fonts.setFont(u8g2_font_6x13_t_cyrillic);
  drawText(4, 8, "<", GREY, BACKGROUND_COLOR);
  drawText(306, 8, ">", GREY, BACKGROUND_COLOR);

  const char* dayHdrs[] = { "Пн", "Вт", "Ср", "Чт", "Пт", "Сб", "Нд" };
  const int cellW = 320 / 7;
  const int cellH = 34;
  const int startY = 34;

  for (int i = 0; i < 7; i++) {
    drawText(i * cellW + 4, startY + 10, dayHdrs[i], i >= 5 ? RED : DARKGREEN, BACKGROUND_COLOR);
  }
  tft.drawFastHLine(0, startY + 14, 320, GREY);

  tmElements_t tm;
  tm.Year = CalendarYrToTm(displayYear);
  tm.Month = displayMonth;
  tm.Day = 1;
  tm.Hour = 0;
  tm.Minute = 0;
  tm.Second = 0;

  int firstWday = weekday(makeTime(tm));
  int startCol = (firstWday == 1) ? 6 : firstWday - 2;
  int x = startCol * cellW;
  int y = startY + cellH;

  u8g2Fonts.setFont(u8g2_font_10x20_t_cyrillic);
  for (int d = 1; d <= dim; d++) {
    bool isToday = (d == today && displayMonth == month() && displayYear == year());
    uint16_t fColor;
    if (isToday) fColor = GREEN;
    else {
      int col = (startCol + d - 1) % 7;
      fColor = (col >= 5 ? RED : WHITE);
    }
    int numberX = (d < 10) ? x + 14 : x + 5;
    char dStr[4];
    itoa(d, dStr, 10);
    drawText(numberX, y, dStr, fColor, BACKGROUND_COLOR);

    x += cellW;
    if ((d + startCol) % 7 == 0) {
      x = 0;
      y += cellH;
    }
  }

  u8g2Fonts.setFont(u8g2_font_6x13_t_cyrillic);
  drawTextCentered(160, 232, "< свайп > мiсяць    тап  назад", GREY, BACKGROUND_COLOR);
    notesDrawCalendar(displayMonth);
}

bool httpGetOK(const String& url) {
  return weatherHttpsGet(httpClient, weatherTlsClient, url, timeClient.valid());
}

void showNoInternet() {
  tft.fillScreen(BACKGROUND_COLOR);
  u8g2Fonts.setFont(u8g2_font_10x20_t_cyrillic);
  drawTextCentered(160, 110, "Пiдключiть до iнтернету", RED, BACKGROUND_COLOR);
}

const uint16_t BEEP_ALERT[] = {
  150, 150, 150, 150, 150, 450,
  450, 150, 450, 150, 450, 450,
  150, 150, 150, 150, 150, 0
};
const uint16_t BEEP_CLEAR[] = { 1500, 0 };
const uint16_t* beepPat = nullptr;
int beepIdx = 0;
unsigned long beepNext = 0;
bool minuteSilenceActive = false, minuteSilencePulse = false;
unsigned long minuteSilenceEndsAt = 0, minuteSilenceNextAt = 0, minuteSilencePulseEndsAt = 0;
uint32_t minuteSilenceDayKey = 0;
volatile int alertState = 0;
volatile int alertEvent = 0;
volatile int alertLastCode = 0;
char compactAlertStatuses[UA_MAP_REGION_COUNT + 1] = "";
char mapDrawnStatuses[UA_MAP_REGION_COUNT + 1] = "";
bool mapDrawnFresh = false;
volatile unsigned long alertLastOkMs = 0;
volatile unsigned long cityLastOkMs = 0;
volatile unsigned long cityLastOkMsBySlot[2] = { 0, 0 };
volatile int alertErrCount = 0;
volatile int alertLastErr = 0;
volatile unsigned long backupLastOkMs = 0;
volatile int backupErrCount = 0;
volatile int backupLastCode = 0;
volatile bool backupUsingSlot[2] = { false, false };
TaskHandle_t alertTaskHandle = NULL;
volatile bool wifiPortalActive = false;
unsigned long wifiPortalStartedMs = 0;

int sysScroll = 0;
int sysLastSy = -1;
unsigned long lastSysDraw = 0;
bool sysForce = true;
unsigned long sysBuiltMs = 0;

#define ALERT_LOG_MAX 20
struct AlertEv {
  uint32_t t;
  uint8_t lvl;
};
AlertEv alertLog[2][ALERT_LOG_MAX];
int alertLogCount[2] = { 0, 0 };
volatile int alertCurLevel[2] = { -1, -1 };
portMUX_TYPE alertLogMux = portMUX_INITIALIZER_UNLOCKED;
bool alertHistScreen = false;

unsigned long loopCountTmp = 0, loopRate = 0, loopMaxTmp = 0, loopMaxGap = 0;
unsigned long lastRateMs = 0, prevLoopMs = 0;
char lastRestartWhy[64] = "";
int wifiDrops = 0;
volatile uint8_t lastWifiDiscReason = 0;
char lastWifiEvent[56] = "-";

const char* wifiDiscWhy(uint8_t r) {
  switch (r) {
    case WIFI_REASON_AUTH_EXPIRE: return "роутер скинув сесiю";
    case WIFI_REASON_AUTH_LEAVE: return "роутер вiдключив";
    case WIFI_REASON_ASSOC_EXPIRE: return "сесiя прострочена";
    case WIFI_REASON_ASSOC_LEAVE: return "штатний disconnect";
    case WIFI_REASON_AUTH_FAIL: return "пароль / auth";
    case WIFI_REASON_NO_AP_FOUND: return "AP не видно (антена)";
    case WIFI_REASON_BEACON_TIMEOUT: return "немає маякiв AP (сигнал/живлення)";
    case WIFI_REASON_HANDSHAKE_TIMEOUT: return "handshake timeout";
    case WIFI_REASON_CONNECTION_FAIL: return "не вдалось з'єднатись";
    default: return "див. код";
  }
}

void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    uint8_t r = info.wifi_sta_disconnected.reason;
    lastWifiDiscReason = r;
    snprintf(lastWifiEvent, sizeof(lastWifiEvent), "%u %s", r, wifiDiscWhy(r));
    Serial.printf("[WiFi] DISCONNECT reason=%u (%s) RSSI=%d heap=%u\n",
                  r, wifiDiscWhy(r), WiFi.RSSI(), (unsigned)ESP.getFreeHeap());
  } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    Serial.printf("[WiFi] GOT_IP %s RSSI=%d dBm\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
  }
}

void buzzOn() {
  if (BUZZER_ACTIVE) digitalWrite(BUZZER_PIN, HIGH);
  else { ledcWriteTone(BUZZER_PIN, BUZZER_FREQ); ledcWrite(BUZZER_PIN, 128); }
}

void buzzOff() {
  if (BUZZER_ACTIVE) digitalWrite(BUZZER_PIN, LOW);
  else { ledcWriteTone(BUZZER_PIN, 0); ledcWrite(BUZZER_PIN, 0); }
}

void startBeep(const uint16_t* pat) {
  beepPat = pat;
  beepIdx = 0;
  beepNext = 0;
}

void beepTick() {
  if (!beepPat) return;
  unsigned long n = millis();
  if (n < beepNext) return;
  uint16_t d = beepPat[beepIdx];
  if (d == 0) {
    buzzOff();
    beepPat = nullptr;
    return;
  }
  if (beepIdx % 2 == 0) buzzOn();
  else buzzOff();
  beepNext = n + d;
  beepIdx++;
}

void saveAlertLog() {
  File f = LittleFS.open("/alerts.bin", "w");
  if (!f) return;
  AlertEv logCopy[2][ALERT_LOG_MAX];
  int cnt[2];
  portENTER_CRITICAL(&alertLogMux);
  memcpy(logCopy, alertLog, sizeof(alertLog));
  cnt[0] = alertLogCount[0];
  cnt[1] = alertLogCount[1];
  portEXIT_CRITICAL(&alertLogMux);
  f.write((uint8_t*)cnt, sizeof(cnt));
  f.write((uint8_t*)logCopy, sizeof(logCopy));
  f.close();
}

void loadAlertLog() {
  File f = LittleFS.open("/alerts.bin", "r");
  if (!f) return;
  if (f.size() == sizeof(int) * 2 + sizeof(alertLog)) {
    int cnt[2];
    f.read((uint8_t*)cnt, sizeof(cnt));
    f.read((uint8_t*)alertLog, sizeof(alertLog));
    for (int r = 0; r < 2; r++) alertLogCount[r] = constrain(cnt[r], 0, ALERT_LOG_MAX);
  }
  f.close();
}

void addAlertEvent(int r, uint32_t t, uint8_t lvl) {
  portENTER_CRITICAL(&alertLogMux);
  if (alertLogCount[r] >= ALERT_LOG_MAX) {
    memmove(&alertLog[r][0], &alertLog[r][1], sizeof(AlertEv) * (ALERT_LOG_MAX - 1));
    alertLogCount[r] = ALERT_LOG_MAX - 1;
  }
  alertLog[r][alertLogCount[r]].t = t;
  alertLog[r][alertLogCount[r]].lvl = lvl;
  alertLogCount[r]++;
  portEXIT_CRITICAL(&alertLogMux);
  saveAlertLog();
}

int mapCharLevel(char c) {
  if (c == 'R' || c == 'A') return 2;
  if (c == 'Y' || c == 'P' || c == '?') return 1;
  if (c == 'N') return 0;
  return -1;
}

int slotLevelFromMap(const char* st, uint8_t kind, int idx) {
  if (kind == 2 && idx >= 0 && idx < UA_MAP_REGION_COUNT) return mapCharLevel(st[idx]);
  if (kind == 1 && idx >= 0 && idx < UA_MAP_STATE_COUNT) {
    int best = -1;
    for (int i = 0; i < UA_MAP_REGION_COUNT; i++) {
      if (pgm_read_byte(&uaMapRegionState[i]) != idx) continue;
      int l = mapCharLevel(st[i]);
      if (l > best) best = l;
    }
    return best;
  }
  return -1;
}

void alertTask(void* arg) {
  NetworkClientSecure client;
  configureTrustedTls(client);
  HTTPClient http;
  HTTPClient backupHttp;

  int cycle = 0, cityTurn = 0;
  int prevCity = 0;
  bool firstCity = true;
  int prevLvl[2] = { -1, -1 };
  int fails[3] = { 0, 0, 0 };
  uint8_t sigKind[2] = { 255, 255 };
  int16_t sigIdx[2] = { -1, -1 };
  uint32_t sigUid[2] = { 0, 0 };
  bool sigInit = false;

  auto logChange = [&](int r, int lvl) {
    if (!timeClient.valid()) return;
    uint32_t nowUtc = (uint32_t)time(nullptr);
    int last = (alertLogCount[r] > 0) ? alertLog[r][alertLogCount[r] - 1].lvl : -1;
    if (prevLvl[r] == -1) prevLvl[r] = last;
    if (prevLvl[r] == -1) {
      if (lvl > 0) addAlertEvent(r, nowUtc, lvl);
    } else if (lvl != prevLvl[r]) {
      addAlertEvent(r, nowUtc, lvl);
    }
    prevLvl[r] = lvl;
  };

  auto applyLevel = [&](int slot, int level, bool fromBackup) {
    backupUsingSlot[slot] = fromBackup;
    alertCurLevel[slot] = level;
    if (level < 0) {
      if (slot == 0) alertState = 0;
      return;
    }
    logChange(slot, level);
    if (slot == 0) {
      if (!firstCity && level > prevCity) alertEvent = 1;
      else if (!firstCity && level == 0 && prevCity > 0) alertEvent = 2;
      firstCity = false;
      prevCity = level;
      alertState = (level == 2) ? 2 : (level == 1) ? 3
                                                   : 1;
    }
  };

  for (;;) {
    if (wifiPortalActive || menuState == 7 || webConfigBusy()) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    AppCfg c;
    portENTER_CRITICAL(&cfgMux);
    memcpy(&c, &cfg, sizeof(c));
    portEXIT_CRITICAL(&cfgMux);

    for (int s = 0; s < 2; s++) {
      bool changed = (sigKind[s] != c.locKind[s] || sigIdx[s] != c.locIdx[s] || sigUid[s] != c.cityUid[s]);
      if (!changed) continue;
      sigKind[s] = c.locKind[s];
      sigIdx[s] = c.locIdx[s];
      sigUid[s] = c.cityUid[s];
      if (!sigInit) continue;
      prevLvl[s] = -1;
      portENTER_CRITICAL(&alertLogMux);
      alertLogCount[s] = 0;
      portEXIT_CRITICAL(&alertLogMux);
      saveAlertLog();
      alertCurLevel[s] = -1;
      if (s == 0) {
        firstCity = true;
        prevCity = 0;
        alertState = 0;
      }
    }
    sigInit = true;

    if ((!c.alertsToken[0] && !c.ukAlarmToken[0]) || (!c.locKind[0] && !c.locKind[1])) {
      alertState = 0;
      alertCurLevel[0] = -1;
      alertCurLevel[1] = -1;
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }
    for (int s = 0; s < 2; s++)
      if (!c.locKind[s]) alertCurLevel[s] = -1;

    bool city0 = c.locKind[0] && c.cityUid[0];
    bool city1 = c.locKind[1] && c.cityUid[1];
    int n = cycle++;
    bool mapJob = (n % 3 == 1) || !(city0 || city1);
    int slot = 0;
    if (!mapJob) {
      if (city0 && city1) slot = (cityTurn++ & 1);
      else slot = city0 ? 0 : 1;
    }
    bool primaryOk = false;
    const bool netReady = (WiFi.status() == WL_CONNECTED) && timeClient.valid();

    // 1. основне джерело (alerts.in.ua)
    if (netReady && c.alertsToken[0] && httpsLock(8000)) {
      String url = mapJob ? String("https://api.alerts.in.ua/v1/alerts/active.json")
                          : String("https://api.alerts.in.ua/v2/iot/active_air_raid_alerts/") + String((unsigned long)c.cityUid[slot]) + ".json";
      http.setReuse(false);
      http.begin(client, url);
      http.useHTTP10(true);
      http.addHeader("Authorization", String("Bearer ") + c.alertsToken);
      http.setTimeout(mapJob ? 8000 : 4000);
      http.setConnectTimeout(3000);

      int code = tlsHeapAvailable(mapJob ? "alerts.in.ua map" : "alerts.in.ua city") ? http.GET() : -1000;
      alertLastCode = code;
      if (code != 200) {
        alertErrCount++;
        alertLastErr = code;
      }

      if (code == 200 && mapJob) {
        char next[UA_MAP_REGION_COUNT + 1];
        if (uaParseActiveAlerts(http, next, sizeof(next))) {
          portENTER_CRITICAL(&alertLogMux);
          memcpy(compactAlertStatuses, next, sizeof(next));
          portEXIT_CRITICAL(&alertLogMux);

          for (int s = 0; s < 2; s++) {
            if (!c.locKind[s] || c.cityUid[s]) continue;
            applyLevel(s, slotLevelFromMap(next, c.locKind[s], c.locIdx[s]), false);
          }
          alertLastOkMs = millis();
          primaryOk = true;
        }
      } else if (code == 200 && !mapJob) {
        String st = http.getString();
        st.trim();
        st.replace("\"", "");
        char ch = st.length() ? st.charAt(0) : '?';
        int level = -1;
        if (ch == 'N') level = 0;
        else if (ch == 'Y' || ch == 'P') level = 1;
        else if (ch == 'R') level = 2;

        if (level >= 0) {
          applyLevel(slot, level, false);
          cityLastOkMs = millis();
          cityLastOkMsBySlot[slot] = cityLastOkMs;
          primaryOk = true;
        }
      }
      tlsClose(client, http);
      httpsUnlock();
    }

    // 2. Резерв: якщо alerts.in.ua не відповів. Окремий TLS-сеанс на інший хост.
    if (!primaryOk && c.ukAlarmToken[0] && netReady && httpsLock(8000)) {
      const char* backupNames[2] = { nullptr, nullptr };
      for (int s = 0; s < 2; s++) {
        if (!c.locKind[s]) continue;
        if (c.locKind[s] == 1 && c.locIdx[s] >= 0 && c.locIdx[s] < UA_MAP_STATE_COUNT)
          backupNames[s] = uaMapStateNames[c.locIdx[s]];
        else if (c.locKind[s] == 2 && c.locIdx[s] >= 0 && c.locIdx[s] < UA_MAP_REGION_COUNT)
          backupNames[s] = uaMapRegionNames[c.locIdx[s]];
      }

      backupHttp.setReuse(false);
      backupHttp.begin(client, "https://api.ukrainealarm.com/api/v3/alerts");
      backupHttp.setTimeout(6000);
      backupHttp.setConnectTimeout(3000);
      backupHttp.addHeader("Authorization", c.ukAlarmToken);

      int backupCode = tlsHeapAvailable("UkraineAlarm") ? backupHttp.GET() : -1000;
      backupLastCode = backupCode;

      if (backupCode == 200) {
        int backupLevels[2] = { -1, -1 };
        if (uaBackupParseActiveAlerts(backupHttp, backupNames[0], c.locKind[0],
                                      backupNames[1], c.locKind[1], backupLevels)) {
          unsigned long okMs = millis();
          for (int s = 0; s < 2; s++) {
            if (!backupNames[s]) continue;
            applyLevel(s, backupLevels[s], true);
            cityLastOkMsBySlot[s] = okMs;
          }
          cityLastOkMs = okMs;
          backupLastOkMs = okMs;
        }
      } else {
        backupErrCount++;
      }
      tlsClose(client, backupHttp);
      httpsUnlock();
    }

    vTaskDelay(pdMS_TO_TICKS(ALERT_POLL_MS));
  }
}

const char* shortDayName(int wday) {
  const char* names[] = { "Нд", "Пн", "Вт", "Ср", "Чт", "Пт", "Сб" };
  return names[wday];
}

void restartWithReason(const char* why) {
  File f = LittleFS.open("/restart.txt", "w");
  if (f) {
    f.println(why);
    f.close();
  }
  delay(100);
  ESP.restart();
}

void loadRestartReason() {
  File f = LittleFS.open("/restart.txt", "r");
  if (f) {
    String tmp = f.readStringUntil('\n');
    tmp.trim();
    strncpy(lastRestartWhy, tmp.c_str(), sizeof(lastRestartWhy) - 1);
    lastRestartWhy[sizeof(lastRestartWhy) - 1] = '\0';
    f.close();
    LittleFS.remove("/restart.txt");
  }
}

void rawToScreen(int rawX, int rawY, int& sx, int& sy) {
  sx = map(rawX, TS_MINX, TS_MAXX, 0, 320);
  sy = map(rawY, TS_MINY, TS_MAXY, 0, 240);
  if (TS_INVERT_X) sx = 319 - sx;
  if (TS_INVERT_Y) sy = 239 - sy;
}

bool inRect(int x, int y, int x1, int y1, int x2, int y2) {
  return x >= x1 && x <= x2 && y >= y1 && y <= y2;
}

void drawGear(int cx, int cy, uint16_t col, uint16_t bg) {
  tft.fillCircle(cx, cy, 11, col);
  for (int i = 0; i < 12; i++) {
    float a = i * TWO_PI / 12.0f;
    float ca = cos(a), sa = sin(a);
    float px = -sa * 2.5f, py = ca * 2.5f;
    int x1 = lroundf(cx + ca * 9 + px), y1 = lroundf(cy + sa * 9 + py);
    int x2 = lroundf(cx + ca * 9 - px), y2 = lroundf(cy + sa * 9 - py);
    int x3 = lroundf(cx + ca * 16 - px), y3 = lroundf(cy + sa * 16 - py);
    int x4 = lroundf(cx + ca * 16 + px), y4 = lroundf(cy + sa * 16 + py);
    tft.fillTriangle(x1, y1, x2, y2, x3, y3, col);
    tft.fillTriangle(x1, y1, x3, y3, x4, y4, col);
  }
  tft.fillCircle(cx, cy, 6, bg);
  tft.drawCircle(cx, cy, 6, col);
}

void drawMenuButton() {
  const int X = MENU_BTN_X, Y = MENU_BTN_Y, W = MENU_BTN_W, H = MENU_BTN_H;
  // плитка з рамкою і м'якою тінню
  tft.fillRoundRect(X, Y, W, H, 12, RGB565(22, 27, 42));
  tft.drawRoundRect(X, Y, W, H, 12, RGB565(110, 122, 150));
  tft.drawRoundRect(X + 1, Y + 1, W - 2, H - 2, 11, RGB565(52, 60, 82));
  // сітка 3x3 із кольорових квадратиків
  static const uint16_t cells[9] = { 0xF548, 0xEB29, 0xF548,
                                     0xEB29, 0xF548, 0x5E5A,
                                     0xF548, 0xFF7D, 0xEB29 };
  const int cell = 15, gap = 4;
  const int pad = (W - (3 * cell + 2 * gap)) / 2;
  for (int r = 0; r < 3; r++) {
    for (int c = 0; c < 3; c++) {
      tft.fillRoundRect(X + pad + c * (cell + gap), Y + pad + r * (cell + gap), cell, cell, 3, cells[r * 3 + c]);
    }
  }
}

void drawAlertStatus() {
  int st = alertState;
  shownAlertState = st;
  uint16_t bg, fg;
  const char* txt;
  if (st == 2) {
    bg = RED;
    fg = WHITE;
    txt = "ТРИВОГА";
  } else if (st == 3) {
    bg = YELLOW;
    fg = BLACK;
    txt = "ТРИВОГА";
  } else if (st == 1) {
    bg = DARKGREEN;
    fg = WHITE;
    txt = "ВСЕ СПОКIЙНО";
  } else {
    bg = GREY;
    fg = WHITE;
    txt = "ХЗ";
  }
  tft.fillRoundRect(10, 142, 140, 16, 4, bg);
  u8g2Fonts.setFont(u8g2_font_6x13_t_cyrillic);
  int tw = u8g2Fonts.getUTF8Width(txt);
  drawText(10 + (140 - tw) / 2, 154, txt, fg, bg);
}

// у Features.h (нічний режим + автояскравість)

void loadBrightness() {
  int b0 = brightness, r = bgR, g = bgG, b = bgB;
  cfgLoadUi(b0, r, g, b);
  brightness = b0;
}

void saveBrightness() {
  cfgSaveBrightness(brightness);
}

// ---------------------------------------------------------------------
// ГЕНЕРАТОР QR-КОДІВ (ESP-IDF 5.x / Arduino 3.x)
// ---------------------------------------------------------------------
static int qrDrawX = 0, qrDrawY = 0, qrDrawScale = 6;

static void drawQrModules(esp_qrcode_handle_t qr) {
  const int quiet = 2;
  const int size = esp_qrcode_get_size(qr);
  const int total = (size + quiet * 2) * qrDrawScale;
  tft.fillRect(qrDrawX, qrDrawY, total, total, WHITE);
  for (int y = 0; y < size; y++) {
    for (int x = 0; x < size; x++) {
      if (esp_qrcode_get_module(qr, x, y)) {
        tft.fillRect(qrDrawX + (x + quiet) * qrDrawScale,
                     qrDrawY + (y + quiet) * qrDrawScale,
                     qrDrawScale, qrDrawScale, BLACK);
      }
    }
  }
}

void drawQr(const char* text, int x0, int y0, int scale) {
  qrDrawX = x0;
  qrDrawY = y0;
  qrDrawScale = scale;

  esp_qrcode_config_t config;
  memset(&config, 0, sizeof(config));
  config.display_func = drawQrModules;
  config.max_qrcode_version = 4;
  config.qrcode_ecc_level = ESP_QRCODE_ECC_LOW;

  if (esp_qrcode_generate(&config, text) != ESP_OK) {
    u8g2Fonts.setFont(u8g2_font_6x13_t_cyrillic);
    drawText(x0, y0 + 20, "QR: текст задовгий", RED, BACKGROUND_COLOR);
  }
}

void drawSettingsQr() {
  webStart();
  tft.fillScreen(BACKGROUND_COLOR);
  if (WiFi.status() != WL_CONNECTED) {
    u8g2Fonts.setFont(u8g2_font_10x20_t_cyrillic);
    drawTextCentered(160, 100, "Немає Wi-Fi", RED, BACKGROUND_COLOR);
  } else {
    char url[64];
    snprintf(url, sizeof(url), "http://%s/?pin=%s", WiFi.localIP().toString().c_str(), cfg.pin);
    drawQr(url, 6, 9, 6);

    u8g2Fonts.setFont(u8g2_font_6x13_t_cyrillic);
    drawText(236, 26, "Налаштування", CYAN, BACKGROUND_COLOR);
    drawText(236, 56, "Наведи камеру", WHITE, BACKGROUND_COLOR);
    drawText(236, 72, "телефона ", WHITE, BACKGROUND_COLOR);
    drawText(236, 88, "QR-код", WHITE, BACKGROUND_COLOR);

    String pinStr = String("PIN ") + cfg.pin;
    drawText(236, 128, pinStr.c_str(), YELLOW, BACKGROUND_COLOR);

    drawText(236, 156, "Телефон і ", GREY, BACKGROUND_COLOR);
    drawText(236, 172, "станція в одній", GREY, BACKGROUND_COLOR);
    drawText(236, 188, "Wi-Fi мережі", GREY, BACKGROUND_COLOR);
  }

  tft.fillRoundRect(236, 198, 80, 38, 8, DARKGREEN);
  u8g2Fonts.setFont(u8g2_font_10x20_t_cyrillic);
  drawTextCentered(276, 223, "Назад", WHITE, DARKGREEN);
}

void minuteSilenceOff() {
  if (BUZZER_ACTIVE) digitalWrite(BUZZER_PIN, LOW);
  else { ledcWriteTone(BUZZER_PIN, 0); ledcWrite(BUZZER_PIN, 0); }
  minuteSilencePulse = false;
}

void minuteSilenceTick(unsigned long now) {
  if (!cfg.minuteSilence) {
    if (minuteSilencePulse && !alarmActive && !beepPat && !alertEvent) minuteSilenceOff();
    else minuteSilencePulse = false;
    minuteSilenceActive = false;
    return;
  }
  if (!minuteSilenceActive && timeClient.valid() && hour() == 9 && minute() == 0) {
    uint32_t dayKey = (uint32_t)year() * 10000UL + (uint32_t)month() * 100UL + (uint32_t)day();
    if (dayKey != minuteSilenceDayKey) {
      minuteSilenceDayKey = dayKey;
      minuteSilenceActive = true;
      minuteSilenceEndsAt = now + 60000UL;
      minuteSilenceNextAt = now;
    }
  }
  if (!minuteSilenceActive) return;
  if (alarmActive || beepPat || alertEvent) {
    if (minuteSilencePulse) minuteSilenceOff();
    minuteSilenceNextAt = now + 1000UL;
    return;
  }
  if ((long)(now - minuteSilenceEndsAt) >= 0) {
    minuteSilenceActive = false;
    minuteSilenceOff();
    return;
  }
  if (minuteSilencePulse && (long)(now - minuteSilencePulseEndsAt) >= 0) minuteSilenceOff();
  if ((long)(now - minuteSilenceNextAt) >= 0) {
    minuteSilenceNextAt = now + 1000UL;
    if (!alarmActive && !beepPat && !alertEvent) {
      if (BUZZER_ACTIVE) digitalWrite(BUZZER_PIN, HIGH);
      else { ledcWriteTone(BUZZER_PIN, 900); ledcWrite(BUZZER_PIN, 24); }
      minuteSilencePulse = true;
      minuteSilencePulseEndsAt = now + 70UL;
    }
  }
}

bool alarmActive = false;
int alarmKind = 0;
unsigned long alarmStartMs = 0;

void drawWrappedCentered(const char* text, int cx, int y, int lineH, int maxW, uint16_t fg, uint16_t bg) {
  char line[64] = "";
  char word[40];
  const char* p = text;
  while (*p) {
    int n = 0;
    while (*p && *p != ' ' && n < 39) word[n++] = *p++;
    word[n] = '\0';
    while (*p == ' ') p++;
    char trial[110];
    snprintf(trial, sizeof(trial), "%s%s%s", line, line[0] ? " " : "", word);
    if (line[0] && u8g2Fonts.getUTF8Width(trial) > maxW) {
      drawTextCentered(cx, y, line, fg, bg);
      y += lineH;
      strlcpy(line, word, sizeof(line));
    } else {
      strlcpy(line, trial, sizeof(line));
    }
  }
  if (line[0]) drawTextCentered(cx, y, line, fg, bg);
}

void drawAlarmFrame(bool inv) {
  uint16_t bg = (alarmKind == 1) ? (inv ? BLACK : RED) : DARKGREEN;
  uint16_t fg = (alarmKind == 1) ? (inv ? RED : WHITE) : WHITE;
  tft.fillScreen(bg);

  u8g2Fonts.setFont(u8g2_font_10x20_t_cyrillic);
  if (cfg.locTitle[0][0]) drawTextCentered(160, 28, cfg.locTitle[0], fg, bg);

  u8g2Fonts.setFont(u8g2_font_inr30_t_cyrillic);
  if (alarmKind == 1) drawWrappedCentered(cfg.alarmText, 160, 110, 40, 300, fg, bg);
  else drawWrappedCentered("ВIДБIЙ", 160, 125, 40, 300, fg, bg);

  u8g2Fonts.setFont(u8g2_font_6x13_t_cyrillic);
  drawTextCentered(160, 230, "торкнiться - закрити", fg, bg);
}

void startAlarmScreen(int kind) {
  alarmKind = kind;
  alarmActive = true;
  alarmStartMs = millis();
  ledcWrite(BL_PIN, BL_INVERT ? 0 : 255);
  drawAlarmFrame(false);
}

#define ALARM_DURATION 5000UL

void alarmTick(unsigned long now) {
  if (now - alarmStartMs >= ALARM_DURATION) {
    alarmActive = false;
    applyBrightness();
    restoreAfterAlarm();
  }
}

void decodeUkraineMapRow(int y, uint8_t* row) {
  memset(row, 255, UA_MAP_W);
  if (y < 0 || y >= UA_MAP_H) return;

  uint16_t pos = pgm_read_word(&uaMapRowOffsets[y]);
  uint16_t end = pgm_read_word(&uaMapRowOffsets[y + 1]);
  int x = 0;
  while (pos < end && x < UA_MAP_W) {
    uint8_t len = pgm_read_byte(&uaMapRle[pos++]);
    uint8_t region = pgm_read_byte(&uaMapRle[pos++]);
    for (int i = 0; i < len && x < UA_MAP_W; i++) row[x++] = region;
  }
}

inline void classifyMapBoundary(uint8_t id, uint8_t neighbor, uint8_t state,
                                bool& districtEdge, bool& oblastEdge, bool& outline) {
  if (neighbor == id) return;
  if (neighbor == 255 || neighbor >= UA_MAP_REGION_COUNT) {
    outline = true;
    return;
  }
  const uint8_t neighborState = pgm_read_byte(&uaMapRegionState[neighbor]);
  if (state != neighborState) oblastEdge = true;
  else if (id > neighbor) districtEdge = true;
}

// ----- Мапа тривог:
#define MAP_SRC_X0 24  // де в початковій картинці починається країна
#define MAP_SRC_Y0 2
#define MAP_SRC_W 272
#define MAP_SRC_H 185
#define MAP_DST_W 310  // розмір на екрані
#define MAP_DST_H 211
#define MAP_DST_X0 5
#define MAP_DST_Y0 8
#define MAP_BG RGB565(14, 18, 26)

static uint8_t mapSrcRow[UA_MAP_W];
static int mapSrcRowY = -1;

// id областей для кожного пікселя рядка
void decodeScaledMapRow(int dy, uint8_t* out) {
  if (dy < 0 || dy >= MAP_DST_H) {
    memset(out, 255, MAP_DST_W);
    return;
  }
  int sy = MAP_SRC_Y0 + (dy * MAP_SRC_H) / MAP_DST_H;
  if (sy != mapSrcRowY) {
    decodeUkraineMapRow(sy, mapSrcRow);
    mapSrcRowY = sy;
  }
  for (int dx = 0; dx < MAP_DST_W; dx++) out[dx] = mapSrcRow[MAP_SRC_X0 + (dx * MAP_SRC_W) / MAP_DST_W];
}

char mapClockDrawn[9] = "";
void drawMapClock(bool force) {
  if (!timeClient.valid()) return;
  time_t t = (time_t)timeClient.getEpochTime();
  tm* p = gmtime(&t);
  char buf[9];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", p->tm_hour, p->tm_min, p->tm_sec);
  if (!force && strcmp(buf, mapClockDrawn) == 0) return;
  tft.fillRect(0, 0, 92, 19, MAP_BG);
  u8g2Fonts.setFont(u8g2_font_logisoso16_tn);
  u8g2Fonts.setBackgroundColor(MAP_BG);
  u8g2Fonts.setForegroundColor(WHITE);
  u8g2Fonts.setCursor(4, 17);
  u8g2Fonts.print(buf);
  strcpy(mapClockDrawn, buf);
}

// Скільки секунд тому оновились дані (правий нижній кут): білий — свіжі, помаранчевий — застарілі
void drawMapAge() {
  unsigned long lastOk = alertLastOkMs;
  char buf[32];
  uint16_t col = WHITE;
  if (!lastOk) {
    strcpy(buf, "немає даних");
    col = RGB565(150, 155, 165);
  } else {
    unsigned long secs = (millis() - lastOk) / 1000UL;
    snprintf(buf, sizeof(buf), "оновл. %lu с тому", secs);
    if (secs * 1000UL > MAP_FRESH_MS) col = ORANGE;
  }
  tft.fillRect(198, 223, 122, 17, MAP_BG);
  u8g2Fonts.setFont(u8g2_font_6x13_t_cyrillic);
  u8g2Fonts.setBackgroundColor(MAP_BG);
  u8g2Fonts.setForegroundColor(col);
  u8g2Fonts.setCursor(204, 236);
  u8g2Fonts.print(buf);
}

void drawAlertMap(bool full) {
  char statuses[UA_MAP_REGION_COUNT + 1];
  portENTER_CRITICAL(&alertLogMux);
  memcpy(statuses, compactAlertStatuses, sizeof(statuses));
  portEXIT_CRITICAL(&alertLogMux);
  statuses[UA_MAP_REGION_COUNT] = '\0';

  unsigned long lastOk = alertLastOkMs;
  bool hasData = lastOk != 0 && statuses[0] != '\0';
  bool fresh = hasData && millis() - lastOk < MAP_FRESH_MS;

  const uint16_t mapBg = MAP_BG;
  const uint16_t oblastBorder = RGB565(38, 40, 46);
  const uint16_t districtBorder = RGB565(72, 75, 82);
  if (full) tft.fillScreen(mapBg);  // повна заливка лише при першому відкритті

  uint8_t rowStorage[3][MAP_DST_W];
  uint8_t* rowA = rowStorage[0];
  uint8_t* rowB = rowStorage[1];
  uint8_t* rowC = rowStorage[2];
  uint16_t colors[MAP_DST_W];
  mapSrcRowY = -1;
  decodeScaledMapRow(0, rowA);
  decodeScaledMapRow(0, rowB);
  decodeScaledMapRow(1, rowC);

  tft.startWrite();
  tft.setAddrWindow(MAP_DST_X0, MAP_DST_Y0, MAP_DST_W, MAP_DST_H);
  for (int y = 0; y < MAP_DST_H; y++) {
    for (int x = 0; x < MAP_DST_W; x++) {
      uint8_t id = rowB[x];
      if (id == 255) {
        colors[x] = mapBg;
      } else {
        const uint8_t state = pgm_read_byte(&uaMapRegionState[id]);
        bool districtEdge = false, oblastEdge = false;
        bool outline = x == 0 || x == MAP_DST_W - 1 || y == 0 || y == MAP_DST_H - 1;
        classifyMapBoundary(id, x > 0 ? rowB[x - 1] : 255, state, districtEdge, oblastEdge, outline);
        classifyMapBoundary(id, x + 1 < MAP_DST_W ? rowB[x + 1] : 255, state, districtEdge, oblastEdge, outline);
        classifyMapBoundary(id, rowA[x], state, districtEdge, oblastEdge, outline);
        classifyMapBoundary(id, rowC[x], state, districtEdge, oblastEdge, outline);

        if (outline || oblastEdge) colors[x] = oblastBorder;
        else if (districtEdge) colors[x] = districtBorder;
        else colors[x] = uaDistrictMapColor(id, statuses, hasData);
      }
    }
    tft.writePixels(colors, MAP_DST_W, true);

    uint8_t* oldA = rowA;
    rowA = rowB;
    rowB = rowC;
    rowC = oldA;
    if (y + 2 < MAP_DST_H) decodeScaledMapRow(y + 2, rowC);
    else memset(rowC, 255, MAP_DST_W);
  }
  tft.endWrite();

  // верхній ряд: годинник (зліва) і кнопка "НАЗАД" (справа) — у порожніх кутах мапи
  tft.fillRoundRect(244, 0, 74, 19, 5, DARKGREEN);
  tft.drawRoundRect(244, 0, 74, 19, 5, WHITE);
  u8g2Fonts.setFont(u8g2_font_6x13_t_cyrillic);
  drawTextCentered(281, 14, "НАЗАД", WHITE, DARKGREEN);
  mapClockDrawn[0] = '\0';
  drawMapClock(true);

  // легенда в один рядок + час оновлення справа
  tft.fillRect(0, 222, 320, 18, mapBg);
  tft.fillRect(4, 227, 8, 8, RGB565(220, 35, 35));
  tft.fillRect(66, 227, 8, 8, RGB565(245, 185, 0));
  tft.fillRect(134, 227, 8, 8, RGB565(25, 155, 65));
  u8g2Fonts.setFont(u8g2_font_6x13_t_cyrillic);
  drawText(16, 236, "ТРИВОГА", WHITE, mapBg);
  drawText(78, 236, "ЧАСТКОВА", WHITE, mapBg);
  drawText(146, 236, "СПОКІЙНО", WHITE, mapBg);
  drawMapAge();

  memcpy(mapDrawnStatuses, statuses, sizeof(mapDrawnStatuses));
  mapDrawnFresh = fresh;
}

void updateAlertMapIfNeeded() {
  if (menuState != 5 || touchTracking) return;
  drawMapClock(false);
  static unsigned long lastAgeDraw = 0;
  if (millis() - lastAgeDraw >= 5000) {
    lastAgeDraw = millis();
    drawMapAge();
  }

  char current[UA_MAP_REGION_COUNT + 1];
  portENTER_CRITICAL(&alertLogMux);
  memcpy(current, compactAlertStatuses, sizeof(current));
  portEXIT_CRITICAL(&alertLogMux);
  current[UA_MAP_REGION_COUNT] = '\0';

  unsigned long lastOk = alertLastOkMs;
  bool fresh = lastOk != 0 && millis() - lastOk < MAP_FRESH_MS;
  if (fresh != mapDrawnFresh || memcmp(current, mapDrawnStatuses, sizeof(current)) != 0)
    drawAlertMap(false);
}

constexpr int MENU_COL1_X = 12;
constexpr int MENU_COL2_X = 166;
constexpr int MENU_COL_W = 142;
constexpr int MENU_ROW1_Y = 44;
constexpr int MENU_ROW2_Y = 92;
constexpr int MENU_ROW3_Y = 140;
constexpr int MENU_ITEM_H = 40;
constexpr int MENU_BACK_Y = 194;
constexpr int MENU_BACK_H = 40;

void drawMenu() {
  tft.fillScreen(BACKGROUND_COLOR);
  u8g2Fonts.setFont(u8g2_font_10x20_t_cyrillic);
  drawTextCentered(160, 30, "Меню", CYAN, BACKGROUND_COLOR);

  tft.fillRoundRect(MENU_COL1_X, MENU_ROW1_Y, MENU_COL_W, MENU_ITEM_H, 8, GREY);
  tft.fillRoundRect(MENU_COL2_X, MENU_ROW1_Y, MENU_COL_W, MENU_ITEM_H, 8, GREY);
  tft.fillRoundRect(MENU_COL1_X, MENU_ROW2_Y, MENU_COL_W, MENU_ITEM_H, 8, GREY);
  tft.fillRoundRect(MENU_COL2_X, MENU_ROW2_Y, MENU_COL_W, MENU_ITEM_H, 8, GREY);
  tft.fillRoundRect(MENU_COL1_X, MENU_ROW3_Y, MENU_COL_W, MENU_ITEM_H, 8, GREY);
  tft.fillRoundRect(MENU_COL2_X, MENU_ROW3_Y, MENU_COL_W, MENU_ITEM_H, 8, GREY);
  tft.fillRoundRect(MENU_COL1_X, MENU_BACK_Y, 296, MENU_BACK_H, 8, DARKGREEN);

  drawTextCentered(MENU_COL1_X + MENU_COL_W / 2, MENU_ROW1_Y + 27, "Яскравість", WHITE, GREY);
  drawTextCentered(MENU_COL2_X + MENU_COL_W / 2, MENU_ROW1_Y + 27, "Параметри", WHITE, GREY);
  drawTextCentered(MENU_COL1_X + MENU_COL_W / 2, MENU_ROW2_Y + 27, "Фон", WHITE, GREY);
  drawTextCentered(MENU_COL2_X + MENU_COL_W / 2, MENU_ROW2_Y + 27, "Мапа тривог", WHITE, GREY);
  drawTextCentered(MENU_COL1_X + MENU_COL_W / 2, MENU_ROW3_Y + 27, "Зміна Wi-Fi", WHITE, GREY);
  drawTextCentered(MENU_COL2_X + MENU_COL_W / 2, MENU_ROW3_Y + 27, "Налаштування", WHITE, GREY);
  drawTextCentered(160, MENU_BACK_Y + 27, "Назад", WHITE, DARKGREEN);
}

void drawWifiConfigScreen(const char* portalPassword, bool manual) {
  tft.fillScreen(BACKGROUND_COLOR);
  char qrText[72];
  snprintf(qrText, sizeof(qrText), "WIFI:T:WPA;S:ESP32-WeatherStation;P:%s;;", portalPassword);
  drawQr(qrText, 6, 9, 6);
  u8g2Fonts.setFont(u8g2_font_6x13_t_cyrillic);

  drawText(236, 26, "Підключення", CYAN, BACKGROUND_COLOR);
  drawText(236, 56, "Наведи камеру", WHITE, BACKGROUND_COLOR);
  drawText(236, 72, "на QR-код,", WHITE, BACKGROUND_COLOR);
  drawText(236, 88, "далі обери", WHITE, BACKGROUND_COLOR);
  drawText(236, 104, "свою Wi-Fi", WHITE, BACKGROUND_COLOR);
  drawText(236, 120, "мережу", WHITE, BACKGROUND_COLOR);
  drawText(236, 150, "вручну:", GREY, BACKGROUND_COLOR);
  drawText(236, 166, portalPassword, YELLOW, BACKGROUND_COLOR);
  drawText(236, 184, "192.168.4.1", GREY, BACKGROUND_COLOR);

  if (manual) {
    tft.fillRoundRect(236, 198, 80, 38, 8, DARKGREEN);
    u8g2Fonts.setFont(u8g2_font_10x20_t_cyrillic);
    drawTextCentered(276, 223, "Назад", WHITE, DARKGREEN);
  }
}

void closeWifiConfigPortal() {
  bool ok = (wpState == WP_OK);
  wpStop();
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  wifiPortalActive = false;
  wifiPortalStartedMs = 0;
  if (ok) {
    menuState = 7;
    drawSettingsQr();
  } else {
    menuState = 1;
    drawMenu();
  }
}

void startWifiConfigPortal() {
  wifiPortalActive = true;
  wifiPortalStartedMs = millis();
  menuState = 6;
  char portalPassword[10];
  snprintf(portalPassword, sizeof(portalPassword), "W%08lX", (unsigned long)esp_random());
  drawWifiConfigScreen(portalPassword, true);
  webStop();
  wpStart(portalPassword);
}

void drawBrightnessBar() {
  u8g2Fonts.setFont(u8g2_font_inr30_t_cyrillic);
  tft.fillRect(0, 55, 320, 50, BACKGROUND_COLOR);
  String bStr = String(brightness) + " %";
  drawTextCentered(160, 92, bStr.c_str(), YELLOW, BACKGROUND_COLOR);

  tft.drawRoundRect(30, 120, 260, 30, 6, WHITE);
  tft.fillRect(32, 122, 256, 26, BACKGROUND_COLOR);
  int w = map(brightness, 0, 100, 0, 256);
  tft.fillRect(32, 122, w, 26, YELLOW);
}

void drawBrightness() {
  tft.fillScreen(BACKGROUND_COLOR);
  u8g2Fonts.setFont(u8g2_font_10x20_t_cyrillic);
  drawTextCentered(160, 30, "Яскравiсть", CYAN, BACKGROUND_COLOR);
  drawBrightnessBar();

  tft.fillRoundRect(20, 190, 280, 42, 8, DARKGREEN);
  drawTextCentered(160, 219, "Назад", WHITE, DARKGREEN);
}

void setBrightnessFromTouch(int sx, int sy) {
  if (!inRect(sx, sy, 20, 105, 300, 170)) return;
  int level = constrain((sx - 30) * 100 / 260, 10, 100);
  if (level != brightness) {
    brightness = level;
    applyBrightness();
    drawBrightnessBar();
  }
}

void handleMenuTap(int sx, int sy) {
  if (menuState == 6) {
    if (inRect(sx, sy, 12, MENU_BACK_Y, 308, MENU_BACK_Y + MENU_BACK_H)) {
      closeWifiConfigPortal();
    }
  } else if (menuState == 7) {
    if (inRect(sx, sy, 226, 188, 319, 239)) {
      menuState = 1;
      webConfigSessionActive = false;
      lastWeatherUpdate = 0;
      drawMenu();
    }
  } else if (menuState == 1) {
    if (inRect(sx, sy, MENU_COL1_X, MENU_ROW1_Y, MENU_COL1_X + MENU_COL_W, MENU_ROW1_Y + MENU_ITEM_H)) {
      menuState = 2;
      drawBrightness();
    } else if (inRect(sx, sy, MENU_COL2_X, MENU_ROW1_Y, MENU_COL2_X + MENU_COL_W, MENU_ROW1_Y + MENU_ITEM_H)) {
      menuState = 3;
      sysScroll = 0;
      sysForce = true;
      drawSystem();
    } else if (inRect(sx, sy, MENU_COL1_X, MENU_ROW2_Y, MENU_COL1_X + MENU_COL_W, MENU_ROW2_Y + MENU_ITEM_H)) {
      menuState = 4;
      bgR = bgAR;
      bgG = bgAG;
      bgB = bgAB;
      drawBgScreen();
    } else if (inRect(sx, sy, MENU_COL2_X, MENU_ROW2_Y, MENU_COL2_X + MENU_COL_W, MENU_ROW2_Y + MENU_ITEM_H)) {
      menuState = 5;
      drawAlertMap(true);
    } else if (inRect(sx, sy, MENU_COL1_X, MENU_ROW3_Y, MENU_COL1_X + MENU_COL_W, MENU_ROW3_Y + MENU_ITEM_H)) {
      startWifiConfigPortal();
    } else if (inRect(sx, sy, MENU_COL2_X, MENU_ROW3_Y, MENU_COL2_X + MENU_COL_W, MENU_ROW3_Y + MENU_ITEM_H)) {
      menuState = 7;
      drawSettingsQr();
    } else if (inRect(sx, sy, 12, MENU_BACK_Y, 308, MENU_BACK_Y + MENU_BACK_H)) {
      menuState = 0;
      returnToMain();
    }
  } else if (menuState == 2) {
    if (inRect(sx, sy, 20, 190, 300, 232)) {
      saveBrightness();
      menuState = 1;
      drawMenu();
    } else {
      setBrightnessFromTouch(sx, sy);
    }
  } else if (menuState == 3) {
    if (inRect(sx, sy, 20, 212, 300, 238)) {
      menuState = 1;
      drawMenu();
    }
  } else if (menuState == 4) {
    if (inRect(sx, sy, 4, 190, 94, 232)) {
      bgR = 0;
      bgG = 0;
      bgB = 40;
      for (int ch = 0; ch < 3; ch++) drawBgSlider(ch);
      drawBgPreview();
    } else if (inRect(sx, sy, 98, 190, 232, 232)) {
      bgAR = bgR;
      bgAG = bgG;
      bgAB = bgB;
      bgColor = RGB565(bgR, bgG, bgB);
      saveBg();
      menuState = 0;
      returnToMain();
    } else if (inRect(sx, sy, 236, 190, 316, 232)) {
      bgR = bgAR;
      bgG = bgAG;
      bgB = bgAB;
      menuState = 1;
      drawMenu();
    } else {
      setBgFromTouch(sx, sy);
    }
  } else if (menuState == 5) {
    if (inRect(sx, sy, 236, 0, 319, 34)) {
      menuState = 1;
      drawMenu();
    }
  }
}

#define SYS_LINE_H 18
#define SYS_TOP 28
#define SYS_BOTTOM 210
#define SYS_MAX_LINES 40

String sysLbl[SYS_MAX_LINES];
String sysVal[SYS_MAX_LINES];
int sysCount = 0;

String fmtKB(uint32_t bytes) {
  return String(bytes / 1024) + " КБ";
}
String fmtUptime() {
  unsigned long s = millis() / 1000UL;
  unsigned long d = s / 86400UL;
  s %= 86400UL;
  char b[28];
  snprintf(b, sizeof(b), "%lu д %02lu:%02lu:%02lu", d, s / 3600UL, (s / 60UL) % 60UL, s % 60UL);
  return String(b);
}

const char* resetReasonText() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "ввiмкнення живлення";
    case ESP_RST_SW: return "програмний restart";
    case ESP_RST_PANIC: return "ЗБIЙ програми (panic)";
    case ESP_RST_INT_WDT: return "watchdog (int)";
    case ESP_RST_TASK_WDT: return "watchdog (задача)";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_BROWNOUT: return "ПРОСАДКА живлення";
    case ESP_RST_EXT: return "кнопка / зовнiшнiй";
    case ESP_RST_DEEPSLEEP: return "глибокий сон";
    default: return "невiдомо";
  }
}

void addSys(const char* label, const String& value) {
  if (sysCount < SYS_MAX_LINES) {
    sysLbl[sysCount] = label;
    sysVal[sysCount] = value;
    sysCount++;
  }
}

void buildSysLines() {
  sysCount = 0;
  bool wifiOk = (WiFi.status() == WL_CONNECTED);

  addSys("Wi-Fi", wifiOk ? WiFi.SSID() : String("немає зв'язку"));
  addSys("IP", wifiOk ? WiFi.localIP().toString() : String("-"));
  addSys("Шлюз", WiFi.gatewayIP().toString());
  addSys("DNS", WiFi.dnsIP().toString());
  addSys("MAC", WiFi.macAddress());
  addSys("Канал", String(WiFi.channel()));
  int rssi = WiFi.RSSI();
  const char* q = rssi >= -50 ? "вiдмiнний" : rssi >= -60 ? "дуже добрий"
                                            : rssi >= -70 ? "добрий"
                                            : rssi >= -80 ? "слабкий"
                                                          : "поганий";
  addSys("Сигнал", wifiOk ? String(rssi) + " dBm (" + q + ")" : String("-"));
  addSys("Працює", fmtUptime());
  addSys("Причина старту", resetReasonText());
  if (strlen(lastRestartWhy) > 0) addSys("Чому restart", lastRestartWhy);
  addSys("Обривiв Wi-Fi", String(wifiDrops));
  addSys("Останнiй обрив", lastWifiEvent);
  addSys("Чип", String(ESP.getChipModel()) + " rev" + String(ESP.getChipRevision()) + ", " + String(ESP.getChipCores()) + " ядра");
  addSys("Частота CPU", String(ESP.getCpuFreqMHz()) + " МГц");
  float tc = temperatureRead();
  addSys("Темп. чипа", (isnan(tc) || tc < -20 || tc > 110) ? String("-") : String(tc, 0) + " C");
  addSys("Heap вiльно", fmtKB(ESP.getFreeHeap()) + " / " + fmtKB(ESP.getHeapSize()));
  addSys("Heap мiнiмум", fmtKB(ESP.getMinFreeHeap()));
  addSys("Flash", String(ESP.getFlashChipSize() / 1048576) + " МБ");
  addSys("LittleFS", fmtKB(LittleFS.usedBytes()) + " / " + fmtKB(LittleFS.totalBytes()));
  addSys("Цикл loop", String(loopRate) + " /с, макс. пауза " + String(loopMaxGap) + " мс");
  addSys("Тривога: мiсто 1", cityLastOkMsBySlot[0] ? "ok " + String((millis() - cityLastOkMsBySlot[0]) / 1000UL) + " с тому" : String("немає вiдповiдi"));
  addSys("Тривога: мiсто 2", cityLastOkMsBySlot[1] ? "ok " + String((millis() - cityLastOkMsBySlot[1]) / 1000UL) + " с тому" : String("немає вiдповiдi"));
  addSys("Помилок API", String(alertErrCount) + " (остання: " + String(alertLastErr) + ")");
  addSys("Погода", lastWeatherUpdate ? "оновл. " + String((millis() - lastWeatherUpdate) / 60000UL) + " хв тому" : String("-"));
  addSys("Час (NTP)", timeClient.valid() ? String("синхронiзовано") : String("НЕ синхронiзовано"));
}

void drawSystem() {
  if (sysForce || millis() - sysBuiltMs >= 5000) {
    buildSysLines();
    sysBuiltMs = millis();
    sysForce = false;
  }
  int contentH = sysCount * SYS_LINE_H;
  int viewH = SYS_BOTTOM - SYS_TOP;
  int maxScroll = max(0, contentH - viewH);
  sysScroll = constrain(sysScroll, 0, maxScroll);

  tft.fillRect(0, SYS_TOP, 320, viewH, BACKGROUND_COLOR);
  u8g2Fonts.setFont(u8g2_font_6x13_t_cyrillic);
  for (int i = 0; i < sysCount; i++) {
    int base = SYS_TOP + 13 + i * SYS_LINE_H - sysScroll;
    if (base < SYS_TOP + 12 || base > SYS_BOTTOM - 2) continue;
    drawText(6, base, sysLbl[i].c_str(), CYAN, BACKGROUND_COLOR);
    drawText(112, base, sysVal[i].c_str(), WHITE, BACKGROUND_COLOR);
  }

  if (maxScroll > 0) {
    int barH = max(16, viewH * viewH / contentH);
    int barY = SYS_TOP + (viewH - barH) * sysScroll / maxScroll;
    tft.fillRect(316, SYS_TOP, 4, viewH, 0x2104);
    tft.fillRect(316, barY, 4, barH, GREY);
  }

  tft.fillRect(0, 0, 320, SYS_TOP, BACKGROUND_COLOR);
  u8g2Fonts.setFont(u8g2_font_10x20_t_cyrillic);
  drawTextCentered(160, 22, "Параметри", CYAN, BACKGROUND_COLOR);

  tft.fillRect(0, SYS_BOTTOM + 1, 320, 240 - SYS_BOTTOM - 1, BACKGROUND_COLOR);
  tft.fillRoundRect(20, 212, 280, 26, 6, DARKGREEN);
  drawTextCentered(160, 231, "Назад", WHITE, DARKGREEN);
}

void drawAlertHist() {
  AlertEv logCopy[2][ALERT_LOG_MAX];
  int cnt[2];
  portENTER_CRITICAL(&alertLogMux);
  memcpy(logCopy, alertLog, sizeof(alertLog));
  cnt[0] = alertLogCount[0];
  cnt[1] = alertLogCount[1];
  portEXIT_CRITICAL(&alertLogMux);

  time_t nowLocal = (time_t)time(nullptr) + cachedTZ;
  tm* np = gmtime(&nowLocal);
  int todayY = np->tm_year, todayM = np->tm_mon, todayD = np->tm_mday;

  tft.fillScreen(BACKGROUND_COLOR);
  u8g2Fonts.setFont(u8g2_font_10x20_t_cyrillic);
  drawTextCentered(160, 20, "Журнал тривог", CYAN, BACKGROUND_COLOR);

  const char* names[2] = { cfg.locTitle[0][0] ? cfg.locTitle[0] : "-", cfg.locTitle[1][0] ? cfg.locTitle[1] : "-" };
  const int ROWS = 8;
  for (int r = 0; r < 2; r++) {
    int x0 = 4 + r * 160;
    int cur = alertCurLevel[r];
    uint16_t hbg = (cur == 2) ? RED : (cur == 1) ? YELLOW
                                    : (cur == 0) ? DARKGREEN
                                                 : GREY;
    uint16_t hfg = (cur == 1) ? BLACK : WHITE;
    tft.fillRoundRect(x0, 28, 152, 20, 4, hbg);
    u8g2Fonts.setFont(u8g2_font_6x13_t_cyrillic);
    drawTextCentered(x0 + 76, 42, names[r], hfg, hbg);

    int startIdx = max(0, cnt[r] - ROWS);
    int row = 0;
    for (int i = startIdx; i < cnt[r]; i++) {
      int lvl = logCopy[r][i].lvl;
      uint16_t bg = (lvl == 2) ? RED : (lvl == 1) ? YELLOW
                                                  : DARKGREEN;
      uint16_t fg = (lvl == 1) ? BLACK : WHITE;
      const char* lab = (lvl == 2) ? "ТРИВОГА" : (lvl == 1) ? "ЖОВТИЙ"
                                                            : "ВIДБIЙ";

      time_t lt = (time_t)logCopy[r][i].t + cachedTZ;
      tm* p = gmtime(&lt);
      bool isToday = (p->tm_year == todayY && p->tm_mon == todayM && p->tm_mday == todayD);
      char line[40];
      if (isToday) {
        snprintf(line, sizeof(line), "%02d:%02d %s", p->tm_hour, p->tm_min, lab);
      } else {
        snprintf(line, sizeof(line), "%02d.%02d %02d:%02d %s", p->tm_mday, p->tm_mon + 1, p->tm_hour, p->tm_min, lab);
      }
      int y = 52 + row * 20;
      tft.fillRoundRect(x0, y, 152, 18, 4, bg);
      drawText(x0 + 6, y + 13, line, fg, bg);
      row++;
    }
    if (row == 0) {
      drawText(x0 + 6, 70, "немає подiй", GREY, BACKGROUND_COLOR);
    }
  }

  u8g2Fonts.setFont(u8g2_font_6x13_t_cyrillic);
  drawTextCentered(160, 232, "тап - назад", GREY, BACKGROUND_COLOR);
}

void loadBg() {
  int b0 = brightness;
  cfgLoadUi(b0, bgR, bgG, bgB);
  bgAR = bgR;
  bgAG = bgG;
  bgAB = bgB;
  bgColor = RGB565(bgR, bgG, bgB);
}

void saveBg() {
  cfgSaveBg(bgR, bgG, bgB);
}

void drawBgSlider(int ch) {
  int y = 40 + ch * 40;
  int val = (ch == 0) ? bgR : (ch == 1) ? bgG
                                        : bgB;
  uint16_t col = (ch == 0) ? RED : (ch == 1) ? GREEN
                                             : RGB565(70, 130, 255);
  const char* name = (ch == 0) ? "R" : (ch == 1) ? "G"
                                                 : "B";

  tft.fillRect(0, y - 4, 320, 32, bgColor);
  u8g2Fonts.setFont(u8g2_font_10x20_t_cyrillic);
  drawText(8, y + 19, name, col, bgColor);

  tft.drawRoundRect(30, y, 220, 24, 5, WHITE);
  tft.fillRect(32, y + 2, 216, 20, 0x2104);
  int w = map(val, 0, 255, 0, 216);
  tft.fillRect(32, y + 2, w, 20, col);

  String valStr = String(val);
  drawText(258, y + 19, valStr.c_str(), WHITE, bgColor);
}

void drawBgPreview() {
  uint16_t c = RGB565(bgR, bgG, bgB);
  tft.fillRoundRect(20, 156, 280, 28, 6, c);
  tft.drawRoundRect(20, 156, 280, 28, 6, WHITE);
  u8g2Fonts.setFont(u8g2_font_6x13_t_cyrillic);
  drawTextCentered(90, 175, "Чернiгiв  12.3 C", YELLOW, c);
  drawTextCentered(220, 175, "Приклад тексту", WHITE, c);
}

void drawBgScreen() {
  tft.fillScreen(bgColor);
  u8g2Fonts.setFont(u8g2_font_10x20_t_cyrillic);
  drawTextCentered(160, 22, "Колiр фону", CYAN, bgColor);
  for (int ch = 0; ch < 3; ch++) drawBgSlider(ch);
  drawBgPreview();

  tft.fillRoundRect(4, 190, 90, 42, 8, RGB565(130, 70, 0));
  tft.fillRoundRect(98, 190, 134, 42, 8, DARKGREEN);
  tft.fillRoundRect(236, 190, 80, 42, 8, GREY);

  drawTextCentered(49, 217, "Скинути", WHITE, RGB565(130, 70, 0));
  drawTextCentered(165, 217, "Застосувати", WHITE, DARKGREEN);
  drawTextCentered(276, 217, "Назад", WHITE, GREY);
}

void setBgFromTouch(int sx, int sy) {
  for (int ch = 0; ch < 3; ch++) {
    int y = 40 + ch * 40;
    if (sy >= y - 4 && sy <= y + 28) {
      int x = constrain(sx, 32, 248);
      int value = map(x, 32, 248, 0, 255);
      if (ch == 0) bgR = value;
      else if (ch == 1) bgG = value;
      else bgB = value;
      drawBgSlider(ch);
      drawBgPreview();
      return;
    }
  }
}

void drawHourly() {
  tft.fillScreen(BACKGROUND_COLOR);
  u8g2Fonts.setFont(u8g2_font_10x20_t_cyrillic);
  drawTextCentered(160, 28, "Прогноз по годинах", CYAN, BACKGROUND_COLOR);
  if (hrCount == 0) {
    drawTextCentered(160, 120, "Немає даних", GREY, BACKGROUND_COLOR);
  }
  for (int i = 0; i < hrCount; i++) {
    int x = i * FORECAST_ITEM_W + 1;
    char buf[12];
    snprintf(buf, sizeof(buf), "%02d:%02d", hrCache[i].hh, hrCache[i].mm);
    int tw = u8g2Fonts.getUTF8Width(buf);
    drawText(x + (IMG_WIDTH - tw) / 2, 70, buf, CYAN, BACKGROUND_COLOR);

    displayWeatherIcon(hrCache[i].cond, x, 80, hrCache[i].night);

    snprintf(buf, sizeof(buf), "%d C", hrCache[i].temp);
    tw = u8g2Fonts.getUTF8Width(buf);
    drawText(x + (IMG_WIDTH - tw) / 2, 160, buf, GREEN, BACKGROUND_COLOR);
  }
  drawTextCentered(160, 232, "тап   назад", GREY, BACKGROUND_COLOR);
}

// Рядок тісного списку (крок 13 пікс.)
void drawTextTight(int x, int y, const char* txt, uint16_t fgColor, uint16_t bgColorOverride = BACKGROUND_COLOR) {
  int tw = u8g2Fonts.getUTF8Width(txt);
  tft.fillRect(x, y - 10, tw + 2, 13, bgColorOverride);
  u8g2Fonts.setBackgroundColor(bgColorOverride);
  u8g2Fonts.setForegroundColor(fgColor);
  u8g2Fonts.setCursor(x, y);
  u8g2Fonts.print(txt);
}

// Якість повітря: індекс AQI 1–5 від OpenWeatherMap (це не відсотки)
int lastAqi = 0;
float wxLat = 0, wxLon = 0;
bool wxHaveCoord = false;

const char* aqiWord(int a) {
  switch (a) {
    case 1: return "чисте";
    case 2: return "добре";
    case 3: return "помiрне";
    case 4: return "погане";
    case 5: return "дуже погане";
  }
  return "";
}

uint16_t aqiColor(int a) {
  return a <= 2 ? GREEN : a == 3 ? YELLOW
                        : a == 4 ? ORANGE
                                 : RED;
}

void drawAqiLine() {
  u8g2Fonts.setFont(u8g2_font_6x13_t_cyrillic);
  tft.fillRect(TEMP_X, WX_Y_AQI - 10, 150, 13, BACKGROUND_COLOR);
  if (lastAqi <= 0) return;
  char buf[48];
  snprintf(buf, sizeof(buf), "Повiтря: %d/5 %s", lastAqi, aqiWord(lastAqi));
  drawTextTight(TEMP_X, WX_Y_AQI, buf, aqiColor(lastAqi), BACKGROUND_COLOR);
}

void redrawMain() {
  tft.fillScreen(BACKGROUND_COLOR);
  u8g2Fonts.setFont(u8g2_font_6x13_t_cyrillic);
  if (strlen(lastTemperature) > 0) {
    String tempStr = String(cfg.weatherTitle) + "  " + lastTemperature;
    drawText(10, 10, tempStr.c_str(), YELLOW, BACKGROUND_COLOR);
  }
  if (strlen(lastSunrise) > 0) {
    char buf[32];
    snprintf(buf, sizeof(buf), "Схід: %s", lastSunrise);
    drawTextTight(TEMP_X, WX_Y_SUN, buf, MAGENTA, BACKGROUND_COLOR);
  }
  if (strlen(lastSunset) > 0) {
    char buf[32];
    snprintf(buf, sizeof(buf), "Зах: %s", lastSunset);
    drawTextTight(TEMP_X + 70, WX_Y_SUN, buf, MAGENTA, BACKGROUND_COLOR);
  }
  if (strlen(lastPressure) > 0) {
    char buf[40];
    snprintf(buf, sizeof(buf), "Тиск: %s%s", lastPressure, cfg.pressMm ? " мм рт.ст." : " hPa");
    drawTextTight(TEMP_X, WX_Y_PRESS, buf, MAGENTA, BACKGROUND_COLOR);
  }
  if (strlen(lastHumidity) > 0) {
    char buf[32];
    snprintf(buf, sizeof(buf), "Вологiсть: %s %%", lastHumidity);
    drawTextTight(TEMP_X, WX_Y_HUM, buf, WHITE, BACKGROUND_COLOR);
  }
  if (strlen(lastWindSpeed) > 0) {
    char buf[32];
    snprintf(buf, sizeof(buf), "Вiтер: %s м/с", lastWindSpeed);
    drawTextTight(TEMP_X, WX_Y_WIND, buf, WHITE, BACKGROUND_COLOR);
  }
  if (lastAqi > 0) drawAqiLine();
  if (strlen(lastWeatherCondition) > 0) {
    displayWeatherIcon(lastWeatherCondition, TEMP_X + 10, TEMP_Y + 5);
  }

  for (int i = 0; i < fcCount; i++) {
    drawForecastItem(i, fcCache[i].cond, fcCache[i].tmin, fcCache[i].tmax, fcCache[i].name);
  }

  drawAlertStatus();
  drawMenuButton();
  displayCalendar();
}

void returnToMain() {
  lastHours[0] = '\0';
  lastMinutes[0] = '\0';
  lastSeconds[0] = '\0';
  lastDayOfWeek[0] = '\0';
  redrawMain();
  lastCalendarUpdate = millis();
}

void drawClockFull(bool force) {
  time_t t = (time_t)timeClient.getEpochTime();
  tm* ptm = gmtime(&t);

  char hh[3], mm[3], ss[3];
  snprintf(hh, sizeof(hh), "%02d", ptm->tm_hour);
  snprintf(mm, sizeof(mm), "%02d", ptm->tm_min);
  snprintf(ss, sizeof(ss), "%02d", ptm->tm_sec);

  char hm[6];
  snprintf(hm, sizeof(hm), "%s:%s", hh, mm);

  static unsigned long colonOffUntil = 0;
  static bool lastShowColon = true;
  bool secondChanged = (strcmp(cfSec, ss) != 0);
  if (secondChanged) colonOffUntil = millis() + 150UL;
  bool showColon = (long)(millis() - colonOffUntil) >= 0;

  const char* dayNames[] = { "Недiля", "Понедiлок", "Вiвторок", "Середа", "Четвер", "П'ятниця", "Субота" };
  const char* dow = dayNames[ptm->tm_wday];
  const char* monthNames[] = { "січня", "лютого", "березня", "квітня", "травня", "червня", "липня", "серпня", "вересня", "жовтня", "листопада", "грудня" };
  char date[24];
  snprintf(date, sizeof(date), "%d %s", ptm->tm_mday, monthNames[ptm->tm_mon]);

  u8g2Fonts.setFont(u8g2_font_logisoso32_tn);
  int digitW = u8g2Fonts.getUTF8Width("00");
  int colonW = u8g2Fonts.getUTF8Width(":");
  int startX = (320 - (3 * digitW + 2 * colonW)) / 2;

  int hourX = startX;
  int colon1X = hourX + digitW;
  int minuteX = colon1X + colonW;
  int colon2X = minuteX + digitW;
  int secondX = colon2X + colonW;

  bool hourChanged = force || cfTime[0] == '\0' || strncmp(cfTime, hh, 2) != 0;
  bool minuteChanged = force || cfTime[0] == '\0' || strncmp(cfTime + 3, mm, 2) != 0;
  bool secondsChanged = force || strcmp(cfSec, ss) != 0;

  if (hourChanged) {
    tft.fillRect(hourX - 2, 38, digitW + 4, 40, BACKGROUND_COLOR);
    drawText(hourX, 70, hh, RED, BACKGROUND_COLOR);
  }

  if (force || hourChanged || minuteChanged) {
    tft.fillRect(colon1X, 38, colonW, 40, BACKGROUND_COLOR);
    drawText(colon1X, 70, ":", CYAN, BACKGROUND_COLOR);
  }

  if (minuteChanged) {
    tft.fillRect(minuteX - 2, 38, digitW + 4, 40, BACKGROUND_COLOR);
    drawText(minuteX, 70, mm, RED, BACKGROUND_COLOR);
  }

  if (secondsChanged || showColon != lastShowColon || force) {
    tft.fillRect(colon2X, 38, colonW, 40, BACKGROUND_COLOR);
    if (showColon) {
      drawText(colon2X, 70, ":", CYAN, BACKGROUND_COLOR);
    }
  }

  if (secondsChanged) {
    tft.fillRect(secondX - 2, 38, digitW + 4, 40, BACKGROUND_COLOR);
    drawText(secondX, 70, ss, ORANGE, BACKGROUND_COLOR);
  }

  strlcpy(cfTime, hm, sizeof(cfTime));
  strlcpy(cfSec, ss, sizeof(cfSec));
  lastShowColon = showColon;

  if (force || strcmp(cfDate, date) != 0) {
    tft.fillRect(0, 108, 320, 44, BACKGROUND_COLOR);
    u8g2Fonts.setFont(u8g2_font_inr30_t_cyrillic);
    drawTextCentered(160, 139, date, CYAN, BACKGROUND_COLOR);
    strlcpy(cfDate, date, sizeof(cfDate));
  }

  if (force || strcmp(cfDow, dow) != 0) {
    tft.fillRect(0, 185, 320, 45, BACKGROUND_COLOR);
    u8g2Fonts.setFont(u8g2_font_inr30_t_cyrillic);
    drawTextCentered(160, 215, dow, DARKGREEN, BACKGROUND_COLOR);
    strlcpy(cfDow, dow, sizeof(cfDow));
  }
}

void restoreAfterAlarm() {
  if (calendarFullscreen) displayCalendarFullscreen(calDisplayMonth, calDisplayYear);
  else if (clockFullscreen) { tft.fillScreen(BACKGROUND_COLOR); drawClockFull(true); }
  else if (alertHistScreen) drawAlertHist();
  else if (hourlyScreen) drawHourly();
  else if (menuState == 5) drawAlertMap(true);
  else if (menuState == 4) drawBgScreen();
  else if (menuState == 3) drawSystem();
  else if (menuState == 2) drawBrightness();
  else if (menuState == 1) drawMenu();
  else returnToMain();
}

static void wxField(char* last, size_t cap, const char* val, int x, int y,
                    uint16_t color, const char* pre, const char* post) {
  if (strcmp(last, val) == 0) return;
  char buf[72];
  u8g2Fonts.setFont(u8g2_font_6x13_t_cyrillic);
  if (last[0]) {
    snprintf(buf, sizeof(buf), "%s%s%s", pre, last, post);
    drawTextTight(x, y, buf, BACKGROUND_COLOR, BACKGROUND_COLOR);  // затираємо старий текст
  }
  snprintf(buf, sizeof(buf), "%s%s%s", pre, val, post);
  drawTextTight(x, y, buf, color, BACKGROUND_COLOR);
  strlcpy(last, val, cap);
}

void updateWeather(unsigned long now) {
  lastWeatherUpdate = now;
  if (!WiFi.isConnected()) {
    lastWeatherUpdate = now - 3600000UL + 30000UL;
    return;
  }
  if (!httpsLock(25000)) {
    Serial.println("[https] погоду відкладено: зайняті запити тривог");
    lastWeatherUpdate = now - 3600000UL + 120000UL;
    return;
  }

  {
    String url = "https://api.openweathermap.org/data/2.5/weather?q=" + String(cfg.weatherCity)
                 + "&lang=ua&units=metric&appid=" + String(cfg.owmKey);
    if (httpGetOK(url)) {
      JsonDocument doc;
      if (!deserializeJson(doc, httpClient.getStream())) {
        char cond[24], tempBuf[16], srBuf[6], ssBuf[6], pressBuf[8], humBuf[8], windBuf[8], title[40];
        strlcpy(cond, doc["weather"][0]["main"] | "", sizeof(cond));
        float temp = doc["main"]["temp"] | 0.0f;
        float wind = doc["wind"]["speed"] | 0.0f;
        float press = doc["main"]["pressure"] | 0.0f;
        int hum = doc["main"]["humidity"] | 0;
        wxLat = doc["coord"]["lat"] | 0.0f;  // для запиту якості повітря
        wxLon = doc["coord"]["lon"] | 0.0f;
        wxHaveCoord = (wxLat != 0.0f || wxLon != 0.0f);
        time_t sr = (time_t)doc["sys"]["sunrise"].as<long long>() + currentTZOffset();
        time_t ss = (time_t)doc["sys"]["sunset"].as<long long>() + currentTZOffset();
        strftime(srBuf, sizeof(srBuf), "%H:%M", gmtime(&sr));
        strftime(ssBuf, sizeof(ssBuf), "%H:%M", gmtime(&ss));
        snprintf(tempBuf, sizeof(tempBuf), "%.1f C", temp);
        snprintf(pressBuf, sizeof(pressBuf), "%.0f", cfg.pressMm ? press * 0.750062f : press);  // мм рт.ст. або гПа
        snprintf(humBuf, sizeof(humBuf), "%d", hum);
        snprintf(windBuf, sizeof(windBuf), "%.1f", wind);
        snprintf(title, sizeof(title), "%s  ", cfg.weatherTitle);

        wxField(lastTemperature, sizeof(lastTemperature), tempBuf, 10, 10, YELLOW, title, "");
        wxField(lastSunrise, sizeof(lastSunrise), srBuf, TEMP_X, WX_Y_SUN, MAGENTA, "Схід: ", "");
        wxField(lastSunset, sizeof(lastSunset), ssBuf, TEMP_X + 70, WX_Y_SUN, MAGENTA, "Зах: ", "");
        wxField(lastPressure, sizeof(lastPressure), pressBuf, TEMP_X, WX_Y_PRESS, MAGENTA, "Тиск: ", cfg.pressMm ? " мм рт.ст." : " hPa");
        wxField(lastHumidity, sizeof(lastHumidity), humBuf, TEMP_X, WX_Y_HUM, WHITE, "Вологiсть: ", " %");
        wxField(lastWindSpeed, sizeof(lastWindSpeed), windBuf, TEMP_X, WX_Y_WIND, WHITE, "Вiтер: ", " м/с");

        if (strcmp(cond, lastWeatherCondition) != 0) {
          tft.fillRect(TEMP_X + 10, TEMP_Y + 5, IMG_WIDTH, IMG_HEIGHT, BACKGROUND_COLOR);
          displayWeatherIcon(cond, TEMP_X + 10, TEMP_Y + 5, featuresIsNight());
          strlcpy(lastWeatherCondition, cond, sizeof(lastWeatherCondition));
        }
      }
    }
    tlsClose(weatherTlsClient, httpClient);
  }

  {
    String url = "https://api.openweathermap.org/data/2.5/forecast?q=" + String(cfg.weatherCity)
                 + "&lang=ua&units=metric&cnt=48&appid=" + String(cfg.owmKey);
    if (httpGetOK(url)) {
      JsonDocument filter;
      filter["list"][0]["dt"] = true;
      filter["list"][0]["main"]["temp"] = true;
      filter["list"][0]["weather"][0]["main"] = true;
      filter["list"][0]["sys"]["pod"] = true;

      JsonDocument doc;
      if (!deserializeJson(doc, httpClient.getStream(), DeserializationOption::Filter(filter))) {
        JsonArray list = doc["list"].as<JsonArray>();
        long tz = currentTZOffset();
        time_t nowLocal = (time_t)timeClient.getEpochTime();
        tm* todayTm = gmtime(&nowLocal);
        char todayStr[11];
        snprintf(todayStr, sizeof(todayStr), "%04d-%02d-%02d",
                 todayTm->tm_year + 1900, todayTm->tm_mon + 1, todayTm->tm_mday);

        char lastDate[11] = "";
        time_t nowUtc = nowLocal - tz;
        int hrFill = 0;
        hrCount = 0;
        int dayCount = 0;
        float dayMin = 1000.0f, dayMax = -1000.0f;
        char dayCond[16] = "";
        int bestCondPrio = -1;
        time_t firstDtOfDay = 0;

        for (JsonObject item : list) {
          if (dayCount >= 5) break;
          time_t dt = (time_t)item["dt"].as<long long>();
          time_t localDt = dt + tz;
          tm* ltm = gmtime(&localDt);
          char dateBuf[11];
          snprintf(dateBuf, sizeof(dateBuf), "%04d-%02d-%02d",
                   ltm->tm_year + 1900, ltm->tm_mon + 1, ltm->tm_mday);

          if (hrFill < 5 && dt > nowUtc) {
            strlcpy(hrCache[hrFill].cond, item["weather"][0]["main"] | "", sizeof(hrCache[hrFill].cond));
            hrCache[hrFill].temp = (int)round(item["main"]["temp"].as<float>());
            hrCache[hrFill].hh = ltm->tm_hour;
            hrCache[hrFill].mm = ltm->tm_min;
            const char* partOfDay = item["sys"]["pod"] | "d";
            hrCache[hrFill].night = (partOfDay[0] == 'n');
            hrFill++;
            hrCount = hrFill;
          }

          if (strcmp(dateBuf, todayStr) == 0) continue;

          float tmp = item["main"]["temp"].as<float>();
          char cond[16];
          strlcpy(cond, item["weather"][0]["main"] | "", sizeof(cond));

          if (lastDate[0] != '\0' && strcmp(dateBuf, lastDate) != 0) {
            time_t firstLocal = firstDtOfDay + tz;
            tm* outTm = gmtime(&firstLocal);
            displayForecastItem(dayCount++, dayCond, (int)round(dayMin), (int)round(dayMax),
                                shortDayName(outTm->tm_wday));
            dayMin = 1000.0f;
            dayMax = -1000.0f;
            bestCondPrio = -1;
          }
          if (lastDate[0] == '\0' || strcmp(dateBuf, lastDate) != 0) {
            strlcpy(lastDate, dateBuf, sizeof(lastDate));
            firstDtOfDay = dt;
          }
          if (tmp < dayMin) dayMin = tmp;
          if (tmp > dayMax) dayMax = tmp;
          int prio = condPriority(cond);
          if (prio > bestCondPrio) {
            bestCondPrio = prio;
            strlcpy(dayCond, cond, sizeof(dayCond));
          }
        }

        if (dayCount < 5 && lastDate[0] != '\0') {
          time_t firstLocal = firstDtOfDay + tz;
          tm* outTm = gmtime(&firstLocal);
          displayForecastItem(dayCount, dayCond, (int)round(dayMin), (int)round(dayMax),
                              shortDayName(outTm->tm_wday));
        }
      }
    }
    tlsClose(weatherTlsClient, httpClient);
  }

  // ── 3. ЯКІСТЬ ПОВІТРЯ (OpenWeatherMap: індекс AQI 1–5) ──
  if (wxHaveCoord) {
    String url = "https://api.openweathermap.org/data/2.5/air_pollution?lat=" + String(wxLat, 4)
                 + "&lon=" + String(wxLon, 4) + "&appid=" + String(cfg.owmKey);
    if (httpGetOK(url)) {
      JsonDocument doc;
      if (!deserializeJson(doc, httpClient.getStream())) {
        int aqi = doc["list"][0]["main"]["aqi"] | 0;
        if (aqi >= 1 && aqi <= 5 && aqi != lastAqi) {
          lastAqi = aqi;
          drawAqiLine();
        }
      }
    }
    tlsClose(weatherTlsClient, httpClient);
  }

  httpsUnlock();

  if (!lastTemperature[0] || fcCount == 0) {
    lastWeatherUpdate = now - 3600000UL + 30000UL;
  }
}

// =====================================================================
// SETUP ТА LOOP
// =====================================================================
void setup() {
  Serial.begin(115200);
  delay(400);
  Serial.printf("[boot] reset=%s (%d)\n", resetReasonText(), (int)esp_reset_reason());
  esp_task_wdt_config_t wdtConfig;
  wdtConfig.timeout_ms = 60000;
  wdtConfig.idle_core_mask = (1U << 0) | (1U << 1);
  wdtConfig.trigger_panic = true;
  esp_task_wdt_reconfigure(&wdtConfig);

  httpsLockInit();
  configureTrustedTls(weatherTlsClient);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  if (!BUZZER_ACTIVE) ledcAttach(BUZZER_PIN, BUZZER_FREQ, 8);

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS init failed!");
  }

  cfgLoad();
  notesLoad();
  loadBrightness();
  loadBg();
  loadAlertLog();
  loadRestartReason();

  ledcAttach(BL_PIN, 5000, 8);
  featuresBegin();
  applyBrightness();

  pinMode(TOUCH_CS, OUTPUT);
  digitalWrite(TOUCH_CS, HIGH);

  SPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI);

  tft.begin(20000000);
  u8g2Fonts.begin(tft);
  u8g2Fonts.setFontMode(1);
  tft.setRotation(1);
  tft.fillScreen(BACKGROUND_COLOR);

  u8g2Fonts.setFont(u8g2_font_10x20_t_cyrillic);
  drawText(40, 110, "Made by V E R T", CYAN, BACKGROUND_COLOR);
  delay(1500);
  tft.fillScreen(BACKGROUND_COLOR);

  // ── СТАБІЛЬНИЙ СТАРТ WI-FI З МАКСИМАЛЬНОЮ ПОТУЖНІСТЮ ──
  WiFi.onEvent(onWiFiEvent);
  WiFi.mode(WIFI_STA);
  WiFi.persistent(true);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);

  // Примусово вмикаємо максимальну потужність передавача для пробиття перешкод
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  Serial.println("[WiFi] Встановлено максимальну потужність TX: 19.5 dBm");

  // Пауза для стабілізації радіомодуля в ядрі 3.x
  delay(300);

  WiFi.begin();

  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < (WIFI_CONNECT_TIMEOUT * 1000UL)) {
    delay(200);
  }
  bool connected = (WiFi.status() == WL_CONNECTED);

  if (!connected) {
    snprintf(bootPortalPass, sizeof(bootPortalPass), "W%08lX", (unsigned long)esp_random());
    drawWifiConfigScreen(bootPortalPass, false);
    wpStart(bootPortalPass);
    unsigned long t0 = millis();
    while (millis() - t0 < 600000UL) {
      if (wpLoop() == WP_OK) {
        connected = true;
        break;
      }
      delay(2);
    }
    wpStop();
  }

  if (!connected) {
    showNoInternet();
    delay(15000);
    restartWithReason("немає Wi-Fi при стартi");
  }

  tft.fillScreen(BACKGROUND_COLOR);

  ts.begin();
  ts.setRotation(1);

  timeClient.begin();
  timeClient.setTimeOffset(TZ_WINTER);
  timeClient.forceUpdate();

  cachedTZ = isUkraineSummerTime((time_t)timeClient.getEpochTime() - TZ_WINTER) ? TZ_SUMMER : TZ_WINTER;
  timeClient.setTimeOffset(cachedTZ);
  updateNTPTime();

  calDisplayMonth = month();
  calDisplayYear = currentYear;

  displayCalendar();
  drawMenuButton();

  xTaskCreatePinnedToCore(alertTask, "alert", 16384, NULL, 1, &alertTaskHandle, 0);
  if (!cfgComplete()) {
    menuState = 7;
    drawSettingsQr();
  }
}

void loop() {
  delay(2);
  unsigned long now = millis();
  featuresTick(now);  // світлодіод, нічний режим, автояскравість
  webLoop();  // обслуговуємо веб навіть під час тривоги
  minuteSilenceTick(now);

  if (alarmActive) {
    beepTick();
    alarmTick(now);
    if (ts.touched()) {
      delay(80);
      while (ts.touched()) { webLoop(); delay(10); }
      alarmActive = false;
      applyBrightness();
      restoreAfterAlarm();
    }
    return;
  }


  if (wifiPortalActive) {
    wpLoop();
    bool timedOut = (millis() - wifiPortalStartedMs >= WIFI_MANUAL_PORTAL_TIMEOUT * 1000UL);
    if (wpState == WP_OK || timedOut) closeWifiConfigPortal();
  }

  loopCountTmp++;
  if (now - prevLoopMs > loopMaxTmp) loopMaxTmp = now - prevLoopMs;
  prevLoopMs = now;
  if (now - lastRateMs >= 1000) {
    loopRate = loopCountTmp;
    loopMaxGap = loopMaxTmp;
    loopCountTmp = 0;
    loopMaxTmp = 0;
    lastRateMs = now;
  }

  if (alertEvent && !wifiPortalActive && menuState != 7 && !webConfigBusy()) {
    int ev = alertEvent;
    alertEvent = 0;
    if (ev == 1) startBeep(BEEP_ALERT);
    else if (!featuresIsNight()) startBeep(BEEP_CLEAR);  // вночі відбій без звуку
    if (!wifiPortalActive) {
      startAlarmScreen(ev);
      return;  // ВАЖЛИВО: у цьому проході більше нічого не малюємо поверх екрана тривоги
    }
  }
  beepTick();

  // Wi-Fi.status() під час TLS інколи бреше "disconnected". Рахуємо обрив лише після
  // кількох перевірок підряд, і не рвемо сокети через disconnect().
  static unsigned long lastWifiCheck = 0, wifiDownSince = 0;
  static bool wifiWasUp = true;
  static uint8_t wifiFailStreak = 0;
  if (!wifiPortalActive && now - lastWifiCheck >= 6000UL) {
    lastWifiCheck = now;
    if (WiFi.status() == WL_CONNECTED) {
      wifiFailStreak = 0;
      if (!wifiWasUp) {
        if (menuState == 7) {
          webStop();
          webStart();
        }
        Serial.println("[WiFi] Мережа відновлена.");
      }
      wifiDownSince = 0;
      wifiWasUp = true;
    } else if (++wifiFailStreak < 3) {
      Serial.printf("[WiFi] статус %d, ще не обрив (%u/3)\n", (int)WiFi.status(), wifiFailStreak);
    } else {
      if (wifiWasUp) {
        wifiDrops++;
        wifiWasUp = false;
        Serial.println("[WiFi] Зв'язок втрачено.");
      }
      if (wifiDownSince == 0) wifiDownSince = now;

      static unsigned long lastKick = 0;
      if (now - wifiDownSince > 30000UL && now - lastKick > 30000UL) {
        lastKick = now;
        WiFi.reconnect();
      }

      if (now - wifiDownSince > 600000UL) restartWithReason("Wi-Fi вiдсутнiй 10 хв");
    }
  }

  static unsigned long touchLockUntil = 0;
  if (now - lastTouchCheck >= 25 && (long)(now - touchLockUntil) >= 0) {
    lastTouchCheck = now;
    static int onCnt = 0, offCnt = 0;
    static bool stableTouched = false;

    if (ts.touched()) {
      onCnt++;
      offCnt = 0;
    } else {
      offCnt++;
      onCnt = 0;
    }
    if (!stableTouched && onCnt >= 2) stableTouched = true;
    if (stableTouched && offCnt >= 2) stableTouched = false;

    if (stableTouched) {
      TS_Point p = ts.getPoint();
      if (!touchTracking) {
        touchTracking = true;
        touchStartRawX = p.x;
        touchStartRawY = p.y;
      }
      touchCurrRawX = p.x;
      touchCurrRawY = p.y;

      if (menuState == 2) {
        int sx, sy;
        rawToScreen(p.x, p.y, sx, sy);
        setBrightnessFromTouch(sx, sy);
      } else if (menuState == 3) {
        int sx, sy;
        rawToScreen(p.x, p.y, sx, sy);
        if (sysLastSy < 0) sysLastSy = sy;
        if (abs(sy - sysLastSy) >= 3) {
          sysScroll -= (sy - sysLastSy);
          sysLastSy = sy;
          drawSystem();
        }
      } else if (menuState == 4) {
        int sx, sy;
        rawToScreen(p.x, p.y, sx, sy);
        setBgFromTouch(sx, sy);
      }
    } else if (touchTracking) {
      touchTracking = false;
      int delta = touchCurrRawX - touchStartRawX;

      if (calendarFullscreen) {
        if (abs(delta) >= SWIPE_THRESHOLD) {
          if (delta > 0) {
            if (++calDisplayMonth > 12) {
              calDisplayMonth = 1;
              calDisplayYear++;
            }
          } else {
            if (--calDisplayMonth < 1) {
              calDisplayMonth = 12;
              calDisplayYear--;
            }
          }
          displayCalendarFullscreen(calDisplayMonth, calDisplayYear);
        } else {
          calendarFullscreen = false;
          returnToMain();
        }
      } else if (clockFullscreen) {
        clockFullscreen = false;
        returnToMain();
      } else if (alertHistScreen) {
        alertHistScreen = false;
        returnToMain();
      } else if (hourlyScreen) {
        hourlyScreen = false;
        returnToMain();
      } else if (menuState != 0) {
        sysLastSy = -1;
        int sx, sy;
        rawToScreen(touchCurrRawX, touchCurrRawY, sx, sy);
        bool dragged = (abs(touchCurrRawY - touchStartRawY) > 250 || abs(delta) > 250);
        if (!(menuState == 3 && dragged)) handleMenuTap(sx, sy);
      } else if (abs(delta) < SWIPE_THRESHOLD) {
        int sx, sy;
        rawToScreen(touchStartRawX, touchStartRawY, sx, sy);
        if (inRect(sx, sy, MENU_X1, MENU_Y1, MENU_X2, MENU_Y2)) {
          menuState = 1;
          drawMenu();
        } else if (inRect(sx, sy, CAL_X1, CAL_Y1, CAL_X2, CAL_Y2)) {
          calDisplayMonth = month();
          calDisplayYear = currentYear;
          calendarFullscreen = true;
          displayCalendarFullscreen(calDisplayMonth, calDisplayYear);
        } else if (inRect(sx, sy, CLK_X1, CLK_Y1, CLK_X2, CLK_Y2)) {
          clockFullscreen = true;
          cfTime[0] = '\0';
          cfSec[0] = '\0';
          cfDow[0] = '\0';
          cfDate[0] = '\0';
          tft.fillScreen(BACKGROUND_COLOR);
          drawClockFull(true);
        } else if (inRect(sx, sy, WX_X1, WX_Y1, WX_X2, WX_Y2)) {
          hourlyScreen = true;
          drawHourly();
        } else if (inRect(sx, sy, ALERT_X1, ALERT_Y1, ALERT_X2, ALERT_Y2)) {
          alertHistScreen = true;
          drawAlertHist();
        }
      }
      touchLockUntil = millis() + 180;
    }
  }

  static unsigned long settingsSince = 0;
  if (menuState == 7 && cfgComplete()) {
    if (settingsSince == 0) settingsSince = now;
    else if (now - settingsSince > 300000UL) {
      settingsSince = 0;
      menuState = 0;
      webConfigSessionActive = false;
      lastWeatherUpdate = 0;
      returnToMain();
    }
  } else {
    settingsSince = 0;
  }

  if (cfgDirty) {
    cfgDirty = false;
    lastWeatherUpdate = 0;
    featuresRecheck();  // нічний режим / автояскравість застосувати одразу
    shownAlertState = -1;
    bool onMain = (menuState == 0 && !calendarFullscreen && !clockFullscreen && !alertHistScreen && !hourlyScreen && !alarmActive);
    if (menuState == 7 && cfgComplete()) {
      menuState = 0;
      webConfigSessionActive = false;
      onMain = true;
    }
    if (onMain) returnToMain();
  }

  if (menuState == 3 && !touchTracking && now - lastSysDraw >= 5000) {
    lastSysDraw = now;
    drawSystem();
  }

  if (alertHistScreen && !touchTracking && now - lastSysDraw >= 15000) {
    lastSysDraw = now;
    drawAlertHist();
  }

  updateAlertMapIfNeeded();

  if (calendarFullscreen || menuState != 0 || hourlyScreen || alertHistScreen) return;

  if (clockFullscreen) {
    if (now - lastTimeDraw >= 200) {
      lastTimeDraw = now;
      if (!minuteSilenceActive) timeClient.update();
      drawClockFull(false);
    }
    return;
  }

  if (alertState != shownAlertState) drawAlertStatus();
notesTick(now);
  if (timeClient.valid()) {
    if (!minuteSilenceActive && (now - lastNTPUpdate >= 60000UL || lastNTPUpdate == 0)) {
      updateNTPTime();
      lastNTPUpdate = now;
    }
    if (!minuteSilenceActive && (now - lastDSTCheck >= 3600000UL || lastDSTCheck == 0)) {
      updateDSTCache();
      lastDSTCheck = now;
    }
  }

  if (now - lastCalendarUpdate >= 360000UL) {
    displayCalendar();
    lastCalendarUpdate = now;
  }

  if (timeClient.valid() && now - lastTimeDraw >= 150) {
    lastTimeDraw = now;
    if (!minuteSilenceActive) timeClient.update();
    time_t localNow = (time_t)timeClient.getEpochTime();
    setTime(localNow);

    tm* tp = gmtime(&localNow);
    char h[3], m[3], s[3];
    snprintf(h, sizeof(h), "%02d", tp->tm_hour);
    snprintf(m, sizeof(m), "%02d", tp->tm_min);
    snprintf(s, sizeof(s), "%02d", tp->tm_sec);
    u8g2Fonts.setFont(u8g2_font_logisoso38_tn);

    static unsigned long dotsOffUntil = 0;
    static bool lastShowDots = true;
    bool secondsChanged = (strcmp(s, lastSeconds) != 0);
    if (secondsChanged) dotsOffUntil = millis() + 150UL;
    bool showDots = (long)(millis() - dotsOffUntil) >= 0;

    int digitW = u8g2Fonts.getUTF8Width("00");
    const int separatorWidth = 12;
    int startX = (320 - (3 * digitW + 2 * separatorWidth)) / 2;

    int hourX = startX;
    int colon1X = hourX + digitW;
    int minuteX = colon1X + separatorWidth;
    int colon2X = minuteX + digitW;
    int secondX = colon2X + separatorWidth;

    bool hourChanged = (strcmp(h, lastHours) != 0);
    bool minuteChanged = (strcmp(m, lastMinutes) != 0);

    if (hourChanged) {
      tft.fillRect(hourX - 2, TIME_Y - 42, digitW + 4, 48, BACKGROUND_COLOR);
      drawText(hourX, TIME_Y, h, RED, BACKGROUND_COLOR);
      strcpy(lastHours, h);
    }

    if (hourChanged || minuteChanged) {
      tft.fillRect(colon1X, TIME_Y - 30, separatorWidth, 30, BACKGROUND_COLOR);
      tft.fillCircle(colon1X + 6, TIME_Y - 22, 2, RED);
      tft.fillCircle(colon1X + 6, TIME_Y - 10, 2, RED);
    }

    if (minuteChanged) {
      tft.fillRect(minuteX - 2, TIME_Y - 42, digitW + 4, 48, BACKGROUND_COLOR);
      drawText(minuteX, TIME_Y, m, RED, BACKGROUND_COLOR);
      strcpy(lastMinutes, m);
    }

    if (secondsChanged) {
      tft.fillRect(secondX - 2, TIME_Y - 42, digitW + 4, 48, BACKGROUND_COLOR);
      drawText(secondX, TIME_Y, s, ORANGE, BACKGROUND_COLOR);
      strcpy(lastSeconds, s);
    }

    if (secondsChanged || showDots != lastShowDots) {
      tft.fillRect(colon2X, TIME_Y - 30, separatorWidth, 30, BACKGROUND_COLOR);
      if (showDots) {
        tft.fillCircle(colon2X + 6, TIME_Y - 22, 2, RED);
        tft.fillCircle(colon2X + 6, TIME_Y - 10, 2, RED);
      }
      lastShowDots = showDots;
    }

    tm* ptm = gmtime(&localNow);
    const char* dayNames[] = { "Недiля", "Понедiлок", "Вiвторок", "Середа", "Четвер", "П'ятниця", "Субота" };
    const char* dow = dayNames[ptm->tm_wday];
      if (!notesTickerActive() && strcmp(dow, lastDayOfWeek) != 0) {
      u8g2Fonts.setFont(u8g2_font_10x20_t_cyrillic);
      tft.fillRect(110, 2, 120, 15, BACKGROUND_COLOR);
      int textWidth = u8g2Fonts.getUTF8Width(dow);
      drawTextCentered(160, 13, dow, DARKGREEN, BACKGROUND_COLOR);
      strlcpy(lastDayOfWeek, dow, sizeof(lastDayOfWeek));
    }
  }

  if (!webConfigBusy() && !minuteSilenceActive && (now - lastWeatherUpdate >= 3600000UL || lastWeatherUpdate == 0) && cfg.owmKey[0] && cfg.weatherCity[0] && timeClient.valid()) {
    updateWeather(now);
  }
}
