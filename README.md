# ESP32-S3 CMSIS-DAP v2 Debug Probe

Turn an ESP32-S3 into a USB CMSIS-DAP v2 debug probe for ARM Cortex-M targets.

Tested with Keil MDK + STM32F103C8.

## What Works

| Feature | Status |
|---------|--------|
| SWD (Serial Wire Debug) | Enabled, default port |
| JTAG | Enabled (nTRST not wired) |
| Target nRESET | GPIO10, functional — `Reset and Run` works |
| CDC USB-UART bridge | Available on GPIO43/44 (originally for testing) |
| SWO | Not implemented |
| CMSIS-DAP UART protocol | Disabled |

Based on [CherryDAP](https://github.com/EZ32Inc/esp32jtag_firmware) (CherryUSB + ARM DAPLink).

## Pins

### Debug (SWD / JTAG)

| ESP32-S3 | Target | Signal |
|----------|--------|--------|
| GPIO47 | SWCLK / TCK | Clock |
| GPIO41 | SWDIO / TMS | Data |
| GPIO40 | TDI | JTAG only |
| GPIO15 | TDO | JTAG only |
| GPIO45 | — | SWDIO direction (optional) |
| GPIO10 | NRST | Target reset |
| GND | GND | Ground |

### CDC UART (testing)

| ESP32-S3 | Target |
|----------|--------|
| GPIO43 | TX |
| GPIO44 | RX |

### Onboard

| ESP32-S3 | Function |
|----------|----------|
| GPIO48 | WS2812 status LED |

To change pins: `components/CherryDAP/projects/esp32s3/main/port_common.h`

## Build

ESP-IDF v5.5+ required (tested v5.5.4).

```powershell
. /path/to/esp-idf/export.ps1
idf.py set-target esp32s3
idf.py build
```

## Flash

```powershell
idf.py -p COMx flash
```

## Usage

1. Wire ESP32-S3 to target as above
2. Plug ESP32-S3 USB into PC
3. Keil MDK will detect **CMSIS-DAP v2** — select it in Debug settings

If target won't run after flash, ensure CubeMX SYS Debug is `Serial Wire` (not `No Debug`).

## Troubleshooting

See [CMSIS_DAP_TROUBLESHOOTING.md](CMSIS_DAP_TROUBLESHOOTING.md).

## Structure

```
├── main/                   # app_main(), FreeRTOS tasks
├── components/CherryDAP/   # CMSIS-DAP + CherryUSB + CherryRB
│   ├── dap_main.c/h        # DAP command handler
│   ├── DAP/                # SWD/JTAG protocol engine
│   ├── CherryUSB/          # USB device stack
│   └── projects/esp32s3/   # ESP32-S3 platform (pins, usb2uart)
├── sdkconfig.defaults
└── CMakeLists.txt
```

## License

Apache 2.0 — see `components/CherryDAP/LICENSE`.
