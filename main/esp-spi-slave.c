#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_common.h"
#include "driver/spi_slave.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "hal/gpio_types.h"
#include "hal/spi_types.h"
#include "hal/uart_types.h"
#include "soc/gpio_num.h"

#define MISO_IO_NUM 13
#define MOSI_IO_NUM 12
#define SCLK_IO_NUM 14
#define CS_IO_NUM 15

void app_main(void)
{
    if(!uart_is_driver_installed(UART_NUM_0)){
        ESP_ERROR_CHECK(uart_driver_install(
            UART_NUM_0,
            256,
            0,
            0,
            NULL,
            0));
    }

    uart_vfs_dev_use_driver(UART_NUM_0);

    const spi_bus_config_t bus_conf = {
        .mosi_io_num = MOSI_IO_NUM,
        .miso_io_num = MISO_IO_NUM,
        .sclk_io_num = SCLK_IO_NUM,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    const spi_slave_interface_config_t slave_if_conf = {
        .spics_io_num = CS_IO_NUM,
        .queue_size = 1,
        .mode = 0,
    };
    ESP_ERROR_CHECK(spi_slave_initialize(SPI2_HOST, &bus_conf, &slave_if_conf, SPI_DMA_CH_AUTO));

    uint8_t *tx_buf = spi_bus_dma_memory_alloc(SPI2_HOST, 8, 0);
    if(tx_buf == NULL) {
        return;
    }
    

    uint8_t *rx_buf = spi_bus_dma_memory_alloc(SPI2_HOST, 8, 0);
    if(rx_buf == NULL) {
        return;
    }
    
    char input_buf[64];

    const gpio_config_t gpio_conf = {
        .pin_bit_mask = (1ULL << GPIO_NUM_2),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&gpio_conf);
    gpio_set_direction(GPIO_NUM_2, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_2, 1);

    while(1){
        if(fgets(input_buf, sizeof(input_buf), stdin) == NULL){
            clearerr(stdin);
            continue;
        }
        input_buf[strcspn(input_buf, "\r\n")] = '\0';
        memcpy(tx_buf, input_buf, sizeof(input_buf));

        spi_slave_transaction_t transaction = {
            .flags = 0,
            .tx_buffer = tx_buf,
            .length = sizeof(input_buf) * 8,
            .rx_buffer = rx_buf,
            .trans_len = 0,
        };
        gpio_set_level(GPIO_NUM_2, 0);
        ESP_ERROR_CHECK(spi_slave_transmit(SPI2_HOST, &transaction, portMAX_DELAY));
        printf("%s\n", rx_buf);
        gpio_set_level(GPIO_NUM_2, 1);
        vTaskDelay(pdMS_TO_TICKS(10));
    }    
}
