#include "HX711.h"

#define DOUT 2
#define CLK 4

HX711 scale;

void setup() {
  Serial.begin(115200);
  
  scale.begin(DOUT, CLK);
  
  Serial.println("Load Cell Test");
}

void loop() {

  if (scale.is_ready()) {
    long reading = scale.read();
    Serial.print("Raw reading: ");
    Serial.println(reading);
  } 
  else {
    Serial.println("HX711 not found.");
  }

  delay(1000);
}
