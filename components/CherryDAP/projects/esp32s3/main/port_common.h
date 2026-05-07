#pragma once
#include <stdint.h>
#include "gpio.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/uart_select.h"
#include "esp_log.h"

#ifndef __STATIC_INLINE
#define __STATIC_INLINE static inline
#endif

#ifndef __WEAK
#define __WEAK __attribute__((weak))
#endif

#ifndef DAP_USE_SWDIO_DIR_PIN
#define DAP_USE_SWDIO_DIR_PIN 1
#endif

#ifndef SWDIO_RDnWR_PIN
#define SWDIO_RDnWR_PIN GPIO_NUM_45
#endif

#ifndef PIN_SWCLK_TCK
#define PIN_SWCLK_TCK GPIO_NUM_47
#endif

#ifndef PIN_SWDIO_TMS
#define PIN_SWDIO_TMS GPIO_NUM_41
#endif

#ifndef PIN_TDI
#define PIN_TDI GPIO_NUM_40
#endif

#ifndef PIN_TDO
#define PIN_TDO GPIO_NUM_15
#endif

#ifndef PIN_nRESET
#define PIN_nRESET GPIO_NUM_10
#endif

#ifndef GPIO_UART_TXD
#define GPIO_UART_TXD GPIO_NUM_43
#endif

#ifndef GPIO_UART_RXD
#define GPIO_UART_RXD GPIO_NUM_44
#endif

#ifndef UART_PORT_NUM
#define UART_PORT_NUM UART_NUM_1
#endif

#define DAP_UART_TX GPIO_UART_TXD
#define DAP_UART_RX GPIO_UART_RXD

#define DAP_CPU_CLOCK 0U

void dap_platform_init(void);
void dap_gpio_init(void);

void set_led_connect(uint32_t bit);
void set_led_running(uint32_t bit);
uint32_t get_led_connect(void);
uint32_t get_led_running(void);


static inline uint32_t dap_get_time_stamp(void)
{
    return (uint32_t)xTaskGetTickCount();
}
#define CHERRY_GPIO_OUTPUT_PIN_SEL \
    (((DAP_USE_SWDIO_DIR_PIN ? (1ULL << SWDIO_RDnWR_PIN) : 0ULL)) | \
     (1ULL << PIN_SWCLK_TCK) | (1ULL << PIN_SWDIO_TMS) | (1ULL << PIN_TDI))

static inline void pins_init_cherry() {
    gpio_config_t io_conf;
    //disable interrupt
    io_conf.intr_type = GPIO_INTR_DISABLE;
    //set as output mode
    io_conf.mode = GPIO_MODE_OUTPUT;
    //bit mask of the pins that you want to set
    io_conf.pin_bit_mask = CHERRY_GPIO_OUTPUT_PIN_SEL;
    //disable pull-down mode
    io_conf.pull_down_en = 0;
    //disable pull-up mode
    io_conf.pull_up_en = 0;
    //configure GPIO with the given settings
    gpio_config(&io_conf);

    //set SWDIO_RDnWR_PIN when present
    if (DAP_USE_SWDIO_DIR_PIN) {
        gpio_set_direction(SWDIO_RDnWR_PIN, GPIO_MODE_OUTPUT);
    }

    //set TDO:
    gpio_set_direction(PIN_TDO, GPIO_MODE_INPUT);

    // Release target reset. NRST is active-low and normally has a target-side
    // pull-up, so input mode behaves like open-drain high.
    gpio_set_pull_mode(PIN_nRESET, GPIO_PULLUP_ONLY);
    gpio_set_direction(PIN_nRESET, GPIO_MODE_INPUT);

}

