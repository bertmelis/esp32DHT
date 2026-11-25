# esp32DHT

[![CI](https://github.com/bertmelis/esp32DHT/actions/workflows/ci.yaml/badge.svg)](https://github.com/bertmelis/esp32DHT/actions/workflows/ci.yaml)

This is a DHT11/22 library for ESP32 using the RMT peripheral, for use in the Arduino framework.  
For ESP8266, please look into this repo: [DHT](https://github.com/bertmelis/DHT)

The library is non blocking and is usable in async frameworks. Sensor communication runs in a separate FreeRTOS task, with results delivered through callbacks. The library limits polling frequency for each initialized object. You are still responsible for the sensor's wiring, power-up requirements and coordination between tasks. Supplementary functions like dew point calculation are not included.

## Compatibility

This branch uses the modern RMT RX API introduced in ESP-IDF 5. The included PlatformIO configuration targets pioarduino `61.04.00`, which provides Arduino-ESP32 4.0.0 / ESP-IDF 6.1.1. It does not provide a legacy driver implementation for Arduino-ESP32 2.x / ESP-IDF 4.

RMT memory allocation follows the target's hardware capabilities, including ESP32-C3 and ESP32-C6. The source retains conditional configuration for ESP-IDF 5 / Arduino-ESP32 3.x, but that combination is not covered by the current CI configuration. Successful build checks do not establish sensor operation on physical hardware.

## Installation

* For Arduino IDE: see [the Arduino Guide](https://www.arduino.cc/en/Guide/Libraries#toc4)
* For PlatformIO: see the [PlatformIO guide](https://docs.platformio.org/en/latest/projectconf/section_env_library.html). Select a platform package providing a compatible Arduino-ESP32 core.

## Usage

```C++
#include <Arduino.h>
#include <esp32DHT.h>

DHT sensor;
bool initialized = false;

void setup() {
  Serial.begin(115200);

  sensor.onData([](float humidity, float temperature) {
    Serial.printf("Temperature: %.1f C\nHumidity: %.1f%%\n", temperature, humidity);
  });
  sensor.onError([](DHT::Status status) {
    Serial.printf("Sensor error: %s\n", DHT::statusToString(status));
  });

  // Choose a suitable GPIO for your board. Use Type::DHT11 for a DHT11.
  initialized = sensor.setup(4, DHT::Type::DHT22);
  if (!initialized) {
    Serial.printf("Setup failed: %s\n", DHT::statusToString(sensor.getStatus()));
  }
}

void loop() {
  if (initialized) {
    sensor.poll();  // Returns immediately; results arrive through callbacks.
  }
  delay(50);
}
```

`setup(pin, type)` allocates an RMT RX channel automatically. There is no channel number argument. Other RMT users share the available hardware resources, so always check the return value of `setup()`.

### Polling and callbacks

`poll()` returns `true` when a new asynchronous request is accepted. It returns `false` if the object is not ready, a request is already in progress, or the minimum interval has not elapsed. An accepted request can still fail; use `onData()` for successful measurements and `onError()` for acquisition errors.

| Sensor type | Start signal delay | Minimum read interval |
| --- | --- | --- |
| `DHT::Type::DHT11` | 20 ms | 1000 ms |
| `DHT::Type::DHT22` | 2 ms | 2000 ms |

The interval is checked against the completion time of the previous request, after its callback returns. The first request is also delayed until the board's uptime exceeds the interval. `isPolling()` reports whether a request is in progress; `getLastReadTime()` reports its most recent completion time in milliseconds, including failed requests.

Callbacks run in the library's worker task, not in `loop()` or an interrupt handler. Keep them short and synchronize application data shared with other tasks. Do not call `reset()`, reconfigure the sensor, or destroy it from a callback. Request these actions from your application task after polling has finished. Serialize access to the object; atomic status fields do not make simultaneous calls to all methods safe.

`getStatus()` provides the current lifecycle state. The implementation finishes each request with `Status::READED`, including requests that failed, so this state is not proof of a valid measurement. Use the callback arguments as the source of successful readings and errors rather than assuming the getters contain a valid sample after every completed request.

### Reset and configuration

Before calling `setup()` again, wait for `isPolling()` to become `false` and call `reset()` from your application task. `reset()` releases the channel, task and queue, and clears the callbacks and polling timestamp. Register callbacks again when configuring the next sensor.

Polling intervals belong to the current object configuration. Resetting or creating another object does not preserve a physical sensor's last read time. When switching pins or reinitializing, keep track of each sensor's minimum interval in the application.

The worker task defaults can be overridden with global compiler definitions that also apply when compiling the library:

| Definition | Default |
| --- | --- |
| `DHT_TASK_NAME` | `"esp32DHT"` |
| `DHT_TASK_STACK_SIZE` | `2048` bytes |
| `DHT_TASK_PRIORITY` | `5` |

For example, add `-DDHT_TASK_STACK_SIZE=4096` to PlatformIO `build_flags`. Defining a macro only in a sketch does not configure the separately compiled library source.

## Migrating from the legacy API

This branch changes the public API. Existing sketches need the following updates:

| Legacy API | Current API |
| --- | --- |
| `DHT11 sensor;` / `DHT22 sensor;` | `DHT sensor;`, with the type passed to `setup()` |
| `sensor.setup(pin[, channel]);` | `sensor.setup(pin, DHT::Type::DHT22)` or `DHT::Type::DHT11`; check the returned `bool` |
| Explicit `RMT_CHANNEL_*` selection | Automatic RMT channel allocation |
| `sensor.read();` | `sensor.poll();`; check whether the request was accepted |
| `onError([](uint8_t error) { ... })` | `onError([](DHT::Status status) { ... })` |
| `sensor.getError()` | `DHT::statusToString(status)` using the status supplied to the error callback |
| `esp32DHTInternals::OnData_CB` / `OnError_CB` | `DHT::DataCallback` / `DHT::ErrorCallback` |

The data callback still receives humidity first and temperature second.

## Examples

* [DHT22](examples/DHT22/DHT22.ino): polls one sensor with `Ticker` and prints callbacks and lifecycle state changes.
* [DHT22_multiple](examples/DHT22_multiple/DHT22_multiple.ino): reuses one `DHT` object by resetting it and switching between two GPIOs sequentially. It does not demonstrate simultaneous polling of independent objects.

Change the GPIOs in the examples to match your board and wiring.

## Building and CI

`platformio.ini` shares the platform version and build settings across all environments. The default environment is `esp32-s3`; select `esp32`, `esp32-s2`, `esp32-c3` or `esp32-c6` to build for another target:

```sh
pio run --environment esp32-c3
```

Set `src_dir` in `platformio.ini` to choose a local example. CI uses the same configuration with the `ci` environment, selecting the board through `PIO_BOARD` and the example through `PLATFORMIO_SRC_DIR`. It builds both examples for ESP32, S2, S3, C3 and C6 and runs cpplint separately.

To select a board and example the same way as CI, run this command in a Bash shell:

```sh
PIO_BOARD=lolin_c3_mini PLATFORMIO_SRC_DIR="$PWD/examples/DHT22" pio run --environment ci
```

The workflow invokes PlatformIO and cpplint directly. To run the same lint check locally:

```sh
python -m pip install cpplint==2.0.2
python -m cpplint --repository=. --recursive --linelength=120 --verbose=4 --filter=-build/include,-build/header_guard,-runtime/int ./src
```

## Known issues

- A first read after powerup may report `UNDERFLOW_DATA`. Allow for the sensor's startup time and handle errors through `onError()`.
- Use the issue tracker if you encounter any other problems.

## History

Whatever can be done using hardware should not be done by software. ESP32 has a RMT peripheral device which is remarkably versatile. As the DHT sensors rely on tight timing, the RMT device is perfect to accomplish reliable communication. I didn't find any other Arduino library that uses the RMT and/or doesn't block during communication. So I created my own one.

> This library won't exist without the examples for RMT. Credits go to the team and contributors of ESP-IDF and @nkolban!
