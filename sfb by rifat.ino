#include <WiFi.h>
#include <FirebaseESP32.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ESP32Servo.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

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
AsyncWebSocket ws("/ws");

TaskHandle_t Task0;
TaskHandle_t Task1;

byte dotAnim[8] = { 0b00000, 0b00000, 0b01110, 0b01110, 0b01110, 0b00000, 0b00000, 0b00000 };

volatile float currentTempC = 0.00;
volatile float currentPH = 7.00;
volatile float targetTempMin = 0.0;
volatile float targetTempMax = 0.0;
volatile float targetPhMin = 0.0;
volatile float targetPhMax = 0.0;
volatile int currentFishMode = 0;

float neutralVoltage = 2.00;
float offset = 0.0;
float lastSentTemp = -100.0;
float lastSentPH = -100.0;

bool lastPhysHeater = false;
bool lastPhysCooler = false;
bool lastPhysAcid = false;
bool lastPhysAlkali = false;
bool lastPhysAir = false;
bool lastPhysFlow = false;
bool lastPhysRain = false;
bool lastPhysLight = false;

String savedSSID = "";
String savedPASS = "";
String savedAPSSID = "SmartBreeder_AP";
String savedAPPASS = "12345678";
String currentFishName = "Manual";

bool newWifiPending = false;
String pendingSSID = "";
String pendingPASS = "";
bool isOnline = false;
bool isAPMode = false;

unsigned long lastSensorReadTime = 0;
unsigned long relayScreenStartTime = 0;
bool showingRelays = false;
int lcdScreen = 0;
unsigned long pingCounter = 0;
unsigned long lastPingTime = 0;
unsigned long lastCycleTime = 0;
bool isInCycle = false;
int cycleState = 0;
unsigned long cycleStateStartTime = 0;

int phState = 0; 
unsigned long phTimer = 0;
unsigned long wsUpdateTimer = 0;

int initStage = 0;
unsigned long initTimer = 0;
int dotCount = 1;

void loadConfig() {
  if (!LittleFS.begin(true)) return;
  File file = LittleFS.open("/config.json", "r");
  if (!file) return;
  StaticJsonDocument<512> doc;
  deserializeJson(doc, file);
  if(doc.containsKey("s")) savedSSID = doc["s"].as<String>();
  if(doc.containsKey("p")) savedPASS = doc["p"].as<String>();
  if(doc.containsKey("f")) currentFishName = doc["f"].as<String>();
  if(doc.containsKey("as")) savedAPSSID = doc["as"].as<String>();
  if(doc.containsKey("ap")) savedAPPASS = doc["ap"].as<String>();
  file.close();
}

void saveConfig() {
  StaticJsonDocument<512> doc;
  doc["s"] = savedSSID;
  doc["p"] = savedPASS;
  doc["f"] = currentFishName;
  doc["as"] = savedAPSSID;
  doc["ap"] = savedAPPASS;
  File file = LittleFS.open("/config.json", "w");
  if(file) {
    serializeJson(doc, file);
    file.close();
  }
}

int getMedianADC(int pin) {
  int buf[10];
  for(int i = 0; i < 10; i++) buf[i] = analogRead(pin);
  for(int i = 0; i < 9; i++) {
    for(int j = i + 1; j < 10; j++) {
      if(buf[i] > buf[j]) { int temp = buf[i]; buf[i] = buf[j]; buf[j] = temp; }
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

void broadcastWS() {
  StaticJsonDocument<512> doc;
  doc["temp"] = currentTempC;
  doc["ph"] = currentPH;
  doc["fishMode"] = currentFishMode;
  doc["REL_ACID_PUMP"] = digitalRead(REL_ACID_PUMP) == LOW;
  doc["REL_ALKALI_PUMP"] = digitalRead(REL_ALKALI_PUMP) == LOW;
  doc["REL_COOLER_FAN"] = digitalRead(REL_COOLER_FAN) == LOW;
  doc["REL_WATER_HEATER"] = digitalRead(REL_WATER_HEATER) == LOW;
  doc["REL_AIR_PUMP"] = digitalRead(REL_AIR_PUMP) == LOW;
  doc["REL_WATER_FLOW"] = digitalRead(REL_WATER_FLOW) == LOW;
  doc["REL_RAIN_PUMP"] = digitalRead(REL_RAIN_PUMP) == LOW;
  doc["REL_LIGHT_CTRL"] = digitalRead(REL_LIGHT_CTRL) == LOW;
  doc["camPan"] = panServo.read();
  doc["camTilt"] = tiltServo.read();
  String response;
  serializeJson(doc, response);
  ws.textAll(response);
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
      StaticJsonDocument<512> doc;
      deserializeJson(doc, (char*)data);
      if(doc.containsKey("cmd")) {
        String cmd = doc["cmd"].as<String>();
        if(cmd == "setRelay") {
          String id = doc["id"].as<String>();
          int state = doc["state"].as<int>();
          if (id == "REL_ACID_PUMP") digitalWrite(REL_ACID_PUMP, state ? LOW : HIGH);
          else if (id == "REL_ALKALI_PUMP") digitalWrite(REL_ALKALI_PUMP, state ? LOW : HIGH);
          else if (id == "REL_COOLER_FAN") digitalWrite(REL_COOLER_FAN, state ? LOW : HIGH);
          else if (id == "REL_WATER_HEATER") digitalWrite(REL_WATER_HEATER, state ? LOW : HIGH);
          else if (id == "REL_AIR_PUMP") digitalWrite(REL_AIR_PUMP, state ? LOW : HIGH);
          else if (id == "REL_WATER_FLOW") digitalWrite(REL_WATER_FLOW, state ? LOW : HIGH);
          else if (id == "REL_RAIN_PUMP") digitalWrite(REL_RAIN_PUMP, state ? LOW : HIGH);
          else if (id == "REL_LIGHT_CTRL") digitalWrite(REL_LIGHT_CTRL, state ? LOW : HIGH);
        } else if (cmd == "setMode") {
          currentFishMode = doc["id"].as<int>();
          currentFishName = doc["name"].as<String>();
          targetTempMin = doc["tMin"].as<float>();
          targetTempMax = doc["tMax"].as<float>();
          targetPhMin = doc["pMin"].as<float>();
          targetPhMax = doc["pMax"].as<float>();
          if (doc["flow"].as<int>() == 1) digitalWrite(REL_WATER_FLOW, LOW);
          if (doc["rain"].as<int>() == 1) digitalWrite(REL_RAIN_PUMP, LOW);
          saveConfig();
        } else if (cmd == "moveCam") {
          panServo.write(doc["pan"].as<int>());
          tiltServo.write(doc["tilt"].as<int>());
        } else if (cmd == "setWifi") {
          savedSSID = doc["ssid"].as<String>();
          savedPASS = doc["pass"].as<String>();
          saveConfig();
          ESP.restart();
        }
        broadcastWS();
      }
    }
  }
}

void streamCallback(StreamData data) {
  String path = data.dataPath();
  if (path == "/targetTempMin") targetTempMin = data.doubleData();
  else if (path == "/targetTempMax") targetTempMax = data.doubleData();
  else if (path == "/targetPhMin") targetPhMin = data.doubleData();
  else if (path == "/targetPhMax") targetPhMax = data.doubleData();
  else if (path == "/REL_WATER_HEATER") digitalWrite(REL_WATER_HEATER, data.boolData() ? LOW : HIGH);
  else if (path == "/REL_COOLER_FAN") digitalWrite(REL_COOLER_FAN, data.boolData() ? LOW : HIGH);
  else if (path == "/REL_ACID_PUMP") digitalWrite(REL_ACID_PUMP, data.boolData() ? LOW : HIGH);
  else if (path == "/REL_ALKALI_PUMP") digitalWrite(REL_ALKALI_PUMP, data.boolData() ? LOW : HIGH);
  else if (path == "/REL_AIR_PUMP") digitalWrite(REL_AIR_PUMP, data.boolData() ? LOW : HIGH);
  else if (path == "/REL_WATER_FLOW") digitalWrite(REL_WATER_FLOW, data.boolData() ? LOW : HIGH);
  else if (path == "/REL_RAIN_PUMP") digitalWrite(REL_RAIN_PUMP, data.boolData() ? LOW : HIGH);
  else if (path == "/REL_LIGHT_CTRL") digitalWrite(REL_LIGHT_CTRL, data.boolData() ? LOW : HIGH);
  else if (path == "/camera/pan") panServo.write(data.intData());
  else if (path == "/camera/tilt") tiltServo.write(data.intData());
  else if (path == "/currentFishName") {
    String tempName = data.stringData();
    if (tempName != currentFishName && tempName.length() > 0) {
      currentFishName = tempName;
      saveConfig();
    }
  }
  else if (path == "/esp32/ssid") {
    String s = data.stringData();
    if (s.length() > 1 && s != "null" && s != savedSSID) { pendingSSID = s; newWifiPending = true; }
  }
  else if (path == "/esp32/pass") pendingPASS = data.stringData();
}

void streamTimeoutCallback(bool timeout) {}

void Task0Code(void * pvParameters) {
  for(;;) {
    unsigned long curMs = millis();
    if (isAPMode) {
      ws.cleanupClients();
    } else {
      isOnline = (WiFi.status() == WL_CONNECTED);
      if (isOnline && Firebase.ready()) {
        if (abs(currentTempC - lastSentTemp) >= 0.05) { Firebase.setFloat(firebaseData, "/temp", currentTempC); lastSentTemp = currentTempC; }
        if (abs(currentPH - lastSentPH) >= 0.02) { Firebase.setFloat(firebaseData, "/ph", currentPH); lastSentPH = currentPH; }
        
        if (curMs - lastPingTime >= 3000) {
          lastPingTime = curMs;
          pingCounter++; 
          Firebase.setInt(firebaseData, "/ping", pingCounter);
        }

        bool curH = (digitalRead(REL_WATER_HEATER) == LOW); if (curH != lastPhysHeater) { Firebase.setBool(firebaseData, "/REL_WATER_HEATER", curH); lastPhysHeater = curH; }
        bool curF = (digitalRead(REL_COOLER_FAN) == LOW); if (curF != lastPhysCooler) { Firebase.setBool(firebaseData, "/REL_COOLER_FAN", curF); lastPhysCooler = curF; }
        bool curA = (digitalRead(REL_ACID_PUMP) == LOW); if (curA != lastPhysAcid) { Firebase.setBool(firebaseData, "/REL_ACID_PUMP", curA); lastPhysAcid = curA; }
        bool curK = (digitalRead(REL_ALKALI_PUMP) == LOW); if (curK != lastPhysAlkali) { Firebase.setBool(firebaseData, "/REL_ALKALI_PUMP", curK); lastPhysAlkali = curK; }
        bool curP = (digitalRead(REL_AIR_PUMP) == LOW); if (curP != lastPhysAir) { Firebase.setBool(firebaseData, "/REL_AIR_PUMP", curP); lastPhysAir = curP; }
        bool curW = (digitalRead(REL_WATER_FLOW) == LOW); if (curW != lastPhysFlow) { Firebase.setBool(firebaseData, "/REL_WATER_FLOW", curW); lastPhysFlow = curW; }
        bool curR = (digitalRead(REL_RAIN_PUMP) == LOW); if (curR != lastPhysRain) { Firebase.setBool(firebaseData, "/REL_RAIN_PUMP", curR); lastPhysRain = curR; }
        bool curL = (digitalRead(REL_LIGHT_CTRL) == LOW); if (curL != lastPhysLight) { Firebase.setBool(firebaseData, "/REL_LIGHT_CTRL", curL); lastPhysLight = curL; }

        if (newWifiPending) {
          newWifiPending = false;
          savedSSID = pendingSSID;
          savedPASS = pendingPASS;
          saveConfig();
          Firebase.deleteNode(firebaseData, "/esp32");
          vTaskDelay(2000 / portTICK_PERIOD_MS);
          ESP.restart();
        }
      }
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void Task1Code(void * pvParameters) {
  for(;;) {
    unsigned long currentMillis = millis();

    if (initStage < 6) {
      if (initStage == 0) {
        if (savedSSID.length() > 1 && WiFi.status() != WL_CONNECTED && currentMillis - initTimer >= 250) {
          initTimer = currentMillis;
          lcd.setCursor(0, 0); lcd.print("Connecting WiFi.");
          String displaySSID = savedSSID;
          if(displaySSID.length() > 10) displaySSID = displaySSID.substring(0, 10);
          String dots = ""; for(int j = 0; j < dotCount; j++) dots += ".";
          String line2 = displaySSID + dots; padString(line2, 16);
          lcd.setCursor(0, 1); lcd.print(line2);
          dotCount++; if(dotCount > 4) dotCount = 1;
          if (currentMillis > 15000) initStage = 1; 
        } else if (WiFi.status() == WL_CONNECTED) {
          initStage = 2; initTimer = currentMillis;
        } else if (savedSSID.length() <= 1) {
          initStage = 1;
        }
      } else if (initStage == 1) {
        isAPMode = true; isOnline = false;
        WiFi.mode(WIFI_AP);
        WiFi.softAP(savedAPSSID.c_str(), savedAPPASS.c_str());
        lcd.clear(); lcd.setCursor(0, 0); lcd.print("AP MODE READY   ");
        String appDisplay = savedAPSSID; if(appDisplay.length() > 16) appDisplay = appDisplay.substring(0, 16);
        lcd.setCursor(0, 1); lcd.print(appDisplay);
        initStage = 3; initTimer = currentMillis;
      } else if (initStage == 2) {
        isAPMode = false; isOnline = true;
        lcd.clear(); lcd.setCursor(0, 0); lcd.print("WiFi Connected  ");
        lcd.setCursor(0, 1); lcd.print("System is Online");
        config.api_key = "AIzaSyB8XmbJmsIMttA-FYlvP9ygRlW59WBUo50";
        config.database_url = "smart-fish-breeder-default-rtdb.firebaseio.com";
        config.signer.test_mode = true;
        firebaseData.setBSSLBufferSize(4096, 1024);
        Firebase.begin(&config, &auth);
        Firebase.reconnectWiFi(true);
        Firebase.setStreamCallback(streamData, streamCallback, streamTimeoutCallback);
        Firebase.beginStream(streamData, "/");
        initStage = 3; initTimer = currentMillis;
      } else if (initStage == 3 && currentMillis - initTimer >= 2000) {
        ws.onEvent(onEvent);
        server.addHandler(&ws);
        server.begin();
        lcd.clear(); lcd.setCursor(0, 0); lcd.print("Automatic Fish");
        lcd.setCursor(0, 1); lcd.print("Breeder Machine");
        initStage = 4; initTimer = currentMillis;
      } else if (initStage == 4 && currentMillis - initTimer >= 2000) {
        lcd.clear(); lcd.setCursor(0, 0); lcd.print("App name");
        lcd.setCursor(0, 1); lcd.print("SmartFishBreeder");
        initStage = 5; initTimer = currentMillis;
      } else if (initStage == 5 && currentMillis - initTimer >= 2000) {
        lcd.clear(); lcd.setCursor(0, 0); lcd.print("Team Leader");
        lcd.setCursor(0, 1); lcd.print("Naim Islam");
        initStage = 6; initTimer = currentMillis;
      }
      vTaskDelay(10 / portTICK_PERIOD_MS);
      continue;
    }

    if (currentMillis - lastSensorReadTime >= 1000) {
      lastSensorReadTime = currentMillis;
      float t = sensors.getTempCByIndex(0);
      if (t != DEVICE_DISCONNECTED_C) currentTempC = round(t * 100.0) / 100.0;
      sensors.requestTemperatures();
      currentPH = readPH();
    }

    bool autoMode = (targetTempMin > 0 && targetTempMax > 0 && targetPhMin > 0 && targetPhMax > 0);
    if (autoMode) {
      if (currentTempC < targetTempMin) digitalWrite(REL_WATER_HEATER, LOW);
      else if (currentTempC > targetTempMax) digitalWrite(REL_COOLER_FAN, LOW);
      else { digitalWrite(REL_WATER_HEATER, HIGH); digitalWrite(REL_COOLER_FAN, HIGH); }

      if (phState == 0) {
        if (currentPH > targetPhMax) {
          digitalWrite(REL_ACID_PUMP, LOW);
          phState = 1; phTimer = currentMillis;
        } else if (currentPH < targetPhMin) {
          digitalWrite(REL_ALKALI_PUMP, LOW);
          phState = 2; phTimer = currentMillis;
        } else {
          digitalWrite(REL_ACID_PUMP, HIGH); digitalWrite(REL_ALKALI_PUMP, HIGH);
        }
      } else if (phState == 1) {
        if (currentMillis - phTimer >= 3000) {
          digitalWrite(REL_ACID_PUMP, HIGH);
          phState = 3; phTimer = currentMillis;
        }
      } else if (phState == 2) {
        if (currentMillis - phTimer >= 3000) {
          digitalWrite(REL_ALKALI_PUMP, HIGH);
          phState = 3; phTimer = currentMillis;
        }
      } else if (phState == 3) {
        if (currentMillis - phTimer >= 300000) {
          phState = 0;
        }
      }
    }

    if (currentMillis - lastCycleTime >= 180000) {
      isInCycle = true; cycleState = 10; cycleStateStartTime = currentMillis; lastCycleTime = currentMillis;
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
    } else lcdScreen = 0;

    String line1 = ""; String line2 = "";
    if (lcdScreen == 0) { line1 = "Mode:" + currentFishName; line2 = "pH:" + String(currentPH, 2) + " T:" + String(currentTempC, 2) + "c"; }
    else if (lcdScreen == 1) {
      line1 = String("H:") + (digitalRead(REL_WATER_HEATER) == LOW ? "ON " : "OFF ");
      line1 += String("F:") + (digitalRead(REL_COOLER_FAN) == LOW ? "ON" : "OFF");
      line2 = String("A:") + (digitalRead(REL_ACID_PUMP) == LOW ? "ON " : "OFF ");
      line2 += String("K:") + (digitalRead(REL_ALKALI_PUMP) == LOW ? "ON" : "OFF");
    } else if (lcdScreen == 2) {
      line1 = String("P:") + (digitalRead(REL_AIR_PUMP) == LOW ? "ON " : "OFF ");
      line1 += String("W:") + (digitalRead(REL_WATER_FLOW) == LOW ? "ON" : "OFF");
      line2 = String("R:") + (digitalRead(REL_RAIN_PUMP) == LOW ? "ON " : "OFF ");
      line2 += String("L:") + (digitalRead(REL_LIGHT_CTRL) == LOW ? "ON" : "OFF");
    } else if (lcdScreen == 10) { line1 = "Automatic Fish"; line2 = "Breeder Machine"; }
    else if (lcdScreen == 11) { line1 = "App name"; line2 = "SmartFishBreeder"; }
    else if (lcdScreen == 12) { line1 = "Bogura Polytech."; line2 = "Institute"; }
    else if (lcdScreen == 13) { line1 = "Team Leader"; line2 = "Naim Islam"; }
    
    padString(line1, 16); padString(line2, 16);
    lcd.setCursor(0, 0); lcd.print(line1);
    lcd.setCursor(0, 1); lcd.print(line2);
    
    if (isOnline && lcdScreen == 0) {
      lcd.setCursor(15, 0);
      if ((currentMillis / 500) % 2 == 0) lcd.write(0); else lcd.print(" ");
    }

    if (isAPMode && currentMillis - wsUpdateTimer >= 1000) {
      wsUpdateTimer = currentMillis;
      broadcastWS();
    }

    bool curH = (digitalRead(REL_WATER_HEATER) == LOW);
    bool curF = (digitalRead(REL_COOLER_FAN) == LOW);
    bool curA = (digitalRead(REL_ACID_PUMP) == LOW);
    bool curK = (digitalRead(REL_ALKALI_PUMP) == LOW);
    bool curP = (digitalRead(REL_AIR_PUMP) == LOW);
    bool curW = (digitalRead(REL_WATER_FLOW) == LOW);
    bool curR = (digitalRead(REL_RAIN_PUMP) == LOW);
    bool curL = (digitalRead(REL_LIGHT_CTRL) == LOW);

    if (curH != lastPhysHeater || curF != lastPhysCooler || curA != lastPhysAcid || curK != lastPhysAlkali || 
        curP != lastPhysAir || curW != lastPhysFlow || curR != lastPhysRain || curL != lastPhysLight) {
      showingRelays = true;
      relayScreenStartTime = currentMillis;
      lastPhysHeater = curH; lastPhysCooler = curF; lastPhysAcid = curA; lastPhysAlkali = curK;
      lastPhysAir = curP; lastPhysFlow = curW; lastPhysRain = curR; lastPhysLight = curL;
    }

    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void setup() {
  Wire.begin(SDA_PIN, SCL_PIN);
  lcd.init(); lcd.backlight();
  lcd.createChar(0, dotAnim);
  
  pinMode(REL_ACID_PUMP, OUTPUT); digitalWrite(REL_ACID_PUMP, HIGH);
  pinMode(REL_ALKALI_PUMP, OUTPUT); digitalWrite(REL_ALKALI_PUMP, HIGH);
  pinMode(REL_COOLER_FAN, OUTPUT); digitalWrite(REL_COOLER_FAN, HIGH);
  pinMode(REL_WATER_HEATER, OUTPUT); digitalWrite(REL_WATER_HEATER, HIGH);
  pinMode(REL_AIR_PUMP, OUTPUT); digitalWrite(REL_AIR_PUMP, HIGH);
  pinMode(REL_WATER_FLOW, OUTPUT); digitalWrite(REL_WATER_FLOW, HIGH);
  pinMode(REL_RAIN_PUMP, OUTPUT); digitalWrite(REL_RAIN_PUMP, HIGH);
  pinMode(REL_LIGHT_CTRL, OUTPUT); digitalWrite(REL_LIGHT_CTRL, HIGH);

  ESP32PWM::allocateTimer(0); ESP32PWM::allocateTimer(1); ESP32PWM::allocateTimer(2); ESP32PWM::allocateTimer(3);
  panServo.setPeriodHertz(50); tiltServo.setPeriodHertz(50);
  panServo.attach(SERVO_PAN_PIN); tiltServo.attach(SERVO_TILT_PIN);
  panServo.write(90); tiltServo.write(90);

  sensors.begin();
  sensors.setWaitForConversion(false);
  sensors.requestTemperatures();
  
  loadConfig();
  
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  if (savedSSID.length() > 1) {
    WiFi.begin(savedSSID.c_str(), savedPASS.c_str());
  }

  xTaskCreatePinnedToCore(Task0Code, "Task0", 10000, NULL, 1, &Task0, 0);
  xTaskCreatePinnedToCore(Task1Code, "Task1", 10000, NULL, 1, &Task1, 1);
}

void loop() {
  vTaskDelete(NULL);
}
