#pragma once
// =====================================================================
// WebConfig.h — сторінка налаштувань для телефона (порт 80, локальна мережа).
// Відкривається по QR-коду з екрана: http://<IP>/?pin=<PIN>
// Потрібно, щоб перед цим файлом було підключено UkraineAlertMap.h і Settings.h
// =====================================================================
#include <WebServer.h>
#include <Update.h>

extern volatile int alertCurLevel[2];  // визначено в скетчі
extern volatile int alertEvent;
void notesWebRoutes();  // сторінка заміток календаря (Notes.h)        // 1 — тривога, 2 — відбій (читає loop)

WebServer webServer(80);
bool webStarted = false;
bool webRoutesSet = false;
volatile bool webConfigSessionActive = false;
volatile uint32_t webConfigLastActivityMs = 0;
static constexpr unsigned long WEB_CONFIG_IDLE_MS = 180000UL;
inline bool webConfigBusy() {
  return webConfigSessionActive && (uint32_t)(millis() - webConfigLastActivityMs) < WEB_CONFIG_IDLE_MS;
}

static const char PAGE_A[] PROGMEM = R"HTML(<!doctype html><html lang="uk"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Погодна станція</title>
<style>
:root{color-scheme:dark}
body{font:16px system-ui,sans-serif;background:#0b1020;color:#e8ecf5;margin:0 auto;padding:16px;max-width:560px}
h1{font-size:22px;margin:4px 0 2px}
h2{font-size:15px;color:#8fb4ff;margin:26px 0 6px;text-transform:uppercase;letter-spacing:.04em}
label{display:block;margin:12px 0 4px;font-size:13px;color:#9aa7c7}
input,select{width:100%;box-sizing:border-box;padding:12px;border-radius:10px;border:1px solid #2a3558;background:#121a33;color:#fff;font-size:16px}
button{margin-top:26px;width:100%;padding:15px;border:0;border-radius:12px;background:#2f6bff;color:#fff;font-size:17px;font-weight:600}
.card{background:#101733;border-radius:14px;padding:4px 14px 14px;margin-top:10px}
.st{display:inline-block;padding:5px 12px;border-radius:8px;font-size:14px;font-weight:600;background:#555}
.row{display:flex;justify-content:space-between;align-items:center;padding:10px 0}
small{color:#7d8ab0;display:block;margin-top:4px;font-size:12px}
a{color:#8fb4ff}
</style></head><body>
<h1>Погодна станція</h1>
<small>Налаштування пристрою</small>
<h2>Стан тривоги (на момент відкриття сторінки)</h2>
<div class="card">
<div class="row"><span id="n0">Локація 1</span><span class="st" id="s0">—</span></div>
<div class="row"><span id="n1">Локація 2</span><span class="st" id="s1">—</span></div>
</div>
<form method="post" action="/save" onsubmit="return pack()">
<input type="hidden" name="pin" id="pin">
<h2>Тривога</h2>
<div class="card">
<label>Локація 1 — головна (екран, звук, повний екран)</label>
<select id="o0"></select>
<label>Район (можна не вибирати — тоді вся область)</label>
<select id="r0"></select>
<label>UID міста в alerts.in.ua (необов'язково, для точного статусу міста)</label>
<input name="u0" id="u0" inputmode="numeric" placeholder="напр. 1591">
<label>Підпис на екрані (порожньо = назва автоматично)</label>
<input name="t0" id="t0" maxlength="25">
<input type="hidden" name="k0" id="k0"><input type="hidden" name="i0" id="i0">
</div>
<div class="card">
<label>Локація 2 — друга колонка журналу</label>
<select id="o1"></select>
<label>Район</label>
<select id="r1"></select>
<label>UID міста (необов'язково)</label>
<input name="u1" id="u1" inputmode="numeric">
<label>Підпис на екрані</label>
<input name="t1" id="t1" maxlength="25">
<input type="hidden" name="k1" id="k1"><input type="hidden" name="i1" id="i1">
</div>
<div class="card">
<label>Текст на весь екран при тривозі</label>
<input name="txt" id="txt" maxlength="30">
</div>
<h2>Екран і звук</h2>
<div class="card">
<label>Тиск</label>
<select name="pm" id="pm"><option value="1">мм рт. ст.</option><option value="0">гПа (hPa)</option></select>
<label>Нічний режим (22:00–07:00): приглушений екран, відбій без звуку</label>
<select name="no" id="no"><option value="1">Увімкнено</option><option value="0">Вимкнено</option></select>
<label>Яскравість вночі, %</label>
<input name="nb" id="nb" type="number" min="3" max="100" inputmode="numeric">
<label>Автояскравість за датчиком світла (датчик на GPIO34)</label>
<select name="ab" id="ab"><option value="0">Вимкнено (немає датчика)</option><option value="1">Увімкнено</option></select>
<label>&#1065;&#1086;&#1076;&#1077;&#1085;&#1085;&#1080;&#1081; &#1089;&#1080;&#1075;&#1085;&#1072;&#1083; &#1086; 09:00 (1 &#1093;&#1074;&#1080;&#1083;&#1080;&#1085;&#1072;)</label><select name="ms" id="ms"><option value="1">&#1059;&#1074;&#1110;&#1084;&#1082;&#1085;&#1077;&#1085;&#1086;</option><option value="0">&#1042;&#1080;&#1084;&#1082;&#1085;&#1077;&#1085;&#1086;</option></select>
<label>LED: червоний GPIO27 при тривозі, зелений GPIO32 при відбої</label>
<select name="lm" id="lm"><option value="1">Червоний блимає</option><option value="2">Червоний світить постійно</option><option value="0">Вимкнено</option></select>
</div>
<h2>Погода</h2>
<div class="card">
<label>Місто для погоди (латиницею, напр. Kyiv,UA)</label>
<input name="city" id="city" maxlength="38" autocapitalize="off">
<label>Підпис на екрані (до 10 літер)</label>
<input name="wt" id="wt" maxlength="11">
</div>
<h2>Ключі доступу</h2>
<div class="card">
<label>Ключ OpenWeatherMap</label>
<input name="owm" id="owm" autocomplete="off" autocapitalize="off">
<small><a href="https://home.openweathermap.org/api_keys" target="_blank">отримати ключ</a></small>
<label>Токен alerts.in.ua</label>
<input name="alerts" id="alerts" autocomplete="off" autocapitalize="off">
<small><a href="https://alerts.in.ua/api-request" target="_blank">отримати токен</a></small>
<label>Токен ukrainealarm.com (резервне джерело, поки не використовується)</label>
<input name="uk" id="uk" autocomplete="off" autocapitalize="off">
<small>Поле порожнє = залишити збережене значення.</small>
</div>
<button type="submit">Зберегти</button>
</form>
<p style="margin-top:22px"><a href="#" id="tst">Тест: показати екран тривоги на пристрої</a></p>
<p><a href="#" id="nl">Замітки календаря (дні народження, дати) →</a></p>
<h2>Обслуговування</h2>
<div class="card">
<label>Оновлення прошивки: оберіть файл .bin</label>
<input type="file" id="fwf" accept=".bin">
<button type="button" id="fwb" style="margin-top:12px">Оновити прошивку</button>
<small id="fwst"></small>
<label style="margin-top:22px">Повне скидання: стирає налаштування і збережений Wi-Fi</label>
<button type="button" id="rst" style="margin-top:6px;background:#8a2b2b">Скинути до заводських</button>
</div>
<script>
)HTML";

static const char PAGE_B[] PROGMEM = R"HTML(
const q=id=>document.getElementById(id);
q('pin').value=PIN;
const LV={'-1':['Немає даних','#555'],'0':['Спокійно','#1b8a3a'],'1':['Жовтий','#d9a400'],'2':['ТРИВОГА','#d62828']};
function rd(n){const so=q('o'+n),sr=q('r'+n);sr.innerHTML='';sr.add(new Option('— уся область —','-1'));
 RG.forEach((r,i)=>{if(String(r[1])===so.value)sr.add(new Option(r[0],i));});}
function initSlot(n){
 const so=q('o'+n);so.add(new Option('— не використовувати —','-1'));
 ST.forEach((s,i)=>so.add(new Option(s,i)));
 so.onchange=()=>rd(n);
 const k=CUR.k[n],i=CUR.i[n];
 if(k===2&&RG[i]){so.value=String(RG[i][1]);rd(n);q('r'+n).value=String(i);}
 else if(k===1){so.value=String(i);rd(n);}
 else{so.value='-1';rd(n);}
 q('u'+n).value=CUR.u[n]||'';q('t'+n).value=CUR.t[n]||'';
 q('n'+n).textContent=CUR.t[n]||('Локація '+(n+1));
}
function packSlot(n){
 const o=parseInt(q('o'+n).value),r=parseInt(q('r'+n).value);let k=0,i=0;
 if(o>=0){if(r>=0){k=2;i=r;}else{k=1;i=o;}}
 q('k'+n).value=k;q('i'+n).value=i;
}
function pack(){packSlot(0);packSlot(1);return true;}
initSlot(0);initSlot(1);
q('txt').value=CUR.txt;q('city').value=CUR.city;q('wt').value=CUR.wt;
q('pm').value=String(CUR.pm);q('no').value=String(CUR.no);q('nb').value=CUR.nb;
q('ab').value=String(CUR.ab);q('lm').value=String(CUR.lm);q('ms').value=String(CUR.ms);
q('owm').placeholder=CUR.ho?'збережено':'вставте ключ';
q('alerts').placeholder=CUR.ha?'збережено':'вставте токен';
q('uk').placeholder=CUR.hu?'збережено':'необов\'язково';
for(let n=0;n<2;n++){const v=LV[String(CUR.a[n])]||LV['-1'];const e=q('s'+n);e.textContent=v[0];e.style.background=v[1];}
setInterval(()=>fetch('/api/status?pin='+encodeURIComponent(PIN)).catch(()=>{}),30000);
q('tst').onclick=e=>{e.preventDefault();fetch('/test?pin='+PIN);};
q('nl').href='/notes?pin='+PIN;
q('fwb').onclick=()=>{
 const f=q('fwf').files[0];
 if(!f){alert('Спершу оберіть файл .bin');return;}
 if(!confirm('Оновити прошивку? Плата перезавантажиться.'))return;
 const x=new XMLHttpRequest();x.open('POST','/update?pin='+PIN);
 x.upload.onprogress=e=>{if(e.lengthComputable)q('fwst').textContent='Завантаження '+Math.round(e.loaded*100/e.total)+'%';};
 x.onload=()=>{q('fwst').textContent=x.status==200?'Готово ✓ Плата перезавантажується…':'Помилка: '+x.responseText;};
 x.onerror=()=>{q('fwst').textContent='Зв\'язок обірвано (якщо плата вже перезавантажилась — усе гаразд)';};
 const fd=new FormData();fd.append('firmware',f);x.send(fd);
};
q('rst').onclick=()=>{
 if(!confirm('Стерти ВСІ налаштування і Wi-Fi? Плата перезавантажиться.'))return;
 fetch('/reset?pin='+PIN,{method:'POST'}).catch(()=>{});
 q('fwst').textContent='Скинуто. Плата перезавантажується…';
};
</script></body></html>
)HTML";

static void webJsStr(String& out, const char* s) {
  out += '"';
  for (; *s; s++) {
    unsigned char c = (unsigned char)*s;
    if (c == '"' || c == '\\') {
      out += '\\';
      out += (char)c;
    } else if (c == '<') out += "\\u003c";
    else if (c < 0x20) out += ' ';
    else out += (char)c;
  }
  out += '"';
}

static bool webAuth() {
  bool ok = webServer.arg("pin") == String(cfg.pin);
  if (ok) {
    webConfigLastActivityMs = millis();
    webConfigSessionActive = true;
  }
  return ok;
}

static void webDenied() {
  webServer.send(403, "text/plain; charset=utf-8", "Скануйте QR-код на екрані пристрою");
}

static void webRoot() {
  if (!webAuth()) {
    webDenied();
    return;
  }
  AppCfg c;
  portENTER_CRITICAL(&cfgMux);
  memcpy(&c, &cfg, sizeof(c));
  portEXIT_CRITICAL(&cfgMux);

  // Динамічна частина сторінки збирається в пам'яті
  String d;
  d.reserve(7500);
  d += "const PIN=";
  webJsStr(d, c.pin);
  d += ";const ST=[";
  for (int i = 0; i < UA_MAP_STATE_COUNT; i++) {
    if (i) d += ',';
    webJsStr(d, uaMapStateNames[i]);
  }
  d += "];const RG=[";
  for (int i = 0; i < UA_MAP_REGION_COUNT; i++) {
    if (i) d += ',';
    d += '[';
    webJsStr(d, uaMapRegionNames[i]);
    d += ',';
    d += (int)pgm_read_byte(&uaMapRegionState[i]);
    d += ']';
  }
  d += "];const CUR={k:[";
  d += (int)c.locKind[0];
  d += ',';
  d += (int)c.locKind[1];
  d += "],i:[";
  d += (int)c.locIdx[0];
  d += ',';
  d += (int)c.locIdx[1];
  d += "],u:[";
  d += (unsigned long)c.cityUid[0];
  d += ',';
  d += (unsigned long)c.cityUid[1];
  d += "],t:[";
  webJsStr(d, c.locTitle[0]);
  d += ',';
  webJsStr(d, c.locTitle[1]);
  d += "],txt:";
  webJsStr(d, c.alarmText);
  d += ",city:";
  webJsStr(d, c.weatherCity);
  d += ",wt:";
  webJsStr(d, c.weatherTitle);
  d += ",ho:";
  d += c.owmKey[0] ? "true" : "false";
  d += ",ha:";
  d += c.alertsToken[0] ? "true" : "false";
  d += ",hu:";
  d += c.ukAlarmToken[0] ? "true" : "false";
  d += ",pm:";
  d += (int)c.pressMm;
  d += ",no:";
  d += (int)c.nightOn;
  d += ",nb:";
  d += (int)c.nightBr;
  d += ",ab:";
  d += (int)c.autoBr;
  d += ",ms:";
  d += (int)c.minuteSilence;
  d += ",lm:";
  d += (int)c.ledMode;
  d += ",a:[";
  d += (int)alertCurLevel[0];
  d += ',';
  d += (int)alertCurLevel[1];
  d += "]};";

  const size_t total = strlen_P(PAGE_A) + d.length() + strlen_P(PAGE_B);
  webServer.sendHeader("Cache-Control", "no-store");

  // ГОЛОВНЕ ДЛЯ СТАБІЛЬНОСТІ: уся сторінка йде ОДНІЄЮ відповіддю з точним розміром.
  // Раніше було багато маленьких шматків (chunked) — телефон то отримував їх, то зависав.
  String page;
  if (page.reserve(total + 16)) {
    page += FPSTR(PAGE_A);
    page += d;
    page += FPSTR(PAGE_B);
    webServer.send(200, "text/html; charset=utf-8", page);
  } else {  // запасний варіант, якщо мало пам'яті: той самий розмір, три шматки
    webServer.setContentLength(total);
    webServer.send(200, "text/html; charset=utf-8", "");
    webServer.sendContent_P(PAGE_A);
    webServer.sendContent(d);
    webServer.sendContent_P(PAGE_B);
  }
}

// Залишає в рядку лише символи, що можуть бути в ключах/токенах
static void webCleanToken(String& s) {
  String o;
  for (unsigned i = 0; i < s.length(); i++) {
    char ch = s[i];
    if (isalnum((unsigned char)ch) || ch == ':' || ch == '_' || ch == '-' || ch == '.') o += ch;
  }
  s = o;
}

static void webSave() {
  if (!webAuth()) {
    webDenied();
    return;
  }
  AppCfg t;
  portENTER_CRITICAL(&cfgMux);
  memcpy(&t, &cfg, sizeof(t));
  portEXIT_CRITICAL(&cfgMux);

  // ключі: порожнє поле = залишити як було
  String v;
  v = webServer.arg("owm");
  webCleanToken(v);
  if (v.length()) strlcpy(t.owmKey, v.c_str(), sizeof(t.owmKey));
  v = webServer.arg("alerts");
  webCleanToken(v);
  if (v.length()) strlcpy(t.alertsToken, v.c_str(), sizeof(t.alertsToken));
  v = webServer.arg("uk");
  webCleanToken(v);
  if (v.length()) strlcpy(t.ukAlarmToken, v.c_str(), sizeof(t.ukAlarmToken));

  // місто для погоди: лише латиниця, цифри, кома, пробіл (→ "+")
  v = webServer.arg("city");
  v.trim();
  String city;
  for (unsigned i = 0; i < v.length(); i++) {
    char ch = v[i];
    if (isalnum((unsigned char)ch) || ch == ',' || ch == '-' || ch == '.') city += ch;
    else if (ch == ' ') city += '+';
  }
  strlcpy(t.weatherCity, city.c_str(), sizeof(t.weatherCity));

  v = webServer.arg("wt");
  v.trim();
  cfgCopyUtf8(t.weatherTitle, sizeof(t.weatherTitle), v.c_str());
  cfgScreenText(t.weatherTitle);

  v = webServer.arg("txt");
  v.trim();
  if (v.length() == 0) v = "ТРИВОГА!";
  cfgCopyUtf8(t.alarmText, sizeof(t.alarmText), v.c_str());
  cfgScreenText(t.alarmText);

  t.pressMm = webServer.arg("pm").toInt() ? 1 : 0;
  t.nightOn = webServer.arg("no").toInt() ? 1 : 0;
  t.autoBr = webServer.arg("ab").toInt() ? 1 : 0;
  int lm = webServer.arg("lm").toInt();
  t.ledMode = (lm >= 0 && lm <= 2) ? lm : 1;
  t.minuteSilence = webServer.arg("ms").toInt() ? 1 : 0;
  String nbArg = webServer.arg("nb");
  if (nbArg.length()) t.nightBr = constrain((int)nbArg.toInt(), 3, 100);

  for (int n = 0; n < 2; n++) {
    char key[4];
    snprintf(key, sizeof(key), "k%d", n);
    int k = webServer.arg(key).toInt();
    snprintf(key, sizeof(key), "i%d", n);
    int idx = webServer.arg(key).toInt();
    if (k == 1 && idx >= 0 && idx < UA_MAP_STATE_COUNT) {
      t.locKind[n] = 1;
    } else if (k == 2 && idx >= 0 && idx < UA_MAP_REGION_COUNT) {
      t.locKind[n] = 2;
    } else {
      t.locKind[n] = 0;
      idx = 0;
    }
    t.locIdx[n] = (int16_t)idx;

    snprintf(key, sizeof(key), "u%d", n);
    String us = webServer.arg(key);
    us.trim();
    t.cityUid[n] = (us.length() && us.length() < 10) ? (uint32_t)strtoul(us.c_str(), nullptr, 10) : 0;

    snprintf(key, sizeof(key), "t%d", n);
    String title = webServer.arg(key);
    title.trim();
    if (title.length() == 0 && t.locKind[n] != 0)
      title = (t.locKind[n] == 1) ? uaMapStateNames[idx] : uaMapRegionNames[idx];
    cfgCopyUtf8(t.locTitle[n], sizeof(t.locTitle[n]), title.c_str());
    cfgScreenText(t.locTitle[n]);
  }

  cfgSave(t);  // повільно (флеш) — поза критичною секцією
  portENTER_CRITICAL(&cfgMux);
  memcpy(&cfg, &t, sizeof(cfg));
  portEXIT_CRITICAL(&cfgMux);
  cfgDirty = true;

  String ok = F("<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
                "<meta http-equiv=refresh content='2;url=/?pin=");
  ok += t.pin;
  ok += F("'><body style='font:20px system-ui;background:#0b1020;color:#e8ecf5;text-align:center;padding-top:30vh'>Збережено ✓");
  webServer.send(200, "text/html; charset=utf-8", ok);
}

static void webStatus() {
  if (!webAuth()) {
    webDenied();
    return;
  }
  String s = "{\"a\":[";
  s += (int)alertCurLevel[0];
  s += ',';
  s += (int)alertCurLevel[1];
  s += "]}";
  webServer.send(200, "application/json", s);
}

static void webTest() {
  if (!webAuth()) {
    webDenied();
    return;
  }
  alertEvent = 1;  // як справжня тривога: звук + червоний екран
  webServer.send(200, "text/plain", "ok");
}

// ---------- Оновлення прошивки через сторінку (без сервера) ----------
static bool otaAuthOk = false;

static void webUpdateUpload() {
  HTTPUpload& up = webServer.upload();
  if (up.status == UPLOAD_FILE_START) {
    otaAuthOk = webAuth();
    if (otaAuthOk && !Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (otaAuthOk && !Update.hasError()) {
      if (Update.write(up.buf, up.currentSize) != up.currentSize) Update.printError(Serial);
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (otaAuthOk && !Update.hasError()) {
      if (!Update.end(true)) Update.printError(Serial);
    }
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    otaAuthOk = false;
  }
}

static void webUpdateDone() {
  if (!otaAuthOk) {
    webDenied();
    return;
  }
  otaAuthOk = false;
  if (Update.hasError()) {
    String e = String("Не вдалось: ") + Update.errorString() + " (потрібна схема розділів з OTA?)";
    webServer.send(500, "text/plain; charset=utf-8", e);
    return;
  }
  webServer.send(200, "text/plain", "OK");
  delay(500);
  ESP.restart();
}

static void webReset() {
  if (!webAuth()) {
    webDenied();
    return;
  }
  cfgFactoryReset();
  WiFi.disconnect(true, true);  // забути збережений Wi-Fi
  webServer.send(200, "text/plain", "OK");
  delay(500);
  ESP.restart();
}

inline void webStart() {
  if (!webRoutesSet) {
    webServer.on("/", HTTP_GET, webRoot);
    webServer.on("/save", HTTP_POST, webSave);
    webServer.on("/api/status", HTTP_GET, webStatus);
    webServer.on("/test", HTTP_GET, webTest);
    webServer.on("/update", HTTP_POST, webUpdateDone, webUpdateUpload);
    webServer.on("/reset", HTTP_POST, webReset);
    notesWebRoutes();
    webServer.onNotFound([]() {
      webServer.send(404, "text/plain", "404");
    });
    webRoutesSet = true;
  }
  if (!webStarted) {
    webServer.begin();
    webStarted = true;
  }
}

inline void webStop() {
  if (webStarted) {
    webServer.stop();
    webStarted = false;
  }
}

inline void webLoop() {
  if (webStarted) webServer.handleClient();
}
