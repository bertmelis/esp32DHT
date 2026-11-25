/*

Copyright 2018 Bert Melis

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/


#include <Arduino.h>
#include <Ticker.h>
#include <esp32DHT.h>

Ticker ticker;
DHT sensor;
DHT::Status prevStatus = DHT::Status::NONE;

void readDHT() {
  sensor.poll();
}

void setup() {
  Serial.begin(115200);

  // PIN 12 is DATA
  sensor.setup(12, DHT::Type::DHT22);
  // DHT11 also works:
  // sensor.setup(12, DHT::Type::DHT11);

  // register onData callback
  sensor.onData([](float humidity, float temperature) {
    Serial.printf("[%lu][CALLBACK] Temperature: %.2f°C, Humidity: %.2f%%\n", millis(), temperature, humidity);
  });

  // register onError callback
  sensor.onError([](DHT::Status status) {
    Serial.printf("[%lu][CALLBACK] Sensor error: %s\n", millis(), DHT::statusToString(status));
  });

  ticker.attach(10, readDHT);
}

void loop() {
  auto status = sensor.getStatus();
  if (status != prevStatus) {
    Serial.printf("[%lu] Status '%s' => '%s'\n", millis(), DHT::statusToString(prevStatus), DHT::statusToString(status));
    prevStatus = status;

    if (status == DHT::Status::READED || status == DHT::Status::RECEIVED) {
      Serial.printf("[%lu] T: %g, H: %g\n", millis(), sensor.getTemperature(), sensor.getHumidity());
    }
  }
}
