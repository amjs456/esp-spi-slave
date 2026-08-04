#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/spi_common.h"
#include "driver/spi_slave.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "hal/spi_types.h"

#define MISO_IO_NUM 13
#define MOSI_IO_NUM 12
#define SCLK_IO_NUM 14
#define CS_IO_NUM 15

void app_main(void)
{
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
    char tx_data[8] = "Hello";
    memcpy(tx_buf, tx_data, sizeof(tx_data));

    uint8_t *rx_buf = spi_bus_dma_memory_alloc(SPI2_HOST, 8, 0);
    if(rx_buf == NULL) {
        return;
    }
    
    while(1){
        spi_slave_transaction_t transaction = {
            .flags = 0,
            .tx_buffer = tx_buf,
            .length = sizeof(tx_data) * 8,
            .rx_buffer = rx_buf,
            .trans_len = 0,
        };
        ESP_ERROR_CHECK(spi_slave_transmit(SPI2_HOST, &transaction, portMAX_DELAY));
        printf("%s\n", rx_buf);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }    
}
