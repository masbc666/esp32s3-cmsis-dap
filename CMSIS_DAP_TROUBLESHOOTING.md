# ESP32-S3 CMSIS-DAP 仿真检修记录

本文记录当前这版 ESP32-S3 模拟 CMSIS-DAP v2 固件的核心工作点、容易出错的位置、Windows/Keil 识别链路和下次故障时的检修流程。

当前工程路径：

```text
D:\ESP32IDF\S3CMSISDAP
```

当前固件目标：

- ESP32-S3 作为 USB CMSIS-DAP v2 调试器。
- USB interface 0 是 CMSIS-DAP WinUSB bulk interface。
- USB interface 1 是 CDC ACM 串口桥。
- CMSIS-DAP 轮询任务固定到 FreeRTOS core 0。
- 已删除板载 WS2812 状态灯代码和整个显示任务。

## 当前关键结论

这版固件的 Keil 识别核心不只是设备管理器显示 `ESP32JTAG CMSIS-DAP`，而是必须同时满足下面几件事：

1. Windows 能枚举到 USB 复合设备。
2. `MI_00` 必须绑定到 `WINUSB` 服务。
3. `MI_00` 必须有 CMSIS-DAP v2 使用的 `DeviceInterfaceGUIDs`。
4. Keil 能通过 WinUSB 打开 `MI_00`，向 bulk OUT 端点发送 `DAP_Info` 等 CMSIS-DAP 命令。
5. 固件里的 `CMSIS_DAP_TASK` 必须持续调用 `chry_dap_handle()`，把请求交给 `DAP_ExecuteCommand()` 并从 bulk IN 端点返回响应。

如果设备管理器能看到 `ESP32JTAG CMSIS-DAP`，但 Keil 提示没有找到 CMSIS-DAP，通常不要先怀疑 USB 线或者设备名。更常见的原因是 Windows 只绑定了 WinUSB，却没有写入 Keil 枚举需要的接口 GUID，或者固件任务没有及时响应 DAP 命令。

## 入口任务结构

主入口文件：

```text
main/main.c
```

当前入口流程：

```c
void app_main(void)
{
    ESP_LOGI(TAG, "starting ESP32 CMSIS-DAP v2");

    uartx_preinit();
    chry_dap_init(0, ESP_USBD_BASE);

    ESP_LOGI(TAG, "ESP32 CMSIS-DAP v2 is ready");

    if (xTaskCreatePinnedToCore(CMSIS_DAP_TASK, "cmsis_dap", 4096, NULL, 10, &CMSIS_DAP_TASK_HANDLE, 0) != pdPASS) {
        ESP_LOGE(TAG, "failed to create CMSIS-DAP task");
        return;
    }

    vTaskDelete(NULL);
}
```

当前任务轮询：

```c
static void CMSIS_DAP_TASK(void *arg)
{
    (void) arg;

    while (true) {
        uart_event_task(NULL);
        chry_dap_handle();
        chry_dap_usb2uart_handle();
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
```

重点：

- `xTaskCreatePinnedToCore(..., 0)` 把 CMSIS-DAP 轮询固定在 core 0。
- 任务优先级是 `10`。
- 堆栈是 `4096`。
- 第 4 个参数传 `NULL`，所以任务函数中的 `void *arg` 当前没有实际数据。
- `(void) arg;` 只是告诉编译器这个参数故意不用。
- 每轮调用 `vTaskDelay(pdMS_TO_TICKS(1))`，避免完全占满 CPU。
- `uart_event_task(NULL)` 当前不是独立 FreeRTOS task，只是在 CMSIS-DAP 任务里被轮询调用。

## 已删除 WS2812 逻辑

当前版本已经从 `main/main.c` 删除：

- `BOARD_WS2812_GPIO`
- `WS2812_*` 宏
- RMT channel 和 encoder
- `ws2812_init()`
- `ws2812_set_rgb()`
- `DISPLAY_TASK`
- `DISPLAY_TASK_HANDLE`
- 显示任务的 `xTaskCreate()`

`main/CMakeLists.txt` 也已经精简：

```cmake
idf_component_register(
    SRCS
        "main.c"
    INCLUDE_DIRS "."
    REQUIRES CherryDAP
)
```

注意：构建输出里仍可能看到 `esp_driver_rmt` 组件被 IDF 构建出来，这是因为 IDF 或其他组件依赖链会带进来，不代表 `main` 里还使用 WS2812/RMT。

检索确认命令：

```powershell
rg -n "WS2812|ws2812|DISPLAY_TASK|BOARD_WS2812|rmt_|esp_driver_rmt|xTaskCreate\(" main
```

正常情况下只应该看到 `xTaskCreatePinnedToCore`，不应该再看到显示任务或 WS2812 代码。

## USB 设备整体结构

核心文件：

```text
components/CherryDAP/dap_main.h
components/CherryDAP/dap_main.c
components/CherryDAP/DAP/Source/DAP.c
components/CherryDAP/projects/esp32s3/main/DAP_config.h
components/CherryDAP/projects/esp32s3/main/usb2uart.c
```

USB VID/PID：

```c
#define USBD_VID 0x0D28
#define USBD_PID 0x0204
```

当前端点分配：

```c
#define DAP_IN_EP  0x81
#define DAP_OUT_EP 0x02

#define CDC_IN_EP  0x83
#define CDC_OUT_EP 0x04
#define CDC_INT_EP 0x85
```

当前 USB interface 分配：

- Interface 0：CMSIS-DAP v2，vendor class，bulk OUT + bulk IN。
- Interface 1/2：CDC ACM 串口。
- MSC 默认关闭，除非定义 `CONFIG_CHERRYDAP_USE_MSC`。

配置描述符里的 CMSIS-DAP interface：

```c
USB_INTERFACE_DESCRIPTOR_INIT(0x00, 0x00, 0x02, 0xFF, 0x00, 0x00, 0x04)
```

含义：

- `bInterfaceNumber = 0x00`
- `bNumEndpoints = 0x02`
- `bInterfaceClass = 0xFF`，vendor specific
- `iInterface = 0x04`，对应字符串 `"CMSIS-DAP v2"`

这个 `iInterface = 0x04` 很重要。Keil 和 Windows 工具显示 interface 时会更明确，而不是只看到产品名。

## 描述符字符串

当前字符串表：

```c
char *string_descriptors[] = {
        (char[]) {0x09, 0x04},             /* Langid */
        "EZ32 Inc",                        /* Manufacturer */
        "ESP32JTAG CMSIS-DAP",             /* Product */
        "00000000000000000000000001400002", /* Serial Number */
        "CMSIS-DAP v2",                    /* Interface */
};
```

重点：

- Product 字符串是 `ESP32JTAG CMSIS-DAP`。
- Interface 字符串是 `CMSIS-DAP v2`。
- Serial Number 当前末尾是 `01400002`。

为什么改序列号：

Windows 会缓存 USB 设备的枚举结果、驱动绑定、MS OS 扩展属性。之前序列号是：

```text
00000000000000000000000001400001
```

当 MS OS 2.0 描述符修正后，如果序列号不变，Windows 可能继续沿用旧注册表实例，导致新描述符不被重新读取。把序列号改成：

```text
00000000000000000000000001400002
```

可以强制 Windows 把它当成新设备实例重新枚举。

## bcdDevice 版本

当前 device descriptor：

```c
USB_DEVICE_DESCRIPTOR_INIT(USB_2_1, 0xEF, 0x02, 0x01, USBD_VID, USBD_PID, 0x0101, 0x01)
```

重点：

- `USB_2_1`：必须保留，用于 BOS/MS OS 2.0 能力描述符。
- device class/subclass/protocol：`0xEF, 0x02, 0x01`，表示复合设备。
- `bcdDevice = 0x0101`。

之前是 `0x0100`。修改为 `0x0101` 也是为了让 Windows 重新认识这一版设备。

## WinUSB 和 MS OS 2.0 描述符

Keil 识别 CMSIS-DAP v2 的关键是 WinUSB interface。当前使用 Microsoft OS 2.0 描述符让 Windows 自动给 `MI_00` 绑定 WinUSB，并写入接口 GUID。

核心开关：

```c
#define USBD_WINUSB_VENDOR_CODE 0x20

#define USBD_WEBUSB_ENABLE 0
#define USBD_BULK_ENABLE   1
#define USBD_WINUSB_ENABLE 1
```

当前 MS OS 2.0 长度宏：

```c
#define WINUSB_DESCRIPTOR_SET_HEADER_SIZE  10
#define WINUSB_CONFIGURATION_SUBSET_HEADER_SIZE 8
#define WINUSB_FUNCTION_SUBSET_HEADER_SIZE 8
#define WINUSB_FEATURE_COMPATIBLE_ID_SIZE  20

#define FUNCTION_SUBSET_LEN                (WINUSB_FUNCTION_SUBSET_HEADER_SIZE + WINUSB_FEATURE_COMPATIBLE_ID_SIZE + DEVICE_INTERFACE_GUIDS_FEATURE_LEN)
#define CONFIGURATION_SUBSET_LEN           (WINUSB_CONFIGURATION_SUBSET_HEADER_SIZE + USBD_WEBUSB_ENABLE * FUNCTION_SUBSET_LEN + USBD_BULK_ENABLE * FUNCTION_SUBSET_LEN)
#define DEVICE_INTERFACE_GUIDS_FEATURE_LEN 132

#define USBD_WINUSB_DESC_SET_LEN (WINUSB_DESCRIPTOR_SET_HEADER_SIZE + CONFIGURATION_SUBSET_LEN)
```

这版修正的重点是加入了 configuration subset：

```c
WBVAL(WINUSB_CONFIGURATION_SUBSET_HEADER_SIZE), /* wLength */
WBVAL(WINUSB_SUBSET_HEADER_CONFIGURATION_TYPE), /* wDescriptorType */
0,                                               /* bConfigurationValue */
0,                                               /* bReserved */
WBVAL(CONFIGURATION_SUBSET_LEN),                 /* wTotalLength */
```

没有这个 configuration subset 时，Windows 可能仍能识别 `USB\MS_COMP_WINUSB` 并绑定 WinUSB，但不会正确写入 `DeviceInterfaceGUIDs`。这样设备管理器会看到 `ESP32JTAG CMSIS-DAP`，Keil 却可能报告找不到 CMSIS-DAP。

当前 CMSIS-DAP WinUSB GUID：

```text
{CDB3B5AD-293B-4663-AA36-1AAE46463776}
```

它在描述符中以 UTF-16LE 形式写入 `DeviceInterfaceGUIDs` 属性。

## BOS 描述符

当前 BOS 中启用了 WinUSB platform capability：

```c
USBD_NUM_DEV_CAPABILITIES
USBD_WINUSB_DESC_LEN
USBD_WINUSB_VENDOR_CODE
USBD_WINUSB_DESC_SET_LEN
```

Windows 会通过 BOS 能力描述符知道设备支持 MS OS 2.0，然后用 vendor request `0x20` 请求 MS OS 2.0 descriptor set。

如果 Keil 找不到，而 Windows 设备管理器看到设备，应该优先检查：

- BOS 是否返回成功。
- MS OS 2.0 descriptor set 长度是否正确。
- 是否有 configuration subset。
- 是否有 function subset，且 `bFirstInterface = 0`。
- `DeviceInterfaceGUIDs` 是否最终写入注册表。

## CMSIS-DAP 数据处理链路

初始化：

```c
chry_dap_init(0, ESP_USBD_BASE);
```

里面做的事情：

1. 初始化 UART/USB 环形缓冲。
2. 调用 `DAP_Setup()` 初始化 CMSIS-DAP 硬件抽象层。
3. 注册 USB descriptor。
4. 添加 CMSIS-DAP WinUSB interface 和 DAP bulk endpoints。
5. 添加 CDC ACM interface 和 CDC endpoints。
6. 调用 `usbd_initialize()` 初始化 CherryUSB DWC2 device。

USB 配置完成后：

```c
case USBD_EVENT_CONFIGURED:
    USB_RequestIdle = 0U;
    usbd_ep_start_read(0, DAP_OUT_EP, USB_Request[0], DAP_PACKET_SIZE);
    usbd_ep_start_read(0, CDC_OUT_EP, usb_tmpbuffer, DAP_PACKET_SIZE);
```

收到 DAP OUT 包：

```c
void dap_out_callback(uint8_t busid, uint8_t ep, uint32_t nbytes)
```

它会：

- 判断是否是 `ID_DAP_TransferAbort`。
- 否则推进 request ring buffer 写索引。
- 继续启动下一次 OUT endpoint 读取，除非请求队列满。

主任务处理请求：

```c
void chry_dap_handle(void)
```

核心执行：

```c
USB_RespSize[USB_ResponseIndexI] =
        (uint16_t) DAP_ExecuteCommand(USB_Request[USB_RequestIndexO], USB_Response[USB_ResponseIndexI]);
```

然后：

- 更新 request 队列读索引。
- 更新 response 队列写索引。
- 如果 IN endpoint 空闲，则 `usbd_ep_start_write()` 发送响应。

这就是 Keil 发 `DAP_Info`、`DAP_Connect`、`DAP_Transfer` 等命令后能收到响应的关键链路。

## DAP packet 参数

当前实际使用的 DAP_config 来自：

```text
components/CherryDAP/projects/esp32s3/main/DAP_config.h
```

因为 `components/CherryDAP/CMakeLists.txt` 中 include 路径包含：

```cmake
"DAP/Include"
"projects/esp32s3/main"
```

当前 `projects/esp32s3/main/DAP_config.h` 中：

```c
#define DAP_PACKET_SIZE 64U
#define DAP_PACKET_COUNT 1U
```

`dap_main.h` 中有编译期检查：

```c
#ifdef CONFIG_USB_HS
#if DAP_PACKET_SIZE != 512
#error "DAP_PACKET_SIZE must be 512 in hs"
#endif
#else
#if DAP_PACKET_SIZE != 64
#error "DAP_PACKET_SIZE must be 64 in fs"
#endif
#endif
```

当前是 Full Speed bulk，`DAP_PACKET_SIZE` 必须是 64。

## Windows 侧正确状态

连接并枚举后，正常应该看到：

- `USB Composite Device`
- `ESP32JTAG CMSIS-DAP`
- `USB 串行设备 (COMx)`

查询命令，需要管理员 PowerShell：

```powershell
Get-PnpDevice -PresentOnly | Where-Object {
    $_.FriendlyName -like '*CMSIS*' -or
    $_.FriendlyName -like '*DAP*' -or
    $_.InstanceId -like '*VID_0D28*PID_0204*'
} | Format-List -Property Class,FriendlyName,InstanceId,Status
```

正常示例：

```text
Class        : USB
FriendlyName : USB Composite Device
InstanceId   : USB\VID_0D28&PID_0204\00000000000000000000000001400002
Status       : OK

Class        : USBDevice
FriendlyName : ESP32JTAG CMSIS-DAP
InstanceId   : USB\VID_0D28&PID_0204&MI_00\...
Status       : OK

Class        : Ports
FriendlyName : USB 串行设备 (COMx)
InstanceId   : USB\VID_0D28&PID_0204&MI_01\...
Status       : OK
```

`MI_00` 必须是 CMSIS-DAP WinUSB：

```powershell
Get-PnpDeviceProperty -InstanceId 'USB\VID_0D28&PID_0204&MI_00\你的实例ID' |
Where-Object {
    $_.KeyName -in @(
        'DEVPKEY_Device_Service',
        'DEVPKEY_Device_Class',
        'DEVPKEY_Device_ClassGuid',
        'DEVPKEY_Device_Manufacturer',
        'DEVPKEY_Device_FriendlyName',
        'DEVPKEY_Device_HardwareIds',
        'DEVPKEY_Device_CompatibleIds'
    )
} | Format-List -Property KeyName,Data
```

关键结果：

```text
DEVPKEY_Device_Service      WINUSB
DEVPKEY_Device_Class        USBDevice
DEVPKEY_Device_Manufacturer WinUsb Device
DEVPKEY_Device_FriendlyName ESP32JTAG CMSIS-DAP
```

`MI_01` 应该是 CDC 串口：

```text
DEVPKEY_Device_Service usbser
DEVPKEY_Device_Class   Ports
```

## DeviceInterfaceGUIDs 检查

Keil 找不到 CMSIS-DAP 时，重点看 `DeviceInterfaceGUIDs`。

注册表路径一般类似：

```text
HKLM\SYSTEM\CurrentControlSet\Enum\USB\VID_0D28&PID_0204&MI_00\<实例ID>\Device Parameters
```

查询命令：

```powershell
reg query 'HKLM\SYSTEM\CurrentControlSet\Enum\USB\VID_0D28&PID_0204&MI_00\<实例ID>\Device Parameters' /s
```

正常应能看到类似：

```text
DeviceInterfaceGUIDs    REG_MULTI_SZ    {CDB3B5AD-293B-4663-AA36-1AAE46463776}
```

如果只有：

```text
VendorRevision
RevisionId
ExtPropDescSemaphore
```

而没有 `DeviceInterfaceGUIDs`，那么 Windows 虽然可能显示 `ESP32JTAG CMSIS-DAP` 并且 `Service = WINUSB`，但 Keil 仍可能枚举不到。

这正是本次故障的关键现象：

- 设备管理器能看到 `ESP32JTAG CMSIS-DAP`。
- Keil 提示找不到 CMSIS-DAP。
- `MI_00` 确认为 `WINUSB`。
- 注册表没有 `DeviceInterfaceGUIDs`。
- 修正 MS OS 2.0 descriptor set 后，需要通过新 serial/bcdDevice 让 Windows 重新读取扩展属性。

## Keil 排查顺序

当 Keil 提示没有找到 CMSIS-DAP：

1. 确认刷的是最新 `build/S3CMSISDAP.bin`。
2. 拔插 USB。
3. 设备管理器确认新序列号末尾是 `01400002`。
4. 确认有 `ESP32JTAG CMSIS-DAP` 和 `USB 串行设备 (COMx)`。
5. 确认 `MI_00` 的 `Service` 是 `WINUSB`。
6. 确认 `DeviceInterfaceGUIDs` 包含 `{CDB3B5AD-293B-4663-AA36-1AAE46463776}`。
7. 关闭并重新打开 Keil。
8. 在 Keil Debug Adapter 选择 CMSIS-DAP。
9. 如果仍无设备，卸载旧设备实例后重插。

卸载旧设备实例可以在设备管理器里操作：

- 查看。
- 按连接查看设备。
- 找到旧的 `ESP32JTAG CMSIS-DAP`。
- 卸载设备。
- 如果有删除驱动选项，不要随便删除系统 WinUSB 驱动。
- 拔插设备，让 Windows 重新枚举。

也可以用管理员 PowerShell 查询旧实例：

```powershell
pnputil /enum-devices /connected
```

## 固件响应排查

如果 Windows/Keil 能看到设备，但连接或下载失败，重点看 DAP 命令响应链路。

检查点：

- `app_main()` 是否执行到 `ESP32 CMSIS-DAP v2 is ready`。
- `xTaskCreatePinnedToCore()` 是否返回 `pdPASS`。
- `CMSIS_DAP_TASK` 是否持续运行。
- `USBD_EVENT_CONFIGURED` 是否发生。
- `usbd_ep_start_read(0, DAP_OUT_EP, ...)` 是否启动。
- `dap_out_callback()` 是否被调用。
- `chry_dap_handle()` 中 `USB_RequestCountI != USB_RequestCountO` 是否成立。
- `DAP_ExecuteCommand()` 是否返回有效响应长度。
- `usbd_ep_start_write(0, DAP_IN_EP, ...)` 是否发送响应。

临时调试建议：

```c
ESP_LOGI(TAG, "configured");
ESP_LOGI(TAG, "dap out %lu", nbytes);
ESP_LOGI(TAG, "dap resp %u", USB_RespSize[USB_ResponseIndexI]);
```

注意不要在高速 DAP transfer 路径里长期保留大量日志。Keil 下载时命令非常密集，日志会明显拖慢甚至扰乱时序。

## core 0 固定任务注意点

当前用户要求把 CMSIS 轮询固定在 core 0：

```c
xTaskCreatePinnedToCore(CMSIS_DAP_TASK, "cmsis_dap", 4096, NULL, 10, &CMSIS_DAP_TASK_HANDLE, 0)
```

下次出问题时要注意：

- ESP32-S3 当前是双核配置，`CONFIG_FREERTOS_UNICORE` 未启用。
- `CONFIG_FREERTOS_NUMBER_OF_CORES=2`。
- core 0 上也可能有 Wi-Fi、USB、timer、系统任务等负载。
- 当前工程没有 Wi-Fi 等复杂功能，固定 core 0 是可以接受的。
- 如果后续加回 Wi-Fi、ESP-NOW、显示刷新等任务，core 0 负载可能影响 DAP 响应。

如果出现 Keil 识别到但下载超时，可以临时测试：

```c
xTaskCreatePinnedToCore(CMSIS_DAP_TASK, "cmsis_dap", 4096, NULL, 12, &CMSIS_DAP_TASK_HANDLE, 0)
```

或者把 `vTaskDelay(pdMS_TO_TICKS(1))` 临时改短/改为 `taskYIELD()` 做对照。但长期不要让任务完全占满 CPU。

## 构建方式

普通环境如果已经加载 ESP-IDF：

```powershell
idf.py build
```

本机这次使用的完整构建命令等价于：

```powershell
$env:IDF_PATH='D:\espidftoolchain\Espressif\frameworks\esp-idf-v5.5.4'
$env:IDF_PYTHON_ENV_PATH='D:\espidftoolchain\Espressif\python_env\idf5.5_py3.11_env'
$env:PATH='D:\espidftoolchain\Espressif\tools\cmake\3.30.2\bin;D:\espidftoolchain\Espressif\tools\ninja\1.12.1;D:\espidftoolchain\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20260121\xtensa-esp-elf\bin;' + $env:PATH
& 'D:\espidftoolchain\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' 'D:\espidftoolchain\Espressif\frameworks\esp-idf-v5.5.4\tools\idf.py' build
```

构建成功关键输出：

```text
Generated D:/ESP32IDF/S3CMSISDAP/build/S3CMSISDAP.bin
Project build complete.
```

当前 bin 大小大约：

```text
S3CMSISDAP.bin binary size 0x3be00 bytes.
Smallest app partition is 0x100000 bytes.
```

构建中可能出现：

```text
Error while generating esp_rom gdbinit
```

目前这是警告，不影响 `S3CMSISDAP.bin` 生成。

## 刷写方式

构建结束提示的刷写命令：

```powershell
python -m esptool --chip esp32s3 -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 2MB --flash_freq 80m 0x0 build\bootloader\bootloader.bin 0x8000 build\partition_table\partition-table.bin 0x10000 build\S3CMSISDAP.bin
```

或者在 IDF 环境中：

```powershell
idf.py -p COMx flash
```

刷完后一定要：

1. 拔掉 USB。
2. 等 2 秒。
3. 重新插入 USB。
4. 让 Windows 重新枚举新 serial。

如果没有拔插，Windows 可能还保留旧 interface 状态，Keil 仍然看不到。

## 下次快速故障分类

### 设备管理器完全没有设备

优先检查：

- USB 线是否支持数据。
- ESP32-S3 是否正常启动。
- USB D+/D- 是否接原生 USB OTG。
- `chry_dap_init(0, ESP_USBD_BASE)` 是否执行。
- `usbd_initialize()` 是否失败。
- USB PHY 是否初始化失败。

相关文件：

```text
components/CherryDAP/CherryUSB/port/dwc2/usb_glue_esp.c
components/CherryDAP/CherryUSB/port/dwc2/usb_dc_dwc2.c
```

### 只看到 USB Composite Device，没有 CMSIS-DAP interface

优先检查：

- 配置描述符总长度 `USB_CONFIG_SIZE`。
- interface 数量 `INTF_NUM`。
- `CMSIS_DAP_INTERFACE_SIZE` 是否正确。
- `USB_INTERFACE_DESCRIPTOR_INIT(0x00, ...)` 是否存在。
- endpoint 地址是否冲突。

### 看到 ESP32JTAG CMSIS-DAP，但 Keil 找不到

优先检查：

- `MI_00` 是否 `Service = WINUSB`。
- `DeviceInterfaceGUIDs` 是否存在。
- GUID 是否是 `{CDB3B5AD-293B-4663-AA36-1AAE46463776}`。
- Windows 是否缓存了旧 serial。
- `bcdDevice` 和 serial 是否已经变更。
- MS OS 2.0 descriptor set 是否包含 configuration subset。

### Keil 找到设备，但连接失败

优先检查：

- `CMSIS_DAP_TASK` 是否运行。
- `chry_dap_handle()` 是否被调用。
- DAP OUT callback 是否收到包。
- DAP IN 是否回包。
- `DAP_PACKET_SIZE` 是否是 64。
- `DAP_PACKET_COUNT` 是否太小导致拥塞。
- SWD 引脚配置是否正确。
- 目标板供电、GND、SWDIO、SWCLK、NRST 是否连接正常。

### Keil 可以连接，但下载不稳定

优先检查：

- core 0 负载。
- `CMSIS_DAP_TASK` 优先级。
- `vTaskDelay(1ms)` 是否太长。
- 日志是否过多。
- SWDIO 方向控制引脚是否和硬件一致。
- SWD 时钟是否过高。
- 目标板复位脚是否工作。

## 关键检索命令

查入口任务：

```powershell
rg -n "CMSIS_DAP_TASK|xTaskCreatePinnedToCore|xTaskCreate|vTaskDelay" main components\CherryDAP
```

查 WS2812 是否残留：

```powershell
rg -n "WS2812|ws2812|DISPLAY_TASK|BOARD_WS2812|rmt_|esp_driver_rmt" main
```

查 USB descriptor：

```powershell
rg -n "USBD_VID|USBD_PID|USBD_WINUSB|DeviceInterfaceGUIDs|CDB3B5AD|USB_INTERFACE_DESCRIPTOR_INIT|CMSIS-DAP v2|01400002" components\CherryDAP
```

查 DAP 命令处理：

```powershell
rg -n "chry_dap_handle|DAP_ExecuteCommand|dap_out_callback|dap_in_callback|USB_Request|USB_Response" components\CherryDAP
```

查 DAP config：

```powershell
rg -n "DAP_PACKET_SIZE|DAP_PACKET_COUNT|PIN_SWCLK|PIN_SWDIO|PIN_nRESET|DAP_USE" components\CherryDAP\projects\esp32s3\main\DAP_config.h
```

## 不要轻易改动的点

这些点和 Keil 识别高度相关，下次改动要非常谨慎：

- `USBD_VID = 0x0D28`
- `USBD_PID = 0x0204`
- `USB_2_1`
- device class `0xEF, 0x02, 0x01`
- Interface 0 的 class `0xFF`
- Interface 0 的 `iInterface = 0x04`
- 字符串 `"CMSIS-DAP v2"`
- WinUSB vendor code `0x20`
- CMSIS-DAP GUID `{CDB3B5AD-293B-4663-AA36-1AAE46463776}`
- MS OS 2.0 descriptor set 的 configuration subset
- `DAP_IN_EP = 0x81`
- `DAP_OUT_EP = 0x02`
- `DAP_PACKET_SIZE = 64`
- `chry_dap_handle()` 在任务中持续轮询

## 当前健康状态基线

本次记录时的健康基线：

- `main` 中无 WS2812/RMT 显示任务。
- CMSIS-DAP task 固定 core 0。
- `MI_00` 应为 WinUSB CMSIS-DAP v2 interface。
- `MI_01` 应为 USB serial COM port。
- serial number 末尾是 `01400002`。
- `bcdDevice = 0x0101`。
- 构建成功并生成：

```text
D:\ESP32IDF\S3CMSISDAP\build\S3CMSISDAP.bin
```

如果后续再次出现“设备管理器能看到，Keil 找不到”，优先从 `DeviceInterfaceGUIDs` 和 Windows 设备缓存开始查，不要一开始就大改 DAP 命令处理或 SWD 引脚。
