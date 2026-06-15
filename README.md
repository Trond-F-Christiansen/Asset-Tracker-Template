# Asset Tracker Template

[![Release](https://img.shields.io/github/v/release/nrfconnect/Asset-Tracker-Template)](https://github.com/nrfconnect/Asset-Tracker-Template/releases)
[![Quality Gate](https://sonarcloud.io/api/project_badges/measure?project=nrfconnect-asset-tracker-template&metric=alert_status)](https://sonarcloud.io/dashboard?id=nrfconnect-asset-tracker-template)
[![Coverage](https://sonarcloud.io/api/project_badges/measure?project=nrfconnect-asset-tracker-template&metric=coverage)](https://sonarcloud.io/dashboard?id=nrfconnect-asset-tracker-template)
[![On-commit](https://img.shields.io/github/actions/workflow/status/nrfconnect/Asset-Tracker-Template/build-and-target-test.yml?event=push&branch=main&label=on-commit)](https://github.com/nrfconnect/Asset-Tracker-Template/actions/workflows/build-and-target-test.yml?query=branch%3Amain+event%3Apush)
[![Nightly](https://img.shields.io/github/actions/workflow/status/nrfconnect/Asset-Tracker-Template/build-and-target-test.yml?event=schedule&branch=main&label=nightly)](https://github.com/nrfconnect/Asset-Tracker-Template/actions/workflows/build-and-target-test.yml?query=branch%3Amain+event%3Aschedule)
[![PSM Current](https://img.shields.io/endpoint?url=https://nrfconnect.github.io/Asset-Tracker-Template/power_badge.json)](https://nrfconnect.github.io/Asset-Tracker-Template/power_measurements_plot.html)
[![RAM Usage thingy91x](https://img.shields.io/endpoint?url=https://nrfconnect.github.io/Asset-Tracker-Template/ram_badge.json)](https://nrfconnect.github.io/Asset-Tracker-Template/ram_memory_view.html)
[![FLASH Usage thingy91x](https://img.shields.io/endpoint?url=https://nrfconnect.github.io/Asset-Tracker-Template/flash_badge.json)](https://nrfconnect.github.io/Asset-Tracker-Template/flash_memory_view.html)

## Test modifications

The application have been modified to better satisfy the needs of the test.

- Littlefs storage backend speedup (merged into main branch)
- Storage + cloud module: Keep data in the buffer until it has been confirmed sent, fixing data loss
  on send failure. (merged into main branch)
- nrf_cloud_coap confirmable messages error handling bugfixes (in ncs, not yet pulled into main branch)
- Added `att_storage dump` shell command to dump all items in flash storage to the console (also
  already sent data)
  - Speed up nrf9151 <-> nrf5340 uart communication
- west manifest update to fork with coap bugfixes and uart speedup
- Disabled GNSS location
- Enabled Littlefs storage backend with buffer size of 20000 samples
- App modification to manual triggering of data send and keep the modem disconnected while not sending (cfun=2/cfun=4)

### How to run the modified test application

1. Checkout the branch `japan-test`
2. update west to pull in the ncs modifications (coap bugfixes and uart speedup)

    ```bash
    west update
    ```

3. Build and flash the connectivity bridge on the nrf5340

   ```bash
    cd nrf/applications/connectivity_bridge/
    west build -p -b thingy91x_nrf5340_cpuapp
    west flash
   ```

   **note**: to flash the nrf5340 you need to set the swd switch on the thingy91x to the nrf5340 position

4. Build and flash the application on the nrf9151

   ```bash
    cd ../../../project/app/
    west build -p -b thingy91x_nrf9151_ns
    west flash
   ```

The device will start up and start sampling at the sample interval set by `CONFIG_APP_SAMPLING_INTERVAL_SECONDS`. The modem will remain in airplane mode until you trigger a send. For every send the device will blink blue.

To trigger a send, press and hold the button on the thingy91x for 5 seconds until the LED starts blinking yellow. This indicates the device is attempting to connect to LTE network and send data. Once connection is established the LED will blink green while sending data. After the send is complete, the modem will return to airplane mode and the LED will turn off.

### Serial dump

If nessesary you can also dump the stored data to the console, a python script to parse the output and convert it to csv is available in `scripts/parse_storage_dump.py`.

```bash
python3 scripts/storage_dump.py --port /dev/tty.usbmodem1133302 --output dump.json --baud 1000000
```

## Overview

The Asset Tracker Template is a modular framework for developing IoT applications on nRF91-based devices.
It is built on the [nRF Connect SDK](https://www.nordicsemi.com/Products/Development-software/nRF-Connect-SDK) and [Zephyr RTOS](https://docs.zephyrproject.org/latest/), and provides a modular, event-driven architecture suitable for battery-powered IoT use cases.
The framework supports features such as cloud connectivity, location tracking, and sensor data collection.

The system is organized into modules, each responsible for a specific functionality, such as managing network connectivity, handling cloud communication, or collecting environmental data.
Modules communicate through [zbus](https://docs.zephyrproject.org/latest/services/zbus/index.html) channels, ensuring loose coupling and maintainability.

**Supported hardware**:

* [Thingy:91 X](https://www.nordicsemi.com/Products/Development-hardware/Nordic-Thingy-91-X)
* [nRF9151 DK](https://www.nordicsemi.com/Products/Development-hardware/nRF9151-DK)

If you are new to nRF91 series and cellular IoT, consider taking the [Nordic Developer Academy Cellular Fundamentals Course](https://academy.nordicsemi.com/courses/cellular-iot-fundamentals).

<p align="center">
  <img src="docs/images/att-map.png" alt="nRF Cloud - Asset tracking map view" width="800" />
  <br>
  <em>Thingy:91 X reporting its location to nRF Cloud running the Asset Tracker Template</em>
</p>

---

## Get started

To set up your development environment, build the application, flash it to your device, and connect it to [nRF Cloud](https://nrfcloud.com), follow the [Getting Started](docs/common/getting_started.md) guide.

---

## Documentation

<table>
  <tr>
    <td><a href="docs/common/getting_started.md">Getting Started</a></td>
    <td><a href="docs/common/architecture.md">Architecture</a></td>
    <td><a href="docs/common/configuration.md">Configuration</a></td>
  </tr>
  <tr>
    <td><a href="docs/common/modifying.md">Modifying</a></td>
    <td><a href="docs/modules/overview_modules.md">Modules</a></td>
    <td><a href="docs/common/connecting.md">Connecting</a></td>
  </tr>
  <tr>
    <td><a href="docs/common/location_services.md">Location Services</a></td>
    <td><a href="docs/common/low_power.md">Achieving Low Power</a></td>
    <td><a href="docs/common/fota.md">Firmware Updates (FOTA)</a></td>
  </tr>
  <tr>
    <td><a href="docs/common/test_and_ci_setup.md">Testing and CI Setup</a></td>
    <td><a href="docs/common/tooling_troubleshooting.md">Tooling and Troubleshooting</a></td>
    <td><a href="docs/common/known_issues.md">Known Issues</a></td>
  </tr>
  <tr>
    <td><a href="docs/common/release.md">Release Artifacts</a></td>
    <td><a href="docs/common/release_notes.md">Release Notes</a></td>
    <td></td>
  </tr>
</table>

---

## System Overview

![System overview](docs/images/system_overview.svg)

Core modules include:

* **[Main](docs/modules/main.md)**: Central coordinator implementing business logic
* **[Storage](docs/modules/storage.md)**: Data collection and buffering management
* **[Network](docs/modules/network.md)**: LTE connectivity management
* **[Cloud](docs/modules/cloud.md)**: nRF Cloud CoAP communication
* **[Location](docs/modules/location.md)**: GNSS, Wi-Fi, and cellular positioning
* **[LED](docs/modules/led.md)**: RGB LED control for Thingy:91 X
* **[Button](docs/modules/button.md)**: User input handling
* **[FOTA](docs/modules/fota_module.md)**: Firmware over-the-air updates
* **[Environmental](docs/modules/environmental.md)**: Sensor data collection
* **[Power](docs/modules/power.md)**: Battery monitoring and power management

### Key Features

* **State Machine Framework (SMF)**: Predictable behavior with run-to-completion model
* **Message-Based Communication**: Loose coupling via [zbus](https://docs.nordicsemi.com/bundle/ncs-latest/page/zephyr/services/zbus/index.html) channels
* **Modular Architecture**: Separation of concerns with dedicated threads for blocking operations
* **Power Optimization**: LTE PSM enabled by default with configurable power-saving features
