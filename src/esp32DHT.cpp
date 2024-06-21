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

#define RMT_CLK_DIV 80

DHT::DHT() :
  _status(0),
  _data{0},
  _pin(0),
  _channel(RMT_CHANNEL_0),
  _onData(nullptr),
  _onError(nullptr),
  _task(nullptr) {}

DHT::~DHT() {
  rmt_driver_uninstall(_channel);
  vTaskDelete(_task);
}

void DHT::setup(uint8_t pin, rmt_channel_t channel) {
  _pin = pin;
  _channel = channel;
  rmt_config_t config{};
  config.rmt_mode = RMT_MODE_RX;
  config.channel = _channel;
  config.gpio_num = static_cast<gpio_num_t>(_pin);
  config.mem_block_num = 2;
  config.rx_config.filter_en = 1;
  config.rx_config.filter_ticks_thresh = 10;
  config.rx_config.idle_threshold = 1000;
  config.clk_div = RMT_CLK_DIV;
  rmt_config(&config);
  rmt_driver_install(_channel, 400, 0);  // 400 words for ringbuffer containing pulse trains from DHT
  rmt_get_ringbuf_handle(_channel, &_ringBuf);
  xTaskCreate((TaskFunction_t)&_readSensor, "esp32DHT", 2048, this, 5, &_task);
  pinMode(_pin, OUTPUT);
  digitalWrite(_pin, HIGH);
}

void DHT::onData(esp32DHTInternals::OnData_CB callback) {
  _onData = callback;
}

void DHT::onError(esp32DHTInternals::OnError_CB callback) {
  _onError = callback;
}

void DHT::read() {
  xTaskNotifyGive(_task);
}

const char* DHT::getError() const {
  if (_status == 0) {
    return "OK";
  } else if (_status == 1) {
    return "TO";
  } else if (_status == 2) {
    return "NACK";
  } else if (_status == 3) {
    return "DATA";
  } else if (_status == 4) {
    return "CS";
  } else if (_status == 5) {
    return "UNDERFLOW";
  } else if (_status == 6) {
    return "OVERFLOW";
  }
  return "UNKNOWN";
}

void DHT::_readSensor(DHT* instance) {
  size_t rx_size = 0;
  while (1) {
    // reset
    instance->_data[0] =
    instance->_data[1] =
    instance->_data[2] =
    instance->_data[3] =
    instance->_data[4] = 0;
    instance->_status = 0;

    // block and wait for notification
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    // give start signal to sensor
    digitalWrite(instance->_pin, LOW);
    vTaskDelay(18);
    pinMode(instance->_pin, INPUT);
    rmt_rx_start(instance->_channel, 1);
    // rmt_set_pin is used untill platformio updates to latest Arduino core.
    // rmt_set_gpio(instance->_channel, RMT_MODE_RX, static_cast<gpio_num_t>(instance->_pin), false);  // reset after using pin as output
    rmt_set_pin(instance->_channel, RMT_MODE_RX, static_cast<gpio_num_t>(instance->_pin));  // reset after using pin as output

    // blocks until data is available or timeouts after 1000
    rmt_item32_t* items = static_cast<rmt_item32_t*>(xRingbufferReceive(instance->_ringBuf, &rx_size, 1000));
    if (items) {
      instance->_decode(items, rx_size/sizeof(rmt_item32_t));
      vRingbufferReturnItem(instance->_ringBuf, static_cast<void*>(items));
    } else {
      instance->_status = 1;  // timeout error
    }
    rmt_rx_stop(instance->_channel);
    pinMode(instance->_pin, OUTPUT);
    digitalWrite(instance->_pin, HIGH);

    // return results
    instance->_tryCallback();
  }
}

void DHT::_decode(rmt_item32_t* data, int numItems) {
  uint8_t pulse = data[0].duration0 + data[0].duration1;
  if (numItems < 41) {
    _status = 5;
  } else if (numItems > 42) {
    _status = 6;
  } else if (pulse < 130 || pulse > 180) {
    _status = 2;
  } else {
    for (uint8_t i = 1; i < 41; ++i) {  // don't include tail >40
      pulse = data[i].duration0 + data[i].duration1;
      if (pulse > 55 && pulse < 145) {
        _data[(i - 1) / 8] <<= 1;  // shift left
        if (pulse > 110) {
          _data[(i - 1) / 8] |= 1;
        }
      } else {
        _status = 3;  // DATA error
        return;
      }
    }
    if (_data[4] == ((_data[0] + _data[1] + _data[2] + _data[3]) & 0xFF)) {
      _status = 0;
    } else {
      _status = 4;  // checksum error
    }
  }
}

void DHT::_tryCallback() {
  if (_status == 0) {
    if (_onData) _onData(_getHumidity(), _getTemperature());
  } else {
    if (_onError) _onError(_status);
  }
}

float DHT11::_getTemperature() {
  if (_status != 0) return NAN;
  return static_cast<float>(_data[2]);
}

float DHT11::_getHumidity() {
  if (_status != 0) return NAN;
  return static_cast<float>(_data[0]);
}

float DHT22::_getTemperature() {
  if (_status != 0) return NAN;
  float temp = (((_data[2] & 0x7F) << 8) | _data[3]) * 0.1;
  if (_data[2] & 0x80) {  // negative temperature
    temp = -temp;
  }
  return temp;
}

float DHT22::_getHumidity() {
  if (_status != 0) return NAN;
  return ((_data[0] << 8) | _data[1]) * 0.1;
}
