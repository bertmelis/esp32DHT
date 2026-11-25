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

#include <Arduino.h>
#include "esp32DHT.hpp"  // NOLINT

DHT::DHT() :
  _gpio(GPIO_NUM_NC),
  _type(DHT::Type::DHT22),
  _status(DHT::Status::NONE),
  _lastReadTime(0),
  _data{0},
  _onData(nullptr),
  _onError(nullptr),
  _channel(nullptr),
  _task(nullptr),
  _queue(nullptr) {}

DHT::~DHT() {
  reset();
}

void DHT::reset() {
  if (_channel != nullptr) {
    rmt_disable(_channel);
    rmt_del_channel(_channel);
    _channel = nullptr;
  }

  if (_task != nullptr) {
    vTaskDelete(_task);
    _task = nullptr;
  }

  if (_queue != nullptr) {
    vQueueDelete(_queue);
    _queue = nullptr;
  }

  if (_gpio != GPIO_NUM_NC) {
    gpio_set_level(_gpio, 1);
    gpio_set_direction(_gpio, GPIO_MODE_INPUT);
  }

  _gpio = GPIO_NUM_NC;
  _type = Type::DHT22;
  _status.store(Status::NONE);
  _lastReadTime.store(0);
  _onData = nullptr;
  _onError = nullptr;
  _polling.store(false);
}

void DHT::onData(DHT::DataCallback callback) {
  _onData = callback;
}

void DHT::onError(DHT::ErrorCallback callback) {
  _onError = callback;
}

gpio_num_t DHT::getGpio() const {
  return _gpio;
}

DHT::Type DHT::getType() const {
  return _type;
}

DHT::Status DHT::getStatus() const {
  return _status.load();
}

unsigned short DHT::getWakeupDelay() const {
  if (_type == Type::DHT11) {
    return 20;

  } else if (_type == Type::DHT22) {
    return 2;

  } else {
    return 0;
  }
}

unsigned short DHT::getMinReadInterval() const {
  if (_type == Type::DHT11) {
    return 1000;

  } else if (_type == Type::DHT22) {
    return 2000;

  } else {
    return 0;
  }
}

unsigned long DHT::getLastReadTime() const {
  return _lastReadTime.load();
}

bool DHT::setup(gpio_num_t gpio, Type type) {
  if (gpio == GPIO_NUM_NC) {
    _status.store(Status::FAIL_ON_DRIVER);
    return false;
  }

  rmt_rx_channel_config_t rxСonfig{};
  rxСonfig.gpio_num = gpio;
  rxСonfig.clk_src = RMT_CLK_SRC_DEFAULT;
  rxСonfig.resolution_hz = 1000000;
  rxСonfig.mem_block_symbols = SOC_RMT_MEM_WORDS_PER_CHANNEL;
  rxСonfig.flags.with_dma = false;
#if ESP_IDF_VERSION_MAJOR < 6
  rxСonfig.flags.io_loop_back = true;
#endif

  if (rmt_new_rx_channel(&rxСonfig, &_channel) != ESP_OK) {
    _status.store(Status::FAIL_ON_DRIVER);
    return false;
  }

  _queue = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));
  if (_queue == nullptr) {
    rmt_del_channel(_channel);
    _channel = nullptr;
    _status.store(Status::FAIL_ON_DRIVER);
    return false;
  }

  rmt_rx_event_callbacks_t cbs = {
    .on_recv_done = _onReceiveDone
  };
  if (rmt_rx_register_event_callbacks(_channel, &cbs, this) != ESP_OK || rmt_enable(_channel) != ESP_OK) {
    vQueueDelete(_queue);
    rmt_del_channel(_channel);
    _queue = nullptr;
    _channel = nullptr;
    _status.store(Status::FAIL_ON_DRIVER);
    return false;
  }

  const BaseType_t taskResult = xTaskCreate(
    (TaskFunction_t)&_read,
    DHT_TASK_NAME,
    DHT_TASK_STACK_SIZE,
    this,
    DHT_TASK_PRIORITY,
    &_task
  );

  if (taskResult != pdPASS) {
    rmt_disable(_channel);
    rmt_del_channel(_channel);
    vQueueDelete(_queue);
    _channel = nullptr;
    _queue = nullptr;
    _task = nullptr;
    _status.store(Status::FAIL_ON_DRIVER);
    return false;
  }

  gpio_pullup_dis(gpio);
  gpio_pulldown_dis(gpio);
  gpio_set_direction(gpio, GPIO_MODE_INPUT_OUTPUT);
  gpio_set_intr_type(gpio, GPIO_INTR_DISABLE);
  gpio_set_level(gpio, 1);

  _gpio = gpio;
  _type = type;
  _status.store(Status::INITIALIZED);

  return true;
}

bool DHT::isPolling() const {
  return _polling.load();
}

bool DHT::poll() {
  if (isPolling()) {
    return false;
  }

  // check interval
  if ((millis() - _lastReadTime.load()) <= getMinReadInterval()) {
    return false;
  }

  // check status
  const Status status = _status.load();
  if (status == Status::NONE || status == Status::FAIL_ON_DRIVER ||
      status == Status::REQUESTING || status == Status::RECEIVING || status == Status::RECEIVED) {
    return false;
  }

  // set requesting status
  _status.store(Status::REQUESTING);
  _polling.store(true);

  if (xTaskNotifyGive(_task) != pdPASS) {
    _status.store(status);
    _polling.store(false);
    return false;
  }

  return true;
}

float DHT::getTemperature() const {
  const Status status = _status.load();
  if (status != Status::RECEIVED && status != Status::READED) {
    return NAN;
  }

  if (_type == Type::DHT11) {
    return static_cast<float>(_data[2]);

  } else if (_type == Type::DHT22) {
    float value = (((_data[2] & 0x7F) << 8) | _data[3]) * 0.1f;

    // check if negative temperature
    if (_data[2] & 0x80) {
      value = -value;
    }

    return value;

  } else {
    return NAN;
  }
}

float DHT::getHumidity() const {
  const Status status = _status.load();
  if (status != Status::RECEIVED && status != Status::READED) {
    return NAN;
  }

  if (_type == Type::DHT11) {
    return static_cast<float>(_data[0]);

  } else if (_type == Type::DHT22) {
    return ((_data[0] << 8) | _data[1]) * 0.1f;

  } else {
    return NAN;
  }
}

const char* DHT::statusToString(const Status status) {
  switch (status) {
    case Status::NONE:
      return "NONE";
      break;

    case Status::INITIALIZED:
      return "INITIALIZED";
      break;

    case Status::REQUESTING:
      return "REQUESTING";
      break;

    case Status::RECEIVING:
      return "RECEIVING";
      break;

    case Status::RECEIVED:
      return "RECEIVED";
      break;

    case Status::READED:
      return "READED";
      break;

    case Status::FAIL_ON_DRIVER:
      return "FAIL_ON_DRIVER";
      break;

    case Status::NACK:
      return "NACK";
      break;

    case Status::TIMEOUT:
      return "TIMEOUT";
      break;

    case Status::BAD_DATA:
      return "BAD_DATA";
      break;

    case Status::UNDERFLOW_DATA:
      return "UNDERFLOW_DATA";
      break;

    case Status::OVERFLOW_DATA:
      return "OVERFLOW_DATA";
      break;

    case Status::BAD_CHECKSUM:
      return "BAD_CHECKSUM";
      break;

    default:
      return "UNKNOWN";
      break;
  }
}

void DHT::_read(DHT* instance) {
  while (true) {
    // block and wait for notification
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    // reset data
    memset(instance->_data, 0, sizeof(instance->_data));

    // start receive
    rmt_symbol_word_t buffer[128];
    rmt_receive_config_t receiveConfig{};
    receiveConfig.signal_range_min_ns = 3000;
    receiveConfig.signal_range_max_ns = 21000000;
    rmt_receive(instance->_channel, buffer, sizeof(buffer), &receiveConfig);

    // wakeup
    gpio_set_level(instance->_gpio, 0);
    vTaskDelay(pdMS_TO_TICKS(instance->getWakeupDelay()));
    gpio_set_level(instance->_gpio, 1);

    // set receiving status
    instance->_status.store(Status::RECEIVING);

    // blocks until data is available or timeouts after 1s
    rmt_rx_done_event_data_t eData = {};
    if (xQueueReceive(instance->_queue, &eData, pdMS_TO_TICKS(1000)) == pdTRUE) {
      instance->_decode(eData.received_symbols, eData.num_symbols);

    } else {
      instance->_status.store(Status::TIMEOUT);
    }

    gpio_set_level(instance->_gpio, 1);

    // return results
    const Status status = instance->_status.load();
    if (status == Status::RECEIVED && instance->_onData) {
      instance->_onData(instance->getHumidity(), instance->getTemperature());

    } else if (status != Status::RECEIVED && instance->_onError) {
      instance->_onError(status);
    }

    instance->_lastReadTime.store(millis());
    if (status == Status::RECEIVED) {
      instance->_status.store(Status::READED);
    }
    instance->_polling.store(false);
  }
}

bool IRAM_ATTR DHT::_onReceiveDone(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *eData, void *uCtx) {
  BaseType_t wakeup = pdFALSE;
  DHT* instance = static_cast<DHT*>(uCtx);
  if (instance->_queue != nullptr) {
    xQueueSendFromISR(instance->_queue, eData, &wakeup);
  }

  return (wakeup == pdTRUE);
}

void DHT::_decode(const rmt_symbol_word_t* symbols, const size_t numSymbols) {
  /* Serial.printf("numSymbols: %zu\n", numSymbols);
  for (size_t i = 0; i < std::min(static_cast<size_t>(10), numSymbols); ++i) {
    Serial.printf(
      "Symbol %zu: level0=%u dur0=%u, level1=%u dur1=%u\n", i,
        symbols[i].level0,
        symbols[i].duration0,
        symbols[i].level1,
        symbols[i].duration1
    );
  } */

  // Check length
  if (numSymbols < 42) {
    _status.store(Status::UNDERFLOW_DATA);
    return;

  } else if (numSymbols > 43) {
    _status.store(Status::OVERFLOW_DATA);
    return;
  }

  // Ack
  auto ackDuration = symbols[1].duration0 + symbols[1].duration1;
  if (ackDuration < 130 || ackDuration > 180) {
    _status.store(Status::NACK);
    return;
  }

  // Data
  for (size_t i = 2; i < 42; ++i) {
    auto lowDuration = symbols[i].duration0;
    if (lowDuration < 40 || lowDuration > 60) {
      _status.store(Status::BAD_DATA);
      return;
    }

    auto highDuration = symbols[i].duration1;
    if (highDuration < 15 || highDuration > 90) {
      _status.store(Status::BAD_DATA);
      return;
    }

    size_t byteIndex = (i - 2) / 8;
    size_t bitIndex = 7 - ((i - 2) % 8); // MSB first
    if (highDuration > 40) {
      _data[byteIndex] |= (1 << bitIndex);
    }
  }

  // Checksum
  uint8_t checksum = _data[0] + _data[1] + _data[2] + _data[3];
  if (checksum != _data[4]) {
    _status.store(Status::BAD_CHECKSUM);
    return;
  }

  _status.store(Status::RECEIVED);
}
