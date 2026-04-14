// ============================================================
//  Smart Toilet Hygiene Monitor — Temporal Physics Edition
//  Hardware : ESP32 + MQ-135 + DHT11 + PIR
//             + Relay (Fan) + Yellow LED (Deodorizer)
//             + Green LED (UV scan)
//  Optimized: PROGMEM Web Server + FreeRTOS Dual-Core
// ============================================================

#include <WiFi.h>
#include <WebServer.h>
#include <DHT.h>

// ── PIN DEFINITIONS ──────────────────────────────────────────
#define DHTPIN        14
#define DHTTYPE       DHT11
#define GAS_PIN       34    // MQ-135 analog out (AO) — 12-bit ADC
#define PIR_PIN       27    // PIR sensor output
#define RELAY_PIN     19    // Fan relay (active-LOW)
#define DEOD_LED_PIN  22    // Yellow LED — Deodorizer
#define UV_LED_PIN    23    // Green LED  — UV sanitiser

// ── WIFI ACCESS POINT ────────────────────────────────────────
const char* AP_SSID = "SmartToilet";
const char* AP_PASS = "toilet123";

// ── THRESHOLDS ───────────────────────────────────────────────
#define GAS_CLEAN      2000
#define GAS_MILD       2199
#define GAS_MODERATE   2999
#define GAS_HEAVY      3499

#define GAS_FAN_ON     2500
#define GAS_DEOD_ON    2800

#define TEMP_LOW       15.0   
#define TEMP_HIGH      35.0   
#define TEMP_COMFORT_L 18.0   
#define TEMP_COMFORT_H 26.0   

#define HUM_TOO_DRY    35.0   
#define HUM_OPTIMAL_L  45.0   
#define HUM_OPTIMAL_H  60.0   
#define HUM_HIGH       75.0   

#define SCORE_CLEAN    75     
#define SCORE_WARNING  45     

#define UV_SCORE_MIN   45
#define UV_SCORE_MAX   90

// ── OBJECTS & RTOS ───────────────────────────────────────────
DHT dht(DHTPIN, DHTTYPE);
WebServer server(80);
SemaphoreHandle_t dataMutex; // Mutex for thread-safe variable access

// ── SHARED DATA (Protected by Mutex) ─────────────────────────
bool fanOn  = false;
bool deodOn = false;
bool uvOn   = false;

float lastTemp     = 20.0; 
float lastHum      = 50.0;
int   lastGas      = 2000;
bool  lastOccupied = false;

float currentScore = 100.0; 
int   lastScore    = 100;
String lastStatus  = "CLEAN";
String lastGasLabel = "Very clean";

// ── HELPERS ──────────────────────────────────────────────────
String gasLabel(int gas) {
  if (gas > 3500) return "Dangerous";
  if (gas > 3199) return "Heavily polluted";
  if (gas > 2799) return "Moderate odour";
  if (gas > 2499) return "Mild odour";
  return "Very clean";
}

String toiletStatus(int score, bool occupied) {
  if (occupied) return "OCCUPIED";
  if (score >= SCORE_CLEAN)   return "CLEAN";
  if (score >= SCORE_WARNING) return "WARNING";
  return "UNCLEAN";
}

void calculateColors(String status, int score, String &bgA, String &bgB, String &accent, String &textCol, String &statusBg, String &statusText, String &barColor) {
  if (status == "OCCUPIED") {
    bgA = "#0a1628"; bgB = "#0d2344"; accent = "#4d9fff"; textCol = "#c8e0ff"; statusBg = "#1a3a6b"; statusText = "#7dc4ff";
  } else if (status == "CLEAN") {
    bgA = "#071a0e"; bgB = "#0d2b18"; accent = "#2dff8a"; textCol = "#b0ffd4"; statusBg = "#0e3320"; statusText = "#2dff8a";
  } else if (status == "WARNING") {
    bgA = "#1a1000"; bgB = "#2b1c00"; accent = "#ffb830"; textCol = "#ffe8a0"; statusBg = "#3a2800"; statusText = "#ffb830";
  } else { // UNCLEAN
    bgA = "#1a0505"; bgB = "#2e0808"; accent = "#ff4444"; textCol = "#ffb0b0"; statusBg = "#3d0a0a"; statusText = "#ff6666";
  }
  barColor = (score >= SCORE_CLEAN) ? "#2dff8a" : (score >= SCORE_WARNING) ? "#ffb830" : "#ff4444";
}

// ── HTML PAGE (Stored in Flash Memory for Storage Optimization) ──
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head>
<meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>Smart Toilet</title>
<link href='https://fonts.googleapis.com/css2?family=DM+Mono:wght@400;500&family=DM+Sans:wght@300;400;500&display=swap' rel='stylesheet'>
<style>
  *{box-sizing:border-box;margin:0;padding:0}
  body{font-family:'DM Sans',sans-serif; background:linear-gradient(145deg,#071a0e 0%,#0d2b18 100%); min-height:100vh;color:#b0ffd4;padding:20px 16px 40px; transition:background 1s ease, color 1s ease;}
  .chip{font-size:10px;letter-spacing:.12em;text-transform:uppercase;color:#2dff8a;opacity:.7;margin-bottom:4px;}
  .status-block{background:#0e3320;border:1px solid #2dff8a22;border-radius:16px;padding:20px;margin-bottom:20px;text-align:center; transition:all 1s ease;}
  .status-word{font-size:36px;font-weight:500;letter-spacing:.06em;color:#2dff8a;font-family:'DM Mono',monospace;}
  .score-wrap{margin:14px 0 4px;} .score-label{font-size:12px;opacity:.6;margin-bottom:6px;}
  .score-track{background:#ffffff10;border-radius:99px;height:8px;overflow:hidden;}
  .score-bar{height:8px;border-radius:99px;background:#2dff8a;width:100%;transition:width .6s ease, background 1s ease;}
  .score-num{font-size:28px;font-weight:500;color:#2dff8a;font-family:'DM Mono',monospace;margin-top:8px;}
  .grid{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-bottom:20px;}
  .card{background:#ffffff08;border:0.5px solid #2dff8a20;border-radius:12px;padding:14px;}
  .card.full{grid-column:1/-1;}
  .c-label{font-size:10px;letter-spacing:.1em;text-transform:uppercase;color:#2dff8a;opacity:.5;margin-bottom:6px;}
  .c-val{font-size:24px;font-weight:500;font-family:'DM Mono',monospace;color:#2dff8a;}
  .c-sub{font-size:11px;opacity:.55;margin-top:3px;}
  .pir-row{display:flex;align-items:center;gap:10px;margin-bottom:20px;padding:12px 16px;background:#ffffff06;border-radius:10px;}
  .dot{width:9px;height:9px;border-radius:50%;flex-shrink:0;} .dot-on{background:#2dff8a;} .dot-off{background:#ffffff25;}
  .pir-txt{font-size:13px;letter-spacing:.04em;}
  .act-title{font-size:10px;letter-spacing:.12em;text-transform:uppercase;color:#2dff8a;opacity:.5;margin-bottom:10px;}
  .btns{display:grid;grid-template-columns:1fr 1fr 1fr;gap:8px;margin-bottom:24px;}
  .btn{display:block;text-align:center;padding:14px 4px;border-radius:10px;text-decoration:none;font-family:'DM Mono',monospace;font-size:10px;letter-spacing:.08em;border:0.5px solid #2dff8a30;color:#b0ffd4;background:transparent;transition:background .2s;}
  .btn.active{background:#2dff8a22;border-color:#2dff8a;color:#2dff8a;}
  .btn-state{font-size:14px;font-weight:500;margin-top:4px;display:block;}
  .footer{font-size:10px;opacity:.3;text-align:center;letter-spacing:.08em;}
  .warn-badge{display:inline-block;font-size:10px;padding:2px 8px;border-radius:20px;margin-left:6px;font-family:'DM Mono',monospace;}
  .wb-ok{background:#2dff8a18;color:#2dff8a;} .wb-warn{background:#ffb83020;color:#ffb830;} .wb-bad{background:#ff444420;color:#ff6666;}
</style></head><body id='body'>

<div class='chip'>Smart Toilet Monitor</div>

<div class='status-block' id='statusBlock'>
  <div style='font-size:11px;opacity:.5;letter-spacing:.1em;margin-bottom:8px;text-transform:uppercase;'>Status</div>
  <div class='status-word' id='statusWord'>LOADING</div>
  <div class='score-wrap'>
    <div class='score-label'>Hygiene score</div>
    <div class='score-track'><div class='score-bar' id='scoreBar'></div></div>
    <div class='score-num' id='scoreNum'>-- / 100</div>
  </div>
</div>

<div class='pir-row' id='pirRow'>
  <div class='dot dot-off'></div><span class='pir-txt'>Syncing...</span>
</div>

<div class='grid'>
  <div class='card'><div class='c-label'>Temperature</div>
    <div class='c-val' id='tempVal'>--&deg;</div>
    <div class='c-sub' id='tempSub'>C</div>
  </div>
  <div class='card'><div class='c-label'>Humidity</div>
    <div class='c-val' id='humVal'>--</div>
    <div class='c-sub' id='humSub'>%</div>
  </div>
  <div class='card full'><div class='c-label'>Gas / Odour (raw ADC)</div>
    <div class='c-val' id='gasVal'>--</div>
    <div class='c-sub' id='gasSub'>Syncing...</div>
  </div>
</div> 

<div class='act-title'>Actuators</div>
<div class='btns'>
  <a href='#' onclick='toggleActuator("fan"); return false;' id='btnFan' class='btn'>FAN<span class='btn-state'>OFF</span></a>
  <a href='#' onclick='toggleActuator("deod"); return false;' id='btnDeod' class='btn'>DEOD<span class='btn-state'>OFF</span></a>
  <a href='#' onclick='toggleActuator("uv"); return false;' id='btnUV' class='btn'>UV<span class='btn-state'>OFF</span></a>
</div>

<div class='footer'>LIVE SYNC &nbsp;·&nbsp; 192.168.4.1</div>

<script>
  function toggleActuator(act) { fetch('/' + act).then(r => fetchUpdate()); }
  function fetchUpdate() {
    fetch('/data').then(r => r.json()).then(d => {
      document.body.style.background = 'linear-gradient(145deg,'+d.bgA+' 0%,'+d.bgB+' 100%)';
      document.body.style.color = d.textCol;
      let sb = document.getElementById('statusBlock'); sb.style.background = d.statusBg; sb.style.borderColor = d.accent + '22';
      let sw = document.getElementById('statusWord'); sw.innerText = d.status; sw.style.color = d.statusText;
      document.getElementById('scoreBar').style.width = d.score + '%'; document.getElementById('scoreBar').style.background = d.barColor;
      let snum = document.getElementById('scoreNum'); snum.innerText = d.score + ' / 100'; snum.style.color = d.accent;
      document.getElementById('pirRow').innerHTML = d.pir ? `<div class='dot dot-on' style='background:${d.accent}'></div><span class='pir-txt'>Stall occupied</span>` : `<div class='dot dot-off'></div><span class='pir-txt'>Stall vacant</span>`;
      let tv = document.getElementById('tempVal'); tv.innerHTML = d.temp + '&deg;'; tv.style.color = d.accent;
      document.getElementById('tempSub').innerHTML = 'C' + d.tempBadge;
      let hv = document.getElementById('humVal'); hv.innerText = d.hum; hv.style.color = d.accent;
      document.getElementById('humSub').innerHTML = '%' + d.humBadge;
      let gv = document.getElementById('gasVal'); gv.innerText = d.gas; gv.style.color = d.accent;
      document.getElementById('gasSub').innerHTML = d.gasLbl + d.gasBadge;
      updBtn('btnFan', d.fanOn, 'FAN', d.accent, d.textCol); updBtn('btnDeod', d.deodOn, 'DEOD', d.accent, d.textCol); updBtn('btnUV', d.uvOn, 'UV', d.accent, d.textCol);
      document.querySelectorAll('.chip, .c-label, .act-title').forEach(c => c.style.color = d.accent);
      document.querySelectorAll('.card').forEach(c => c.style.borderColor = d.accent + '20');
    });
  }
  function updBtn(id, on, lbl, acc, tc) {
    let b = document.getElementById(id); b.className = 'btn ' + (on ? 'active' : ''); b.innerHTML = lbl + `<span class='btn-state'>${on?'ON':'OFF'}</span>`;
    if(on) { b.style.background = acc + '22'; b.style.borderColor = acc; b.style.color = acc; }
    else { b.style.background = 'transparent'; b.style.borderColor = acc + '30'; b.style.color = tc; }
  }
  fetchUpdate(); // Hydrate immediately
  setInterval(fetchUpdate, 4000);
</script>
</body></html>
)rawliteral";

// ── ROUTE HANDLERS ────────────────────────────────────────────
void handleRoot() {
  server.send_P(200, "text/html", index_html);
}

void handleData() {
  xSemaphoreTake(dataMutex, portMAX_DELAY); // Lock data for safe reading

  String bgA, bgB, accent, textCol, statusBg, statusText, barColor;
  calculateColors(lastStatus, lastScore, bgA, bgB, accent, textCol, statusBg, statusText, barColor);

  String tempBadge = (lastTemp > TEMP_HIGH || lastTemp < TEMP_LOW) ? "<span class='warn-badge wb-bad'>out of range</span>" : (lastTemp > 28 ? "<span class='warn-badge wb-warn'>warm</span>" : "<span class='warn-badge wb-ok'>ok</span>");
  String humBadge  = (lastHum > HUM_HIGH) ? "<span class='warn-badge wb-bad'>mold risk</span>" : (lastHum > HUM_OPTIMAL_H ? "<span class='warn-badge wb-warn'>elevated</span>" : (lastHum < HUM_TOO_DRY ? "<span class='warn-badge wb-warn'>too dry</span>" : "<span class='warn-badge wb-ok'>optimal</span>"));
  String gasBadge  = (lastGas > 3500) ? "<span class='warn-badge wb-bad'>dangerous</span>" : (lastGas > 3199) ? "<span class='warn-badge wb-bad'>heavy</span>" : (lastGas > 2799) ? "<span class='warn-badge wb-warn'>moderate</span>" : (lastGas > 2499) ? "<span class='warn-badge wb-warn'>mild</span>" : "<span class='warn-badge wb-ok'>clean</span>";

  String json = "{";
  json += "\"score\":" + String(lastScore) + ",";
  json += "\"status\":\"" + lastStatus + "\",";
  json += "\"temp\":\"" + String(lastTemp, 1) + "\",";
  json += "\"hum\":\"" + String(lastHum, 1) + "\",";
  json += "\"gas\":" + String(lastGas) + ",";
  json += "\"pir\":" + String(lastOccupied ? "true" : "false") + ",";
  json += "\"gasLbl\":\"" + lastGasLabel + "\",";
  json += "\"tempBadge\":\"" + tempBadge + "\",";
  json += "\"humBadge\":\"" + humBadge + "\",";
  json += "\"gasBadge\":\"" + gasBadge + "\",";
  json += "\"fanOn\":" + String(fanOn ? "true" : "false") + ",";
  json += "\"deodOn\":" + String(deodOn ? "true" : "false") + ",";
  json += "\"uvOn\":" + String(uvOn ? "true" : "false") + ",";
  json += "\"bgA\":\"" + bgA + "\",";
  json += "\"bgB\":\"" + bgB + "\",";
  json += "\"accent\":\"" + accent + "\",";
  json += "\"textCol\":\"" + textCol + "\",";
  json += "\"statusBg\":\"" + statusBg + "\",";
  json += "\"statusText\":\"" + statusText + "\",";
  json += "\"barColor\":\"" + barColor + "\"";
  json += "}";

  xSemaphoreGive(dataMutex); // Release lock

  server.send(200, "application/json", json);
}

void handleFan() {
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  fanOn = !fanOn;
  digitalWrite(RELAY_PIN, fanOn ? LOW : HIGH);
  xSemaphoreGive(dataMutex);
  server.send(200, "text/plain", "OK");
}

void handleDeod() {
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  deodOn = !deodOn;
  digitalWrite(DEOD_LED_PIN, deodOn ? HIGH : LOW);
  xSemaphoreGive(dataMutex);
  server.send(200, "text/plain", "OK");
}

void handleUV() {
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  if (!uvOn && lastOccupied) {
    xSemaphoreGive(dataMutex);
    server.send(200, "text/plain", "OCCUPIED_DENIED");
    return;
  }
  uvOn = !uvOn;
  digitalWrite(UV_LED_PIN, uvOn ? HIGH : LOW);
  xSemaphoreGive(dataMutex);
  server.send(200, "text/plain", "OK");
}

// ── AUTO ACTUATOR LOGIC ───────────────────────────────────────
void autoActuators() {
  bool shouldFan = (lastGas >= GAS_FAN_ON) || (lastHum > HUM_HIGH);
  if (shouldFan && !fanOn) {
    fanOn = true;
    digitalWrite(RELAY_PIN, LOW);
  } else if (!shouldFan && fanOn) {
    fanOn = false;
    digitalWrite(RELAY_PIN, HIGH);
  }

  bool shouldDeod = (lastGas >= GAS_DEOD_ON) || (lastScore < SCORE_WARNING);
  if (shouldDeod && !deodOn) {
    deodOn = true;
    digitalWrite(DEOD_LED_PIN, HIGH);
  } else if (!shouldDeod && deodOn) {
    deodOn = false;
    digitalWrite(DEOD_LED_PIN, LOW);
  }

  bool shouldUV = (!lastOccupied) && (lastScore >= UV_SCORE_MIN) && (lastScore < UV_SCORE_MAX);
  if (shouldUV && !uvOn) {
    uvOn = true;
    digitalWrite(UV_LED_PIN, HIGH);
  } else if (!shouldUV && uvOn) {
    uvOn = false;
    digitalWrite(UV_LED_PIN, LOW);
  }
}

// ── RTOS TASKS ────────────────────────────────────────────────

// Core 0: Network & Web Server Task
void webServerTask(void *param) {
  while (true) {
    server.handleClient();
    vTaskDelay(pdMS_TO_TICKS(10)); // Yield to network stack
  }
}

// Core 1: Sensors & Temporal Physics Computation Task
void sensorComputeTask(void *param) {
  while (true) {
    // 1. Read Sensors locally to avoid locking up everything
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    int gasVal = analogRead(GAS_PIN);
    bool occ = !(digitalRead(PIR_PIN) == HIGH);

    xSemaphoreTake(dataMutex, portMAX_DELAY); // Lock to write globals

    if (!isnan(t)) lastTemp = t;
    if (!isnan(h)) lastHum  = h;
    lastGas      = gasVal;
    lastOccupied = occ;

    // 1. Normalization (Clamping sensors to 0.0 - 1.0)
    float g_norm = (lastGas - 1500.0) / 2500.0; // Matches paper (1500-4000 range)
    g_norm = constrain(g_norm, 0.0, 1.0);

    float h_norm = (lastHum > 75.0) ? 1.0 : 0.0; // Binary humidity penalty

    // 2. Thermal Catalyst Factor (Heat speeds up degradation)
    float t_factor = 1.0 + max(0.0f, (lastTemp - 30.0f) * 0.05f);

    // 3. Evaluation of State (Degrading vs Recovering)
    if (lastOccupied || g_norm > 0.2 || h_norm > 0.0) {
      // DEGRADATION PHASE
      // Formula: Drop = T_factor * (4.0 * (G_norm + H_norm + Occupied_penalty))
      float drop = t_factor * (4.0 * (g_norm + h_norm + (lastOccupied ? 0.5 : 0.0)));
      currentScore -= drop;
    } else {
      // RECOVERY PHASE (Inertia-Driven Exponential)
      // Formula: Recovery = 0.2 + 3.0 * (H_ratio^2)
      float h_ratio = currentScore / 100.0;
      float recovery = 0.2 + (3.0 * (h_ratio * h_ratio));
      currentScore += recovery;
    }
    
    // 2. Physics-Based Temporal Degradation Model
    // float g_norm = (lastGas - 2000.0) / 1500.0;
    // if (g_norm < 0.0) g_norm = 0.0;
    // if (g_norm > 1.0) g_norm = 1.0;

    // float h_norm = 0.0;
    // if (lastHum < HUM_OPTIMAL_L) {
    //   h_norm = (HUM_OPTIMAL_L - lastHum) / HUM_OPTIMAL_L;
    // } else if (lastHum > HUM_OPTIMAL_H) {
    //   h_norm = (lastHum - HUM_OPTIMAL_H) / (100.0 - HUM_OPTIMAL_H);
    // }
    // if (h_norm < 0.0) h_norm = 0.0;
    // if (h_norm > 1.0) h_norm = 1.0;

    // float u_val = lastOccupied ? 1.0 : 0.0;

    // float alpha = 0.50; 
    // float beta  = 0.30; 
    // float gamma = 0.20; 
    // float delta = 0.05;

    // float dH_dt_deg = (alpha * g_norm) + (beta * h_norm) + (gamma * u_val);
    // float dH_dt_rec = delta * (100.0 - currentScore) * (1.0 - g_norm);

    // currentScore += (dH_dt_rec - dH_dt_deg) * 4.0; // 4-sec cycle weight
    
    if (currentScore > 100.0) currentScore = 100.0;
    if (currentScore < 0.0) currentScore = 0.0;

    lastScore    = (int)currentScore;
    lastStatus   = toiletStatus(lastScore, lastOccupied);
    lastGasLabel = gasLabel(lastGas);

    // 4. Trigger rules
    autoActuators();

    xSemaphoreGive(dataMutex); // Release lock

    vTaskDelay(pdMS_TO_TICKS(4000)); // Run every 4 seconds
  }
}

// ── SETUP ─────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(300);

  // Initialize Mutex
  dataMutex = xSemaphoreCreateMutex();

  dht.begin();

  pinMode(PIR_PIN,      INPUT);
  pinMode(RELAY_PIN,    OUTPUT);
  pinMode(DEOD_LED_PIN, OUTPUT);
  pinMode(UV_LED_PIN,   OUTPUT);

  digitalWrite(RELAY_PIN,    HIGH);
  digitalWrite(DEOD_LED_PIN, LOW);
  digitalWrite(UV_LED_PIN,   LOW);

  Serial.println("Smart Toilet Monitor — booting");
  for (int i = 0; i < 2; i++) {
    digitalWrite(RELAY_PIN,    LOW);  delay(150);
    digitalWrite(RELAY_PIN,    HIGH); delay(100);
    digitalWrite(DEOD_LED_PIN, HIGH); delay(150);
    digitalWrite(DEOD_LED_PIN, LOW);  delay(100);
    digitalWrite(UV_LED_PIN,   HIGH); delay(150);
    digitalWrite(UV_LED_PIN,   LOW);  delay(100);
  }

  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("AP started. IP: ");
  Serial.println(WiFi.softAPIP()); 

  server.on("/",     handleRoot);
  server.on("/data", handleData); 
  server.on("/fan",  handleFan);
  server.on("/deod", handleDeod);
  server.on("/uv",   handleUV);
  server.begin();

  Serial.println("Waiting for MQ-135 warm-up (2s)...");
  delay(2000);

  // Create RTOS Tasks
  // Core 0 handles Web Server / Network
  xTaskCreatePinnedToCore(webServerTask, "WebServer", 4096, NULL, 1, NULL, 0);
  // Core 1 handles Sensors and Math
  xTaskCreatePinnedToCore(sensorComputeTask, "SensorMath", 4096, NULL, 1, NULL, 1);
}

// ── LOOP ──────────────────────────────────────────────────────
void loop() {
  // Empty. RTOS fully manages execution now!
  vTaskDelete(NULL); 
}