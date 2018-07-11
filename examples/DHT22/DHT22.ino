#include <Arduino.h>
#include <esp32DHT.h>

DHT22 sensor;

void setup() {
  Serial.begin(112500);
  sensor.setup(23);
  sensor.setCallback([](int8_t result) {
    if (result > 0) {
      Serial.printf("Temp: %.1f°C\nHumid: %.1f%%\n", sensor.getTemperature(), sensor.getHumidity());
    } else {
      Serial.printf("Sensor error: %s", sensor.getError());
    }
  });
}

void loop() {
  static uint32_t lastMillis = 0;
  if (millis() - lastMillis > 30000) {
      lastMillis = millis();
      sensor.read();
      Serial.print("Read DHT...\n");
  }
}
