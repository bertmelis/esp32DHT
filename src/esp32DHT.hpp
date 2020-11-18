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

#pragma once

extern "C" {
  #include <freertos/FreeRTOS.h>
  #include <freertos/task.h>
  #include <esp32-hal-gpio.h>
  #include <driver/rmt.h>
  #include <esp_timer.h>
}
#include <functional>

namespace esp32DHTInternals {

typedef std::function<void(float humid, float temp)> OnData_CB;
typedef std::function<void(uint8_t error)> OnError_CB;

}  // end namespace esp32DHTInternals

class DHT {
 public:
  DHT();
  ~DHT();
  void setup(uint8_t pin, rmt_channel_t channel = RMT_CHANNEL_0);  // setPin does complete setup of DHT lib
  void onData(esp32DHTInternals::OnData_CB callback);
  void onError(esp32DHTInternals::OnError_CB callback);
  void read();
  const char* getError() const;

 protected:
  uint8_t _status;
  uint8_t _data[5];

 private:
  static void _readSensor(DHT* instance);
  void _decode(rmt_item32_t* data, int numItems);
  void _tryCallback();
  virtual float _getTemperature() = 0;
  virtual float _getHumidity() = 0;

 private:
  uint8_t _pin;
  rmt_channel_t _channel;
  esp32DHTInternals::OnData_CB _onData;
  esp32DHTInternals::OnError_CB _onError;
  TaskHandle_t _task;
  RingbufHandle_t _ringBuf;
};

class DHT11 : public DHT {
 private:
  float _getTemperature();
  float _getHumidity();
};

class DHT22 : public DHT {
 private:
  float _getTemperature();
  float _getHumidity();
};
