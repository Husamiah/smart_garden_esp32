#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "DHT.h"
#include <Preferences.h>
#include "secrets.h"

// ===== WiFi =====
// ssid and password are now defined in secrets.h (not uploaded to GitHub)

// ===== Pins =====
#define DHTPIN 4
#define DHTTYPE DHT11
#define SOIL_PIN 36
#define TRIG_PIN 17
#define ECHO_PIN 16
#define PUMP_PIN 25
#define LED_PIN 13
#define BUZZER_PIN 26
#define BATT_PIN 34

// ===== Peripherals =====
DHT dht(DHTPIN, DHTTYPE);
LiquidCrystal_I2C lcd(0x27, 20, 4);
WebServer server(80);
Preferences preferences;

// ===== System vars =====
bool pumpState = false;
bool autoMode = true;
bool buzzerEnabled = true;
int moistureThreshold = 30;
unsigned long pumpStartTime = 0;
unsigned long pumpDuration = 0;
unsigned long lastUpdate = 0;
const unsigned long updateInterval = 3000;

float temperature = 0, humidity = 0;
float soilPercent = 0;
float distance = 0;
float batteryVoltage = 0, batteryPercent = 0;
float waterLevelPercent = 0;

#define AVG_WINDOW 5
float tempSamples[AVG_WINDOW] = {0};
float humSamples[AVG_WINDOW] = {0};
float soilSamples[AVG_WINDOW] = {0};
float battSamples[AVG_WINDOW] = {0};
int sampleIndex = 0;

String currentAlert = ""; // current alert type: "battery", "water" or ""

bool lastPumpState = false; // tracks pump stop to refresh the LCD

// ===== HTML page =====
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="ar">
<head>
<meta charset="utf-8">
<title>🌿 لوحة التحكم بالزراعة الذكية</title>
<style>
body { font-family: "Segoe UI", sans-serif; background: linear-gradient(135deg,#d9f7f5,#c6e7ff); text-align:center; color:#222; margin:0; }
h2 { color:#005a5c; margin-top:20px; }
.container { display:flex; flex-wrap:wrap; justify-content:center; gap:15px; margin-top:20px; }
.card { background:white; border-radius:15px; box-shadow:0 4px 10px rgba(0,0,0,0.2); width:220px; padding:15px; }
.card h3 { color:#007a7c; margin-bottom:5px; }
.value { font-size:1.5em; margin:5px 0; }
button { padding:10px 25px; border:none; border-radius:10px; font-size:1em; cursor:pointer; }
.on { background:#0c9e2d; color:white; } .off { background:#c62828; color:white; }
input[type="number"] { width:60px; padding:5px; font-size:1em; border-radius:8px; border:1px solid #ccc; }
.footer { margin-top:25px; font-size:0.9em; color:#444; }
.small { font-size:0.9em; color:#666; margin-top:8px; }
.alert { font-weight:bold; color:#c62828; margin-top:5px; }
</style>
<script>
function refreshData(){
  fetch('/data').then(res=>res.json()).then(data=>{
    document.getElementById('temp').innerText = data.temp + ' °C';
    document.getElementById('hum').innerText = data.hum + ' %';
    document.getElementById('soil').innerText = data.soil + ' %';
    document.getElementById('water').innerText = data.water + ' %';
    document.getElementById('batt').innerText = data.batt + '% (' + data.volt + 'V)';
    document.getElementById('auto').innerText = data.auto ? "مفعل" : "متوقف";
    document.getElementById('moistureVal').value = data.moisture;
    document.getElementById('buzzer').innerText = data.buzzer ? "مفعل" : "متوقف";
    document.getElementById('pump').innerText = data.pump ? "مشتغلة" : "متوقفة";
    let alertText = data.alert == "battery" ? "🔋 بطارية منخفضة!" : data.alert == "water" ? "💧 مستوى الماء منخفض!" : "";
    document.getElementById('alert').innerText = alertText;
    setTimeout(refreshData, 1000);
  }).catch(()=> setTimeout(refreshData, 2000));
}
function toggleAuto(){ fetch('/setauto');}
function setMoisture(){ let val = document.getElementById('moistureVal').value; fetch('/setmoisture?value=' + val);}
function runPump(){ fetch('/togglepump'); }
function toggleBuzzer(){ fetch('/buzzer');}
function resetDefaults(){ if(confirm('هل تريد إعادة الإعدادات إلى القيم الافتراضية؟')) { fetch('/resetdefaults'); }}
window.onload = refreshData;
</script>
</head>
<body>
<h2>🌱 نظام الزراعة الذكي - ESP32</h2>
<div class="container">
<div class="card"><h3>الحرارة</h3><div id="temp" class="value">--</div></div>
<div class="card"><h3>الرطوبة</h3><div id="hum" class="value">--</div></div>
<div class="card"><h3>رطوبة التربة</h3><div id="soil" class="value">--</div></div>
<div class="card"><h3>مستوى الماء</h3><div id="water" class="value">--</div></div>
<div class="card"><h3>البطارية</h3><div id='batt' class='value'>--</div></div>
<div class="card"><h3>المضخة اليدوية</h3>
<div id="pump" class="value">--</div>
<button class="on" onclick="runPump()">تشغيل/إيقاف</button></div>
<div class="card"><h3>الوضع التلقائي</h3><div class="value" id="auto">--</div>
<button class="on" onclick="toggleAuto()">تبديل</button></div>
<div class="card"><h3>الحد الأدنى للرطوبة</h3>
<input type="number" id="moistureVal" value="30"><br><br>
<button class="on" onclick="setMoisture()">تحديث</button></div>
<div class="card"><h3>التنبيهات الصوتية</h3><div id="buzzer" class="value">--</div>
<button class="off" onclick="toggleBuzzer()">تبديل</button>
<div id="alert" class="alert"></div>
</div>
<div class="card">
  <h3>إعدادات</h3>
  <button class="off" onclick="resetDefaults()">إعادة الإعدادات الافتراضية</button>
  <div class="small">بعد الضغط سيتم حفظ القيم الافتراضية واسترجاعها</div>
</div>
</div>
<div class="footer">© 2025 Smart Garden | ESP32</div>
</body>
</html>
)rawliteral";

// ===== Utility functions =====
float average(float arr[], int size){
  float sum=0; for(int i=0;i<size;i++) sum+=arr[i]; return sum/size;
}
float readSoil(){ int val=analogRead(SOIL_PIN); val=constrain(val,500,3000); return map(val,3000,500,0,100);}
float readDistance(){ digitalWrite(TRIG_PIN,LOW); delayMicroseconds(2); digitalWrite(TRIG_PIN,HIGH); delayMicroseconds(10); digitalWrite(TRIG_PIN,LOW); long duration=pulseInLong(ECHO_PIN,HIGH,30000); float dist=duration*0.034/2; if(dist==0||dist>450) dist=450; return dist;}
float distanceToPercent(float d) {
  float emptyLevel = 40.0; // distance when tank is empty (cm)
  float fullLevel  = 15.0; // distance when tank is full (cm)
  float percent = map(d, emptyLevel, fullLevel, 0, 100);
  return constrain(percent, 0, 100);
}
float readBattery(){ int raw=analogRead(BATT_PIN); float voltage=(raw/4095.0)*3.3; voltage=voltage*((100.0+47.0)/47.0); return voltage;}
float voltageToPercent(float v){ if(v>=8.4) return 100; else if(v<=6.4) return 0; else return (v-6.4)*50;}
void updateLCD(){
  lcd.setCursor(0,0); lcd.print("T:"); lcd.print(temperature,1); lcd.print("C H:"); lcd.print(humidity,0); lcd.print("%   ");
  lcd.setCursor(0,1); lcd.print("Soil: "); lcd.print(soilPercent,0); lcd.print("%   ");
  lcd.setCursor(0,2); lcd.print("Water: "); lcd.print(waterLevelPercent,0); lcd.print("%   ");
  lcd.setCursor(0,3); lcd.print("Batt: "); lcd.print(batteryPercent,0); lcd.print("% ("); lcd.print(batteryVoltage,2); lcd.print("V)   ");
}

// ===== Alarms =====
void handleAlarms(){
  if(!buzzerEnabled){ digitalWrite(LED_PIN,LOW); noTone(BUZZER_PIN); currentAlert=""; return; }
  unsigned long t=millis();
  if(batteryPercent<25){ currentAlert="battery"; digitalWrite(LED_PIN,(t/500)%2); if((t%1000)<200) tone(BUZZER_PIN,800); else noTone(BUZZER_PIN);}
  else if(waterLevelPercent<25){ currentAlert="water"; digitalWrite(LED_PIN,(t/150)%2); if((t%600)<100) tone(BUZZER_PIN,1200); else noTone(BUZZER_PIN);}
  else{ digitalWrite(LED_PIN,LOW); noTone(BUZZER_PIN); currentAlert="";}
}

// ===== Pump control =====
void autoPumpControl(){
  if(!autoMode) return;
  if(soilPercent<moistureThreshold && !pumpState){ pumpState=true; digitalWrite(PUMP_PIN,HIGH); pumpStartTime=millis();}
  else if(soilPercent>=70 && pumpState){ pumpState=false; digitalWrite(PUMP_PIN,LOW);}
}
void manualPumpControl(unsigned long durationMs){ pumpState=true; digitalWrite(PUMP_PIN,HIGH); pumpStartTime=millis(); pumpDuration=durationMs;}
void togglePump(){ pumpState=!pumpState; digitalWrite(PUMP_PIN,pumpState?HIGH:LOW); pumpDuration=0; }

// ===== Web handlers =====
void handleData(){
  String json="{";
  json += "\"temp\":"+String(temperature,1)+",";
  json += "\"hum\":"+String(humidity,0)+",";
  json += "\"soil\":"+String(soilPercent,0)+",";
  json += "\"water\":"+String(waterLevelPercent,0)+",";
  json += "\"batt\":"+String(batteryPercent,0)+",";
  json += "\"volt\":"+String(batteryVoltage,2)+",";
  json += "\"auto\":" + String(autoMode?"true":"false") + ",";
  json += "\"moisture\":"+String(moistureThreshold)+",";
  json += "\"buzzer\":" + String(buzzerEnabled?"true":"false") + ",";
  json += "\"alert\":\""+currentAlert+"\",";
  json += "\"pump\":" + String(pumpState?"true":"false");
  json += "}";
  server.send(200,"application/json",json);
}
void handleSetAuto(){ autoMode=!autoMode; preferences.putBool("autoMode",autoMode); server.send(200,"text/plain","Auto mode toggled");}
void handleSetMoisture(){ moistureThreshold=server.arg("value").toInt(); preferences.putInt("moisture",moistureThreshold); server.send(200,"text/plain","Moisture threshold updated");}
void handleManualPump(){ unsigned long duration=server.arg("duration").toInt()*1000; manualPumpControl(duration); server.send(200,"text/plain","Manual pump activated");}
void handleBuzzerToggle(){ buzzerEnabled=!buzzerEnabled; preferences.putBool("buzzer",buzzerEnabled); server.send(200,"text/plain","Buzzer toggled");}
void handleResetDefaults(){ preferences.clear(); autoMode=true; moistureThreshold=30; buzzerEnabled=true; server.send(200,"text/plain","Settings reset");}
void handleTogglePump(){ togglePump(); server.send(200,"text/plain","Pump toggled");}
void handleRoot(){ server.send(200,"text/html",index_html);}

// ===== Setup =====
void setup(){
  Serial.begin(115200);
  pinMode(TRIG_PIN,OUTPUT); pinMode(ECHO_PIN,INPUT);
  pinMode(PUMP_PIN,OUTPUT); pinMode(LED_PIN,OUTPUT); pinMode(BUZZER_PIN,OUTPUT);
  digitalWrite(PUMP_PIN,LOW); digitalWrite(LED_PIN,LOW); noTone(BUZZER_PIN);

  dht.begin();
  lcd.init(); lcd.backlight();

  // load saved settings
  preferences.begin("smartgarden", false);
  autoMode = preferences.getBool("autoMode", true);
  moistureThreshold = preferences.getInt("moisture", 30);
  buzzerEnabled = preferences.getBool("buzzer", true);

  // connect to WiFi and show IP for 5 seconds
  lcd.setCursor(0,0); lcd.print("Connecting WiFi...");
  WiFi.begin(ssid,password);
  while(WiFi.status()!=WL_CONNECTED){ delay(500); Serial.print("."); }
  lcd.clear(); lcd.print("WiFi Connected"); lcd.setCursor(0,1); lcd.print(WiFi.localIP());
  delay(5000);
  lcd.clear(); updateLCD();

  // Web server
  server.on("/",handleRoot);
  server.on("/data",handleData);
  server.on("/setauto",handleSetAuto);
  server.on("/setmoisture",handleSetMoisture);
  server.on("/manualpump",handleManualPump);
  server.on("/buzzer",handleBuzzerToggle);
  server.on("/resetdefaults",handleResetDefaults);
  server.on("/togglepump",handleTogglePump);
  server.begin();
}

// ===== Loop =====
void loop(){
  server.handleClient();

  // continuously update LED and buzzer alerts
  handleAlarms();

  // update sensor readings every few seconds
  if(millis()-lastUpdate>=updateInterval){
    lastUpdate=millis();
    tempSamples[sampleIndex]=dht.readTemperature();
    humSamples[sampleIndex]=dht.readHumidity();
    soilSamples[sampleIndex]=readSoil();
    battSamples[sampleIndex]=readBattery();
    distance=readDistance();
    sampleIndex=(sampleIndex+1)%AVG_WINDOW;

    temperature=average(tempSamples,AVG_WINDOW);
    humidity=average(humSamples,AVG_WINDOW);
    soilPercent=average(soilSamples,AVG_WINDOW);
    batteryVoltage=average(battSamples,AVG_WINDOW);
    batteryPercent=voltageToPercent(batteryVoltage);
    waterLevelPercent=distanceToPercent(distance);

    autoPumpControl();
    if(pumpState && pumpDuration>0 && millis()-pumpStartTime>=pumpDuration){ pumpState=false; digitalWrite(PUMP_PIN,LOW); pumpDuration=0;}

    // refresh the LCD when the pump stops
    if(lastPumpState && !pumpState){
      lcd.noBacklight();
      delay(100);
      lcd.init();
      lcd.backlight();
      updateLCD();
    }
    lastPumpState = pumpState;

    updateLCD();
  }
}
