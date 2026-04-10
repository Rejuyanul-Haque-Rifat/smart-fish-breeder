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

const uint8_t SERVO_PAN_PIN = 12;
const uint8_t SERVO_TILT_PIN = 13;

FirebaseData firebaseData;
FirebaseAuth auth;
FirebaseConfig config;

LiquidCrystal_I2C lcd(0x27, 16, 2);
OneWire oneWire(TEMP_PIN);
DallasTemperature sensors(&oneWire);
Servo panServo;
Servo tiltServo;

float currentTempC = 0.0;
float currentPH = 7.0;
String phCondition = "Normal";
String tempCondition = "Normal";
float targetTempMin = 0.0;
float targetTempMax = 0.0;
float targetPhMin = 0.0;
float targetPhMax = 0.0;
int activeFish = 0;

String savedSSID = "";
String savedPASS = "";
bool newWifiPending = false;
String pendingSSID = "";
String pendingPASS = "";
String currentFishName = "Manual";
bool connected = false;

unsigned long lastSyncTime = 0;
unsigned long lastTempRequest = 0;
unsigned long pingCounter = 0;
bool isOnline = false;
unsigned long lastIntroShowTime = 0;
const unsigned long INTRO_REPEAT_MS = 240000UL;  // 4 minutes
const unsigned long INTRO_SLIDE_MS = 3000UL;    // each intro line visible longer
const unsigned long PH_PULSE_MS = 3000UL;
const unsigned long PH_LOGIC_INTERVAL_MS = 120000UL;  // 2 min between pH dose checks (mixing time)
const unsigned long LCD_FISH_PAGE_MS = 6000UL;  // top line: Temp vs fish name

unsigned long acidPulseEndMs = 0;
unsigned long alkPulseEndMs = 0;
unsigned long lastPhLogicCheckMs = 0;
unsigned long lastLcdPageFlip = 0;
int lcdTopPage = 0;

void saveWiFi(String s, String p) {
  for (int i = 0; i < 96; ++i) EEPROM.write(i, 0);
  for (int i = 0; i < s.length(); ++i) EEPROM.write(i, s[i]);
  for (int i = 0; i < p.length(); ++i) EEPROM.write(32 + i, p[i]);
  EEPROM.commit();
  // Serial.println("WiFi Saved");
}

void loadWiFi() {
  savedSSID = "";
  savedPASS = "";
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
  // Serial.println("Saved Wifi : ");
  // Serial.println("SSID : " + savedSSID + " PASS : " + savedPASS);
}

float readPH() {
  int buf[15];
  for (byte i = 0; i < 15; i++) {
    buf[i] = analogRead(PH_PIN);
    delay(2);
  }
  for (byte i = 0; i < 14; i++) {
    for (byte j = i + 1; j < 15; j++) {
      if (buf[j] < buf[i]) {
        int t = buf[i];
        buf[i] = buf[j]; buf[j] = t;
      }
    }
  }
  float voltage = buf[7] * (3.3 / 4095.0);
  float ph = 7.0 + (2.50 - voltage) * 3.0 + 0.20;
  if (ph < 0) ph = 0;
  if (ph > 14) ph = 14;
  return ph;
}

void evaluatePHCondition() {
  if (currentPH < 6.5) {
    phCondition = "Acidik";
  } else if (currentPH > 7.5) {
    phCondition = "Alkaline";
  } else {
    phCondition = "Normal";
  }
}

void evaluateTempCondition() {
  if (currentTempC > 30.0f) {
    tempCondition = "Hot";
  } else if (currentTempC < 20.0f) {
    tempCondition = "Cold";
  } else {
    tempCondition = "Normal";
  }
}

/** Shorten "Normal" on line 1 so Temp + value fits 16 chars. */
String tempStatusForLcd() {
  return (tempCondition == "Normal") ? "Norm" : tempCondition;
}

void padString(String &str, int len) {
  if (str.length() > len) str = str.substring(0, len);
  while(str.length() < len) str += " ";
}

void showIntroSequence() {
  const char* msg1[7] = {
    "Automatic Fish",
    "App Name",
    "Team Name",
    "Bogura Polytech-",
    "Team Leader",
    "SoftWare Dev..",
    "HardWare Dev.."
  };

  const char* msg2[7] = {
    "Breeding Mach.",
    "Smart Breeder",
    "Team AquaNAR",
    "nic Institute",
    "Md Naim Islam",
    "Abu Hosain",
    "Rakibul Hasan"
  };

  for (int i = 0; i < 7; i++) {
    String line1 = msg1[i];
    String line2 = msg2[i];
    padString(line1, 16);
    padString(line2, 16);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(line1);
    lcd.setCursor(0, 1);
    lcd.print(line2);
    delay(INTRO_SLIDE_MS);
  }
}

/** Line 1 alternates Temp vs current fish name; line 2 = PH + condition. */
void updateLCD() {
  String line1;
  if (lcdTopPage == 0) {
    line1 = "Temp " + String(currentTempC, 1) + "C " + tempStatusForLcd();
  } else {
    line1 = "Mode:" + currentFishName;
  }
  String line2 = "PH:" + String(currentPH, 2) + " " + phCondition;
  padString(line1, 16);
  padString(line2, 16);
  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);
}

bool fishAutomationActive() {
  return activeFish > 0;
}

/** Call only on PH_LOGIC_INTERVAL_MS: above targetPhMax -> acid 3s; below targetPhMin -> alkali 3s (no overlap). */
void maybeStartPhPulse() {
  if (!fishAutomationActive() || targetPhMin <= 0.0 || targetPhMax <= 0.0) return;
  unsigned long now = millis();
  bool pulseActive =
      (acidPulseEndMs != 0 && now < acidPulseEndMs) ||
      (alkPulseEndMs != 0 && now < alkPulseEndMs);
  if (pulseActive) return;
  if (currentPH > targetPhMax) {
    acidPulseEndMs = now + PH_PULSE_MS;
  } else if (currentPH < targetPhMin) {
    alkPulseEndMs = now + PH_PULSE_MS;
  }
}

void refreshPhPumpPins() {
  if (!fishAutomationActive() || targetPhMin <= 0.0 || targetPhMax <= 0.0) {
    if (!fishAutomationActive()) return;
    digitalWrite(REL_ACID_PUMP, HIGH);
    digitalWrite(REL_ALKALI_PUMP, HIGH);
    acidPulseEndMs = 0;
    alkPulseEndMs = 0;
    return;
  }
  unsigned long now = millis();
  if (acidPulseEndMs != 0 && now >= acidPulseEndMs) acidPulseEndMs = 0;
  if (alkPulseEndMs != 0 && now >= alkPulseEndMs) alkPulseEndMs = 0;
  bool acidOn = (acidPulseEndMs != 0 && now < acidPulseEndMs);
  bool alkOn = (alkPulseEndMs != 0 && now < alkPulseEndMs);
  digitalWrite(REL_ACID_PUMP, acidOn ? LOW : HIGH);
  digitalWrite(REL_ALKALI_PUMP, alkOn ? LOW : HIGH);
}

/** Temp above targetTempMax -> fan; below targetTempMin -> heater; in range -> both off. */
void applyTemperatureControl() {
  if (!fishAutomationActive() || targetTempMin <= 0.0 || targetTempMax <= 0.0) return;
  if (currentTempC > targetTempMax) {
    digitalWrite(REL_COOLER_FAN, LOW);
    digitalWrite(REL_WATER_HEATER, HIGH);
  } else if (currentTempC < targetTempMin) {
    digitalWrite(REL_WATER_HEATER, LOW);
    digitalWrite(REL_COOLER_FAN, HIGH);
  } else {
    digitalWrite(REL_COOLER_FAN, HIGH);
    digitalWrite(REL_WATER_HEATER, HIGH);
  }
}

/** Push live sensors + status to RTDB every sensor cycle (matches app relay bool: true = ON). */
void pushLiveToFirebase() {
  if (!isOnline || !Firebase.ready()) return;
  pingCounter++;
  Firebase.setInt(firebaseData, "/ping", pingCounter);
  Firebase.setFloat(firebaseData, "/temp", currentTempC);
  Firebase.setFloat(firebaseData, "/ph", currentPH);
  Firebase.setString(firebaseData, "/tempCondition", tempCondition);
  Firebase.setString(firebaseData, "/phCondition", phCondition);
  if (fishAutomationActive()) {
    Firebase.setBool(firebaseData, "/REL_ACID_PUMP", digitalRead(REL_ACID_PUMP) == LOW);
    Firebase.setBool(firebaseData, "/REL_ALKALI_PUMP", digitalRead(REL_ALKALI_PUMP) == LOW);
    Firebase.setBool(firebaseData, "/REL_COOLER_FAN", digitalRead(REL_COOLER_FAN) == LOW);
    Firebase.setBool(firebaseData, "/REL_WATER_HEATER", digitalRead(REL_WATER_HEATER) == LOW);
  }
}

void setup() {
  Serial.begin(115200);
  EEPROM.begin(512);
  Wire.begin(SDA_PIN, SCL_PIN);
  lcd.init();
  lcd.backlight();
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("System Starting");
  // Serial.println("System Starting");
  delay(1500);

  showIntroSequence();
  lastIntroShowTime = millis();

  pinMode(REL_ACID_PUMP, OUTPUT); digitalWrite(REL_ACID_PUMP, HIGH);
  pinMode(REL_ALKALI_PUMP, OUTPUT); digitalWrite(REL_ALKALI_PUMP, HIGH);
  pinMode(REL_COOLER_FAN, OUTPUT); digitalWrite(REL_COOLER_FAN, HIGH);
  pinMode(REL_WATER_HEATER, OUTPUT); digitalWrite(REL_WATER_HEATER, HIGH);
  pinMode(REL_AIR_PUMP, OUTPUT); digitalWrite(REL_AIR_PUMP, HIGH);
  pinMode(REL_WATER_FLOW, OUTPUT); digitalWrite(REL_WATER_FLOW, HIGH);
  pinMode(REL_RAIN_PUMP, OUTPUT); digitalWrite(REL_RAIN_PUMP, HIGH);
  pinMode(REL_LIGHT_CTRL, OUTPUT); digitalWrite(REL_LIGHT_CTRL, HIGH);

  panServo.attach(SERVO_PAN_PIN);
  tiltServo.attach(SERVO_TILT_PIN);
  panServo.write(90);
  tiltServo.write(90);

  sensors.begin();
  sensors.setWaitForConversion(false);
  sensors.requestTemperatures();
  
  loadWiFi();

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  if (savedSSID.length() > 1) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Connecting WiFi");
    lcd.setCursor(0, 1);
    lcd.print(savedSSID.substring(0, 16));
    // Serial.print("Connecting to: ");
    // Serial.println(savedSSID);
    
    WiFi.begin(savedSSID.c_str(), savedPASS.c_str());
    for (int i = 0; i <= 60; i++) {
      if (WiFi.status() == WL_CONNECTED) { 
        connected = true;
        // Serial.println("Connected to Saved WiFi");
        break; 
      }
      delay(250);
    }
  }

  if (!connected) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Connecting WiFi");
    lcd.setCursor(0, 1);
    lcd.print("Smart Breeder");
    // Serial.println("Connecting to: Smart Breeder");
    
    WiFi.begin("Smart Breeder", "smartbreeder");
    for (int i = 0; i <= 60; i++) {
      if (WiFi.status() == WL_CONNECTED) { 
        connected = true;
        // Serial.println("Connected to Default WiFi");
        break; 
      }
      delay(250);
    }
  }

  isOnline = connected;
  lcd.clear();
  lcd.setCursor(0,0);

  if (isOnline) {
    lcd.print("System Online");
    // Serial.println("System is Online Initializing Firebase");
    config.api_key = "AIzaSyB8XmbJmsIMttA-FYlvP9ygRlW59WBUo50";
    config.database_url = "smart-fish-breeder-default-rtdb.firebaseio.com";
    config.signer.test_mode = true;
    firebaseData.setBSSLBufferSize(4096, 1024);
    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);
    // Serial.println("Firebase Ready");
  } else {
    lcd.print("Offline Mode");
    // Serial.println("System is Offline");
  }
  delay(2000);
  lastLcdPageFlip = millis();
}

void loop() {
  unsigned long currentMillis = millis();
  isOnline = (WiFi.status() == WL_CONNECTED);

  refreshPhPumpPins();
  applyTemperatureControl();

  if (currentMillis - lastLcdPageFlip >= LCD_FISH_PAGE_MS) {
    lastLcdPageFlip = currentMillis;
    lcdTopPage = (lcdTopPage == 0) ? 1 : 0;
    updateLCD();
  }

  if (currentMillis - lastTempRequest > 2000) {
    // DS18B20 is a digital sensor; use digital bus read each cycle.
    float t = sensors.getTempCByIndex(0);
    if (t != DEVICE_DISCONNECTED_C) currentTempC = t;
    sensors.requestTemperatures();
    currentPH = readPH();
    evaluatePHCondition();
    evaluateTempCondition();
    lastTempRequest = currentMillis;

    // Serial.print("Temp: "); Serial.print(currentTempC);
    // Serial.print(" ("); Serial.print(tempCondition);
    // Serial.print(") pH: "); Serial.print(currentPH);
    // Serial.print(" "); Serial.println(phCondition);

    // pH dosing decision only every 2 min (after 3 s dose, water needs time to mix)
    if (fishAutomationActive() && targetPhMin > 0.0 && targetPhMax > 0.0) {
      if (lastPhLogicCheckMs == 0) {
        lastPhLogicCheckMs = currentMillis;
      } else if (currentMillis - lastPhLogicCheckMs >= PH_LOGIC_INTERVAL_MS) {
        lastPhLogicCheckMs = currentMillis;
        maybeStartPhPulse();
      }
    }
    refreshPhPumpPins();
    if (isOnline && Firebase.ready()) {
      pushLiveToFirebase();
    }
    updateLCD();
  }

  if (currentMillis - lastIntroShowTime >= INTRO_REPEAT_MS) {
    showIntroSequence();
    lastIntroShowTime = millis();
    updateLCD();
  }

  if (isOnline && Firebase.ready()) {
    if (newWifiPending) {
      newWifiPending = false;
      // Serial.println("Rebooting to connect to new WiFi");
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Saving New WiFi");
      saveWiFi(pendingSSID, pendingPASS);
      Firebase.deleteNode(firebaseData, "/esp32");
      delay(2000);
      ESP.restart();
    }

    if (currentMillis - lastSyncTime > 3000) {
      lastSyncTime = currentMillis;
      
      if (Firebase.getJSON(firebaseData, "/")) {
        FirebaseJson &json = firebaseData.jsonObject();
        FirebaseJsonData data;

        if (json.get(data, "fishMode")) {
          activeFish = data.intValue;
          if (activeFish <= 0) {
            acidPulseEndMs = 0;
            alkPulseEndMs = 0;
            lastPhLogicCheckMs = 0;
          }
        }
        if (json.get(data, "targetTempMin")) targetTempMin = data.doubleValue;
        if (json.get(data, "targetTempMax")) targetTempMax = data.doubleValue;
        if (json.get(data, "targetPhMin")) targetPhMin = data.doubleValue;
        if (json.get(data, "targetPhMax")) targetPhMax = data.doubleValue;
        if (json.get(data, "currentFishName")) currentFishName = data.stringValue;

        bool fishAuto = fishAutomationActive();

        if (json.get(data, "REL_ACID_PUMP") && !fishAuto) {
          digitalWrite(REL_ACID_PUMP, data.boolValue ? LOW : HIGH);
        }
        if (json.get(data, "REL_ALKALI_PUMP") && !fishAuto) {
          digitalWrite(REL_ALKALI_PUMP, data.boolValue ? LOW : HIGH);
        }
        if (json.get(data, "REL_COOLER_FAN") && !fishAuto) {
          digitalWrite(REL_COOLER_FAN, data.boolValue ? LOW : HIGH);
        }
        if (json.get(data, "REL_WATER_HEATER") && !fishAuto) {
          digitalWrite(REL_WATER_HEATER, data.boolValue ? LOW : HIGH);
        }
        if (json.get(data, "REL_AIR_PUMP")) digitalWrite(REL_AIR_PUMP, data.boolValue ? LOW : HIGH);
        if (json.get(data, "REL_WATER_FLOW")) digitalWrite(REL_WATER_FLOW, data.boolValue ? LOW : HIGH);
        if (json.get(data, "REL_RAIN_PUMP")) digitalWrite(REL_RAIN_PUMP, data.boolValue ? LOW : HIGH);
        if (json.get(data, "REL_LIGHT_CTRL")) digitalWrite(REL_LIGHT_CTRL, data.boolValue ? LOW : HIGH);

        if (json.get(data, "camera/pan")) panServo.write(data.intValue);
        if (json.get(data, "camera/tilt")) tiltServo.write(data.intValue);

        if (json.get(data, "esp32/ssid")) {
          String tempSsid = data.stringValue;
          if (json.get(data, "esp32/pass")) {
            if (tempSsid.length() > 1) {
              pendingSSID = tempSsid;
              pendingPASS = data.stringValue;
              newWifiPending = true;

              // Serial.println("Setting new net info ...............");
              // Serial.println(pendingSSID + pendingPASS);
            }
          }
        }
        if (fishAutomationActive()) {
          refreshPhPumpPins();
          applyTemperatureControl();
        }
        // Serial.println("Data Synced via JSON");
      } else {
        // Serial.print("Sync Error: ");
        // Serial.println(firebaseData.errorReason());
      }
    }
  }
  yield();
}