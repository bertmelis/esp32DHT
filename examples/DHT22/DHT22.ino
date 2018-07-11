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
#if DHT_ENABLE_RAW
    // print raw RMT timing data, converted to microseconds: 42 values: start, 40x data, stop
    uint32_t array[42] = {0};
    sensor.getRawData(array);
    for (uint8_t i = 0; i < 42; ++i) {
      Serial.printf("%u: %u\n", i, array[i]);
    }
#endif
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
