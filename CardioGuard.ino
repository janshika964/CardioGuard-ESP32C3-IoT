#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <MAX30105.h>

// ================= Pin Definitions for ESP32-C3 =================
#define I2C_SDA        8    // Default ESP32-C3 SDA
#define I2C_SCL        9    // Default ESP32-C3 SCL
#define ECG_OUTPUT_PIN 0    // Analog Input (ADC1_CH0)
#define ECG_LO_PLUS    4    // Digital Input for Lead-Off Detection +
#define ECG_LO_MINUS   5    // Digital Input for Lead-Off Detection -

// ================= Display Setup =================
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ================= Sensor Setup =================
MAX30105 particleSensor;

// ================= Wi-Fi Setup =================
const char* ssid     = "vivo Y19e";     // Change to your Wi-Fi name
const char* password = "12345678"; // Change to your Wi-Fi password

WebServer server(80);

// Global telemetry variables
int ecgValue = 0;
bool ecgLeadOff = false;
int heartRateBPM = 75;  // Simulated fallback/calculated value
int spo2Percentage = 98; // Simulated fallback/calculated value

// ================= HTML Web Page =================
const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>CardioGuard Live Dashboard</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <style>
        body { font-family: Arial, sans-serif; background: #0f172a; color: #fff; text-align: center; margin: 0; padding: 20px; }
        h1 { color: #ef4444; }
        .grid { display: flex; justify-content: center; gap: 20px; flex-wrap: wrap; margin-bottom: 20px; }
        .card { background: #1e293b; padding: 20px; border-radius: 10px; width: 180px; box-shadow: 0 4px 6px rgba(0,0,0,0.3); }
        .value { font-size: 2.5em; font-weight: bold; color: #38bdf8; }
        .chart-container { background: #1e293b; padding: 15px; border-radius: 10px; max-width: 800px; margin: 0 auto; }
    </style>
</head>
<body>
    <h1>🫀 CardioGuard IoT System</h1>
    <div class="grid">
        <div class="card"><h3>Heart Rate</h3><div class="value" id="bpm">--</div><span>BPM</span></div>
        <div class="card"><h3>Blood Oxygen</h3><div class="value" id="spo2">--</div><span>% SpO2</span></div>
        <div class="card"><h3>ECG Signal</h3><div class="value" id="ecg">--</div><span>ADC</span></div>
    </div>
    <div class="chart-container">
        <canvas id="ecgChart"></canvas>
    </div>

    <script>
        const ctx = document.getElementById('ecgChart').getContext('2d');
        const chart = new Chart(ctx, {
            type: 'line',
            data: {
                labels: Array.from({length: 30}, (_, i) => i),
                datasets: [{ label: 'ECG Real-Time Data', data: Array(30).fill(0), borderColor: '#ef4444', borderWidth: 2, fill: false, pointRadius: 0 }]
            },
            options: { animation: false, scales: { y: { min: 0, max: 4095 } } }
        });

        setInterval(() => {
            fetch('/data')
                .then(res => res.json())
                .then(data => {
                    document.getElementById('bpm').innerText = data.bpm;
                    document.getElementById('spo2').innerText = data.spo2;
                    document.getElementById('ecg').innerText = data.leadOff ? 'DISCONNECTED' : data.ecg;

                    if (!data.leadOff) {
                        chart.data.datasets[0].data.push(data.ecg);
                        chart.data.datasets[0].data.shift();
                        chart.update();
                    }
                });
        }, 200);
    </script>
</body>
</html>
)rawliteral";

// ================= API & Route Handlers =================
void handleRoot() {
    server.send(200, "text/html", HTML_PAGE);
}

void handleData() {
    String json = "{";
    json += "\"ecg\":" + String(ecgValue) + ",";
    json += "\"leadOff\":" + String(ecgLeadOff ? "true" : "false") + ",";
    json += "\"bpm\":" + String(heartRateBPM) + ",";
    json += "\"spo2\":" + String(spo2Percentage);
    json += "}";
    server.send(200, "application/json", json);
}

// ================= Setup =================
void setup() {
    Serial.begin(115200);

    // Initialize Pins
    pinMode(ECG_LO_PLUS, INPUT);
    pinMode(ECG_LO_MINUS, INPUT);
    analogReadResolution(12); // 0 to 4095 range for ESP32-C3

    // Initialize I2C with ESP32-C3 pins
    Wire.begin(I2C_SDA, I2C_SCL);

    // Initialize OLED
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println(F("OLED Initialization Failed"));
    } else {
        display.clearDisplay();
        display.setTextColor(WHITE);
        display.setTextSize(1);
        display.setCursor(0, 10);
        display.println("CardioGuard Initializing...");
        display.display();
    }

    // Initialize MAX30102
    if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
        Serial.println("MAX30102 not found. Check wiring!");
    } else {
        particleSensor.setup(); 
    }

    // Connect to Wi-Fi
    WiFi.begin(ssid, password);
    Serial.print("Connecting to Wi-Fi...");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWi-Fi Connected!");
    Serial.print("Server IP Address: ");
    Serial.println(WiFi.localIP());

    // Setup Web Routes
    server.on("/", handleRoot);
    server.on("/data", handleData);
    server.begin();

    // Display IP on OLED
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Wi-Fi Connected!");
    display.print("IP: ");
    display.println(WiFi.localIP());
    display.display();
    delay(2000);
}

// ================= Main Loop =================
void loop() {
    server.handleClient();

    // Check ECG Lead Off state
    if ((digitalRead(ECG_LO_PLUS) == 1) || (digitalRead(ECG_LO_MINUS) == 1)) {
        ecgLeadOff = true;
        ecgValue = 0;
    } else {
        ecgLeadOff = false;
        ecgValue = analogRead(ECG_OUTPUT_PIN);
    }

    // Print ECG value to Serial Plotter (Arduino IDE: Tools -> Serial Plotter)
    Serial.println(ecgValue);

    // Update OLED Screen
    static unsigned long lastUpdate = 0;
    if (millis() - lastUpdate > 500) {
        lastUpdate = millis();
        display.clearDisplay();
        display.setCursor(0, 0);
        display.setTextSize(1);
        display.println("CardioGuard Monitor");
        display.println("-------------------");
        
        if (ecgLeadOff) {
            display.println("ECG: Lead Disconnected!");
        } else {
            display.print("ECG ADC: "); display.println(ecgValue);
        }
        
        display.print("BPM: "); display.print(heartRateBPM);
        display.print(" | SpO2: "); display.print(spo2Percentage); display.println("%");
        display.display();
    }

    delay(20); // Smooth polling delay
}