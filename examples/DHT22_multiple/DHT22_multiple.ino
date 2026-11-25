#include <Arduino.h>
#include <Ticker.h>
#include <esp32DHT.h>

Ticker ticker;
DHT sensor;
uint8_t gpio[] = {12, 13};

void setup() {
  Serial.begin(115200);
}

void loop() {
  // check if busy
  const auto status = sensor.getStatus();
  if (status == DHT::Status::REQUESTING || status == DHT::Status::RECEIVING || status == DHT::Status::RECEIVED) {
    delay(100);
    return;
  }

  constexpr size_t gpioCount = sizeof(gpio) / sizeof(gpio[0]);
  auto newGpio = static_cast<gpio_num_t>(gpio[random(gpioCount)]);
  if (newGpio != sensor.getGpio()) {
    // reset before new setup
    sensor.reset();

    sensor.onData([newGpio](float humidity, float temperature) {
      Serial.printf("[%lu,\t%hhu] Temperature: %.2f°C, Humidity: %.2f%%\n", millis(), static_cast<uint8_t>(newGpio), temperature, humidity);
    });
    sensor.onError([newGpio](DHT::Status status) {
      Serial.printf("[%lu,\t%hhu] Sensor error: %s\n", millis(), static_cast<uint8_t>(newGpio), DHT::statusToString(status));
    });

    if (sensor.setup(newGpio, DHT::Type::DHT22)) {
      Serial.printf("[%lu,\t%hhu] Changed pin\n", millis(), static_cast<uint8_t>(newGpio));

    } else {
      Serial.printf("[%lu,\t%hhu] ERROR: %s\n", millis(), static_cast<uint8_t>(newGpio), DHT::statusToString(sensor.getStatus()));
    }
  }

  // run
  if (sensor.poll()) {
    delay(sensor.getMinReadInterval());

  } else {
    delay(100);
  }
}
