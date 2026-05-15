#include <WiFi.h>
#include <FirebaseESP32.h>
#include <EEPROM.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ESP32Servo.h>

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
FirebaseAuth auth;
FirebaseConfig config;

LiquidCrystal_I2C lcd(0x27, 16, 2);
OneWire oneWire(TEMP_PIN);
DallasTemperature sensors(&oneWire);
Servo panServo;
Servo tiltServo;

byte dotAnim[8] = {
  0b00000,
  0b00000,
  0b01110,
  0b01110,
  0b01110,
  0b00000,
  0b00000,
  0b00000
};

float currentTempC = 0.00;
float currentPH = 7.00;
float targetTempMin = 0.0;
float targetTempMax = 0.0;
float targetPhMin = 0.0;
float targetPhMax = 0.0;

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
String currentFishName = "Manual";
bool newWifiPending = false;
String pendingSSID = "";
String pendingPASS = "";
bool connected = false;
bool isOnline = false;

unsigned long lastSensorReadTime = 0;
unsigned long lastFirebaseReadTime = 0;

unsigned long relayScreenStartTime = 0;
bool showingRelays = false;
int lcdScreen = 0;
unsigned long pingCounter = 0;

unsigned long lastCycleTime = 0;
bool isInCycle = false;
int cycleState = 0;
unsigned long cycleStateStartTime = 0;

void saveConfig(String s, String p, String f) {
  for (int i = 0; i < 128; ++i) EEPROM.write(i, 0);
  for (int i = 0; i < s.length(); ++i) EEPROM.write(i, s[i]);
  for (int i = 0; i < p.length(); ++i) EEPROM.write(32 + i, p[i]);
  for (int i = 0; i < f.length(); ++i) EEPROM.write(96 + i, f[i]);
  EEPROM.commit();
}

void loadConfig() {
  savedSSID = "";
  savedPASS = "";
  currentFishName = "";
  for (int i = 0; i < 32; ++i) {
    char c = EEPROM.read(i);
    if(c == 0 || c == 255) break;
    savedSSID += c;
  }
  for (int i = 32; i < 96; ++i) {
    char c = EEPROM.read(i);
    if(c == 0 || c == 255) break;
    savedPASS += c;
  }
  for (int i = 96; i < 128; ++i) {
    char c = EEPROM.read(i);
    if(c == 0 || c == 255) break;
    currentFishName += c;
  }
  if (currentFishName == "") currentFishName = "Manual";
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
  for(int i = 2; i < 8; i++) {
    avgValue += buf[i];
  }
  return avgValue / 6;
}

float readPH() {
  int adc = getMedianADC(PH_PIN);
  float voltage = adc * (3.3 / 4095.0);
  float pH = 7.0;

  if (voltage >= 2.15) {
    pH = 7.0 + (neutralVoltage - voltage) * 20.0;
  } else if (voltage <= 1.80) {
    pH = 7.0 + (neutralVoltage - voltage) * 9.0;
  } else {
    pH = 7.0 + (neutralVoltage - voltage) * 3.0;
  }

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
    bool heater = HIGH;
    bool cooler = HIGH;
    bool acid = HIGH;
    bool alkali = HIGH;

    if (currentTempC < targetTempMin) heater = LOW;
    else if (currentTempC > targetTempMax) cooler = LOW;

    if (currentPH < targetPhMin) alkali = LOW;
    else if (currentPH > targetPhMax) acid = LOW;

    digitalWrite(REL_WATER_HEATER, heater);
    digitalWrite(REL_COOLER_FAN, cooler);
    digitalWrite(REL_ACID_PUMP, acid);
    digitalWrite(REL_ALKALI_PUMP, alkali);
  }
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
    if (elapsed < 5000) {
      lcdScreen = 1;
    } else if (elapsed < 10000) {
      lcdScreen = 2;
    } else {
      showingRelays = false;
      lcdScreen = 0;
    }
  } else {
    lcdScreen = 0;
  }

  String line1 = "";
  String line2 = "";
  
  if (lcdScreen == 0) {
    line1 = "Mode:" + currentFishName;
    line2 = "pH:" + String(currentPH, 2) + " T:" + String(currentTempC, 2) + "c";
  } else if (lcdScreen == 1) {
    line1 = String("H:") + (digitalRead(REL_WATER_HEATER) == LOW ? "ON " : "OFF ");
    line1 += String("F:") + (digitalRead(REL_COOLER_FAN) == LOW ? "ON" : "OFF");
    line2 = String("A:") + (digitalRead(REL_ACID_PUMP) == LOW ? "ON " : "OFF ");
    line2 += String("K:") + (digitalRead(REL_ALKALI_PUMP) == LOW ? "ON" : "OFF");
  } else if (lcdScreen == 2) {
    line1 = String("P:") + (digitalRead(REL_AIR_PUMP) == LOW ? "ON " : "OFF ");
    line1 += String("W:") + (digitalRead(REL_WATER_FLOW) == LOW ? "ON" : "OFF");
    line2 = String("R:") + (digitalRead(REL_RAIN_PUMP) == LOW ? "ON " : "OFF ");
    line2 += String("L:") + (digitalRead(REL_LIGHT_CTRL) == LOW ? "ON" : "OFF");
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
  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);

  if (isOnline && lcdScreen == 0) {
    lcd.setCursor(15, 0);
    if ((currentMillis / 500) % 2 == 0) {
      lcd.write(0);
    } else {
      lcd.print(" ");
    }
  }
}

void setup() {
  EEPROM.begin(512);
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
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  
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
  
  if (savedSSID.length() > 1) {
    WiFi.begin(savedSSID.c_str(), savedPASS.c_str());
    int dotCount = 1;
    for (int i = 0; i <= 60; i++) {
      if (WiFi.status() == WL_CONNECTED) { 
        connected = true;
        break; 
      }
      lcd.setCursor(0, 0);
      lcd.print("Connecting WiFi.");
      
      String displaySSID = savedSSID;
      if(displaySSID.length() > 10) displaySSID = displaySSID.substring(0, 10);
      String dots = "";
      for(int j = 0; j < dotCount; j++) dots += ".";
      String line2 = displaySSID + dots;
      padString(line2, 16);
      
      lcd.setCursor(0, 1);
      lcd.print(line2);
      
      dotCount++; 
      if(dotCount > 4) dotCount = 1;
      delay(250);
    }
  }

  if (!connected) {
    WiFi.disconnect();
    delay(100);
    WiFi.begin("Smart Breeder", "smartbreeder");
    for (int i = 1; i <= 20; i++) {
      if (WiFi.status() == WL_CONNECTED) { 
        connected = true;
        break; 
      }
      lcd.setCursor(0, 0);
      lcd.print("Connecting WiFi.");
      
      char bubble = '.';
      int bState = i % 4;
      if (bState == 0) bubble = '.';
      else if (bState == 1) bubble = 'o';
      else if (bState == 2) bubble = 'O';
      else if (bState == 3) bubble = 'o';

      String line2 = "Default " + String(bubble);
      String countStr = String(i) + "s";
      while(line2.length() + countStr.length() < 16) {
        line2 += " ";
      }
      line2 += countStr;
      
      lcd.setCursor(0, 1);
      lcd.print(line2);
      delay(1000);
    }
  }

  isOnline = connected;

  lcd.clear();
  if (isOnline) {
    lcd.setCursor(0, 0); lcd.print("WiFi Connected  ");
    lcd.setCursor(0, 1); lcd.print("System is Online");
  } else {
    lcd.setCursor(0, 0); lcd.print("System Offline  ");
  }
  delay(3000);

  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Automatic Fish");
  lcd.setCursor(0, 1); lcd.print("Breeder Machine");
  delay(5000);
  
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("App name");
  lcd.setCursor(0, 1); lcd.print("SmartFishBreeder");
  delay(5000);
  
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Bogura Polytech.");
  lcd.setCursor(0, 1); lcd.print("Institute");
  delay(5000);

  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Team Leader");
  lcd.setCursor(0, 1); lcd.print("Naim Islam");
  delay(5000);

  if (isOnline) {
    config.api_key = "AIzaSyB8XmbJmsIMttA-FYlvP9ygRlW59WBUo50";
    config.database_url = "smart-fish-breeder-default-rtdb.firebaseio.com";
    config.signer.test_mode = true;
    firebaseData.setBSSLBufferSize(4096, 1024);
    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);
  }

  lastPhysHeater = (digitalRead(REL_WATER_HEATER) == LOW);
  lastPhysCooler = (digitalRead(REL_COOLER_FAN) == LOW);
  lastPhysAcid = (digitalRead(REL_ACID_PUMP) == LOW);
  lastPhysAlkali = (digitalRead(REL_ALKALI_PUMP) == LOW);
  lastPhysAir = (digitalRead(REL_AIR_PUMP) == LOW);
  lastPhysFlow = (digitalRead(REL_WATER_FLOW) == LOW);
  lastPhysRain = (digitalRead(REL_RAIN_PUMP) == LOW);
  lastPhysLight = (digitalRead(REL_LIGHT_CTRL) == LOW);

  lastCycleTime = millis();
}

void loop() {
  unsigned long currentMillis = millis();
  isOnline = (WiFi.status() == WL_CONNECTED);

  if (currentMillis - lastSensorReadTime >= 1000) {
    lastSensorReadTime = currentMillis;
    float t = sensors.getTempCByIndex(0);
    if (t != DEVICE_DISCONNECTED_C) currentTempC = round(t * 100.0) / 100.0;
    sensors.requestTemperatures();
    currentPH = readPH();
    handleAutoControl();
  }

  bool curHeater = (digitalRead(REL_WATER_HEATER) == LOW);
  bool curCooler = (digitalRead(REL_COOLER_FAN) == LOW);
  bool curAcid = (digitalRead(REL_ACID_PUMP) == LOW);
  bool curAlkali = (digitalRead(REL_ALKALI_PUMP) == LOW);
  bool curAir = (digitalRead(REL_AIR_PUMP) == LOW);
  bool curFlow = (digitalRead(REL_WATER_FLOW) == LOW);
  bool curRain = (digitalRead(REL_RAIN_PUMP) == LOW);
  bool curLight = (digitalRead(REL_LIGHT_CTRL) == LOW);

  if (curHeater != lastPhysHeater || curCooler != lastPhysCooler || 
      curAcid != lastPhysAcid || curAlkali != lastPhysAlkali || 
      curAir != lastPhysAir || curFlow != lastPhysFlow || 
      curRain != lastPhysRain || curLight != lastPhysLight) {
    
    showingRelays = true;
    relayScreenStartTime = currentMillis;

    lastPhysHeater = curHeater;
    lastPhysCooler = curCooler;
    lastPhysAcid = curAcid;
    lastPhysAlkali = curAlkali;
    lastPhysAir = curAir;
    lastPhysFlow = curFlow;
    lastPhysRain = curRain;
    lastPhysLight = curLight;
  }

  updateLCD();

  if (isOnline && Firebase.ready()) {
    if (abs(currentTempC - lastSentTemp) >= 0.05) { Firebase.setFloat(firebaseData, "/temp", currentTempC); lastSentTemp = currentTempC; }
    if (abs(currentPH - lastSentPH) >= 0.02) { Firebase.setFloat(firebaseData, "/ph", currentPH); lastSentPH = currentPH; }
    
    if (currentMillis - lastFirebaseReadTime >= 1500) {
      lastFirebaseReadTime = currentMillis;
      
      bool curH = (digitalRead(REL_WATER_HEATER) == LOW); if (curH != lastPhysHeater) { Firebase.setBool(firebaseData, "/REL_WATER_HEATER", curH); lastPhysHeater = curH; }
      bool curF = (digitalRead(REL_COOLER_FAN) == LOW); if (curF != lastPhysCooler) { Firebase.setBool(firebaseData, "/REL_COOLER_FAN", curF); lastPhysCooler = curF; }
      bool curA = (digitalRead(REL_ACID_PUMP) == LOW); if (curA != lastPhysAcid) { Firebase.setBool(firebaseData, "/REL_ACID_PUMP", curA); lastPhysAcid = curA; }
      bool curK = (digitalRead(REL_ALKALI_PUMP) == LOW); if (curK != lastPhysAlkali) { Firebase.setBool(firebaseData, "/REL_ALKALI_PUMP", curK); lastPhysAlkali = curK; }

      if (Firebase.getJSON(firebaseData, "/")) {
        FirebaseJson &json = firebaseData.jsonObject(); 
        FirebaseJsonData data;

        if (json.get(data, "targetTempMin")) targetTempMin = data.doubleValue;
        if (json.get(data, "targetTempMax")) targetTempMax = data.doubleValue;
        if (json.get(data, "targetPhMin")) targetPhMin = data.doubleValue;
        if (json.get(data, "targetPhMax")) targetPhMax = data.doubleValue;

        if (json.get(data, "REL_WATER_HEATER")) digitalWrite(REL_WATER_HEATER, data.boolValue ? LOW : HIGH);
        if (json.get(data, "REL_COOLER_FAN")) digitalWrite(REL_COOLER_FAN, data.boolValue ? LOW : HIGH);
        if (json.get(data, "REL_ACID_PUMP")) digitalWrite(REL_ACID_PUMP, data.boolValue ? LOW : HIGH);
        if (json.get(data, "REL_ALKALI_PUMP")) digitalWrite(REL_ALKALI_PUMP, data.boolValue ? LOW : HIGH);

        if (json.get(data, "REL_AIR_PUMP")) digitalWrite(REL_AIR_PUMP, data.boolValue ? LOW : HIGH);
        if (json.get(data, "REL_WATER_FLOW")) digitalWrite(REL_WATER_FLOW, data.boolValue ? LOW : HIGH);
        if (json.get(data, "REL_RAIN_PUMP")) digitalWrite(REL_RAIN_PUMP, data.boolValue ? LOW : HIGH);
        if (json.get(data, "REL_LIGHT_CTRL")) digitalWrite(REL_LIGHT_CTRL, data.boolValue ? LOW : HIGH);

        if (json.get(data, "camera/pan")) panServo.write(data.intValue);
        if (json.get(data, "camera/tilt")) tiltServo.write(data.intValue);

        if (json.get(data, "currentFishName")) {
          String tempName = data.stringValue;
          if (tempName != currentFishName && tempName.length() > 0) {
            currentFishName = tempName;
            saveConfig(savedSSID, savedPASS, currentFishName);
          }
        }

        if (json.get(data, "esp32/ssid")) {
          String tempSsid = data.stringValue;
          if (json.get(data, "esp32/pass") && tempSsid.length() > 1 && tempSsid != "null" && tempSsid != savedSSID) {
            pendingSSID = tempSsid; 
            pendingPASS = data.stringValue; 
            newWifiPending = true;
          }
        }
      }
      pingCounter++; 
      Firebase.setInt(firebaseData, "/ping", pingCounter);
    }

    if (newWifiPending) {
      newWifiPending = false;
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("Saving WiFi:");
      String dSsid = pendingSSID;
      if(dSsid.length() > 16) dSsid = dSsid.substring(0, 16);
      lcd.setCursor(0, 1); lcd.print(dSsid);
      saveConfig(pendingSSID, pendingPASS, currentFishName);
      Firebase.deleteNode(firebaseData, "/esp32");
      delay(2000);
      ESP.restart();
    }
  }
  yield();
}
