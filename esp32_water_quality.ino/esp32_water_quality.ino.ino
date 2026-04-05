#include <WiFi.h>
#include <WebSocketsClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <HX711.h>
#include <Preferences.h>
#include <ModbusMaster.h>
#include <WebServer.h>

// ========== WiFi Configuration ==========
const char* ssid = "HUAWEI-2.4G-SW5u";
const char* password = "nrgtnfmly";

// PC Server for WebSocket
const char* pc_server = "192.168.18.8";
const int pc_websocket_port = 8080; 

// ESP32 Web Server for Calibration
WebServer espServer(80);
WebSocketsClient webSocket;
Preferences preferences;

// ========== Pin Definitions ==========
// Relay Pins (NO Mode - Active LOW)
#define RELAY_SENSOR1 27
#define RELAY_SENSOR2 14
#define RELAY_SENSOR3 13
#define RELAY_WATER_PUMP 12
#define RELAY_SOLENOID 33

// Sensor Pins
#define DO_PIN 32
#define PH_PIN 35
#define TDS_PIN 34
#define TURBIDITY_PIN 36
#define ONE_WIRE_BUS 5
#define LOADCELL_DOUT 25
#define LOADCELL_SCK 26
#define TRIG_PIN 18
#define ECHO_PIN 19
#define WATER_DETECT_PIN 39

// RS485 for Ammonium
#define RS485_CONTROL 4
#define RS485_RX 16
#define RS485_TX 17
#define AMMONIUM_ADDR 1

ModbusMaster ammoniumSensor;
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature ds18b20(&oneWire);
HX711 scale;

// ========== Sensor Structure ==========
struct {
  float do_value = 0.0;
  float temperature = 0.0;
  float compensated_do = 0.0;
  float ph_value = 0.0;
  float tds_value = 0.0;
  float turbidity = 0.0;
  float ammonium = 0.0;
  float weight = 0.0;
  float water_level_cm = 0.0;
  bool water_detected = false;
  
  bool do_ok = false;
  bool ph_ok = false;
  bool tds_ok = false;
  bool turbidity_ok = false;
  bool ammonium_ok = false;
  bool temp_ok = false;
  bool loadcell_ok = false;
  bool waterlevel_ok = false;
} sensors;

// ========== DO Calibration Variables ==========
float do_zero_raw = 0.0;
float do_100_raw = 0.0;
bool do_calibrated = false;

enum DOCalState { DO_IDLE, DO_WAITING_ZERO, DO_WAITING_100 };
DOCalState do_cal_state = DO_IDLE;

// ========== pH Calibration Variables ==========
float ph_zero_raw = 0.0;
float ph_slope_raw = 0.0;
bool ph_calibrated = false;

enum PHCalState { PH_IDLE, PH_WAITING_6_86, PH_WAITING_4_01 };
PHCalState ph_cal_state = PH_IDLE;

// ========== TDS Calibration Variables ==========
float tds_15ppt_raw = 0.0;
float tds_25ppt_raw = 0.0;
bool tds_calibrated = false;

enum TDSCalState { TDS_IDLE, TDS_WAITING_15PPT, TDS_WAITING_25PPT };
TDSCalState tds_cal_state = TDS_IDLE;

// ========== Turbidity Calibration Variables ==========
float turbidity_zero_raw = 0.0;      // ADC reading at 0 NTU (distilled water)
float turbidity_1000_raw = 0.0;      // ADC reading at 1000 NTU standard
bool turbidity_calibrated = false;

enum TurbCalState { TURB_IDLE, TURB_WAITING_ZERO, TURB_WAITING_1000 };
TurbCalState turb_cal_state = TURB_IDLE;

// ========== Other Calibration Variables ==========
float ph_slope = 1.0, ph_intercept = 0.0;
float tds_slope = 1.0, tds_intercept = 0.0;
float turbidity_slope = -1125.0, turbidity_intercept = 5000.0;
float cal_factor = -7050.0;
float container_height = 100.0;
float empty_dist = 0.0, full_dist = 0.0;
bool empty_cal = false, full_cal = false;

// ========== Solenoid Setting ==========
int solenoid_time = 10;

// ========== Test State Machine ==========
enum TestState {
  IDLE,
  SENSOR_RELAY_ON,
  PUMP_ON,
  WAIT_WATER,
  STABILIZE,
  RECORD,
  SENSOR_RELAY_OFF,
  SOLENOID_ON,
  DONE
};
TestState state = IDLE;
unsigned long state_start = 0;
int current_test_id = 0;

// ========== ADC Settings ==========
const float ADC_REF = 3.9;
const float ADC_MAX = 4095.0;
const float DIV_RATIO = 2.0;
const float PH_NEUTRAL = 2.5;
const float PH_SLOPE_VAL = 0.18;

// ========== Timing ==========
unsigned long last_data_send = 0;
unsigned long last_temp_read = 0;
unsigned long last_ammonium_read = 0;
unsigned long last_serial_print = 0;
bool temp_waiting = false;

// ========== Helper Functions ==========
void printAction(const char* action) {
  Serial.printf("[ACTION] %s at %lu ms\n", action, millis());
}

void printSensorReading() {
  Serial.println("\n========== SENSOR READINGS ==========");
  
  if (sensors.ammonium_ok) Serial.printf("Ammonia: %.2f mg/L\n", sensors.ammonium);
  else Serial.printf("Ammonia: -- mg/L\n");
  
  if (sensors.do_ok) Serial.printf("DO: %.2f mg/L\n", sensors.do_value);
  else Serial.printf("DO: -- mg/L\n");
  
  if (sensors.ph_ok) Serial.printf("pH: %.2f\n", sensors.ph_value);
  else Serial.printf("pH: --\n");
  
  if (sensors.temp_ok) Serial.printf("Temperature: %.1f C\n", sensors.temperature);
  else Serial.printf("Temperature: -- C\n");
  
  if (sensors.tds_ok) Serial.printf("TDS: %.0f ppm\n", sensors.tds_value);
  else Serial.printf("TDS: -- ppm\n");
  
  if (sensors.turbidity_ok) Serial.printf("Turbidity: %.0f NTU\n", sensors.turbidity);
  else Serial.printf("Turbidity: -- NTU\n");
  
  if (sensors.loadcell_ok) Serial.printf("Weight: %.1f g\n", sensors.weight);
  else Serial.printf("Weight: -- g\n");
  
  if (sensors.waterlevel_ok) Serial.printf("Water Level: %.1f cm\n", sensors.water_level_cm);
  else Serial.printf("Water Level: -- cm\n");
  
  if (sensors.water_detected) Serial.printf("Water Detected: YES\n");
  else Serial.printf("Water Detected: NO\n");
  
  if (do_calibrated) Serial.printf("DO Cal: Zero ADC=%.2f, 100%% ADC=%.2f\n", do_zero_raw, do_100_raw);
  if (ph_calibrated) Serial.printf("pH Cal: 6.86 ADC=%.2f, 4.01 ADC=%.2f\n", ph_zero_raw, ph_slope_raw);
  if (tds_calibrated) Serial.printf("TDS Cal: 15ppt ADC=%.2f, 25ppt ADC=%.2f\n", tds_15ppt_raw, tds_25ppt_raw);
  if (turbidity_calibrated) Serial.printf("Turbidity Cal: 0 NTU ADC=%.2f, 1000 NTU ADC=%.2f\n", turbidity_zero_raw, turbidity_1000_raw);
  
  Serial.println("=====================================\n");
}

void setRelay(int relay, bool on) {
  int pin;
  switch(relay) {
    case 1: pin = RELAY_SENSOR1; break;
    case 2: pin = RELAY_SENSOR2; break;
    case 3: pin = RELAY_SENSOR3; break;
    case 4: pin = RELAY_WATER_PUMP; break;
    case 5: pin = RELAY_SOLENOID; break;
    default: return;
  }
  digitalWrite(pin, on ? LOW : HIGH);
  char msg[50];
  sprintf(msg, "Relay %d: %s", relay, on ? "ON" : "OFF");
  printAction(msg);
}

void allSensorRelays(bool on) {
  setRelay(1, on);
  setRelay(2, on);
  setRelay(3, on);
}

// ========== Temperature Compensation Functions ==========
float getSaturatedDO(float tempC) {
  return 14.6 - (0.394 * tempC) + (0.00714 * tempC * tempC);
}

float getPHVoltageAtTemp(float ph_value, float tempC) {
  float kelvin = 273.15 + tempC;
  float slope = 0.1984 * kelvin / 1000.0;
  float voltage = PH_NEUTRAL + ((7.0 - ph_value) * slope);
  return voltage;
}

float adcToPH(int adc, float tempC) {
  float voltage = (adc * ADC_REF / ADC_MAX) * DIV_RATIO;
  
  if (!ph_calibrated || ph_slope_raw <= ph_zero_raw) {
    return (PH_NEUTRAL - voltage) / PH_SLOPE_VAL;
  }
  
  float kelvin = 273.15 + tempC;
  float current_slope = 0.1984 * kelvin / 1000.0;
  float intercept = PH_NEUTRAL + (current_slope * 7.0);
  float ph = (intercept - voltage) / current_slope;
  return constrain(ph, 0.0, 14.0);
}

float compensateTDS(float raw_tds, float tempC) {
  if (!sensors.temp_ok) return raw_tds;
  float temp_coefficient = 1.0 + (0.02 * (tempC - 25.0));
  return raw_tds / temp_coefficient;
}

float adcToTDS(int adc, float tempC) {
  if (!tds_calibrated || tds_25ppt_raw <= tds_15ppt_raw) {
    float voltage = (adc * ADC_REF / ADC_MAX) * DIV_RATIO;
    float raw_tds = (tds_slope * voltage) + tds_intercept;
    return compensateTDS(raw_tds, tempC);
  }
  
  float tds_raw = 15000.0 + (adc - tds_15ppt_raw) * (25000.0 - 15000.0) / (tds_25ppt_raw - tds_15ppt_raw);
  return compensateTDS(constrain(tds_raw, 0.0, 50000.0), tempC);
}

// ========== DO Calibration Functions ==========
void startDOCalibration() {
  do_cal_state = DO_WAITING_ZERO;
  Serial.println("\n=== DO CALIBRATION STARTED ===");
  Serial.println("Place sensor in ZERO OXYGEN solution (boiled water)");
}

void setDOZeroPoint() {
  if (do_cal_state != DO_WAITING_ZERO) {
    Serial.println("Error: Not in zero calibration mode.");
    return;
  }
  
  float sum = 0;
  for (int i = 0; i < 10; i++) {
    sum += analogRead(DO_PIN);
    delay(50);
  }
  do_zero_raw = sum / 10.0;
  
  Serial.printf("Zero point saved: ADC = %.2f\n", do_zero_raw);
  Serial.println("Now place sensor in OPEN AIR for 100% point");
  do_cal_state = DO_WAITING_100;
}

void setDO100Point() {
  if (do_cal_state != DO_WAITING_100) {
    Serial.println("Error: Not in 100% calibration mode.");
    return;
  }
  
  float sum = 0;
  for (int i = 0; i < 10; i++) {
    sum += analogRead(DO_PIN);
    delay(50);
  }
  do_100_raw = sum / 10.0;
  
  do_calibrated = true;
  preferences.putFloat("do_zero_raw", do_zero_raw);
  preferences.putFloat("do_100_raw", do_100_raw);
  preferences.putBool("do_calibrated", true);
  
  Serial.printf("100%% point saved: ADC = %.2f\n", do_100_raw);
  Serial.println("=== DO CALIBRATION COMPLETE ===");
  do_cal_state = DO_IDLE;
}

void resetDOCalibration() {
  do_calibrated = false;
  do_zero_raw = 0;
  do_100_raw = 0;
  preferences.putBool("do_calibrated", false);
  Serial.println("DO calibration reset");
}

// ========== pH Calibration Functions ==========
void startPHCalibration() {
  ph_cal_state = PH_WAITING_6_86;
  Serial.println("\n=== pH CALIBRATION STARTED ===");
  Serial.println("Place sensor in pH 6.86 buffer solution");
}

void setPH_6_86_Point() {
  if (ph_cal_state != PH_WAITING_6_86) {
    Serial.println("Error: Not in pH 6.86 calibration mode.");
    return;
  }
  
  float sum = 0;
  for (int i = 0; i < 10; i++) {
    sum += analogRead(PH_PIN);
    delay(50);
  }
  ph_zero_raw = sum / 10.0;
  
  Serial.printf("pH 6.86 point saved: ADC = %.2f\n", ph_zero_raw);
  Serial.println("Now place sensor in pH 4.01 buffer solution");
  ph_cal_state = PH_WAITING_4_01;
}

void setPH_4_01_Point() {
  if (ph_cal_state != PH_WAITING_4_01) {
    Serial.println("Error: Not in pH 4.01 calibration mode.");
    return;
  }
  
  float sum = 0;
  for (int i = 0; i < 10; i++) {
    sum += analogRead(PH_PIN);
    delay(50);
  }
  ph_slope_raw = sum / 10.0;
  
  ph_calibrated = true;
  preferences.putFloat("ph_zero_raw", ph_zero_raw);
  preferences.putFloat("ph_slope_raw", ph_slope_raw);
  preferences.putBool("ph_calibrated", true);
  
  Serial.printf("pH 4.01 point saved: ADC = %.2f\n", ph_slope_raw);
  Serial.println("=== pH CALIBRATION COMPLETE ===");
  ph_cal_state = PH_IDLE;
}

void resetPHCalibration() {
  ph_calibrated = false;
  ph_zero_raw = 0;
  ph_slope_raw = 0;
  preferences.putBool("ph_calibrated", false);
  Serial.println("pH calibration reset");
}

// ========== TDS Calibration Functions ==========
void startTDSCalibration() {
  tds_cal_state = TDS_WAITING_15PPT;
  Serial.println("\n=== TDS CALIBRATION STARTED ===");
  Serial.println("Place sensor in 15 ppt (15000 ppm) solution");
}

void setTDS_15PPT_Point() {
  if (tds_cal_state != TDS_WAITING_15PPT) {
    Serial.println("Error: Not in TDS 15ppt calibration mode.");
    return;
  }
  
  float sum = 0;
  for (int i = 0; i < 10; i++) {
    sum += analogRead(TDS_PIN);
    delay(50);
  }
  tds_15ppt_raw = sum / 10.0;
  
  Serial.printf("TDS 15ppt point saved: ADC = %.2f\n", tds_15ppt_raw);
  Serial.println("Now place sensor in 25 ppt (25000 ppm) solution");
  tds_cal_state = TDS_WAITING_25PPT;
}

void setTDS_25PPT_Point() {
  if (tds_cal_state != TDS_WAITING_25PPT) {
    Serial.println("Error: Not in TDS 25ppt calibration mode.");
    return;
  }
  
  float sum = 0;
  for (int i = 0; i < 10; i++) {
    sum += analogRead(TDS_PIN);
    delay(50);
  }
  tds_25ppt_raw = sum / 10.0;
  
  tds_calibrated = true;
  preferences.putFloat("tds_15ppt_raw", tds_15ppt_raw);
  preferences.putFloat("tds_25ppt_raw", tds_25ppt_raw);
  preferences.putBool("tds_calibrated", true);
  
  Serial.printf("TDS 25ppt point saved: ADC = %.2f\n", tds_25ppt_raw);
  Serial.println("=== TDS CALIBRATION COMPLETE ===");
  tds_cal_state = TDS_IDLE;
}

void resetTDSCalibration() {
  tds_calibrated = false;
  tds_15ppt_raw = 0;
  tds_25ppt_raw = 0;
  preferences.putBool("tds_calibrated", false);
  Serial.println("TDS calibration reset");
}

// ========== Turbidity Calibration Functions ==========
void startTurbidityCalibration() {
  turb_cal_state = TURB_WAITING_ZERO;
  Serial.println("\n=== TURBIDITY CALIBRATION STARTED ===");
  Serial.println("Place sensor in DISTILLED WATER (0 NTU)");
}

void setTurbidityZeroPoint() {
  if (turb_cal_state != TURB_WAITING_ZERO) {
    Serial.println("Error: Not in zero calibration mode.");
    return;
  }
  
  float sum = 0;
  for (int i = 0; i < 10; i++) {
    sum += analogRead(TURBIDITY_PIN);
    delay(50);
  }
  turbidity_zero_raw = sum / 10.0;
  
  Serial.printf("0 NTU point saved: ADC = %.2f\n", turbidity_zero_raw);
  Serial.println("Now place sensor in 1000 NTU standard solution");
  turb_cal_state = TURB_WAITING_1000;
}

void setTurbidity1000Point() {
  if (turb_cal_state != TURB_WAITING_1000) {
    Serial.println("Error: Not in 1000 NTU calibration mode.");
    return;
  }
  
  float sum = 0;
  for (int i = 0; i < 10; i++) {
    sum += analogRead(TURBIDITY_PIN);
    delay(50);
  }
  turbidity_1000_raw = sum / 10.0;
  
  turbidity_calibrated = true;
  preferences.putFloat("turbidity_zero_raw", turbidity_zero_raw);
  preferences.putFloat("turbidity_1000_raw", turbidity_1000_raw);
  preferences.putBool("turbidity_calibrated", true);
  
  Serial.printf("1000 NTU point saved: ADC = %.2f\n", turbidity_1000_raw);
  Serial.println("=== TURBIDITY CALIBRATION COMPLETE ===");
  turb_cal_state = TURB_IDLE;
}

void resetTurbidityCalibration() {
  turbidity_calibrated = false;
  turbidity_zero_raw = 0;
  turbidity_1000_raw = 0;
  preferences.putBool("turbidity_calibrated", false);
  Serial.println("Turbidity calibration reset");
}

// ========== Load Cell Calibration Functions ==========
void saveLoadCellFactor() {
  preferences.putFloat("weight_factor", cal_factor);
  scale.set_scale(cal_factor);
  Serial.printf("[LOADCELL] Factor saved: %.2f\n", cal_factor);
}

void updateLoadCellFactor(float new_factor) {
  cal_factor = new_factor;
  saveLoadCellFactor();
  Serial.printf("[LOADCELL] Factor updated to: %.2f\n", cal_factor);
}

void adjustLoadCellFactor(int delta) {
  cal_factor += delta;
  saveLoadCellFactor();
  Serial.printf("[LOADCELL] Factor adjusted by %d, new factor: %.2f\n", delta, cal_factor);
}

// ========== Sensor Reading Functions ==========
float readDO() {
  if (!sensors.do_ok) return 0.0;
  int adc = analogRead(DO_PIN);
  
  if (do_calibrated && sensors.temp_ok && do_100_raw > do_zero_raw) {
    float saturated = getSaturatedDO(sensors.temperature);
    float mgL = (adc - do_zero_raw) / (do_100_raw - do_zero_raw) * saturated;
    sensors.compensated_do = constrain(mgL, 0.0, 20.0);
    return sensors.compensated_do;
  }
  
  float voltage = (adc * ADC_REF / ADC_MAX) * DIV_RATIO;
  sensors.compensated_do = (voltage / 5.0) * 20.0;
  return sensors.compensated_do;
}

float readPH() {
  if (!sensors.ph_ok) return 0.0;
  int adc = analogRead(PH_PIN);
  if (adc <= 10 || adc >= 4090) { 
    sensors.ph_ok = false; 
    return 0.0; 
  }
  
  if (sensors.temp_ok) {
    return adcToPH(adc, sensors.temperature);
  }
  
  float voltage = (adc * ADC_REF / ADC_MAX) * DIV_RATIO;
  return (PH_NEUTRAL - voltage) / PH_SLOPE_VAL;
}

float readTDS() {
  if (!sensors.tds_ok) return 0.0;
  int adc = analogRead(TDS_PIN);
  
  if (sensors.temp_ok) {
    return adcToTDS(adc, sensors.temperature);
  }
  
  float voltage = (adc * ADC_REF / ADC_MAX) * DIV_RATIO;
  return (tds_slope * voltage) + tds_intercept;
}

float readTurbidity() {
  if (!sensors.turbidity_ok) return 0.0;
  int adc = analogRead(TURBIDITY_PIN);
  
  // Use calibrated two-point method if available
  if (turbidity_calibrated && turbidity_1000_raw > turbidity_zero_raw) {
    float ntu = (adc - turbidity_zero_raw) / (turbidity_1000_raw - turbidity_zero_raw) * 1000.0;
    return constrain(ntu, 0.0, 4000.0);
  }
  
  // Fallback to original formula (uncalibrated)
  float voltage = (adc * ADC_REF / ADC_MAX) * DIV_RATIO;
  float raw = turbidity_intercept + (turbidity_slope * voltage);
  return constrain(raw, 0.0, 4000.0);
}

float readWeight() {
  if (!sensors.loadcell_ok) return 0.0;
  if (scale.is_ready()) return scale.get_units(3);
  return sensors.weight;
}

float readUltrasonic() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  if (duration == 0) return 0.0;
  float dist = duration * 0.0343 / 2;
  return (dist > 0 && dist < 500) ? dist : 0.0;
}

float getWaterPercent() {
  if (!empty_cal || !full_cal) return 0.0;
  float depth = empty_dist - sensors.water_level_cm;
  float total = empty_dist - full_dist;
  if (total <= 0) return 0.0;
  return constrain((depth / total) * 100.0, 0.0, 100.0);
}

void readAmmonium() {
  uint8_t result = ammoniumSensor.readInputRegisters(0x0000, 1);
  if (result == ammoniumSensor.ku8MBSuccess) {
    sensors.ammonium = ammoniumSensor.getResponseBuffer(0) / 10.0;
    sensors.ammonium_ok = true;
  } else {
    sensors.ammonium_ok = false;
  }
}

void checkConnections() {
  int do_adc = analogRead(DO_PIN);
  sensors.do_ok = (do_adc > 10 && do_adc < 4090);
  
  int ph_adc = analogRead(PH_PIN);
  sensors.ph_ok = (ph_adc > 10 && ph_adc < 4090);
  
  int tds_adc = analogRead(TDS_PIN);
  sensors.tds_ok = (tds_adc > 10 && tds_adc < 4090);
  
  int turb_adc = analogRead(TURBIDITY_PIN);
  sensors.turbidity_ok = (turb_adc > 10 && turb_adc < 4090);
  
  sensors.temp_ok = (ds18b20.getDeviceCount() > 0);
  sensors.loadcell_ok = scale.is_ready();
  
  float test = readUltrasonic();
  sensors.waterlevel_ok = (test > 0 && test < 500);
  sensors.water_detected = digitalRead(WATER_DETECT_PIN) == HIGH;
  
  Serial.println("[STATUS] Sensor Connection Check:");
  Serial.printf("  DO Sensor: %s\n", sensors.do_ok ? "Connected" : "Not Connected");
  Serial.printf("  pH Sensor: %s\n", sensors.ph_ok ? "Connected" : "Not Connected");
  Serial.printf("  TDS Sensor: %s\n", sensors.tds_ok ? "Connected" : "Not Connected");
  Serial.printf("  Turbidity Sensor: %s\n", sensors.turbidity_ok ? "Connected" : "Not Connected");
  Serial.printf("  Temperature: %s\n", sensors.temp_ok ? "Connected" : "Not Connected");
}

// ========== Send JSON Data to PC ==========
void sendSensorData() {
  String json = "{";
  json += "\"do\":" + String(sensors.do_ok ? sensors.do_value : -1, 2) + ",";
  json += "\"temperature\":" + String(sensors.temp_ok ? sensors.temperature : -1, 2) + ",";
  json += "\"compensated_do\":" + String(sensors.do_ok ? sensors.compensated_do : -1, 2) + ",";
  json += "\"ph\":" + String(sensors.ph_ok ? sensors.ph_value : -1, 2) + ",";
  json += "\"tds\":" + String(sensors.tds_ok ? sensors.tds_value : -1, 0) + ",";
  json += "\"turbidity\":" + String(sensors.turbidity_ok ? sensors.turbidity : -1, 0) + ",";
  json += "\"ammonium\":" + String(sensors.ammonium_ok ? sensors.ammonium : -1, 2) + ",";
  json += "\"weight\":" + String(sensors.loadcell_ok ? sensors.weight : -1, 2) + ",";
  json += "\"water_level\":" + String(sensors.waterlevel_ok ? sensors.water_level_cm : -1, 1) + ",";
  json += "\"water_percent\":" + String(getWaterPercent(), 1) + ",";
  json += "\"water_detected\":" + String(sensors.water_detected ? "true" : "false") + ",";
  json += "\"test_state\":" + String(state);
  json += "}";
  webSocket.sendTXT(json);
}

// ========== Test State Machine ==========
void startTest() {
  if (state != IDLE) {
    Serial.println("[WARN] Test already running!");
    return;
  }
  printAction("TEST STARTED");
  state = SENSOR_RELAY_ON;
  state_start = millis();
}

void updateTestMachine() {
  if (state == IDLE) return;
  
  switch(state) {
    case SENSOR_RELAY_ON:
      if (millis() - state_start > 500) {
        allSensorRelays(true);
        state = PUMP_ON;
        state_start = millis();
      }
      break;
      
    case PUMP_ON:
      if (millis() - state_start > 500) {
        setRelay(4, true);
        state = WAIT_WATER;
        state_start = millis();
      }
      break;
      
    case WAIT_WATER:
      if (sensors.water_detected) {
        setRelay(4, false);
        state = STABILIZE;
        state_start = millis();
        Serial.println("[TEST] Stabilizing for 10 minutes...");
      } else if (millis() - state_start > 30000) {
        Serial.println("[ERROR] Timeout: No water detected!");
        allSensorRelays(false);
        setRelay(4, false);
        state = IDLE;
      }
      break;
      
    case STABILIZE:
      if (millis() - state_start >= 600000) {
        state = RECORD;
        state_start = millis();
      }
      break;
      
    case RECORD:
      sendSensorData();
      webSocket.sendTXT("RECORD_READING");
      state = SENSOR_RELAY_OFF;
      state_start = millis();
      break;
      
    case SENSOR_RELAY_OFF:
      if (millis() - state_start > 500) {
        allSensorRelays(false);
        state = SOLENOID_ON;
        state_start = millis();
      }
      break;
      
    case SOLENOID_ON:
      setRelay(5, true);
      state = DONE;
      state_start = millis();
      Serial.printf("[TEST] Solenoid ON for %d seconds\n", solenoid_time);
      break;
      
    case DONE:
      if (millis() - state_start >= (unsigned long)solenoid_time * 1000) {
        setRelay(5, false);
        state = IDLE;
        webSocket.sendTXT("TEST_COMPLETE");
        Serial.println("[TEST] === TEST COMPLETE ===");
      }
      break;
  }
}

// ========== Update All Sensors ==========
void updateSensors() {
  sensors.do_value = readDO();
  sensors.ph_value = readPH();
  sensors.tds_value = readTDS();
  sensors.turbidity = readTurbidity();
  sensors.weight = readWeight();
  
  if (sensors.waterlevel_ok) {
    sensors.water_level_cm = readUltrasonic();
  }
  sensors.water_detected = digitalRead(WATER_DETECT_PIN) == HIGH;
  
  if (sensors.temp_ok) {
    if (!temp_waiting || (millis() - last_temp_read > 750)) {
      sensors.temperature = ds18b20.getTempCByIndex(0);
      if (sensors.temperature != DEVICE_DISCONNECTED_C && sensors.temperature != 85.0) {
        if (!do_calibrated && sensors.do_ok) {
          sensors.compensated_do = sensors.do_value;
        }
      }
      ds18b20.requestTemperatures();
      last_temp_read = millis();
      temp_waiting = true;
    }
  }
  
  if (millis() - last_ammonium_read > 3000) {
    readAmmonium();
    last_ammonium_read = millis();
  }
}

// ========== WebSocket Events ==========
void wsEvent(WStype_t type, uint8_t* payload, size_t len) {
  switch(type) {
    case WStype_CONNECTED:
      Serial.println("[WEBSOCKET] Connected to PC server");
      break;
    case WStype_DISCONNECTED:
      Serial.println("[WEBSOCKET] Disconnected from PC server");
      break;
    case WStype_TEXT:
      String msg = String((char*)payload);
      Serial.printf("[WEBSOCKET] Received: %s\n", msg.c_str());
      if (msg == "START_TEST") {
        startTest();
      } else if (msg == "RECORD_READING") {
        webSocket.sendTXT("RECORD_CONFIRM");
      }
      break;
  }
}

// ========== ESP32 Web Server ==========
void sendHTML() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>ESP32 Calibration</title>
    <style>
        body { font-family: Arial; margin: 20px; background: #f0f0f0; }
        .container { max-width: 700px; margin: auto; }
        .card { background: white; padding: 20px; margin: 10px 0; border-radius: 10px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); }
        button { background: #007bff; color: white; border: none; padding: 10px 20px; margin: 5px; border-radius: 5px; cursor: pointer; font-size: 14px; }
        button:hover { background: #0056b3; }
        .btn-success { background: #28a745; }
        .btn-success:hover { background: #218838; }
        .btn-warning { background: #ffc107; color: #333; }
        .btn-danger { background: #dc3545; }
        .btn-secondary { background: #6c757d; }
        .cal-step { background: #e9ecef; padding: 15px; border-radius: 5px; margin-top: 10px; text-align: center; }
        .reading { font-size: 20px; font-weight: bold; text-align: center; margin: 10px 0; }
        .connected { color: green; }
        .disconnected { color: red; }
        table { width: 100%; }
        td { padding: 5px; }
        .factor-control { display: flex; flex-wrap: wrap; justify-content: center; gap: 5px; margin: 10px 0; }
        .factor-input { display: flex; gap: 10px; margin-top: 10px; align-items: center; justify-content: center; }
        .factor-input input { padding: 8px; font-size: 16px; width: 120px; text-align: center; border: 1px solid #ccc; border-radius: 5px; }
        .factor-input button { margin: 0; }
        .factor-buttons { display: flex; flex-wrap: wrap; justify-content: center; gap: 8px; margin: 15px 0; }
        .factor-btn { background: #17a2b8; min-width: 70px; }
        .factor-btn:hover { background: #138496; }
    </style>
</head>
<body>
<div class="container">
    <h2>ESP32 Calibration Panel</h2>
    
    <div class="card">
        <h3>📡 Sensor Status</h3>
        <div id="sensorStatus"></div>
    </div>
    
    <div class="card">
        <h3>🌊 DO Sensor</h3>
        <div class="reading">DO: <span id="doCurrent">--</span> mg/L | Temp: <span id="tempCurrent">--</span>°C</div>
        <div id="doCalStep"></div>
        <div id="doMsg"></div>
    </div>
    
    <div class="card">
        <h3>🧪 pH Sensor</h3>
        <div class="reading">pH: <span id="phCurrent">--</span></div>
        <div id="phCalStep"></div>
        <div id="phMsg"></div>
    </div>
    
    <div class="card">
        <h3>💧 TDS Sensor</h3>
        <div class="reading">TDS: <span id="tdsCurrent">--</span> ppm</div>
        <div id="tdsCalStep"></div>
        <div id="tdsMsg"></div>
    </div>
    
    <div class="card">
        <h3>🌊 Turbidity Sensor</h3>
        <div class="reading">Turbidity: <span id="turbCurrent">--</span> NTU</div>
        <div id="turbCalStep"></div>
        <div id="turbMsg"></div>
    </div>
    
    <div class="card">
        <h3>⚖️ Load Cell (HX711) Scale</h3>
        <div class="reading">Weight: <span id="weightCurrent">--</span> g</div>
        <div style="text-align: center; margin: 10px 0;">
            <button class="btn-secondary" onclick="sendCommand('/cal/weight/tare')">⚖️ Tare (Zero)</button>
        </div>
        <div class="factor-buttons">
            <button class="factor-btn" onclick="adjustFactor(-1000)">-1000</button>
            <button class="factor-btn" onclick="adjustFactor(-100)">-100</button>
            <button class="factor-btn" onclick="adjustFactor(-10)">-10</button>
            <button class="factor-btn" onclick="adjustFactor(-1)">-1</button>
            <button class="factor-btn" onclick="adjustFactor(1)">+1</button>
            <button class="factor-btn" onclick="adjustFactor(10)">+10</button>
            <button class="factor-btn" onclick="adjustFactor(100)">+100</button>
            <button class="factor-btn" onclick="adjustFactor(1000)">+1000</button>
        </div>
        <div class="factor-input">
            <input type="number" id="factorInput" step="any" placeholder="Enter calibration factor">
            <button class="btn-success" onclick="setFactor()">Set</button>
        </div>
        <div style="text-align: center; margin-top: 10px; font-size: 14px; color: #666;">
            Current Factor: <strong id="currentFactor">--</strong>
        </div>
        <div style="text-align: center; margin-top: 5px; font-size: 12px; color: #999;">
            💡 Tip: Place a known weight, then adjust factor until weight reading matches
        </div>
    </div>
    
    <div class="card">
        <h3>📏 Water Level</h3>
        <p>Current: <span id="wlCurrent">--</span> cm</p>
        <button onclick="sendCommand('/cal/water/empty')">Set Empty</button>
        <button onclick="sendCommand('/cal/water/full')">Set Full</button>
    </div>
</div>

<script>
let lastDoState=-1, lastPhState=-1, lastTdsState=-1, lastTurbState=-1;

async function sendCommand(url) {
    let resp = await fetch(url);
    let data = await resp.json();
    if(data.message) console.log(data.message);
    updateAll();
}

async function adjustFactor(delta) {
    let resp = await fetch('/cal/weight/adjust?delta=' + delta);
    let data = await resp.json();
    if(data.message) console.log(data.message);
    if(data.factor !== undefined) {
        document.getElementById('currentFactor').innerHTML = data.factor;
        document.getElementById('factorInput').value = data.factor;
    }
    updateAll();
}

async function setFactor() {
    let input = document.getElementById('factorInput');
    let factor = parseFloat(input.value);
    if(isNaN(factor)) {
        alert('Please enter a valid number');
        return;
    }
    let resp = await fetch('/cal/weight/set?factor=' + factor);
    let data = await resp.json();
    if(data.message) console.log(data.message);
    if(data.factor !== undefined) {
        document.getElementById('currentFactor').innerHTML = data.factor;
        document.getElementById('factorInput').value = data.factor;
    }
    updateAll();
}

async function updateAll() {
    try {
        let resp = await fetch('/sensor/values');
        let data = await resp.json();
        
        document.getElementById('doCurrent').innerHTML = data.do;
        document.getElementById('tempCurrent').innerHTML = data.temp;
        document.getElementById('phCurrent').innerHTML = data.ph;
        document.getElementById('tdsCurrent').innerHTML = data.tds;
        document.getElementById('turbCurrent').innerHTML = data.turbidity;
        document.getElementById('weightCurrent').innerHTML = data.weight;
        document.getElementById('wlCurrent').innerHTML = data.water_level;
        document.getElementById('currentFactor').innerHTML = data.cal_factor;
        document.getElementById('factorInput').value = data.cal_factor;
        
        let statusHtml = '<table>';
        statusHtml += '<tr><td>DO:</td><td class="' + (data.do==='--'?'disconnected':'connected') + '">' + (data.do==='--'?'❌':'✅') + '</td><td>' + (data.do_calibrated==='true'?'Calibrated':'Uncalibrated') + '</td></tr>';
        statusHtml += '<tr><td>pH:</td><td class="' + (data.ph==='--'?'disconnected':'connected') + '">' + (data.ph==='--'?'❌':'✅') + '</td><td>' + (data.ph_calibrated==='true'?'Calibrated':'Uncalibrated') + '</td></tr>';
        statusHtml += '<tr><td>TDS:</td><td class="' + (data.tds==='--'?'disconnected':'connected') + '">' + (data.tds==='--'?'❌':'✅') + '</td><td>' + (data.tds_calibrated==='true'?'Calibrated':'Uncalibrated') + '</td></tr>';
        statusHtml += '<tr><td>Turbidity:</td><td class="' + (data.turbidity==='--'?'disconnected':'connected') + '">' + (data.turbidity==='--'?'❌':'✅') + '</td><td>' + (data.turbidity_calibrated==='true'?'Calibrated':'Uncalibrated') + '</td></tr>';
        statusHtml += '<tr><td>Load Cell:</td><td class="' + (data.weight==='--'?'disconnected':'connected') + '">' + (data.weight==='--'?'❌':'✅') + '</td><td>' + (data.cal_factor !== '--' ? 'Factor: ' + data.cal_factor : 'Not set') + '</td></tr>';
        statusHtml += '</table>';
        document.getElementById('sensorStatus').innerHTML = statusHtml;
        
        // DO Cal UI
        let doState = parseInt(data.do_cal_state);
        if(doState !== lastDoState) {
            if(doState === 1) document.getElementById('doCalStep').innerHTML = `<div class="cal-step"><h4>Step 1/2: Set ZERO Point</h4><button class="btn-success" onclick="sendCommand('/cal/do/setzero')">📌 SET 0 POINT</button><button onclick="sendCommand('/cal/do/cancel')">Cancel</button></div>`;
            else if(doState === 2) document.getElementById('doCalStep').innerHTML = `<div class="cal-step"><h4>Step 2/2: Set 100% Point</h4><button class="btn-success" onclick="sendCommand('/cal/do/set100')">📌 SET 100% POINT</button><button onclick="sendCommand('/cal/do/cancel')">Cancel</button></div>`;
            else document.getElementById('doCalStep').innerHTML = `<div><button class="btn-success" onclick="sendCommand('/cal/do/start')">🚀 START DO CAL</button><button onclick="sendCommand('/cal/do/reset')">Reset</button></div>`;
            lastDoState = doState;
        }
        
        // pH Cal UI
        let phState = parseInt(data.ph_cal_state);
        if(phState !== lastPhState) {
            if(phState === 1) document.getElementById('phCalStep').innerHTML = `<div class="cal-step"><h4>Step 1/2: Set pH 6.86</h4><button class="btn-success" onclick="sendCommand('/cal/ph/set686')">📌 SET pH 6.86</button><button onclick="sendCommand('/cal/ph/cancel')">Cancel</button></div>`;
            else if(phState === 2) document.getElementById('phCalStep').innerHTML = `<div class="cal-step"><h4>Step 2/2: Set pH 4.01</h4><button class="btn-success" onclick="sendCommand('/cal/ph/set401')">📌 SET pH 4.01</button><button onclick="sendCommand('/cal/ph/cancel')">Cancel</button></div>`;
            else document.getElementById('phCalStep').innerHTML = `<div><button class="btn-success" onclick="sendCommand('/cal/ph/start')">🚀 START pH CAL</button><button onclick="sendCommand('/cal/ph/reset')">Reset</button></div>`;
            lastPhState = phState;
        }
        
        // TDS Cal UI
        let tdsState = parseInt(data.tds_cal_state);
        if(tdsState !== lastTdsState) {
            if(tdsState === 1) document.getElementById('tdsCalStep').innerHTML = `<div class="cal-step"><h4>Step 1/2: Set 15 ppt (15000 ppm)</h4><button class="btn-success" onclick="sendCommand('/cal/tds/set15ppt')">📌 SET 15 ppt</button><button onclick="sendCommand('/cal/tds/cancel')">Cancel</button></div>`;
            else if(tdsState === 2) document.getElementById('tdsCalStep').innerHTML = `<div class="cal-step"><h4>Step 2/2: Set 25 ppt (25000 ppm)</h4><button class="btn-success" onclick="sendCommand('/cal/tds/set25ppt')">📌 SET 25 ppt</button><button onclick="sendCommand('/cal/tds/cancel')">Cancel</button></div>`;
            else document.getElementById('tdsCalStep').innerHTML = `<div><button class="btn-success" onclick="sendCommand('/cal/tds/start')">🚀 START TDS CAL</button><button onclick="sendCommand('/cal/tds/reset')">Reset</button></div>`;
            lastTdsState = tdsState;
        }
        
        // Turbidity Cal UI
        let turbState = parseInt(data.turb_cal_state);
        if(turbState !== lastTurbState) {
            if(turbState === 1) document.getElementById('turbCalStep').innerHTML = `<div class="cal-step"><h4>Step 1/2: Set 0 NTU Point</h4><p>Place sensor in <strong>DISTILLED WATER</strong></p><button class="btn-success" onclick="sendCommand('/cal/turbidity/setzero')">📌 SET 0 NTU</button><button onclick="sendCommand('/cal/turbidity/cancel')">Cancel</button></div>`;
            else if(turbState === 2) document.getElementById('turbCalStep').innerHTML = `<div class="cal-step"><h4>Step 2/2: Set 1000 NTU Point</h4><p>Place sensor in <strong>1000 NTU STANDARD SOLUTION</strong></p><button class="btn-success" onclick="sendCommand('/cal/turbidity/set1000')">📌 SET 1000 NTU</button><button onclick="sendCommand('/cal/turbidity/cancel')">Cancel</button></div>`;
            else document.getElementById('turbCalStep').innerHTML = `<div><button class="btn-success" onclick="sendCommand('/cal/turbidity/start')">🚀 START TURBIDITY CAL</button><button onclick="sendCommand('/cal/turbidity/reset')">Reset</button></div>`;
            lastTurbState = turbState;
        }
    } catch(e) { console.error(e); }
}

setInterval(updateAll, 500);
updateAll();
</script>
</body>
</html>
)rawliteral";
  espServer.send(200, "text/html", html);
}

void setupCalibrationRoutes() {
  espServer.on("/", sendHTML);
  
  espServer.on("/sensor/values", []() {
    String json = "{";
    json += "\"do\":" + String(sensors.do_ok ? String(sensors.do_value, 2) : "\"--\"") + ",";
    json += "\"do_cal_state\":" + String((int)do_cal_state) + ",";
    json += "\"do_calibrated\":" + String(do_calibrated ? "true" : "false") + ",";
    json += "\"ph\":" + String(sensors.ph_ok ? String(sensors.ph_value, 2) : "\"--\"") + ",";
    json += "\"ph_cal_state\":" + String((int)ph_cal_state) + ",";
    json += "\"ph_calibrated\":" + String(ph_calibrated ? "true" : "false") + ",";
    json += "\"tds\":" + String(sensors.tds_ok ? String(sensors.tds_value, 0) : "\"--\"") + ",";
    json += "\"tds_cal_state\":" + String((int)tds_cal_state) + ",";
    json += "\"tds_calibrated\":" + String(tds_calibrated ? "true" : "false") + ",";
    json += "\"turbidity\":" + String(sensors.turbidity_ok ? String(sensors.turbidity, 0) : "\"--\"") + ",";
    json += "\"turb_cal_state\":" + String((int)turb_cal_state) + ",";
    json += "\"turbidity_calibrated\":" + String(turbidity_calibrated ? "true" : "false") + ",";
    json += "\"temp\":" + String(sensors.temp_ok ? String(sensors.temperature, 1) : "\"--\"") + ",";
    json += "\"weight\":" + String(sensors.loadcell_ok ? String(sensors.weight, 1) : "\"--\"") + ",";
    json += "\"water_level\":" + String(sensors.waterlevel_ok ? String(sensors.water_level_cm, 1) : "\"--\"") + ",";
    json += "\"cal_factor\":" + String(cal_factor);
    json += "}";
    espServer.send(200, "application/json", json);
  });
  
  // DO Routes
  espServer.on("/cal/do/start", []() { startDOCalibration(); espServer.send(200, "application/json", "{\"message\":\"Started\"}"); });
  espServer.on("/cal/do/setzero", []() { setDOZeroPoint(); espServer.send(200, "application/json", "{\"message\":\"Zero saved\"}"); });
  espServer.on("/cal/do/set100", []() { setDO100Point(); espServer.send(200, "application/json", "{\"message\":\"100% saved\"}"); });
  espServer.on("/cal/do/cancel", []() { do_cal_state = DO_IDLE; espServer.send(200, "application/json", "{\"message\":\"Cancelled\"}"); });
  espServer.on("/cal/do/reset", []() { resetDOCalibration(); espServer.send(200, "application/json", "{\"message\":\"Reset\"}"); });
  
  // pH Routes
  espServer.on("/cal/ph/start", []() { startPHCalibration(); espServer.send(200, "application/json", "{\"message\":\"pH started\"}"); });
  espServer.on("/cal/ph/set686", []() { setPH_6_86_Point(); espServer.send(200, "application/json", "{\"message\":\"pH 6.86 saved\"}"); });
  espServer.on("/cal/ph/set401", []() { setPH_4_01_Point(); espServer.send(200, "application/json", "{\"message\":\"pH 4.01 saved\"}"); });
  espServer.on("/cal/ph/cancel", []() { ph_cal_state = PH_IDLE; espServer.send(200, "application/json", "{\"message\":\"Cancelled\"}"); });
  espServer.on("/cal/ph/reset", []() { resetPHCalibration(); espServer.send(200, "application/json", "{\"message\":\"Reset\"}"); });
  
  // TDS Routes
  espServer.on("/cal/tds/start", []() { startTDSCalibration(); espServer.send(200, "application/json", "{\"message\":\"TDS started\"}"); });
  espServer.on("/cal/tds/set15ppt", []() { setTDS_15PPT_Point(); espServer.send(200, "application/json", "{\"message\":\"15ppt saved\"}"); });
  espServer.on("/cal/tds/set25ppt", []() { setTDS_25PPT_Point(); espServer.send(200, "application/json", "{\"message\":\"25ppt saved\"}"); });
  espServer.on("/cal/tds/cancel", []() { tds_cal_state = TDS_IDLE; espServer.send(200, "application/json", "{\"message\":\"Cancelled\"}"); });
  espServer.on("/cal/tds/reset", []() { resetTDSCalibration(); espServer.send(200, "application/json", "{\"message\":\"Reset\"}"); });
  
  // Turbidity Routes
  espServer.on("/cal/turbidity/start", []() { startTurbidityCalibration(); espServer.send(200, "application/json", "{\"message\":\"Turbidity calibration started. Place sensor in distilled water.\"}"); });
  espServer.on("/cal/turbidity/setzero", []() { setTurbidityZeroPoint(); espServer.send(200, "application/json", "{\"message\":\"0 NTU point saved. Now place in 1000 NTU solution.\"}"); });
  espServer.on("/cal/turbidity/set1000", []() { setTurbidity1000Point(); espServer.send(200, "application/json", "{\"message\":\"1000 NTU point saved. Calibration complete!\"}"); });
  espServer.on("/cal/turbidity/cancel", []() { turb_cal_state = TURB_IDLE; espServer.send(200, "application/json", "{\"message\":\"Turbidity calibration cancelled.\"}"); });
  espServer.on("/cal/turbidity/reset", []() { resetTurbidityCalibration(); espServer.send(200, "application/json", "{\"message\":\"Turbidity calibration reset.\"}"); });
  
  // Load Cell Routes
  espServer.on("/cal/weight/tare", []() { 
    scale.tare(10); 
    espServer.send(200, "application/json", "{\"message\":\"Tared\"}");
  });
  
  espServer.on("/cal/weight/adjust", []() {
    if (espServer.hasArg("delta")) {
      int delta = espServer.arg("delta").toInt();
      adjustLoadCellFactor(delta);
      String response = "{\"message\":\"Factor adjusted by " + String(delta) + "\",\"factor\":" + String(cal_factor) + "}";
      espServer.send(200, "application/json", response);
    } else {
      espServer.send(400, "application/json", "{\"error\":\"Missing delta parameter\"}");
    }
  });
  
  espServer.on("/cal/weight/set", []() {
    if (espServer.hasArg("factor")) {
      float factor = espServer.arg("factor").toFloat();
      updateLoadCellFactor(factor);
      String response = "{\"message\":\"Factor set to " + String(factor) + "\",\"factor\":" + String(cal_factor) + "}";
      espServer.send(200, "application/json", response);
    } else {
      espServer.send(400, "application/json", "{\"error\":\"Missing factor parameter\"}");
    }
  });
  
  // Water level routes
  espServer.on("/cal/water/empty", []() { empty_dist = sensors.water_level_cm; empty_cal = true; espServer.send(200, "application/json", "{\"message\":\"Empty set\"}"); });
  espServer.on("/cal/water/full", []() { full_dist = sensors.water_level_cm; full_cal = true; espServer.send(200, "application/json", "{\"message\":\"Full set\"}"); });
}

// ========== Setup ==========
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== ESP32 Water Quality System ===");
  
  pinMode(RELAY_SENSOR1, OUTPUT);
  pinMode(RELAY_SENSOR2, OUTPUT);
  pinMode(RELAY_SENSOR3, OUTPUT);
  pinMode(RELAY_WATER_PUMP, OUTPUT);
  pinMode(RELAY_SOLENOID, OUTPUT);
  digitalWrite(RELAY_SENSOR1, HIGH);
  digitalWrite(RELAY_SENSOR2, HIGH);
  digitalWrite(RELAY_SENSOR3, HIGH);
  digitalWrite(RELAY_WATER_PUMP, HIGH);
  digitalWrite(RELAY_SOLENOID, HIGH);
  
  pinMode(WATER_DETECT_PIN, INPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  
  analogSetAttenuation(ADC_11db);
  analogReadResolution(12);
  
  preferences.begin("calib", false);
  
  // Load all calibrations
  do_zero_raw = preferences.getFloat("do_zero_raw", 0.0);
  do_100_raw = preferences.getFloat("do_100_raw", 0.0);
  do_calibrated = preferences.getBool("do_calibrated", false);
  
  ph_zero_raw = preferences.getFloat("ph_zero_raw", 0.0);
  ph_slope_raw = preferences.getFloat("ph_slope_raw", 0.0);
  ph_calibrated = preferences.getBool("ph_calibrated", false);
  
  tds_15ppt_raw = preferences.getFloat("tds_15ppt_raw", 0.0);
  tds_25ppt_raw = preferences.getFloat("tds_25ppt_raw", 0.0);
  tds_calibrated = preferences.getBool("tds_calibrated", false);
  
  turbidity_zero_raw = preferences.getFloat("turbidity_zero_raw", 0.0);
  turbidity_1000_raw = preferences.getFloat("turbidity_1000_raw", 0.0);
  turbidity_calibrated = preferences.getBool("turbidity_calibrated", false);
  
  ph_slope = preferences.getFloat("ph_slope", 1.0);
  ph_intercept = preferences.getFloat("ph_intercept", 0.0);
  tds_slope = preferences.getFloat("tds_slope", 1.0);
  tds_intercept = preferences.getFloat("tds_intercept", 0.0);
  cal_factor = preferences.getFloat("weight_factor", -7050.0);
  container_height = preferences.getFloat("container_height", 100.0);
  empty_dist = preferences.getFloat("empty_dist", 0.0);
  full_dist = preferences.getFloat("full_dist", 0.0);
  solenoid_time = preferences.getInt("solenoid_time", 10);
  
  if (empty_dist > 0 && full_dist > 0) { empty_cal = true; full_cal = true; }
  
  ds18b20.begin();
  scale.begin(LOADCELL_DOUT, LOADCELL_SCK);
  scale.set_scale(cal_factor);
  scale.tare(10);
  
  pinMode(RS485_CONTROL, OUTPUT);
  digitalWrite(RS485_CONTROL, LOW);
  Serial2.begin(9600, SERIAL_8N1, RS485_RX, RS485_TX);
  ammoniumSensor.begin(AMMONIUM_ADDR, Serial2);
  ammoniumSensor.preTransmission([]() { digitalWrite(RS485_CONTROL, HIGH); });
  ammoniumSensor.postTransmission([]() { digitalWrite(RS485_CONTROL, LOW); });
  
  Serial.print("[WIFI] Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\n[WIFI] Connected");
  Serial.print("[WIFI] IP: ");
  Serial.println(WiFi.localIP());
  
  setupCalibrationRoutes();
  espServer.begin();
  Serial.println("[WEB] http://" + WiFi.localIP().toString());
  
  webSocket.begin(pc_server, pc_websocket_port, "/esp32");
  webSocket.onEvent(wsEvent);
  webSocket.setReconnectInterval(5000);
  
  checkConnections();
  printAction("Setup complete");
}

// ========== Loop ==========
void loop() {
  espServer.handleClient();
  webSocket.loop();
  
  static unsigned long last_state = 0, last_sensor = 0, last_send = 0, last_check = 0, last_print = 0;
  unsigned long now = millis();
  
  if (now - last_state >= 50) { updateTestMachine(); last_state = now; }
  if (now - last_sensor >= 100) { updateSensors(); last_sensor = now; }
  if (now - last_send >= 2000) { sendSensorData(); last_send = now; }
  if (now - last_check >= 5000) { checkConnections(); last_check = now; }
  if (now - last_print >= 5000) { printSensorReading(); last_print = now; }
  
  delay(1);
}