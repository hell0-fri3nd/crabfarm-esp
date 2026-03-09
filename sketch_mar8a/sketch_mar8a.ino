#include <ESP8266WiFi.h>
#include <WebSocketsClient.h>

const char* ssid = "Convergys_2.4G";
const char* password = "MyCoffeeBreak2298!";

WebSocketsClient webSocket;

String deviceId = "scale_01";
#define LED LED_BUILTIN
bool wsConnected = false;

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {

  switch(type) {

    case WStype_DISCONNECTED:
      Serial.println("WebSocket disconnected");
      wsConnected = false;
      break;

    case WStype_CONNECTED:
      Serial.println("WebSocket connected");
      wsConnected = true;
      digitalWrite(LED, LOW); // LED steady when connected
      break;

    case WStype_TEXT:
      Serial.printf("Received: %s\n", payload);
      break;
  }
}

void setup() {

  Serial.begin(115200);

  pinMode(LED, OUTPUT);
  digitalWrite(LED, HIGH);

  WiFi.begin(ssid, password);

  Serial.println("Connecting to WiFi...");

  while (WiFi.status() != WL_CONNECTED) {

    digitalWrite(LED, LOW);
    delay(200);
    digitalWrite(LED, HIGH);
    delay(200);

    Serial.print(".");
  }

  Serial.println("\nWiFi connected");
// ws://192.168.100.11:4572/ws/v1/websockets/sensors
  webSocket.begin("192.168.100.11", 4572, "/ws/v1/websockets/sensors"); // change to your server IP
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000);

}

void loop() {

  webSocket.loop();

  // Blink while websocket not connected
  static unsigned long lastBlink = 0;

  if (!wsConnected) {

    if (millis() - lastBlink > 300) {
      lastBlink = millis();
      digitalWrite(LED, !digitalRead(LED));
    }

  } else {

    digitalWrite(LED, LOW); // stop blinking when connected
  }

  static unsigned long lastSend = 0;

  if (wsConnected && millis() - lastSend > 2000) {

    lastSend = millis();

    float weight = random(100,1000) / 10.0;

    String message = "{\"device\":\"loadcell\",\"weight\":" + String(weight) + "}";

    webSocket.sendTXT(message);

    Serial.println(message);
  }
}