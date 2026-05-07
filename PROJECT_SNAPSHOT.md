# S3CMSISDAP 项目快照

日期：2026-05-02

## 项目目标

本项目把 ESP32-S3 做成一个 USB CMSIS-DAP v2 调试/下载器，用于在 Keil uVision 等工具中给 STM32 目标板烧录和调试。

当前实现已经放弃早期反复大改的自研 TinyUSB/CMSIS-DAP/SWD 方案，改为参考并移植 CherryDAP/CherryUSB 方案：

https://github.com/EZ32Inc/esp32jtag_firmware

## 当前状态

- Keil 可以正常识别为 CMSIS-DAP v2。
- Keil 可以正常给 STM32F103C8 测试工程下载程序。
- 已实现目标板 `NRST` 复位支持，Keil 的 `Reset and Run` 可以实际拉低目标板复位脚。
- 主程序已拆成两个 FreeRTOS 任务：
  - `CMSIS_DAP_TASK`：负责 CMSIS-DAP、USB CDC UART 的持续处理。
  - `DISPLAY_TASK`：负责板载 WS2812 状态灯，检测到 DAP 活动时变色。
- 最近一次成功构建产物：
  - `build/S3CMSISDAP.bin`
  - APP 大小：`0x41f00`
  - 最小 APP 分区：`0x100000`

## 工作区结构

- `main/`
  - ESP-IDF 应用入口。
  - `main.c` 负责初始化 CherryDAP、创建 CMSIS-DAP 任务和 WS2812 显示任务。
- `components/CherryDAP/`
  - 从参考项目移植进来的本地 CherryDAP 组件。
  - 包含 CherryUSB、CMSIS-DAP 命令处理、SWD/JTAG 逻辑、ESP32-S3 平台适配代码。
- `build/`
  - ESP-IDF 构建输出目录。
- `managed_components/`
  - 当前没有额外 managed component 依赖。

## 关键文件

- `main/main.c`
  - 调用 `uartx_preinit()` 初始化 CDC UART 桥。
  - 调用 `chry_dap_init(0, ESP_USBD_BASE)` 初始化 CherryDAP USB 设备。
  - 创建 `CMSIS_DAP_TASK` 和 `DISPLAY_TASK`。
  - 板载 WS2812 默认使用 `GPIO48`，宏为 `BOARD_WS2812_GPIO`。

- `main/CMakeLists.txt`
  - 主组件依赖：
    - `CherryDAP`
    - `esp_driver_gpio`
    - `esp_driver_rmt`

- `components/CherryDAP/dap_main.c`
  - `chry_dap_handle()` 负责处理 DAP 请求。
  - 已增加 `g_chry_dap_last_activity_tick`，每次执行 DAP 命令时更新，用于状态灯判断“最近是否有烧录/调试活动”。

- `components/CherryDAP/dap_main.h`
  - 导出 `g_chry_dap_last_activity_tick` 给 `DISPLAY_TASK` 使用。

- `components/CherryDAP/projects/esp32s3/main/port_common.h`
  - ESP32-S3 到目标 MCU 的引脚定义。
  - 已增加目标复位引脚 `PIN_nRESET`，当前默认 `GPIO10`。

- `components/CherryDAP/projects/esp32s3/main/DAP_config.h`
  - CMSIS-DAP 硬件抽象层。
  - 已实现 `PIN_nRESET_OUT()` 和 `RESET_TARGET()`。

## 当前引脚定义

ESP32-S3 到目标 MCU：

- `GPIO47` -> SWCLK / TCK
- `GPIO41` -> SWDIO / TMS
- `GPIO40` -> TDI
- `GPIO15` -> TDO
- `GPIO45` -> SWDIO 方向控制，如果硬件使用了外部方向控制电路
- `GPIO10` -> 目标板 `NRST`
- `GND` -> 目标板 `GND`

可选 CDC UART：

- `GPIO43` -> UART TX
- `GPIO44` -> UART RX

板载 WS2812：

- 默认 `GPIO48`
- 如果实际板子的板载灯不是这个脚，修改 `main/main.c` 中的：

```c
#define BOARD_WS2812_GPIO GPIO_NUM_48
```

目标板复位脚如果不是 `GPIO10`，修改：

```c
#define PIN_nRESET GPIO_NUM_10
```

文件位置：

`components/CherryDAP/projects/esp32s3/main/port_common.h`

## 任务设计

`CMSIS_DAP_TASK`：

- 优先级：`10`
- 栈大小：`4096`
- 循环调用：
  - `uart_event_task(NULL)`
  - `chry_dap_handle()`
  - `chry_dap_usb2uart_handle()`
- 每轮 `vTaskDelay(1ms)`，避免一直占满 CPU。

`DISPLAY_TASK`：

- 优先级：`2`
- 栈大小：`3072`
- 使用 ESP-IDF RMT 驱动 WS2812，不依赖额外 `led_strip` managed component。
- 空闲时显示很暗的蓝色。
- 最近 500ms 内有 DAP 命令活动时，按红、橙、绿、蓝循环变色。

注意：当前状态灯判断的是“DAP 命令活动”，不是严格区分“烧录”和“调试”。Keil 烧录期间会持续发送 DAP 命令，因此可以作为烧录活动指示；如果后续进入调试态，调试访问也会触发变色。

## 复位问题说明

之前观察到：

- Keil 下载成功。
- STM32 的闪灯测试程序不会自动运行。
- 手动按目标板 `RST` 后程序才开始运行。

根本原因：

- CherryDAP 中目标复位相关钩子原先没有实际控制 `NRST`。
- Keil 的 `Reset and Run` 依赖 CMSIS-DAP 固件真正支持目标 `nRESET`。
- 当固件没有实际复位目标板时，Keil 可以完成烧录，但目标 MCU 不一定会从新程序重新启动。

现在实现方式：

- `PIN_nRESET_OUT(0)`：拉低目标板 `NRST`。
- `PIN_nRESET_OUT(1)`：把 ESP32 复位脚切回输入上拉，等效开漏释放。
- `RESET_TARGET()`：拉低 20ms，再释放 20ms，并返回 `1`。

## 构建环境

已验证构建环境：

- ESP-IDF：`v5.5.4`
- 目标芯片：`esp32s3`
- ESP-IDF 路径：

`D:\espidftoolchain\Espressif\frameworks\esp-idf-v5.5.4`

成功构建命令：

```powershell
$env:PATH='D:\espidftoolchain\Espressif\tools\idf-python\3.11.2;'+$env:PATH
& 'D:\espidftoolchain\Espressif\frameworks\esp-idf-v5.5.4\export.ps1'
idf.py build
```

烧录命令：

```powershell
idf.py -p <端口号> flash
```

或使用 ESP-IDF 输出的 esptool 命令：

```powershell
python -m esptool --chip esp32s3 -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 2MB --flash_freq 80m 0x0 build\bootloader\bootloader.bin 0x8000 build\partition_table\partition-table.bin 0x10000 build\S3CMSISDAP.bin
```

## Keil 测试工程

STM32 测试工程路径：

`D:\CLionSTM32\espcmsistest`

当前观察到的测试目标：

- STM32F103C8
- 闪灯代码通过翻转 `GPIOC PIN13` 实现。

需要注意：

- 该 CubeMX 工程当前配置为 `SYS = No_Debug`。
- 因此生成了 `__HAL_AFIO_REMAP_SWJ_DISABLE();`。
- STM32 程序一启动就会关闭 SWD/JTAG。
- 这不影响单纯跑灯，但会让后续调试很难重新连接。
- 如果需要正常调试，建议把 CubeMX 中的 SYS Debug 改成 `Serial Wire`。

## 已知风险与后续事项

- 需要在真实硬件上确认板载 WS2812 是否确实接在 `GPIO48`。
- 需要确认 `GPIO10` 是否已经正确连接到目标板 `NRST`。
- 需要确认硬件是否真的使用了 `GPIO45` 作为 SWDIO 方向控制；如果没有外部方向控制电路，后续可以考虑关闭或简化 `DAP_USE_SWDIO_DIR_PIN`。
- 当前 CherryDAP 组件内容比“最小 CMSIS-DAP 下载器”需求更大，能工作但后续可以继续裁剪。
- 当前项目目录还不是 git 仓库，交接后建议补上版本管理。

## 交接总结

当前项目已经切换为基于 CherryDAP 的 ESP32-S3 CMSIS-DAP v2 固件。Keil 下载功能已经正常，目标复位支持已经补齐，`Reset and Run` 不再需要手动按目标板复位键。主程序现在分成烧录机任务和 WS2812 显示任务，烧录/调试活动期间板载灯会变色，便于现场观察固件是否正在被 Keil 调用。
