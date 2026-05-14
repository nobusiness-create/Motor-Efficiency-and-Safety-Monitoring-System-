#include <WiFi.h>
#include <HTTPClient.h>
//#include <OneWire.h>
//#include <DallasTemperature.h>
#include <math.h>

// ---- WiFi ----
const char* ssid = "Gopi";
const char* password = "waterghost";

// ---- ThingSpeak ----
const char* tsWriteURL = "http://api.thingspeak.com/update";
const char* tsReadURL = "http://api.thingspeak.com/channels/3383246/feeds/last.json";
const char* writeKey = "NGK5YHPB1WUZ137Y";
const char* readKey = "4RPSQVBBWGW2KYIW";

// ---- Pins ----
#define ACS712_PIN 34
#define VOLTAGE_PIN 35
#define RELAY_PIN 26
#define TEMP_PIN 4

// ---- DS18B20 ----
//OneWire oneWire(TEMP_PIN);
//DallasTemperature ds18b20(&oneWire);

// ---- ACS712 Settings ----
#define SENSITIVITY 0.100
#define ZERO_OFFSET 2.5
#define ADC_MAX 4095.0
#define VREF 3.3

// ---- Voltage Sensor ----
#define V_RATIO 11.0

// ---- Safety Limits ----
#define MAX_AMPS 10.0
#define MAX_TEMP 70.0
#define MIN_VOLTS 50.0
#define MAX_VOLTS 260.0

// ---- Sampling ----
#define SAMPLES 500
#define S_DELAY 100

// ---- Power Factor ----
#define PF 0.85

// ---- Globals ----
float voltage = 0, current = 0, power = 0,  energy = 0;
bool motorON = true;
unsigned long lastUpload = 0;
unsigned long lastRead = 0;
unsigned long startTime = 0;

// ==============================================
void setup() {
  Serial.begin(115200);

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

//  ds18b20.begin();
  startTime = millis();

  connectWiFi();
  Serial.println("System Ready!");
}

// ==============================================
void loop() {
  current = readCurrent();
  voltage = readVoltage();
  //temp = readTemp();
  power = voltage * current * PF;

  float hours = (millis() - startTime) / 3600000.0;
  energy = (power / 1000.0) * hours;

  checkSafety();
  printData();

  if (millis() - lastUpload > 20000) {
    uploadData();
    lastUpload = millis();
  }

  if (millis() - lastRead > 60000) {
    readFromThingSpeak();
    lastRead = millis();
  }

  delay(2000);
}

// ==============================================
float readCurrent() {
  float sum = 0;
  for (int i = 0; i < SAMPLES; i++) {
    float raw = analogRead(ACS712_PIN);
    float volt = (raw / ADC_MAX) * VREF;
    float I = (volt - ZERO_OFFSET) / SENSITIVITY;
    sum += I * I;
    delayMicroseconds(S_DELAY);
  }
  float rms = sqrt(sum / SAMPLES);
  return (rms < 0.05) ? 0.0 : rms;
}

// ==============================================
float readVoltage() {
  float sum = 0;
  for (int i = 0; i < SAMPLES; i++) {
    float raw = analogRead(VOLTAGE_PIN);
    float volt = (raw / ADC_MAX) * VREF;
    float V = volt * V_RATIO;
    sum += V * V;
    delayMicroseconds(S_DELAY);
  }
  return sqrt(sum / SAMPLES);
}

// ==============================================
/*float readTemp() {
  ds18b20.requestTemperatures();
  float t = ds18b20.getTempCByIndex(0);
  if (t == DEVICE_DISCONNECTED_C) {
    Serial.println("DS18B20 error! Check wiring & pull-up resistor.");
    return -1.0;
  }
  return t;
}
*/
// ==============================================
void checkSafety() {
  bool fault = false;
  String cause = "";

  if (current > MAX_AMPS)
    { fault = true; cause = "OVERCURRENT " + String(current,1) + "A"; }
 /* else if (temp > 0 && temp > MAX_TEMP)
    { fault = true; cause = "OVERHEAT " + String(temp,1) + "C"; }*/
  else if (voltage > 0 && voltage < MIN_VOLTS)
    { fault = true; cause = "LOW VOLTAGE " + String(voltage,1) + "V"; }
  else if (voltage > MAX_VOLTS)
    { fault = true; cause = "HIGH VOLTAGE " + String(voltage,1) + "V"; }

  if (fault) {
    digitalWrite(RELAY_PIN, HIGH);
    motorON = false;
    Serial.println("!!! FAULT: " + cause + " - Motor OFF !!!");
  } else if (!motorON) {
    digitalWrite(RELAY_PIN, LOW);
    motorON = true;
    Serial.println("Fault cleared - Motor ON");
  }
}

// ==============================================
void printData() {
  Serial.println("===========================");
  Serial.printf("Voltage    : %.1f V\n", voltage);
  Serial.printf("Current    : %.2f A\n", current);
  Serial.printf("Power      : %.1f W\n", power);
 // Serial.printf("Temperature: %.1f C\n", temp);
  Serial.printf("Energy     : %.4f kWh\n", energy);
  Serial.printf("Motor      : %s\n", motorON ? "RUNNING" : "STOPPED");
  Serial.println("===========================");
}

// ==============================================
void uploadData() {
  if (WiFi.status() != WL_CONNECTED) { connectWiFi(); return; }

  HTTPClient http;
  String url = String(tsWriteURL)
    + "?api_key=" + writeKey
    + "&field1=" + String(voltage, 1)
    + "&field2=" + String(current, 2)
    + "&field3=" + String(power, 1)
    //+ "&field4=" + String(temp, 1)
    + "&field5=" + String(energy, 4)
    + "&field6=" + String(motorON ? 1 : 0);

  http.begin(url);
  int code = http.GET();
  if (code > 0) {
  Serial.println("Upload OK");
  Serial.println(http.getString());
} else {
  Serial.println("Upload FAILED");
}
  http.end();
}

// ==============================================
void readFromThingSpeak() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  String url = String(tsReadURL) + "?api_key=" + readKey;
  http.begin(url);
  int code = http.GET();

  if (code == 200) {
    String payload = http.getString();
    Serial.println("--- ThingSpeak Last Entry ---");
    Serial.println(payload);
    Serial.println("-----------------------------");
  } else {
    Serial.println("ThingSpeak read failed - code: " + String(code));
  }
  http.end();
}

// ==============================================
void connectWiFi() {
  Serial.print("Connecting WiFi");
  WiFi.begin(ssid, password);
  int t = 0;
  while (WiFi.status() != WL_CONNECTED && t++ < 20) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED)
    Serial.println("\nConnected! IP: " + WiFi.localIP().toString());
  else
    Serial.println("\nWiFi failed - offline mode");
}