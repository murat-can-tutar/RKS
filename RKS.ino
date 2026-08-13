#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <EEPROM.h>
#include <ESP32Servo.h>
#include <Adafruit_NeoPixel.h>

/* ========================================================================
   1. GENEL VE DONANIM TANIMLAMALARI
   ======================================================================== */
const char* apSSID     = "RKS";
const char* apPassword = "44AFT748";

WebServer server(80);

// Active-LOW Röle Mantığı
const uint8_t RELAY_ON  = LOW;
const uint8_t RELAY_OFF = HIGH;

// Röle Pin Haritası
#define RELAY_LOCK_OPEN    13
#define RELAY_LOCK_CLOSE   14
#define RELAY_START        16
#define RELAY_ALARM        17
#define RELAY_LED          18
#define RELAY_DOOR         21
#define RELAY_SEAT         22  // Koltuk
#define RELAY_HEADLIGHT    23  // Ana Far ve Sis Farı

// Servo ve Tetik Pinleri
#define SERVO_RIGHT_PIN    25
#define SERVO_LEFT_PIN     26
#define SERVO_TRIGGER_PIN  27  // Kontak Hattı

// Adreslenebilir LED (12V WS2811 / WS281 ARGB)
#define LED_STRIP_PIN      32
#define NUM_LEDS           16  // Şeridinizdeki toplam piksel sayısı
Adafruit_NeoPixel strip(NUM_LEDS, LED_STRIP_PIN, NEO_BRG + NEO_KHZ800);

/* ========================================================================
   2. HAFIZA VE YAPILANDIRMA (EEPROM)
   ======================================================================== */
#define EEPROM_SIZE 64
#define EEPROM_MAGIC 0x4E

struct SystemConfig {
  uint8_t magic;
  uint8_t rightOpen;
  uint8_t leftOpen;
  uint8_t rightClosed;
  uint8_t leftClosed;
  uint8_t animType;
  uint8_t colorR;
  uint8_t colorG;
  uint8_t colorB;
  uint8_t brightness;
  uint16_t animSpeed;
};

SystemConfig cfg;

/* ========================================================================
   3. DURUM DEĞİŞKENLERİ VE ZAMANLAYICILAR
   ======================================================================== */
Servo servoRight;
Servo servoLeft;
bool servosAttached = false;
unsigned long servoAttachTimer = 0;

bool motorRunning   = false;
bool alarmActive    = false;
bool ledState       = false;
bool headlightState = false;
bool fogActive      = false;
bool mirrorsOpen    = false;
bool otaOK          = false;

// Sis Farı 3 Tetik Otomasyonu (Non-blocking)
enum FogSeqState { FOG_IDLE, FOG_P1_ON, FOG_P1_OFF, FOG_P2_ON, FOG_P2_OFF, FOG_P3_ON };
FogSeqState fogSeq = FOG_IDLE;
unsigned long fogTimer = 0;

// LED Animasyon Durumları
enum AnimState { ANIM_OFF, ANIM_OPENING, ANIM_ON, ANIM_CLOSING };
AnimState animState = ANIM_OFF;
int animStep = 0;
int animSubStep = 0;

// Log Tamponu
String logsBuf[10];
uint8_t logsCount = 0;

/* ========================================================================
   4. WEB PANEL (PROGMEM HTML / CSS / JS)
   ======================================================================== */
const char page[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="tr"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>RKS Newlight 125 Pro</title>
<style>
:root{
  --bg:#0b1220;--card:#131c2f;--card2:#1a2740;--line:#2b3a57;
  --txt:#edf2ff;--muted:#9fb0d0;--red:#d63b3b;--green:#1db36b;
  --blue:#2f7df6;--orange:#f08b2d;--gray:#59657d;
}
*{box-sizing:border-box}
body{
  margin:0;font-family:Arial,Helvetica,sans-serif;
  background:linear-gradient(180deg,#08101c 0%,#0d1728 100%);
  color:var(--txt);padding:16px;
}
.wrap{max-width:880px;margin:auto}
.card{
  background:linear-gradient(180deg,var(--card) 0%,var(--card2) 100%);
  border:1px solid var(--line);border-radius:18px;padding:16px;
  margin-bottom:12px;box-shadow:0 10px 25px rgba(0,0,0,.25);
}
h1,h2,h3{margin:0 0 12px}h1{font-size:24px}h3{font-size:17px}
.row{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:10px}
button,.selectStyle,.inputStyle{
  width:100%;min-height:48px;border:none;border-radius:12px;
  color:#fff;font-size:14px;font-weight:700;cursor:pointer;
  transition:.18s ease;box-shadow:0 6px 14px rgba(0,0,0,.18);
}
button:active{transform:scale(.985)}
button:disabled{opacity:.45;cursor:not-allowed}
.red{background:linear-gradient(180deg,#ef5555,#c83030)}
.green{background:linear-gradient(180deg,#24cc7a,#14945a)}
.blue{background:linear-gradient(180deg,#3f96ff,#216ce2)}
.orange{background:linear-gradient(180deg,#f6a246,#dc7821)}
.gray{background:linear-gradient(180deg,#72809a,#556178)}
.sectionTitle{display:flex;justify-content:space-between;align-items:center;gap:8px;margin-bottom:12px}
.statusPill{
  display:inline-flex;align-items:center;gap:8px;padding:8px 12px;
  border-radius:999px;background:#0f1727;border:1px solid var(--line);
  color:#d9e5ff;font-weight:700;font-size:13px;
}
.dot{width:9px;height:9px;border-radius:50%;background:#20d17a;box-shadow:0 0 10px #20d17a}
.grid2{display:grid;grid-template-columns:1fr 1fr;gap:12px}
@media(max-width:760px){.grid2{grid-template-columns:1fr}}
.sliderWrap,.selectWrap{
  background:#0f1727;border:1px solid var(--line);border-radius:14px;
  padding:12px;text-align:left;
}
.sliderHead{display:flex;justify-content:space-between;margin-bottom:8px;font-size:13px}
input[type=range]{width:100%}
input[type=color]{height:45px;padding:2px;background:#0f1727;border:1px solid var(--line);border-radius:10px;cursor:pointer}
select{
  background:#0f1727;color:var(--txt);border:1px solid var(--line);
  padding:10px;border-radius:10px;font-weight:bold;outline:none;
}
.uploadBox{background:#0f1727;border:1px solid var(--line);border-radius:14px;padding:12px}
input[type=file]{width:100%;color:#dbe6ff;margin-bottom:10px}
#log{
  height:180px;overflow:auto;text-align:left;background:#04070d;
  color:#2aff84;border:1px solid var(--line);border-radius:14px;
  padding:12px;font-family:monospace;font-size:12px;line-height:1.5;
}
.hero{display:flex;justify-content:space-between;gap:12px;align-items:center;flex-wrap:wrap}
.badge{padding:8px 12px;border-radius:10px;background:#0f1727;border:1px solid var(--line);color:#d9e5ff;font-size:12px;font-weight:700}
.footer{margin-top:16px;text-align:center;font-size:12px;color:var(--muted);opacity:.9}
</style></head><body>
<div class="wrap">

  <div class="card">
    <div class="hero">
      <div><h1>RKS Newlight Pro</h1></div>
      <div style="display:flex;gap:8px;flex-wrap:wrap">
        <div class="badge">Şase: L8YNCBPS0RKB21245</div>
        <div class="statusPill"><span class="dot"></span><span id="st">Bağlanıyor...</span></div>
      </div>
    </div>
  </div>

  <div class="card">
    <div class="sectionTitle"><h3>Sistem Kontrolü</h3></div>
    <div class="row">
      <button id="motorBtn" class="blue" onclick="cmd('motor')">Start</button>
      <button id="bo" class="gray ctrl" onclick="cmd('lock_open')">Kilit Aç</button>
      <button id="bc" class="gray ctrl" onclick="cmd('lock_close')">Kilitle</button>
      <button id="balarm" class="gray ctrl" onclick="cmd('alarm_toggle')">Alarm</button>
      <button id="bfar" class="gray ctrl" onclick="cmd('far')">Far</button>
      <button id="bfog" class="gray ctrl" onclick="cmd('fog')">Çakar</button>
      <button id="bled" class="gray ctrl" onclick="cmd('led')">Led</button>
      <button id="bseat" class="gray ctrl" onclick="cmd('seat')">Koltuk</button>
    </div>
  </div>

  <div class="card">
    <div class="sectionTitle"><h3>Far Ayarları</h3></div>
    <div class="grid2">
      <div class="selectWrap">
        <div class="sliderHead"><span>Animasyon Stili</span></div>
        <select id="animSel" class="selectStyle">
          <option value="0">1. Rastgele Parıltılı Nefes Efekti</option>
          <option value="1">2. Çift Uçtan Merkeze Senkronize</option>
          <option value="2">3. Sinematik Nefes Alarak Dolum</option>
          <option value="3">4. Cyberpunk Tarama Çizgisi</option>
          <option value="4">5. 3 Aşamalı Özel Kombinasyon</option>
        </select>
      </div>
      <div class="selectWrap">
        <div class="sliderHead"><span>Renk Seçimi</span></div>
        <input type="color" id="ledColor" class="inputStyle" value="#ffffff">
      </div>
      <div class="sliderWrap" style="grid-column: span 2">
        <div class="sliderHead"><span>Parlaklık</span><b id="briV">255</b></div>
        <input id="bri" type="range" min="10" max="255" value="255" oninput="id('briV').textContent=this.value">
      </div>
      <div class="sliderWrap" style="grid-column: span 2">
        <div class="sliderHead"><span>Animasyon Hızı</span><b id="speedVal">30</b></div>
        <input id="animSpeed" type="range" min="5" max="100" value="30" oninput="document.getElementById('speedVal').textContent=this.value">
      </div>
    </div>
    <div style="margin-top:12px">
      <button class="blue ctrl" onclick="saveLed()">Far Ayarlarını Kaydet</button>
    </div>
  </div>

  <div class="card">
    <div class="sectionTitle"><h3>Katlanır Ayna Ayarları</h3></div>
    <div class="grid2">
      <div class="sliderWrap"><div class="sliderHead"><span>Sağ Açık</span><b id="roV">90</b></div><input id="ro" type="range" min="0" max="180" value="90" oninput="syncV()"></div>
      <div class="sliderWrap"><div class="sliderHead"><span>Sol Açık</span><b id="loV">90</b></div><input id="lo" type="range" min="0" max="180" value="90" oninput="syncV()"></div>
      <div class="sliderWrap"><div class="sliderHead"><span>Sağ Kapalı</span><b id="rcV">0</b></div><input id="rc" type="range" min="0" max="180" value="0" oninput="syncV()"></div>
      <div class="sliderWrap"><div class="sliderHead"><span>Sol Kapalı</span><b id="lcV">0</b></div><input id="lc" type="range" min="0" max="180" value="0" oninput="syncV()"></div>
    </div>
    <div style="margin-top:12px"><button class="blue ctrl" onclick="saveMir()">Ayna Ayarlarını Kaydet</button></div>
  </div>

  <div class="grid2">
    <div class="card">
      <div class="sectionTitle"><h3>OTA Güncelleme</h3></div>
      <div class="uploadBox">
        <form method="POST" action="/update" enctype="multipart/form-data">
          <input type="file" name="update"><button class="blue" type="submit">Sistemi Güncele</button>
        </form>
      </div>
    </div>
    <div class="card">
      <div class="sectionTitle"><h3>Canlı Sistem Logları</h3></div>
      <div id="log"></div>
    </div>
  </div>

  <div class="footer">• Designed and Programmed by Murat Can TUTAR •</div>
</div>

<script>
let initDone=false;
function id(x){ return document.getElementById(x); }
function syncV(){
  id('roV').textContent=id('ro').value; id('loV').textContent=id('lo').value;
  id('rcV').textContent=id('rc').value; id('lcV').textContent=id('lc').value;
}
function setCtrls(ok){ document.querySelectorAll('.ctrl').forEach(e=>e.disabled=!ok); }

function cmd(x){
  fetch('/c?x='+encodeURIComponent(x),{cache:'no-store'}).then(()=>setTimeout(upd,150))
  .catch(()=>{ id('st').textContent='Bağlantı Yok'; setCtrls(false); });
}

function saveMir(){
  fetch('/m?ro='+id('ro').value+'&lo='+id('lo').value+'&rc='+id('rc').value+'&lc='+id('lc').value,{cache:'no-store'})
  .then(()=>setTimeout(upd,150));
}

function hexToRgb(hex) {
  let c = parseInt(hex.substring(1), 16);
  return {r: (c >> 16) & 255, g: (c >> 8) & 255, b: c & 255};
}

function rgbToHex(r, g, b) {
  return "#" + ((1 << 24) + (r << 16) + (g << 8) + b).toString(16).slice(1);
}

function saveLed(){
  let rgb = hexToRgb(id('ledColor').value);
  let at = id('animSel').value;
  let br = id('bri').value;
  let sp = id('animSpeed').value;

  fetch('/set_led?at='+at+'&r='+rgb.r+'&g='+rgb.g+'&b='+rgb.b+'&br='+br+'&animSpeed='+sp, {cache:'no-store'})
    .then(()=>setTimeout(upd,150));
}

function upd(){
  fetch('/s',{cache:'no-store'}).then(r=>r.json()).then(d=>{
    id('st').textContent=d.st; setCtrls(true);
    if(!initDone){
      id('ro').value=d.ro; id('lo').value=d.lo;
      id('rc').value=d.rc; id('lc').value=d.lc;
      id('animSel').value=d.at;
      id('animSpeed').value=d.sp; id('speedVal').textContent=d.sp;
      id('ledColor').value=rgbToHex(d.cr, d.cg, d.cb);
      id('bri').value=d.br; id('briV').textContent=d.br;
      syncV(); initDone=true;
    }
    id('motorBtn').textContent=d.m?'Stop':'Start';
    id('motorBtn').className=d.m?'red':'blue';
    id('balarm').className=d.a?'green ctrl':'gray ctrl';
    id('bfar').className=d.f?'green ctrl':'gray ctrl';
    id('bfog').className=d.g?'green ctrl':'gray ctrl';
    id('bled').className=d.l?'green ctrl':'gray ctrl';
    id('bo').disabled=d.m; id('bc').disabled=d.m; id('balarm').disabled=d.m;
    id('log').innerHTML=d.lg||''; id('log').scrollTop=id('log').scrollHeight;
  }).catch(()=>{ id('st').textContent='Bağlantı Yok'; setCtrls(false); });
}
syncV(); upd(); setInterval(upd,1000);
</script></body></html>
)rawliteral";

/* ========================================================================
   5. YARDIMCI FONKSİYONLAR VE HAFIZA YÖNETİMİ
   ======================================================================== */
void addLog(const String &s) {
  if (logsCount < 10) logsBuf[logsCount++] = s;
  else {
    for (uint8_t i = 0; i < 9; i++) logsBuf[i] = logsBuf[i + 1];
    logsBuf[9] = s;
  }
}

String getLogs() {
  String out;
  for (uint8_t i = 0; i < logsCount; i++) {
    out += logsBuf[i];
    if (i < logsCount - 1) out += "<br>";
  }
  return out;
}

void relayWrite(uint8_t pin, bool onState) {
  digitalWrite(pin, onState ? RELAY_ON : RELAY_OFF);
}

void pulseRelay(uint8_t pin, uint16_t onMs = 250) {
  relayWrite(pin, true);
  delay(onMs);
  relayWrite(pin, false);
}

void loadCfg() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, cfg);
  if (cfg.magic != EEPROM_MAGIC) {
    cfg.magic       = EEPROM_MAGIC;
    cfg.rightOpen   = 90; cfg.leftOpen   = 90;
    cfg.rightClosed = 0;  cfg.leftClosed = 0;
    cfg.animType    = 0;  cfg.brightness = 255;
    cfg.colorR      = 255; cfg.colorG    = 255; cfg.colorB = 255;
    cfg.animSpeed   = 30;
    EEPROM.put(0, cfg); EEPROM.commit();
  }
}

void saveCfg() {
  cfg.magic = EEPROM_MAGIC;
  EEPROM.put(0, cfg); EEPROM.commit();
}

/* ========================================================================
   6. SERVO KATLANIR AYNA MİMARİSİ (DETACH DESTEKLİ)
   ======================================================================== */
void attachServos() {
  if (!servosAttached) {
    servoRight.attach(SERVO_RIGHT_PIN, 500, 2400);
    servoLeft.attach(SERVO_LEFT_PIN, 500, 2400);
    servosAttached = true;
  }
  servoAttachTimer = millis();
}

void checkServoDetach() {
  if (servosAttached && (millis() - servoAttachTimer >= 1200)) {
    servoRight.detach();
    servoLeft.detach();
    servosAttached = false;
  }
}

void writeMirrors(int r, int l) {
  attachServos();
  servoRight.write(constrain(r, 0, 180));
  servoLeft.write(constrain(l, 0, 180));
}

void openMirrors() {
  writeMirrors(cfg.rightOpen, cfg.leftOpen);
  mirrorsOpen = true;
  addLog("Aynalar açıldı");
}

void closeMirrors() {
  writeMirrors(cfg.rightClosed, cfg.leftClosed);
  mirrorsOpen = false;
  addLog("Aynalar kapandı");
}

/* ========================================================================
   7. SİS FARI 3 TETİK OTOMASYONU (NON-BLOCKING)
   ======================================================================== */
void triggerFogSequence() {
  if (!fogActive) {
    fogActive = true;
    fogSeq = FOG_P1_ON;
    fogTimer = millis();
    relayWrite(RELAY_HEADLIGHT, true);
    addLog("Sis modu baslatildi (3 tetik)");
  } else {
    fogActive = false;
    fogSeq = FOG_IDLE;
    headlightState = false;
    relayWrite(RELAY_HEADLIGHT, false);
    addLog("Sis/Far kapatildi");
  }
}

void handleFogSequence() {
  if (fogSeq == FOG_IDLE) return;

  unsigned long now = millis();
  uint16_t pulseTime = 140;

  if (fogSeq == FOG_P1_ON && now - fogTimer >= pulseTime) {
    relayWrite(RELAY_HEADLIGHT, false);
    fogSeq = FOG_P1_OFF; fogTimer = now;
  }
  else if (fogSeq == FOG_P1_OFF && now - fogTimer >= pulseTime) {
    relayWrite(RELAY_HEADLIGHT, true);
    fogSeq = FOG_P2_ON; fogTimer = now;
  }
  else if (fogSeq == FOG_P2_ON && now - fogTimer >= pulseTime) {
    relayWrite(RELAY_HEADLIGHT, false);
    fogSeq = FOG_P2_OFF; fogTimer = now;
  }
  else if (fogSeq == FOG_P2_OFF && now - fogTimer >= pulseTime) {
    relayWrite(RELAY_HEADLIGHT, true);
    fogSeq = FOG_P3_ON; fogTimer = now;
  }
  else if (fogSeq == FOG_P3_ON) {
    headlightState = true;
    fogSeq = FOG_IDLE;
  }
}

/* ========================================================================
   8. ÖN FAR ARGB LED ANİMASYON MOTORU
   ======================================================================== */
void setAllLeds(uint8_t r, uint8_t g, uint8_t b) {
  for (int i = 0; i < NUM_LEDS; i++) strip.setPixelColor(i, strip.Color(r, g, b));
  strip.show();
}

void handleLedAnimation() { 
  if (animState == ANIM_ON || animState == ANIM_OFF) return;

  static unsigned long localAnimTimer = 0;
  // Çubuk sağa kaydıkça hız artacak şekilde ayarlandı (yüksek değer = küçük gecikme = daha hızlı)
  uint16_t currentDelay = map(cfg.animSpeed, 5, 100, 110, 5); 
  if (millis() - localAnimTimer < currentDelay) {
    return;
  }
  localAnimTimer = millis();

  int totalLeds = NUM_LEDS;
  int half = totalLeds / 2;
  uint32_t mainColor = strip.Color(cfg.colorR, cfg.colorG, cfg.colorB);

  /* --- AÇILIŞ ANİMASYONLARI --- */
  if (animState == ANIM_OPENING) {
    
    // Stili 0: Rastgele Parıltılı Nefes Efekti
    if (cfg.animType == 0) { 
      if (animStep < 50) {
        int currentBrightness = map(animStep, 0, 50, 10, 255);
        uint32_t breathColor = strip.Color(
          (cfg.colorR * currentBrightness) / 255,
          (cfg.colorG * currentBrightness) / 255,
          (cfg.colorB * currentBrightness) / 255
        );
        
        for (int i = 0; i < totalLeds; i++) {
          strip.setPixelColor(i, breathColor);
        }
        
        for (int k = 0; k < 5; k++) {
          int randIdx = random(totalLeds);
          strip.setPixelColor(randIdx, strip.Color(255, 255, 255));
        }
        
        strip.show();
        animStep++;
      } else {
        setAllLeds(cfg.colorR, cfg.colorG, cfg.colorB);
        animState = ANIM_ON;
        animStep = 0;
      }
    }
    
    // Stili 1: Çift Uçtan Merkeze Senkronize
    else if (cfg.animType == 1) { 
      if (animStep < half + 3) {
        for (int i = 0; i < totalLeds; i++) {
          uint32_t c = strip.getPixelColor(i);
          uint8_t r = (uint8_t)(c >> 16);
          uint8_t g = (uint8_t)(c >> 8);
          uint8_t b = (uint8_t)c;
          strip.setPixelColor(i, strip.Color(r * 0.75, g * 0.75, b * 0.75));
        }
        if (animStep < half) {
          strip.setPixelColor(animStep, mainColor);
          strip.setPixelColor(totalLeds - 1 - animStep, mainColor);
        }
        strip.show();
        animStep++;
      } else {
        setAllLeds(cfg.colorR, cfg.colorG, cfg.colorB);
        animState = ANIM_ON;
        animStep = 0;
      }
    }
    
    // Stili 2: Sinematik Nefes Alarak Dolum
    else if (cfg.animType == 2) { 
      if (animStep < 255) {
        animStep += 12;
        if (animStep > 255) animStep = 255;
        setAllLeds((cfg.colorR * animStep) / 255, (cfg.colorG * animStep) / 255, (cfg.colorB * animStep) / 255);
        if (animStep == 255) {
          animState = ANIM_ON;
          animStep = 0;
        }
      }
    }
    
    // Stili 3: Cyberpunk Tarama Çizgisi
    else if (cfg.animType == 3) { 
      if (animStep < totalLeds + 4) {
        for (int i = 0; i < totalLeds; i++) {
          uint32_t c = strip.getPixelColor(i);
          uint8_t r = (uint8_t)(c >> 16);
          uint8_t g = (uint8_t)(c >> 8);
          uint8_t b = (uint8_t)c;
          strip.setPixelColor(i, strip.Color(r * 0.6, g * 0.6, b * 0.6));
        }
        if (animStep < totalLeds) {
          strip.setPixelColor(animStep, mainColor);
          if (animStep > 0) strip.setPixelColor(animStep - 1, strip.Color(cfg.colorR / 2, cfg.colorG / 2, cfg.colorB / 2));
        }
        strip.show();
        animStep++;
      } else {
        setAllLeds(cfg.colorR, cfg.colorG, cfg.colorB);
        animState = ANIM_ON;
        animStep = 0;
      }
    }
    
    // Stili 4: 3 Aşamalı Özel Kombinasyon
    else if (cfg.animType == 4) { 
      static uint8_t phase4 = 0;
      static int blockIdx = 0;

      if (phase4 == 0) {
        if (animStep < half) {
          strip.setPixelColor(animStep, mainColor);
          strip.setPixelColor(totalLeds - 1 - animStep, mainColor);
          strip.show();
          animStep++;
        } else {
          animStep = 0;
          phase4 = 1;
        }
      } 
      else if (phase4 == 1) {
        if (animStep < half) {
          int idx1 = (half - 1) - animStep;
          int idx2 = half + animStep;
          strip.setPixelColor(idx1, 0);
          strip.setPixelColor(idx2, 0);
          strip.show();
          animStep++;
        } else {
          animStep = 0;
          blockIdx = 0;
          phase4 = 2;
        }
      } 
      else if (phase4 == 2) {
        int maxBlocks = (half + 2) / 3;
        if (blockIdx <= maxBlocks) {
          int startL = blockIdx * 3;
          int endL = min((blockIdx + 1) * 3, half);
          
          for (int i = startL; i < endL; i++) {
            strip.setPixelColor(i, mainColor);
            strip.setPixelColor(totalLeds - 1 - i, mainColor);
          }
          strip.show();
          blockIdx++;
        } else {
          setAllLeds(cfg.colorR, cfg.colorG, cfg.colorB);
          animState = ANIM_ON;
          animStep = 0;
          phase4 = 0;
          blockIdx = 0;
        }
      }
    }
  }

  /* --- KAPANIŞ ANİMASYONU --- */
  else if (animState == ANIM_CLOSING) {
    if (animStep < half) {
      strip.setPixelColor(half - 1 - animStep, 0);
      strip.setPixelColor(half + animStep, 0);
      strip.show();
      animStep++;
    } else {
      strip.clear();
      strip.show();
      animState = ANIM_OFF;
      animStep = 0;
      addLog("Animasyon kapandı");
    }
  }
}

/* ========================================================================
   9. WEB SERVER ARAYÜZ VE KOMUT YÖNETİMİ
   ======================================================================== */
String statusJson() {
  String j = "{";
  j += "\"st\":\"Bağlandı\",";
  j += "\"m\":" + String(motorRunning ? "true" : "false") + ",";
  j += "\"a\":" + String(alarmActive ? "true" : "false") + ",";
  j += "\"f\":" + String(headlightState ? "true" : "false") + ",";
  j += "\"g\":" + String(fogActive ? "true" : "false") + ",";
  j += "\"l\":" + String(ledState ? "true" : "false") + ",";
  j += "\"ro\":" + String(cfg.rightOpen) + ",";
  j += "\"lo\":" + String(cfg.leftOpen) + ",";
  j += "\"rc\":" + String(cfg.rightClosed) + ",";
  j += "\"lc\":" + String(cfg.leftClosed) + ",";
  j += "\"at\":" + String(cfg.animType) + ",";
  j += "\"sp\":" + String(cfg.animSpeed) + ",";
  j += "\"cr\":" + String(cfg.colorR) + ",";
  j += "\"cg\":" + String(cfg.colorG) + ",";
  j += "\"cb\":" + String(cfg.colorB) + ",";
  j += "\"br\":" + String(cfg.brightness) + ",";
  j += "\"lg\":\"" + getLogs() + "\"";
  j += "}";
  return j;
}

void handleCmd() {
  String x = server.arg("x");

  if (x == "motor") {
    if (!motorRunning) {
      addLog("Motor start -> Kilit Ac");
      pulseRelay(RELAY_LOCK_OPEN, 250);
      if (alarmActive) { alarmActive = false; addLog("Alarm kapatildi"); }
      delay(300);
      addLog("Motor start -> Mars x2");
      pulseRelay(RELAY_START, 250); delay(300); pulseRelay(RELAY_START, 250);
      motorRunning = true;
      addLog("Motor calisiyor");
    } else {
      pulseRelay(RELAY_LOCK_OPEN, 250);
      motorRunning = false;
      addLog("Motor durduruldu");
    }
    server.send(200, "text/plain", "OK"); return;
  }

  if (motorRunning && (x == "lock_open" || x == "lock_close" || x == "alarm_toggle")) {
    addLog("Engellendi: Motor acik");
    server.send(200, "text/plain", "LOCKED"); return;
  }

  if (x == "lock_open") {
    pulseRelay(RELAY_LOCK_OPEN, 1000);
    if (alarmActive) { alarmActive = false; addLog("Alarm kapatildi"); }
    addLog("Kilit ac [13]");
  }
  else if (x == "lock_close") {
    relayWrite(RELAY_LOCK_CLOSE, true); relayWrite(RELAY_DOOR, true);
    delay(1000);
    relayWrite(RELAY_LOCK_CLOSE, false); relayWrite(RELAY_DOOR, false);
    addLog("Kilit kapat [14] + Kapi [23]");
  }
  else if (x == "alarm_toggle") {
    if (!alarmActive) { pulseRelay(RELAY_ALARM, 300); alarmActive = true; addLog("Alarm baslatildi"); }
    else { pulseRelay(RELAY_LOCK_OPEN, 250); alarmActive = false; addLog("Alarm kapatildi"); }
  }
  else if (x == "far") {
    fogActive = false;
    headlightState = !headlightState;
    relayWrite(RELAY_HEADLIGHT, headlightState);
    addLog(headlightState ? "Far açıldı [21]" : "Far kapandı [21]");
  }
  else if (x == "fog") {
    triggerFogSequence();
  }
  else if (x == "led") {
    ledState = !ledState;
    relayWrite(RELAY_LED, ledState);
    addLog(ledState ? "Aksesuar LED açıldı [18]" : "Aydınlatma kapandı [18]");
  }
  else if (x == "seat") {
    addLog("Koltuk pulse [22]");
    pulseRelay(RELAY_SEAT, 1200);
  }

  server.send(200, "text/plain", "OK");
}

void handleLedSave() {
  if (server.hasArg("at") && server.hasArg("r") && server.hasArg("g") && server.hasArg("b") && server.hasArg("br")) {
    cfg.animType   = server.arg("at").toInt();
    cfg.colorR     = server.arg("r").toInt();
    cfg.colorG     = server.arg("g").toInt();
    cfg.colorB     = server.arg("b").toInt();
    cfg.brightness = server.arg("br").toInt();
    if (server.hasArg("animSpeed")) {
      cfg.animSpeed = server.arg("animSpeed").toInt();
    }
    saveCfg();
    addLog("LED ayarlari kaydedildi");

    strip.setBrightness(cfg.brightness);
    if (animState == ANIM_ON) {
      setAllLeds(cfg.colorR, cfg.colorG, cfg.colorB);
    }
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "ARGS");
  }
}

void handleMirrorSave() {
  if (server.hasArg("ro") && server.hasArg("lo") && server.hasArg("rc") && server.hasArg("lc")) {
    cfg.rightOpen   = constrain(server.arg("ro").toInt(), 0, 180);
    cfg.leftOpen    = constrain(server.arg("lo").toInt(), 0, 180);
    cfg.rightClosed = constrain(server.arg("rc").toInt(), 0, 180);
    cfg.leftClosed  = constrain(server.arg("lc").toInt(), 0, 180);
    saveCfg();
    addLog("Ayna ayarlari kaydedildi");

    if (digitalRead(SERVO_TRIGGER_PIN) == HIGH) openMirrors();
    else closeMirrors();

    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "ARGS");
  }
}

void handleUpdateUpload() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    otaOK = Update.begin(UPDATE_SIZE_UNKNOWN);
    addLog(otaOK ? "Web OTA basladi" : "OTA begin hatasi");
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (otaOK && Update.write(upload.buf, upload.currentSize) != upload.currentSize) otaOK = false;
  } else if (upload.status == UPLOAD_FILE_END) {
    if (otaOK && Update.end(true)) addLog("Web OTA tamamlandi");
    else otaOK = false;
  }
}

void handleUpdateDone() {
  server.sendHeader("Connection", "close");
  if (otaOK) {
    server.send(200, "text/plain", "GUNCELLEME_BASARILI");
    delay(700); ESP.restart();
  } else {
    server.send(500, "text/plain", "GUNCELLEME_HATA");
  }
}

/* ========================================================================
   10. SETUP VE MAIN LOOP
   ======================================================================== */
void setup() {
  Serial.begin(115200);
  pinMode(RELAY_LOCK_OPEN, OUTPUT);   digitalWrite(RELAY_LOCK_OPEN, RELAY_OFF);
  pinMode(RELAY_LOCK_CLOSE, OUTPUT);  digitalWrite(RELAY_LOCK_CLOSE, RELAY_OFF);
  pinMode(RELAY_START, OUTPUT);       digitalWrite(RELAY_START, RELAY_OFF);
  pinMode(RELAY_ALARM, OUTPUT);       digitalWrite(RELAY_ALARM, RELAY_OFF);
  pinMode(RELAY_LED, OUTPUT);         digitalWrite(RELAY_LED, RELAY_OFF);
  pinMode(RELAY_HEADLIGHT, OUTPUT);   digitalWrite(RELAY_HEADLIGHT, RELAY_OFF);
  pinMode(RELAY_SEAT, OUTPUT);        digitalWrite(RELAY_SEAT, RELAY_OFF);
  pinMode(RELAY_DOOR, OUTPUT);        digitalWrite(RELAY_DOOR, RELAY_OFF);

  pinMode(SERVO_TRIGGER_PIN, INPUT_PULLDOWN);

  loadCfg();

  strip.begin();
  strip.setBrightness(cfg.brightness);
  strip.show();

  servoRight.setPeriodHertz(50);
  servoLeft.setPeriodHertz(50);
  writeMirrors(cfg.rightClosed, cfg.leftClosed);

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  WiFi.softAP(apSSID, apPassword);

  server.on("/", HTTP_GET, [](){ server.send_P(200, "text/html; charset=utf-8", page); });
  server.on("/s", HTTP_GET, [](){ server.send(200, "application/json", statusJson()); });
  server.on("/c", HTTP_GET, handleCmd);
  server.on("/m", HTTP_GET, handleMirrorSave);
  server.on("/set_led", HTTP_GET, handleLedSave);
  server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
  server.begin();

  addLog("AP baslatildi");
  addLog("Sistem hazir");
}


void loop() {
  server.handleClient();
  handleFogSequence();
  handleLedAnimation();
  checkServoDetach();

  static bool trigState = false;
  static bool firstRun = true;

  bool trig = digitalRead(SERVO_TRIGGER_PIN);

  if (firstRun) {
    firstRun = false;
    trigState = trig;
    if (trigState) {
      openMirrors();
      animState = ANIM_OPENING; animStep = 0; animSubStep = 0;
    } else {
      closeMirrors();
      animState = ANIM_OFF; strip.clear(); strip.show();
    }
  }

  if (!trigState && trig) {
    trigState = true;
    addLog("Kontak açıldı [27]");
    openMirrors();
    animState = ANIM_OPENING; animStep = 0; animSubStep = 0;
  } else if (trigState && !trig) {
    trigState = false;
    addLog("Kontak kapatildi [27]");
    closeMirrors();
    animState = ANIM_CLOSING; animStep = 0; animSubStep = 0;
  }

  delay(2);
}
