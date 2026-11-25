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

#ifndef DHT_TASK_NAME
  #define DHT_TASK_NAME "esp32DHT"
#endif

#ifndef DHT_TASK_STACK_SIZE
  #define DHT_TASK_STACK_SIZE 2048
#endif

#ifndef DHT_TASK_PRIORITY
  #define DHT_TASK_PRIORITY 5
#endif

extern "C" {
  #include <freertos/FreeRTOS.h>
  #include <freertos/task.h>
  #include <freertos/queue.h>
  #include <esp32-hal-gpio.h>
  #include <driver/rmt_rx.h>
  #include <esp_timer.h>
}
#include <atomic>
#include <functional>

class DHT {
 public:
  enum class Status {
    NONE,
    INITIALIZED,
    REQUESTING,
    RECEIVING,
    RECEIVED,
    READED,

    FAIL_ON_DRIVER,
    NACK,
    TIMEOUT,
    BAD_DATA,
    UNDERFLOW_DATA,
    OVERFLOW_DATA,
    BAD_CHECKSUM
  };
  enum class Type {
    DHT11,
    DHT22
  };
  typedef std::function<void(float humidity, float temperature)> DataCallback;
  typedef std::function<void(Status status)> ErrorCallback;

  DHT();
  ~DHT();
  void reset();
  void onData(DataCallback callback);
  void onError(ErrorCallback callback);
  gpio_num_t getGpio() const;
  Type getType() const;
  Status getStatus() const;
  virtual unsigned short getWakeupDelay() const;
  virtual unsigned short getMinReadInterval() const;
  unsigned long getLastReadTime() const;
  bool setup(gpio_num_t gpio, Type type);
  bool setup(uint8_t pin, Type type) { return setup(static_cast<gpio_num_t>(pin), type); }
  bool isPolling() const;
  bool poll();
  virtual float getTemperature() const;
  virtual float getHumidity() const;
  static const char* statusToString(const Status status);

 protected:
  gpio_num_t _gpio;
  Type _type;
  std::atomic<Status> _status;
  std::atomic<bool> _polling{false};
  std::atomic<unsigned long> _lastReadTime;
  uint8_t _data[5];
  DataCallback _onData;
  ErrorCallback _onError;
  rmt_channel_handle_t _channel;
  TaskHandle_t _task;
  QueueHandle_t _queue;

  static void _read(DHT* instance);
  static bool _onReceiveDone(rmt_channel_handle_t, const rmt_rx_done_event_data_t*, void*);
  void _decode(const rmt_symbol_word_t* data, const size_t numItems);
};
