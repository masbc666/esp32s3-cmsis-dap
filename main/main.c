#include <stdbool.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "dap_main.h"
#include "usb2uart.h"
#include "usb_config.h"

static const char *TAG = "app";

static TaskHandle_t CMSIS_DAP_TASK_HANDLE;

static void CMSIS_DAP_TASK(void *arg);

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