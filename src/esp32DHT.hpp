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

#define DHT_ENABLE_RAW 0

extern "C" {
  #include <freertos/FreeRTOS.h>
  #include <freertos/task.h>
  #include <esp32-hal-gpio.h>
  #include <driver/rmt.h>
  #include <esp_timer.h>
}
#include <functional>

typedef std::function<void(int8_t)> Callback;

class DHT {
 public:
  DHT();
  ~DHT();
  void setup(uint8_t pin, rmt_channel_t channel = RMT_CHANNEL_0);  // setPin does complete setup of DHT lib
  void setCallback(Callback cb);
  void read();
  int8_t available() const;
  virtual float getTemperature() = 0;
  virtual float getHumidity() = 0;
  const char* getError();

 protected:
  int8_t _status;
  int8_t _data[5];

 private:
  static void _handleTimer(DHT* instance);
  static void _handleData(DHT* instance);
  bool _isInRange(rmt_item32_t item, uint32_t lowDuration, uint32_t highDuration, uint32_t tolerance);
  void _decode(rmt_item32_t *data, int numItems);

 private:
  uint8_t _pin;
  char _errorStr[5];
  rmt_channel_t _channel;
  esp_timer_handle_t _timer;
  TaskHandle_t _task;
  RingbufHandle_t _ringBuf;
  Callback _cb;
#if DHT_ENABLE_RAW

 public:
  void getRawData(uint32_t array[]);

 private:
  uint32_t _rawData[42];
#endif
};

class DHT11 : public DHT {
 public:
  float getTemperature();
  float getHumidity();
};

class DHT22 : public DHT {
 public:
  float getTemperature();
  float getHumidity();
};
