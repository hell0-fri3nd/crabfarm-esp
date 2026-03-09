#include "HX711.h"
#include <WiFi.h>
#include <WebSocketsClient.h>

const char* ssid = "Convergys_2.4G";
const char* password = "MyCoffeeBreak2298!";

bool wsConnected = false;

#define BACKEND_IP "192.168.100.174"
#define BACKEND_PORT 4572

// Pin definitions
#define LED 13
#define DOUT_PIN 16
#define SCK_PIN 4

HX711 scale;
WebSocketsClient webSocket;

// Calibration factor (already calibrated in grams)
float calibration_factor = 263.50;

void webSocketEvent(WStype_t type, uint8_t* payload, size_t length) {

  switch (type) {
    case WStype_DISCONNECTED:
      Serial.println("WebSocket disconnected");
      wsConnected = false;
      break;

    case WStype_CONNECTED:
      Serial.println("WebSocket connected");
      wsConnected = true;
      digitalWrite(LED, LOW);
      break;

    case WStype_TEXT:
      Serial.printf("Received: %s\n", payload);
      break;
  }
}

void setup() {

  Serial.begin(115200);
  delay(1000);

  pinMode(LED, OUTPUT);
  digitalWrite(LED, HIGH);

  Serial.println("\n=== Load Cell WebSocket (GRAMS) ===");

  scale.begin(DOUT_PIN, SCK_PIN);

  if (scale.wait_ready_timeout(2000)) {
    Serial.println("✓ Scale Ready");

    scale.set_scale(calibration_factor);

    Serial.println("Remove all weight from scale...");
    delay(2000);

    Serial.println("Taring...");
    scale.tare(20);

    Serial.println("✓ Scale tared");
  } else {
    Serial.println("✗ HX711 not detected. Check wiring!");
  }

  WiFi.begin(ssid, password);

  Serial.println("Connecting to WiFi...");

  while (WiFi.status() != WL_CONNECTED) {

    digitalWrite(LED, LOW);
    delay(200);
    digitalWrite(LED, HIGH);
    delay(200);
    Serial.print(".");
  }

  Serial.println("\n✓ WiFi Connected");

  webSocket.begin(BACKEND_IP, BACKEND_PORT, "/ws/v1/websockets/sensors");
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000);
}

// Read weight from load cell in grams
float readWeightInGrams() {

  if (!scale.wait_ready_timeout(100)) {
    return 0;
  }

  // Average 10 readings for stability
  float grams = scale.get_units(10);

  // Remove negative noise
  if (grams < 0) {
    grams = 0;
  }

  // Dead zone filter (ignore very tiny changes)
  if (grams < 0.3) {
    grams = 0;
  }

  return grams;
}

// Create JSON message
String loadCellFunction() {

  float grams = readWeightInGrams();

  // round to 1 decimal
  grams = round(grams * 10) / 10.0;

  // grams = grams < 0 ? 0 : grams;
  String message = "{\"device\":\"loadcell\",\"weight\":" + String(grams,1) + "}";

  return message;
}

void loop() {

  webSocket.loop();

  static unsigned long lastBlink = 0;

  if (!wsConnected) {
    if (millis() - lastBlink > 300) {
      lastBlink = millis();
      digitalWrite(LED, !digitalRead(LED));
    }

  } else {

    digitalWrite(LED, LOW);
  }

  // SERIAL COMMANDS
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();

    if (input == "t" || input == "T") {

      if (scale.wait_ready_timeout(100)) {

        Serial.println("Taring scale...");
        scale.tare(20);

        Serial.println("✓ Done");
      }
    }

    else if (input == "s" || input == "S") {

      float grams = readWeightInGrams();

      Serial.print("Weight: ");
      Serial.print(grams, 1);
      Serial.println(" g");
    }

    else if (input == "f" || input == "F") {

      Serial.print("Calibration factor: ");
      Serial.println(calibration_factor);
    }

    else if (isNumeric(input)) {

      float newFactor = input.toFloat();

      calibration_factor = newFactor;

      scale.set_scale(calibration_factor);

      Serial.print("New factor set: ");
      Serial.println(calibration_factor);
    }
  }

  // SEND DATA
  static unsigned long lastSend = 0;

  if (wsConnected && millis() - lastSend > 2000) {

    lastSend = millis();

    String data = loadCellFunction();

    webSocket.sendTXT(data);

    Serial.print("Sent: ");
    Serial.println(data);

    // Auto-zero drift correction
    if (readWeightInGrams() == 0) {
      scale.tare(1);
    }
  }

  delay(50);
}

bool isNumeric(String str) {

  for (unsigned int i = 0; i < str.length(); i++) {

    if (!(isdigit(str[i]) || str[i] == '.' || (i == 0 && (str[i] == '+' || str[i] == '-')))) {

      return false;
    }
  }

  return str.length() > 0;
}
