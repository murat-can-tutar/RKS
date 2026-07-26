#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <EEPROM.h>
#include <FastLED.h>

// --- AP AYARLARI ---
const char* apSSID = "RKS";
const char* apPassword = "44AFT748";

WebServer server(80);

// --- LED TANIMLARI ---
#define LED_STRIP_PIN     4
#define NUM_LEDS          60

CRGB leds[NUM_LEDS];

#define EEPROM_SIZE  64
#define EEPROM_MAGIC 0xEE

struct LEDConfig {
  uint8_t magic;
  uint8_t effect;     // 0: Kara Şimşek, 1: Gökkuşağı, 2: Ateş, 3: Meteor, 4: Polis, 5: Sabit Renk
  uint8_t brightness; // 0 - 255
  uint8_t speed;      // 1 - 100
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

LEDConfig cfg;
bool otaOK = false;

// --- HTML / WEB ARAYÜZÜ ---
const char page[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="tr"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 LED & OTA Kontrol</title>
<style>
:root{
  --bg:#0b1220;
  --card:#131c2f;
  --line:#2b3a57;
  --txt:#edf2ff;
  --muted:#9fb0d0;
  --blue:#2f7df6;
}
*{box-sizing:border-box}
body{
  margin:0; font-family:Arial,sans-serif;
  background:linear-gradient(180deg,#08101c 0%,#0d1728 100%);
  color:var(--txt); padding:16px;
}
.wrap{max-width:600px;margin:auto}
.card{
  background:var(--card); border:1px solid var(--line);
  border-radius:18px; padding:16px; margin-bottom:12px;
  box-shadow:0 10px 25px rgba(0,0,0,.25);
}
h1,h3{margin:0 0 12px; text-align:center;}
h1{font-size:22px; color:#4da3ff;}
.grid2{display:grid; grid-template-columns:1fr 1fr; gap:12px;}
@media(max-width:500px){.grid2{grid-template-columns:1fr}}
.sliderWrap{
  background:#0f1727; border:1px solid var(--line);
  border-radius:14px; padding:12px; margin-bottom:10px;
}
.sliderHead{display:flex; justify-content:space-between; margin-bottom:8px; font-size:14px;}
input[type=range]{width:100%}
select, input[type=color]{
  width:100%; padding:8px; border-radius:10px;
  background:#04070d; color:#edf2ff; border:1px solid var(--line); font-size:14px;
}
input[type=color]{height:42px; cursor:pointer; padding:2px;}
button{
  width:100%; min-height:48px; border:none; border-radius:14px;
  color:#fff; font-size:15px; font-weight:700; cursor:pointer;
  background:linear-gradient(180deg,#3f96ff,#216ce2);
  box-shadow:0 6px 15px rgba(33,108,226,.4);
}
button:active{transform:scale(.98)}
input[type=file]{width:100%; color:#dbe6ff; margin-bottom:10px;}
.footer{margin-top:18px; text-align:center; font-size:12px; color:var(--muted);}
</style></head><body>
<div class="wrap">

  <div class="card">
    <h1>LED Animasyon & OTA Panel</h1>
  </div>

  <div class="card">
    <h3>LED Kontrol Paneli</h3>
    
    <div class="sliderWrap">
      <div class="sliderHead"><span>Animasyon Efekti</span></div>
      <select id="eff" onchange="saveLed()">
        <option value="0">Kara Şimşek</option>
        <option value="1">Gökkuşağı (Rainbow)</option>
        <option value="2">Ateş Efekti (Fire)</option>
        <option value="3">Meteor Yağmuru</option>
        <option value="4">Polis Çakar</option>
        <option value="5">Sabit Renk</option>
      </select>
    </div>

    <div class="grid2">
      <div class="sliderWrap">
        <div class="sliderHead"><span>Sabit Renk</span></div>
        <input id="col" type="color" value="#ff0000" onchange="saveLed()">
      </div>
      <div class="sliderWrap">
        <div class="sliderHead"><span>Parlaklık</span><b id="brV">200</b></div>
        <input id="br" type="range" min="10" max="255" value="200" oninput="syncV()" onchange="saveLed()">
      </div>
    </div>

    <div class="sliderWrap">
      <div class="sliderHead"><span>Animasyon Hızı</span><b id="spV">50</b></div>
      <input id="sp" type="range" min="1" max="100" value="50" oninput="syncV()" onchange="saveLed()">
    </div>
  </div>

  <div class="card">
    <h3>Sistem Güncelleme (OTA)</h3>
    <form method="POST" action="/update" enctype="multipart/form-data">
      <input type="file" name="update" accept=".bin">
      <button type="submit">Firmware (.bin) Yükle & Güncelle</button>
    </form>
  </div>

  <div class="footer">• ESP32 Wi-Fi OTA & LED Controller •</div>

</div>

<script>
function syncV(){
  document.getElementById('brV').textContent = document.getElementById('br').value;
  document.getElementById('spV').textContent = document.getElementById('sp').value;
}

function saveLed(){
  const eff = document.getElementById('eff').value;
  const br = document.getElementById('br').value;
  const sp = document.getElementById('sp').value;
  const col = document.getElementById('col').value.replace('#','');

  fetch(`/api/led?eff=${eff}&br=${br}&sp=${sp}&col=${col}`)
  .catch(err => console.log(err));
}

// İlk açılışta değerleri çek
fetch('/api/status').then(r=>r.json()).then(d=>{
  document.getElementById('eff').value = d.eff;
  document.getElementById('br').value = d.br;
  document.getElementById('sp').value = d.sp;
  document.getElementById('col').value = '#' + d.col;
  syncV();
});
</script>
</body></html>
)rawliteral";

// --- EEPROM İŞLEMLERİ ---
void loadConfig() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, cfg);

  if (cfg.magic != EEPROM_MAGIC || cfg.effect > 5) {
    cfg.magic = EEPROM_MAGIC;
    cfg.effect = 0;
    cfg.brightness = 200;
    cfg.speed = 50;
    cfg.r = 255;
    cfg.g = 0;
    cfg.b = 0;
    EEPROM.put(0, cfg);
    EEPROM.commit();
  }
}

void saveConfig() {
  cfg.magic = EEPROM_MAGIC;
  EEPROM.put(0, cfg);
  EEPROM.commit();
}

// --- FASTLED EFEKTLERİ ---
void runKaraSimsek() {
  static int pos = 0;
  static int dir = 1;
  fadeToBlackBy(leds, NUM_LEDS, 60);
  int left = (NUM_LEDS / 2) - 1 - pos;
  int right = (NUM_LEDS / 2) + pos;
  if (left >= 0 && left < NUM_LEDS) leds[left] = CRGB::Red;
  if (right >= 0 && right < NUM_LEDS) leds[right] = CRGB::Red;
  pos += dir;
  if (pos >= (NUM_LEDS / 2) - 1 || pos <= 0) dir = -dir;
}

void runRainbow() {
  static uint8_t gHue = 0;
  fill_rainbow(leds, NUM_LEDS, gHue, 7);
  gHue += 2;
}

void runFire() {
  static byte heat[NUM_LEDS];
  for (int i = 0; i < NUM_LEDS; i++) {
    heat[i] = qsub8(heat[i], random8(0, ((50 * 10) / NUM_LEDS) + 2));
  }
  for (int k = NUM_LEDS - 1; k >= 2; k--) {
    heat[k] = (heat[k - 1] + heat[k - 2] + heat[k - 2]) / 3;
  }
  if (random8() < 120) {
    int y = random8(7);
    heat[y] = qadd8(heat[y], random8(160, 255));
  }
  for (int j = 0; j < NUM_LEDS; j++) {
    leds[j] = HeatColor(heat[j]);
  }
}

void runMeteor() {
  static int meteorPos = 0;
  for (int i = 0; i < NUM_LEDS; i++) {
    if (random8() < 70) leds[i].fadeToBlackBy(64);
  }
  for (int j = 0; j < 6; j++) {
    if ((meteorPos - j >= 0) && (meteorPos - j < NUM_LEDS)) {
      leds[meteorPos - j] = CRGB::White;
    }
  }
  meteorPos++;
  if (meteorPos >= NUM_LEDS + 6) meteorPos = 0;
}

void runPolice() {
  static uint8_t step = 0;
  step++;
  if (step % 2 == 0) {
    fill_solid(leds, NUM_LEDS / 2, CRGB::Red);
    fill_solid(leds + NUM_LEDS / 2, NUM_LEDS / 2, CRGB::Black);
  } else {
    fill_solid(leds, NUM_LEDS / 2, CRGB::Black);
    fill_solid(leds + NUM_LEDS / 2, NUM_LEDS / 2, CRGB::Blue);
  }
}

void runSolid() {
  fill_solid(leds, NUM_LEDS, CRGB(cfg.r, cfg.g, cfg.b));
}

void handleLedAnimation() {
  static unsigned long lastUpdate = 0;
  uint16_t interval = map(cfg.speed, 1, 100, 100, 10); // Hız ayarı

  if (millis() - lastUpdate < interval) return;
  lastUpdate = millis();

  FastLED.setBrightness(cfg.brightness);

  switch (cfg.effect) {
    case 0: runKaraSimsek(); break;
    case 1: runRainbow();    break;
    case 2: runFire();       break;
    case 3: runMeteor();     break;
    case 4: runPolice();     break;
    case 5: runSolid();      break;
    default: runKaraSimsek(); break;
  }
  FastLED.show();
}

// --- WEB SUNUCU ENDPOINTLERİ ---
void handleRoot() {
  server.send_P(200, "text/html; charset=utf-8", page);
}

void handleStatusJson() {
  char hexCol[7];
  snprintf(hexCol, sizeof(hexCol), "%02X%02X%02X", cfg.r, cfg.g, cfg.b);
  
  String json = "{";
  json += "\"eff\":" + String(cfg.effect) + ",";
  json += "\"br\":" + String(cfg.brightness) + ",";
  json += "\"sp\":" + String(cfg.speed) + ",";
  json += "\"col\":\"" + String(hexCol) + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

void handleLedSaveApi() {
  if (server.hasArg("eff")) cfg.effect = constrain(server.arg("eff").toInt(), 0, 5);
  if (server.hasArg("br"))  cfg.brightness = constrain(server.arg("br").toInt(), 0, 255);
  if (server.hasArg("sp"))  cfg.speed = constrain(server.arg("sp").toInt(), 1, 100);

  if (server.hasArg("col")) {
    String c = server.arg("col");
    long num = strtol(c.c_str(), NULL, 16);
    cfg.r = (num >> 16) & 0xFF;
    cfg.g = (num >> 8) & 0xFF;
    cfg.b = num & 0xFF;
  }

  saveConfig();
  server.send(200, "text/plain", "OK");
}

// OTA Güncelleme İşleyicileri
void handleUpdateUpload() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    Serial.println("OTA Güncelleme Başladı...");
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      Serial.printf("Güncelleme Başarılı: %u bayt\nYeniden başlatılıyor...\n", upload.totalSize);
    } else {
      Update.printError(Serial);
    }
  }
}

void handleUpdateDone() {
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", (Update.hasError()) ? "Guncelleme Basarisiz!" : "Guncelleme Basarili! Cihaz yeniden baslatiliyor...");
  delay(1000);
  ESP.restart();
}

void setup() {
  Serial.begin(115200);
  loadConfig();

  // FastLED Başlatma
  FastLED.addLeds<WS2812B, LED_STRIP_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(cfg.brightness);
  FastLED.clear();
  FastLED.show();

  // Wi-Fi AP Modu Başlatma
  WiFi.mode(WIFI_AP);
  WiFi.softAP("RKS_LED_Control", "12345678password");
  Serial.println("Wi-Fi AP Aktif. IP Adresi: 192.168.4.1");

  // Web Sunucu Rotaları
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatusJson);
  server.on("/api/led", HTTP_GET, handleLedSaveApi);
  server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);

  server.begin();
}

void loop() {
  server.handleClient();
  handleLedAnimation(); // Seçilen animasyonu kesintisiz oynatır
}
