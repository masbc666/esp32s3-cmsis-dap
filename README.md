# ESP32-S3 CMSIS-DAP v2 调试探针

将 ESP32-S3 做成 USB CMSIS-DAP v2 调试/下载器，用于 ARM Cortex-M 目标芯片。

已在 Keil MDK + STM32F103C8 上验证通过。

## 功能状态

| 功能 | 状态 |
|------|------|
| SWD (串行线调试) | 已启用，默认端口 |
| JTAG | 已启用（nTRST 未接线） |
| 目标 nRESET 复位 | GPIO10，可用 — `Reset and Run` 正常 |
| CDC USB-UART 桥 | 可用，GPIO43/44（原为测试用途） |
| SWO | 未实现 |
| CMSIS-DAP UART 协议 | 已禁用 |

基于 [CherryDAP](https://github.com/EZ32Inc/esp32jtag_firmware)（CherryUSB + ARM DAPLink）。

## 引脚定义

### 调试接口 (SWD / JTAG)

| ESP32-S3 | 目标板 | 信号 |
|----------|--------|------|
| GPIO47 | SWCLK / TCK | 时钟 |
| GPIO41 | SWDIO / TMS | 数据 |
| GPIO40 | TDI | 仅 JTAG |
| GPIO15 | TDO | 仅 JTAG |
| GPIO45 | — | SWDIO 方向控制（可选） |
| GPIO10 | NRST | 目标复位 |
| GND | GND | 地 |

### CDC UART（测试用）

| ESP32-S3 | 目标板 |
|----------|--------|
| GPIO43 | TX |
| GPIO44 | RX |

### 板载

| ESP32-S3 | 功能 |
|----------|------|
| GPIO48 | WS2812 状态灯 |

修改引脚：`components/CherryDAP/projects/esp32s3/main/port_common.h`

## 编译

需要 ESP-IDF v5.5+（已验证 v5.5.4）。

```powershell
. /path/to/esp-idf/export.ps1
idf.py set-target esp32s3
idf.py build
```

## 烧录

```powershell
idf.py -p COMx flash
```

## 使用

1. 按上表接线，将 ESP32-S3 连接到目标板
2. ESP32-S3 USB 插到电脑
3. Keil MDK 中会识别到 **CMSIS-DAP v2**，在 Debug 设置中选择即可

如果烧录后目标板程序不自动运行，检查 CubeMX 中 SYS Debug 是否设为 `Serial Wire`（而非 `No Debug`）。

## 疑难解答

见 [CMSIS_DAP_TROUBLESHOOTING.md](CMSIS_DAP_TROUBLESHOOTING.md)。

## 目录结构

```
├── main/                   # app_main()、FreeRTOS 任务
├── components/CherryDAP/   # CMSIS-DAP + CherryUSB + CherryRB
│   ├── dap_main.c/h        # DAP 命令处理
│   ├── DAP/                # SWD/JTAG 协议引擎
│   ├── CherryUSB/          # USB 设备栈
│   └── projects/esp32s3/   # ESP32-S3 平台适配（引脚、usb2uart）
├── sdkconfig.defaults
└── CMakeLists.txt
```

## 许可证

Apache 2.0 — 见 `components/CherryDAP/LICENSE`。
