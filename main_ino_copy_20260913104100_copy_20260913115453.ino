#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "MAX30105.h"
#include "heartRate.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include <WiFi.h>
#include <WebServer.h>

// --- ESP32-C3 PIN CONFIGURATION ---
#define I2C_SDA          8
#define I2C_SCL          9
#define ECG_ANALOG_PIN   0
#define ECG_LO_PLUS      2
#define ECG_LO_MINUS     3
#define TEMP_BUS_PIN     4

// OLED Display Setup
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Sensors Setup
MAX30105 particleSensor;
OneWire oneWire(TEMP_BUS_PIN);
DallasTemperature tempSensor(&oneWire);

// Web Server Setup
WebServer server(80);
const char* ssid = "DEV";           // Your Wi-Fi Name
const char* password = "123456789"; // Your Wi-Fi Password

// Global Data Variables
int bpm = 0;
float bodyTemp = 0.0;
int rawECG = 0;
int filteredECG = 0;
bool leadsOff = false;

// Heart Rate Calculation Variables
unsigned long lastBeat = 0;
float beatsPerMinute = 0;

// Moving Average Filter for ECG
const int FILTER_WINDOW = 5;
int ecgBuffer[FILTER_WINDOW] = {0};
int bufferIndex = 0;

int getFilteredECG(int rawVal) {
  ecgBuffer[bufferIndex] = rawVal;
  bufferIndex = (bufferIndex + 1) % FILTER_WINDOW;
  long sum = 0;
  for (int i = 0; i < FILTER_WINDOW; i++) {
    sum += ecgBuffer[i];
  }
  return sum / FILTER_WINDOW;
}

// HTML Dashboard
const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Full Health Monitoring System</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: Arial; text-align: center; background: #0f172a; color: #fff; margin:0; padding:20px;}
    .card { background: #1e293b; padding: 15px; margin: 10px auto; max-width: 320px; border-radius: 12px; box-shadow: 0 4px 6px rgba(0,0,0,0.3); }
    h1 { color: #ef4444; margin-bottom: 5px; }
    .val { font-size: 2.2em; color: #38bdf8; font-weight: bold; }
    .status { font-size: 1em; padding: 5px 10px; border-radius: 15px; display: inline-block; margin-top: 5px; }
    .ok { background: #15803d; color: #fff; }
    .warn { background: #b91c1c; color: #fff; }
  </style>
</head>
<body>
  <h1>Patient Health Vitals</h1>
  <p>ESP32-C3 Full IoT System</p>
  
  <div class="card">
    <h3>Heart Rate (MAX30102)</h3>
    <div class="val"><span id="bpm">--</span> <small style="font-size:0.5em">BPM</small></div>
  </div>

  <div class="card">
    <h3>Body Temperature</h3>
    <div class="val"><span id="temp">--</span> <small style="font-size:0.5em">&deg;C</small></div>
  </div>

  <div class="card">
    <h3>Live ECG Value</h3>
    <div class="val" id="ecg">--</div>
    <div id="status" class="status ok">ECG Electrodes Connected</div>
  </div>

  <script>
    setInterval(() => {
      fetch('/data').then(r => r.json()).then(d => {
        document.getElementById('bpm').innerText = d.bpm;
        document.getElementById('temp').innerText = d.temp;
        document.getElementById('ecg').innerText = d.ecg;
        const stat = document.getElementById('status');
        if (d.leadsOff) {
          stat.innerText = "ECG Pads Disconnected!";
          stat.className = "status warn";
        } else {
          stat.innerText = "ECG Electrodes Connected";
          stat.className = "status ok";
        }
      });
    }, 500);
  </script>
</body>
</html>
)rawliteral";

void handleRoot() {
  server.send(200, "text/html", HTML_PAGE);
}

void handleData() {
  String json = "{\"bpm\":" + String(bpm) + ",\"temp\":" + String(bodyTemp, 1) + ",\"ecg\":" + String(filteredECG) + ",\"leadsOff\":" + String(leadsOff ? "true" : "false") + "}";
  server.send(200, "application/json", json);
}

void setup() {
  Serial.begin(9600);
  delay(1000);
  Serial.println("\n--- Starting ESP32-C3 Health Monitor ---");

  // 1. Configure ADC Attenuation
  analogSetPinAttenuation(ECG_ANALOG_PIN, ADC_11db);

  // 2. Initialize Custom I2C Bus & Set Standard Clock Speed
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);
  delay(100);

  // 3. Configure Lead-off Detection Pins
  pinMode(ECG_LO_PLUS, INPUT);
  pinMode(ECG_LO_MINUS, INPUT);

  // 4. OLED Initialization
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("OLED 0x3C failed, trying 0x3D..."));
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3D)) {
      Serial.println(F("OLED not responding at 0x3C or 0x3D!"));
    } else {
      Serial.println(F("OLED Initialized at 0x3D!"));
    }
  } else {
    Serial.println(F("OLED Initialized at 0x3C!"));
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Connecting Wi-Fi...");
  display.display();

  // 5. Initialize MAX30102 Pulse Sensor
  if (!particleSensor.begin(Wire, I2C_SPEED_STANDARD)) {
    Serial.println("MAX30102 not found on I2C bus!");
  } else {
    particleSensor.setup();
    particleSensor.setPulseAmplitudeRed(0x0A);
    particleSensor.setPulseAmplitudeGreen(0);
    Serial.println("MAX30102 Initialized Successfully!");
  }

  // 6. Initialize DS18B20 Temp Sensor
  tempSensor.begin();
  tempSensor.setWaitForConversion(false);
  tempSensor.requestTemperatures();

  // 7. Connect to Wi-Fi
  Serial.print("Connecting to Wi-Fi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\n====================================");
  Serial.println("Wi-Fi Connected Successfully!");
  Serial.print("OPEN THIS URL IN YOUR BROWSER: http://");
  Serial.println(WiFi.localIP());
  Serial.println("====================================\n");

  // 8. Setup Web Server Handlers
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.begin();

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("All Systems Ready!");
  display.println("IP Address:");
  display.println(WiFi.localIP());
  display.display();
  delay(2000);
}

unsigned long lastTempRead = 0;
unsigned long lastDisplayUpdate = 0;

void loop() {
  server.handleClient();

  // 1. Continuous ECG Reading
  if ((digitalRead(ECG_LO_PLUS) == 1) || (digitalRead(ECG_LO_MINUS) == 1)) {
    leadsOff = true;
    rawECG = 0;
  } else {
    leadsOff = false;
    rawECG = analogRead(ECG_ANALOG_PIN);
  }
  filteredECG = getFilteredECG(rawECG);

  // 2. Continuous MAX30102 Pulse Reading
  long irValue = particleSensor.getIR();
  if (irValue > 50000) {
    if (checkForBeat(irValue)) {
      long delta = millis() - lastBeat;
      lastBeat = millis();
      beatsPerMinute = 60 / (delta / 1000.0);

      if (beatsPerMinute < 255 && beatsPerMinute > 20) {
        bpm = (int)beatsPerMinute;
      }
    }
  } else {
    bpm = 0;
  }

  // 3. Asynchronous Temperature Reading (Every 1.5 Seconds)
  if (millis() - lastTempRead > 1500) {
    bodyTemp = tempSensor.getTempCByIndex(0);
    tempSensor.requestTemperatures();
    lastTempRead = millis();
  }

  // 4. OLED Refresh (Every 300 ms)
  if (millis() - lastDisplayUpdate > 300) {
    lastDisplayUpdate = millis();

    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.println("VITAL SIGNS MONITOR");
    display.println("--------------------");

    display.print("BPM: ");
    if (bpm > 0) display.println(bpm); else display.println("No Finger");

    display.print("Temp: ");
    if (bodyTemp > -50 && bodyTemp < 100) {
      display.print(bodyTemp, 1);
      display.println(" C");
    } else {
      display.println("Error");
    }

    display.print("ECG: ");
    if (leadsOff) display.println("NO PADS"); else display.println(filteredECG);

    display.display();
  }
}