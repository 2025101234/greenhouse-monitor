# D12X-DEMO68-V1.2 greenhouse application

This directory contains the RT-Thread application for the greenhouse monitor.

Confirmed board profile:

- PCB: ArtInChip `D12X-DEMO68-V1.2`;
- display/touch: enabled by the demo68 BSP; the application layout adapts to the active resolution;
- onboard buzzer: `BZ1`, driven by `PC7/PWM1_B` at 4 kHz;
- Wi-Fi: the schematic provides an optional `RTL8189FTV` footprint on
  `SDC0/PB6-PB11`; the supplied board photo shows that footprint unpopulated,
  so the current hardware has no usable onboard Wi-Fi module. Its power-enable
  signal would be `PD17` (`WIFI_ON/WL_REG_ON`).
- TF slot: `SDC1/PC0-PC6` (not used by this project);
- upgrade and console: onboard USB-to-UART0 USB-C port (the connector marked `USB TO UART0 UPGRADE`);
- sensors: DHT11 DATA uses J8 `PC.6`; BH1750 uses I2C1 (`TX1/PA.2=SCL`, `RX1/PA.3=SDA`). The driver wakes BH1750 before each measurement and accepts both common addresses `0x23` and `0x5c`.
- no ADC, soil sensor, or TF card is required.

Hardware references: [D12X-DEMO board documentation](https://aicdoc.artinchip.com/topics/product/d12x-demo-v1.html)
and the [official D12X-DEMO schematic PDF](https://aicdoc.artinchip.com/out/downloads/D12x-DEMO-V1-0.pdf).

Current safe defaults:

- real DHT11 acquisition is enabled; use simulation only when the sensor is disconnected;
- if BH1750 is disconnected, light is invalid and shown as `--`; temperature/humidity continue working;
- LVGL realtime values and a 60-point curve are enabled;
- the dashboard is now the final offline screen-only version: Wi-Fi and the HTTP
  server are disabled because the RTL8189FTV footprint on this board is empty;
- the official LVGL Meter demo is disabled so it cannot overwrite the greenhouse dashboard;
- the `SET` button opens touch-screen threshold controls; values are applied to the same data layer used by alarms and the HTTP API;
- `gh_network.c` is retained as optional source for a future board with a fitted
  Wi-Fi module, but `GH_ENABLE_HTTP_SERVER` is `0` in the current competition
  image and no Wi-Fi configuration is needed;
- the real onboard PWM buzzer is enabled;
- the current image does not include the HTTP server, so it starts without any
  Wi-Fi credentials or network dependency;
- do not enable `AIC_WLAN_REALTEK` blindly with `AIC_PRGB_24BIT`: the
  hardware schematic multiplexes `PD17` between `WIFI_ON` and LCD data. First
  verify the R53 strap and the panel's RGB mode, then enable the driver and
  set `AIC_DEV_REALTEK_WLAN0_PWR_GPIO="PD.17"` in menuconfig.
- For a board on which the RTL8189FTV module has actually been installed, the
  Wi-Fi menu is included in `target/d12x/demo68-nor/Kconfig.board`;
  run `scons --menuconfig`, open `WLAN Related -> Using Wireless Lan`, then
  `Using Realtek wlan driver -> rtl8189`.

Important switches in `gh_config.h`:

- `GH_USE_SIMULATED_SENSORS`: currently `0` for real DHT11; set to `1` for no-sensor simulation;
- `GH_ENABLE_LVGL_UI`: controls the dashboard;
- `GH_ENABLE_REAL_BUZZER`: is now `1` for the confirmed PC7/PWM1_B circuit.

Module ownership:

- `gh_board.*`: V1.2 board profile, hardware self-check and BZ1 control;
- `gh_sensors.*`: periodic DHT11 GPIO acquisition;
- `gh_data.*`: protected snapshot, thresholds and two message queues;
- `gh_alarm.*`: confirmation, hysteresis and alarm state;
- `gh_ui.*`: resolution-adaptive LVGL dashboard and realtime curve;
- the touch `SET` page edits the three high-alarm thresholds in 0.1 C / 0.1 % / lux units;
- thresholds are persisted to `/data/greenhouse_limits.bin` when the built-in LittleFS `/data` mount is available;
- `gh_network.*`: status JSON and lightweight HTTP server (`/`, `/api/status`, `/api/threshold`);
- `gh_console.c`: MSH diagnostic commands;
- `greenhouse_app.c`: RT-Thread component startup.

Useful MSH commands:

```text
gh_hwinfo
gh_buzzer_test
gh_buzzer_test 1000
gh_status
gh_dht11_test
gh_threshold
gh_threshold 350 850 80000
gh_alarm_test
gh_json
```

The current competition image is offline and does not require Wi-Fi setup.
Operate the dashboard and threshold page directly on the capacitive screen.
The optional HTTP source and Wi-Fi pin configuration are kept only as a future
extension if a populated RTL8189FTV module becomes available.

Build from `F:\matibei` with:

```bat
build_greenhouse.bat
```

Read `F:\matibei\项目代码框架与后续开发交接总结.md` before continuing hardware integration.
