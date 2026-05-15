#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <FirebaseESP32.h>
#include <LittleFS.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ESP32Servo.h>
#include <ArduinoJson.h>

#define PH_PIN 35
#define TEMP_PIN 27
#define SDA_PIN 21
#define SCL_PIN 22

const uint8_t REL_ACID_PUMP = 16;
const uint8_t REL_ALKALI_PUMP = 23;
const uint8_t REL_COOLER_FAN = 18;
const uint8_t REL_WATER_HEATER= 19;
const uint8_t REL_AIR_PUMP = 26;
const uint8_t REL_WATER_FLOW = 32;
const uint8_t REL_RAIN_PUMP = 33;
const uint8_t REL_LIGHT_CTRL = 25;

const uint8_t SERVO_PAN_PIN = 13;
const uint8_t SERVO_TILT_PIN = 14;

FirebaseData firebaseData;
FirebaseData streamData;
FirebaseAuth auth;
FirebaseConfig config;

LiquidCrystal_I2C lcd(0x27, 16, 2);
OneWire oneWire(TEMP_PIN);
DallasTemperature sensors(&oneWire);
Servo panServo;
Servo tiltServo;
AsyncWebServer server(80);

byte dotAnim[8] = {0b00000, 0b00000, 0b01110, 0b01110, 0b01110, 0b00000, 0b00000, 0b00000};

volatile float currentTempC = 0.00;
volatile float currentPH = 7.00;
volatile float targetTempMin = 0.0;
volatile float targetTempMax = 0.0;
volatile float targetPhMin = 0.0;
volatile float targetPhMax = 0.0;

float neutralVoltage = 2.00;
float offset = 0.0;
float lastSentTemp = -100.0;
float lastSentPH = -100.0;

volatile bool stateHeater = HIGH;
volatile bool stateCooler = HIGH;
volatile bool stateAcid = HIGH;
volatile bool stateAlkali = HIGH;
volatile bool stateAir = HIGH;
volatile bool stateFlow = HIGH;
volatile bool stateRain = HIGH;
volatile bool stateLight = HIGH;

volatile int targetPan = 90;
volatile int targetTilt = 90;

String savedSSID = "";
String savedPASS = "";
String currentFishName = "Manual";
bool newWifiPending = false;
String pendingSSID = "";
String pendingPASS = "";
bool isAPMode = false;
bool isOnline = false;

unsigned long lastSensorReadTime = 0;
unsigned long relayScreenStartTime = 0;
bool showingRelays = false;
int lcdScreen = 0;
unsigned long pingCounter = 0;
unsigned long lastCycleTime = 0;
bool isInCycle = false;
int cycleState = 0;
unsigned long cycleStateStartTime = 0;

TaskHandle_t NetworkTask;

void saveConfig(String s, String p, String f) {
  StaticJsonDocument<256> doc;
  doc["ssid"] = s;
  doc["pass"] = p;
  doc["fishName"] = f;
  File configFile = LittleFS.open("/config.json", "w");
  if (configFile) {
    serializeJson(doc, configFile);
    configFile.close();
  }
}

void loadConfig() {
  savedSSID = "";
  savedPASS = "";
  currentFishName = "Manual";
  if (LittleFS.exists("/config.json")) {
    File configFile = LittleFS.open("/config.json", "r");
    if (configFile) {
      StaticJsonDocument<256> doc;
      DeserializationError error = deserializeJson(doc, configFile);
      if (!error) {
        savedSSID = doc["ssid"].as<String>();
        savedPASS = doc["pass"].as<String>();
        currentFishName = doc["fishName"].as<String>();
      }
      configFile.close();
    }
  }
}

int getMedianADC(int pin) {
  int buf[10];
  for(int i = 0; i < 10; i++) {
    buf[i] = analogRead(pin);
    delay(2);
  }
  for(int i = 0; i < 9; i++) {
    for(int j = i + 1; j < 10; j++) {
      if(buf[i] > buf[j]) {
        int temp = buf[i];
        buf[i] = buf[j];
        buf[j] = temp;
      }
    }
  }
  int avgValue = 0;
  for(int i = 2; i < 8; i++) avgValue += buf[i];
  return avgValue / 6;
}

float readPH() {
  int adc = getMedianADC(PH_PIN);
  float voltage = adc * (3.3 / 4095.0);
  float pH = 7.0;
  if (voltage >= 2.15) pH = 7.0 + (neutralVoltage - voltage) * 20.0;
  else if (voltage <= 1.80) pH = 7.0 + (neutralVoltage - voltage) * 9.0;
  else pH = 7.0 + (neutralVoltage - voltage) * 3.0;
  pH = pH + offset;
  if (pH < 0.4) pH = 0.40;
  if (pH > 13.6) pH = 13.60;
  return round(pH * 100.0) / 100.0;
}

void padString(String &str, int len) {
  while(str.length() < len) str += " ";
}

void handleAutoControl() {
  bool autoMode = (targetTempMin > 0 && targetTempMax > 0 && targetPhMin > 0 && targetPhMax > 0);
  if (autoMode) {
    if (currentTempC < targetTempMin) stateHeater = LOW;
    else if (currentTempC > targetTempMax) stateCooler = LOW;
    if (currentPH < targetPhMin) stateAlkali = LOW;
    else if (currentPH > targetPhMax) stateAcid = LOW;
  }
}

void applyHardwareStates() {
  digitalWrite(REL_WATER_HEATER, stateHeater);
  digitalWrite(REL_COOLER_FAN, stateCooler);
  digitalWrite(REL_ACID_PUMP, stateAcid);
  digitalWrite(REL_ALKALI_PUMP, stateAlkali);
  digitalWrite(REL_AIR_PUMP, stateAir);
  digitalWrite(REL_WATER_FLOW, stateFlow);
  digitalWrite(REL_RAIN_PUMP, stateRain);
  digitalWrite(REL_LIGHT_CTRL, stateLight);
  if (panServo.read() != targetPan) panServo.write(targetPan);
  if (tiltServo.read() != targetTilt) tiltServo.write(targetTilt);
}

void updateLCD() {
  unsigned long currentMillis = millis();
  if (currentMillis - lastCycleTime >= 180000) {
    isInCycle = true;
    cycleState = 10;
    cycleStateStartTime = currentMillis;
    lastCycleTime = currentMillis;
  }
  if (isInCycle) {
    unsigned long elapsed = currentMillis - cycleStateStartTime;
    if (cycleState == 10 && elapsed >= 5000) { cycleState = 11; cycleStateStartTime = currentMillis; }
    else if (cycleState == 11 && elapsed >= 5000) { cycleState = 12; cycleStateStartTime = currentMillis; }
    else if (cycleState == 12 && elapsed >= 5000) { cycleState = 13; cycleStateStartTime = currentMillis; }
    else if (cycleState == 13 && elapsed >= 5000) { cycleState = 1; cycleStateStartTime = currentMillis; }
    else if (cycleState == 1 && elapsed >= 5000) { cycleState = 2; cycleStateStartTime = currentMillis; }
    else if (cycleState == 2 && elapsed >= 5000) { isInCycle = false; cycleState = 0; }
    lcdScreen = cycleState;
  } else if (showingRelays) {
    unsigned long elapsed = currentMillis - relayScreenStartTime;
    if (elapsed < 5000) lcdScreen = 1;
    else if (elapsed < 10000) lcdScreen = 2;
    else { showingRelays = false; lcdScreen = 0; }
  } else {
    lcdScreen = 0;
  }

  String line1 = "";
  String line2 = "";
  
  if (lcdScreen == 0) {
    line1 = "Mode:" + currentFishName;
    line2 = "pH:" + String(currentPH, 2) + " T:" + String(currentTempC, 2) + "c";
  } else if (lcdScreen == 1) {
    line1 = String("H:") + (stateHeater == LOW ? "ON " : "OFF ");
    line1 += String("F:") + (stateCooler == LOW ? "ON" : "OFF");
    line2 = String("A:") + (stateAcid == LOW ? "ON " : "OFF ");
    line2 += String("K:") + (stateAlkali == LOW ? "ON" : "OFF");
  } else if (lcdScreen == 2) {
    line1 = String("P:") + (stateAir == LOW ? "ON " : "OFF ");
    line1 += String("W:") + (stateFlow == LOW ? "ON" : "OFF");
    line2 = String("R:") + (stateRain == LOW ? "ON " : "OFF ");
    line2 += String("L:") + (stateLight == LOW ? "ON" : "OFF");
  } else if (lcdScreen == 10) {
    line1 = "Automatic Fish";
    line2 = "Breeder Machine";
  } else if (lcdScreen == 11) {
    line1 = "App name";
    line2 = "SmartFishBreeder";
  } else if (lcdScreen == 12) {
    line1 = "Bogura Polytech.";
    line2 = "Institute";
  } else if (lcdScreen == 13) {
    line1 = "Team Leader";
    line2 = "Naim Islam";
  }
  
  padString(line1, 16);
  padString(line2, 16);
  lcd.setCursor(0, 0); lcd.print(line1);
  lcd.setCursor(0, 1); lcd.print(line2);

  if (isOnline && lcdScreen == 0) {
    lcd.setCursor(15, 0);
    if ((currentMillis / 500) % 2 == 0) lcd.write(0);
    else lcd.print(" ");
  }
}

void streamCallback(StreamData data) {
  String path = data.dataPath();
  if (data.dataType() == "json") {
    StaticJsonDocument<512> doc;
    deserializeJson(doc, data.jsonString());
    if (doc.containsKey("targetTempMin")) targetTempMin = doc["targetTempMin"];
    if (doc.containsKey("targetTempMax")) targetTempMax = doc["targetTempMax"];
    if (doc.containsKey("targetPhMin")) targetPhMin = doc["targetPhMin"];
    if (doc.containsKey("targetPhMax")) targetPhMax = doc["targetPhMax"];
    if (doc.containsKey("REL_WATER_HEATER")) stateHeater = doc["REL_WATER_HEATER"] ? LOW : HIGH;
    if (doc.containsKey("REL_COOLER_FAN")) stateCooler = doc["REL_COOLER_FAN"] ? LOW : HIGH;
    if (doc.containsKey("REL_ACID_PUMP")) stateAcid = doc["REL_ACID_PUMP"] ? LOW : HIGH;
    if (doc.containsKey("REL_ALKALI_PUMP")) stateAlkali = doc["REL_ALKALI_PUMP"] ? LOW : HIGH;
    if (doc.containsKey("REL_AIR_PUMP")) stateAir = doc["REL_AIR_PUMP"] ? LOW : HIGH;
    if (doc.containsKey("REL_WATER_FLOW")) stateFlow = doc["REL_WATER_FLOW"] ? LOW : HIGH;
    if (doc.containsKey("REL_RAIN_PUMP")) stateRain = doc["REL_RAIN_PUMP"] ? LOW : HIGH;
    if (doc.containsKey("REL_LIGHT_CTRL")) stateLight = doc["REL_LIGHT_CTRL"] ? LOW : HIGH;
    if (doc.containsKey("camera")) {
      if (doc["camera"].containsKey("pan")) targetPan = doc["camera"]["pan"];
      if (doc["camera"].containsKey("tilt")) targetTilt = doc["camera"]["tilt"];
    }
    if (doc.containsKey("currentFishName")) {
      String tempName = doc["currentFishName"].as<String>();
      if (tempName != currentFishName && tempName.length() > 0) {
        currentFishName = tempName;
        saveConfig(savedSSID, savedPASS, currentFishName);
      }
    }
    if (doc.containsKey("esp32")) {
      String tempSsid = doc["esp32"]["ssid"].as<String>();
      if (doc["esp32"].containsKey("pass") && tempSsid.length() > 1 && tempSsid != "null") {
        pendingSSID = tempSsid;
        pendingPASS = doc["esp32"]["pass"].as<String>();
        newWifiPending = true;
      }
    }
  } else {
    if (path == "/targetTempMin") targetTempMin = data.floatData();
    else if (path == "/targetTempMax") targetTempMax = data.floatData();
    else if (path == "/targetPhMin") targetPhMin = data.floatData();
    else if (path == "/targetPhMax") targetPhMax = data.floatData();
    else if (path == "/REL_WATER_HEATER") stateHeater = data.boolData() ? LOW : HIGH;
    else if (path == "/REL_COOLER_FAN") stateCooler = data.boolData() ? LOW : HIGH;
    else if (path == "/REL_ACID_PUMP") stateAcid = data.boolData() ? LOW : HIGH;
    else if (path == "/REL_ALKALI_PUMP") stateAlkali = data.boolData() ? LOW : HIGH;
    else if (path == "/REL_AIR_PUMP") stateAir = data.boolData() ? LOW : HIGH;
    else if (path == "/REL_WATER_FLOW") stateFlow = data.boolData() ? LOW : HIGH;
    else if (path == "/REL_RAIN_PUMP") stateRain = data.boolData() ? LOW : HIGH;
    else if (path == "/REL_LIGHT_CTRL") stateLight = data.boolData() ? LOW : HIGH;
    else if (path == "/camera/pan") targetPan = data.intData();
    else if (path == "/camera/tilt") targetTilt = data.intData();
  }
}

void streamTimeoutCallback(bool timeout) {}

void networkTask(void *pvParameters) {
  unsigned long lastPing = 0;
  for(;;) {
    if (!isAPMode && WiFi.status() == WL_CONNECTED) {
      if (Firebase.ready()) {
        if (abs(currentTempC - lastSentTemp) >= 0.05) { Firebase.setFloat(firebaseData, "/temp", currentTempC); lastSentTemp = currentTempC; }
        if (abs(currentPH - lastSentPH) >= 0.02) { Firebase.setFloat(firebaseData, "/ph", currentPH); lastSentPH = currentPH; }
        if (millis() - lastPing >= 2000) {
          lastPing = millis();
          pingCounter++;
          Firebase.setInt(firebaseData, "/ping", pingCounter);
        }
        if (newWifiPending) {
          newWifiPending = false;
          saveConfig(pendingSSID, pendingPASS, currentFishName);
          Firebase.deleteNode(firebaseData, "/esp32");
          vTaskDelay(2000 / portTICK_PERIOD_MS);
          ESP.restart();
        }
      }
    }
    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
}

void setup() {
  LittleFS.begin();
  Wire.begin(SDA_PIN, SCL_PIN);
  lcd.init();
  lcd.backlight();
  lcd.createChar(0, dotAnim);
  
  pinMode(REL_ACID_PUMP, OUTPUT); digitalWrite(REL_ACID_PUMP, HIGH);
  pinMode(REL_ALKALI_PUMP, OUTPUT); digitalWrite(REL_ALKALI_PUMP, HIGH);
  pinMode(REL_COOLER_FAN, OUTPUT); digitalWrite(REL_COOLER_FAN, HIGH);
  pinMode(REL_WATER_HEATER, OUTPUT); digitalWrite(REL_WATER_HEATER, HIGH);
  pinMode(REL_AIR_PUMP, OUTPUT); digitalWrite(REL_AIR_PUMP, HIGH);
  pinMode(REL_WATER_FLOW, OUTPUT); digitalWrite(REL_WATER_FLOW, HIGH);
  pinMode(REL_RAIN_PUMP, OUTPUT); digitalWrite(REL_RAIN_PUMP, HIGH);
  pinMode(REL_LIGHT_CTRL, OUTPUT); digitalWrite(REL_LIGHT_CTRL, HIGH);

  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  panServo.setPeriodHertz(50);
  tiltServo.setPeriodHertz(50);
  panServo.attach(SERVO_PAN_PIN);
  tiltServo.attach(SERVO_TILT_PIN);
  panServo.write(90);
  tiltServo.write(90);

  sensors.begin();
  sensors.setWaitForConversion(false);
  sensors.requestTemperatures();
  
  loadConfig();
  
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  lcd.clear();
  bool connected = false;
  
  if (savedSSID.length() > 1) {
    WiFi.begin(savedSSID.c_str(), savedPASS.c_str());
    for (int i = 0; i <= 60; i++) {
      if (WiFi.status() == WL_CONNECTED) { connected = true; break; }
      lcd.setCursor(0, 0); lcd.print("Connecting WiFi.");
      lcd.setCursor(0, 1); lcd.print(savedSSID);
      delay(250);
    }
  }

  if (!connected) {
    isAPMode = true;
    WiFi.mode(WIFI_AP);
    WiFi.softAP("Smart_Breeder_AP", "12345678");
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request){
      StaticJsonDocument<256> doc;
      doc["temp"] = currentTempC;
      doc["ph"] = currentPH;
      doc["fishName"] = currentFishName;
      String response;
      serializeJson(doc, response);
      request->send(200, "application/json", response);
    });
    server.on("/api/setWifi", HTTP_GET, [](AsyncWebServerRequest *request){
      if(request->hasParam("ssid") && request->hasParam("pass")) {
        saveConfig(request->getParam("ssid")->value(), request->getParam("pass")->value(), currentFishName);
        request->send(200, "text/plain", "Saved. Restarting...");
        delay(1000);
        ESP.restart();
      } else {
        request->send(400, "text/plain", "Missing args");
      }
    });
    server.begin();
  } else {
    isOnline = true;
    config.api_key = "AIzaSyB8XmbJmsIMttA-FYlvP9ygRlW59WBUo50";
    config.database_url = "smart-fish-breeder-default-rtdb.firebaseio.com";
    config.signer.test_mode = true;
    firebaseData.setBSSLBufferSize(4096, 1024);
    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);
    Firebase.setStreamCallback(streamData, streamCallback, streamTimeoutCallback);
    Firebase.beginStream(streamData, "/");
  }

  lcd.clear();
  if (isOnline) {
    lcd.setCursor(0, 0); lcd.print("WiFi Connected  ");
    lcd.setCursor(0, 1); lcd.print("System is Online");
  } else {
    lcd.setCursor(0, 0); lcd.print("AP Mode Ready   ");
    lcd.setCursor(0, 1); lcd.print("IP: 192.168.4.1 ");
  }
  delay(3000);

  xTaskCreatePinnedToCore(networkTask, "NetworkTask", 10000, NULL, 1, &NetworkTask, 0);
  lastCycleTime = millis();
}

void loop() {
  unsigned long currentMillis = millis();

  if (currentMillis - lastSensorReadTime >= 1000) {
    lastSensorReadTime = currentMillis;
    float t = sensors.getTempCByIndex(0);
    if (t != DEVICE_DISCONNECTED_C) currentTempC = round(t * 100.0) / 100.0;
    sensors.requestTemperatures();
    currentPH = readPH();
    handleAutoControl();
  }

  applyHardwareStates();

  bool curHeater = (stateHeater == LOW);
  bool curCooler = (stateCooler == LOW);
  bool curAcid = (stateAcid == LOW);
  bool curAlkali = (stateAlkali == LOW);
  bool curAir = (stateAir == LOW);
  bool curFlow = (stateFlow == LOW);
  bool curRain = (stateRain == LOW);
  bool curLight = (stateLight == LOW);

  static bool pHeater, pCooler, pAcid, pAlkali, pAir, pFlow, pRain, pLight;
  if (curHeater != pHeater || curCooler != pCooler || curAcid != pAcid || curAlkali != pAlkali || 
      curAir != pAir || curFlow != pFlow || curRain != pRain || curLight != pLight) {
    showingRelays = true;
    relayScreenStartTime = currentMillis;
    pHeater = curHeater; pCooler = curCooler; pAcid = curAcid; pAlkali = curAlkali;
    pAir = curAir; pFlow = curFlow; pRain = curRain; pLight = curLight;
  }

  updateLCD();
}
