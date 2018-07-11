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
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONDHTTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#include "esp32DHT.hpp"  // NOLINT

#define RMT_CLK_DIV 100
#define RMT_TICK_10_US (80000000 / RMT_CLK_DIV / 100000)

DHT::DHT() :
  _status(0),
  _data{0},
  _pin(0),
  _errorStr{"\0"},
  _timer(nullptr),
  _task(nullptr),
  _cb(nullptr) {}

void DHT::setup(uint8_t pin, rmt_channel_t channel) {
  _pin = pin;
  _channel = channel;
  esp_timer_create_args_t _timerConfig;
  _timerConfig.arg = static_cast<void*>(this);
  _timerConfig.callback = reinterpret_cast<esp_timer_cb_t>(_handleTimer);
  _timerConfig.dispatch_method = ESP_TIMER_TASK;
  _timerConfig.name = "Ticker";
  esp_timer_create(&_timerConfig, &_timer);
  rmt_config_t config;
  config.rmt_mode = RMT_MODE_RX;
  config.channel = _channel;
  config.gpio_num = static_cast<gpio_num_t>(_pin);
  config.mem_block_num = 2;
  config.rx_config.filter_en = 1;
  config.rx_config.filter_ticks_thresh = 10;
  config.rx_config.idle_threshold = RMT_TICK_10_US * 10;
  config.clk_div = RMT_CLK_DIV;
  rmt_config(&config);
  rmt_driver_install(_channel, 400, 0);  // 400 words for ringbuffer containing pulse trains from DHT
  rmt_get_ringbuf_handle(_channel, &_ringBuf);
  xTaskCreate((TaskFunction_t)&_handleData, "DHT_watchRingbuf", 2048, this, 5, &_task);
  pinMode(_pin, OUTPUT);
  digitalWrite(_pin, HIGH);
}

DHT::~DHT() {
  esp_timer_delete(_timer);
  rmt_driver_uninstall(_channel);
  vTaskDelete(_task);
}

void DHT::setCallback(Callback cb) {
  _cb = cb;
}

void DHT::read() {
  // _pin should be set to OUTPUT and HIGH
  digitalWrite(_pin, LOW);
  esp_timer_start_once(_timer, 18 * 1000);  // timer is in microseconds
  _data[0] = _data[1] = _data[2] = _data[3] = _data[4] = 0;
  _status = 0;
}

int8_t DHT::available() const {
  return _status;
}

const char* DHT::getError() {
  if (_status == 1) {
    strcpy(_errorStr, "OK");  // NOLINT
  } else if (_status == 0) {
    strcpy(_errorStr, "BUSY");  // NOLINT
  } else if (_status == -1) {
    strcpy(_errorStr, "TO");   // NOLINT
  } else if (_status == -2) {
    strcpy(_errorStr, "NACK");   // NOLINT
  } else if (_status == -3) {
    strcpy(_errorStr, "DATA");   // NOLINT
  } else if (_status == -4) {
    strcpy(_errorStr, "CS");   // NOLINT
  }
  return _errorStr;
}

void DHT::_handleTimer(DHT* instance) {
  digitalWrite(instance->_pin, HIGH);
  pinMode(instance->_pin, INPUT);
  rmt_set_pin(instance->_channel, RMT_MODE_RX, static_cast<gpio_num_t>(instance->_pin));  // reset after using pin as output
  xTaskNotifyGive(instance->_task);
  rmt_rx_start(instance->_channel, 1);
}

void DHT::_handleData(DHT* instance) {
  size_t rx_size = 0;
  while (1) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  // block and wait for notification
    // blocks untill data is available or timeouts after portMAX_DELAY
    rmt_item32_t* items = static_cast<rmt_item32_t*>(xRingbufferReceive(instance->_ringBuf, &rx_size, RMT_TICK_10_US * 100));
    if (items) {
#if DHT_ENABLE_RAW
    uint8_t qty;
    qty = (rx_size / sizeof(rmt_item32_t) < 42) ? rx_size / sizeof(rmt_item32_t) : 42;
    for (uint8_t i = 0; i < qty; ++i) {
      instance->_rawData[i] = (items[i].duration0 * 10 / RMT_TICK_10_US) + (items[i].duration1 * 10 / RMT_TICK_10_US);
    }
#endif
      instance->_decode(items, rx_size/sizeof(rmt_item32_t));
      vRingbufferReturnItem(instance->_ringBuf, static_cast<void*>(items));
      instance->_cb(instance->_status);
      rmt_rx_stop(instance->_channel);
      pinMode(instance->_pin, OUTPUT);
      digitalWrite(instance->_pin, HIGH);
    } else {
      instance->_status = -1;  // timeout error
      instance->_cb(instance->_status);
      rmt_rx_stop(instance->_channel);
      pinMode(instance->_pin, OUTPUT);
      digitalWrite(instance->_pin, HIGH);
    }
  }
}

bool DHT::_isInRange(rmt_item32_t item, uint32_t lowDuration, uint32_t highDuration, uint32_t tolerance) {
  uint32_t lowValue = item.duration0 * 10 / RMT_TICK_10_US;
  uint32_t highValue = item.duration1 * 10 / RMT_TICK_10_US;
  if (lowValue < (lowDuration - tolerance) || lowValue > (lowDuration + tolerance) ||
    (highValue != 0 &&
    (highValue < (highDuration - tolerance) || highValue > (highDuration + tolerance)))) {
    return false;
  }
  return true;
}

void DHT::_decode(rmt_item32_t *data, int numItems) {
  if (!_isInRange(data[0], 80, 80, 20)) {
    _status = -2;
  } else if (numItems != 42) {
    _status = -3;
  } else {
    // parse data
    for (uint8_t i = 1; i < numItems - 1; ++i) {  // exclude ack and end
      if (_isInRange(data[i], 50, 70, 20)) {
        _data[(i - 1) / 8] |= 1;  // shift and add bit
      } else if (_isInRange(data[i], 50, 26, 20)) {
        _data[(i - 1) / 8] <<= 1;  // shift (and add 0)
      } else {
        _status = -3;  // DATA error
        return;
      }
    }
    if (_data[0] + _data[1] + _data[2] + _data[3] == (_data[4] & 0xFF)) {
      _status = 1;
    } else {
      _status = -4;  // checksum error
    }
  }
}

#if DHT_ENABLE_RAW
void DHT::getRawData(uint32_t array[]) {
  for (uint8_t i = 0; i < 42; ++i) array[i] = _rawData[i];
}
#endif

float DHT11::getTemperature() {
  if (_status < 1) return NAN;
  return static_cast<float>(_data[2]);
}

float DHT11::getHumidity() {
  if (_status < 1) return NAN;
  return static_cast<float>(_data[0]);
}

float DHT22::getTemperature() {
  if (_status < 1) return NAN;
  float temp = (((_data[2] & 0x7F) << 8) | _data[3]) * 0.1;
  // float temp = word(_data[2] & 0x7F, _data[3]) * 0.1;
  if (_data[2] & 0x80) {  // negative temperature
    temp = -temp;
  }
  return temp;
}

float DHT22::getHumidity() {
  if (_status < 1) return NAN;
  return ((_data[0] << 8) | _data[1]) * 0.1;
  // return word(_data[0], _data[1]) * 0.1;
}
