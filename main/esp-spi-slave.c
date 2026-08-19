#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_common.h"
#include "driver/spi_master.h"
#include "driver/spi_slave.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "hal/gpio_types.h"
#include "hal/spi_types.h"
#include "hal/uart_types.h"
#include "portmacro.h"
#include "soc/gpio_num.h"
#include "freertos/queue.h"

static char *TAG = "ESP-SPI-SLAVE";

#define CONFIG_QUEUE_SIZE 3
#define CONFIG_BUF_SIZE 64

static QueueHandle_t input_buf_queue;
static BaseType_t xHigherPriorityTaskWoken = pdFALSE;
static TaskHandle_t create_dummy_task_handle;
static uint8_t irq_output_level = 1;

DMA_ATTR static uint8_t tx_buf[CONFIG_QUEUE_SIZE][CONFIG_BUF_SIZE];
DMA_ATTR static uint8_t rx_buf[CONFIG_QUEUE_SIZE][CONFIG_BUF_SIZE];

static spi_slave_transaction_t trans[CONFIG_QUEUE_SIZE];

static const spi_bus_config_t bus_conf = {
    .mosi_io_num = CONFIG_SPI_MOSI_GPIO,
    .miso_io_num = CONFIG_SPI_MISO_GPIO,
    .sclk_io_num = CONFIG_SPI_SCLK_GPIO,
    .quadwp_io_num = -1,
    .quadhd_io_num = -1,
};
static const spi_slave_interface_config_t slave_if_conf = {
    .spics_io_num = CONFIG_SPI_CS_GPIO,
    .queue_size = 1,
    .mode = 0,
};


static int uart_vfs_init() {
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

    return 0;
}

static void IRAM_ATTR isr_handler(void *arg){
    if(irq_output_level == 1){
        xTaskNotifyFromISR(create_dummy_task_handle, 0, eNoAction, NULL);
    }
    
}


static int irq_gpio_init(){
    gpio_config_t gpio_master_to_slave_irq_conf = {
        .pin_bit_mask = (1ULL << CONFIG_MASTER_TO_SLAVE_IRQ_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE, 
    };

    gpio_config(&gpio_master_to_slave_irq_conf);
    gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    gpio_isr_handler_add(CONFIG_MASTER_TO_SLAVE_IRQ_GPIO, isr_handler, NULL);


    const gpio_config_t gpio_slave_to_master_conf = {
        .pin_bit_mask = (1ULL << CONFIG_SLAVE_TO_MASTER_IRQ_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&gpio_slave_to_master_conf);
    gpio_set_direction(CONFIG_SLAVE_TO_MASTER_IRQ_GPIO, GPIO_MODE_OUTPUT);

    return 0;
}

static int dma_buf_init(){
    for(int i = 0; i<CONFIG_QUEUE_SIZE; i++) {
        memset(&trans[i], 0, sizeof(trans[i]));

        trans[i].length = CONFIG_BUF_SIZE * 8;
        trans[i].tx_buffer = tx_buf[i];
        trans[i].rx_buffer = rx_buf[i];
    }
    return 0;
}

static void create_dummy_task(void *arg){
    QueueHandle_t *input_buf_queue = (QueueHandle_t *)arg;
    char dummy[64] = "\0";
    while(1){
        xTaskNotifyWait(0, 0, NULL, portMAX_DELAY);
        xQueueSendToBack(*input_buf_queue, dummy, portMAX_DELAY);
    }
    
}

static void stdin_read_task(void *arg) {
    QueueHandle_t *input_buf_queue = (QueueHandle_t *)arg;

    char input_buf[64];
    
    while(1){
        if(fgets(input_buf, sizeof(input_buf), stdin) == NULL){
            clearerr(stdin);
            continue;
        }
        input_buf[strcspn(input_buf, "\r\n")] = '\0';
        xQueueSendToBack(*input_buf_queue, input_buf, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void transaction_task(void *arg) {
    QueueHandle_t *input_buf_queue = (QueueHandle_t *)arg;
    BaseType_t result;
    char data[64];
    int trans_index = 0;
    spi_slave_transaction_t *done;
    gpio_set_level(CONFIG_SLAVE_TO_MASTER_IRQ_GPIO, irq_output_level);
    while(1){
        result = xQueueReceive(*input_buf_queue, &data, portMAX_DELAY);
        if(result != pdPASS) {
            ESP_LOGE(TAG, "xQueueReceive failed");
            continue;
        }
        memcpy(tx_buf[trans_index], data, sizeof(data));
        spi_slave_queue_trans(SPI2_HOST, &trans[trans_index], portMAX_DELAY);
        trans_index = (trans_index + 1) % CONFIG_QUEUE_SIZE;
        irq_output_level = 0;
        gpio_set_level(CONFIG_SLAVE_TO_MASTER_IRQ_GPIO, irq_output_level);
        spi_slave_get_trans_result(SPI2_HOST, &done, portMAX_DELAY);
        irq_output_level = 1;
        gpio_set_level(CONFIG_SLAVE_TO_MASTER_IRQ_GPIO, irq_output_level);
        printf("%s\n", (char *)done->rx_buffer);
    }
}


void app_main(void)
{
    if(uart_vfs_init() != 0){
        ESP_LOGE(TAG, "uart_vfs_init failed");
    }

    if(irq_gpio_init() != 0){
        ESP_LOGE(TAG, "irq_gpio_init failed");
    }

    if(dma_buf_init() != 0){
        ESP_LOGE(TAG, "dam_buf_init failed");
    }

    ESP_ERROR_CHECK(spi_slave_initialize(SPI2_HOST, &bus_conf, &slave_if_conf, SPI_DMA_CH_AUTO));

    input_buf_queue = xQueueCreate(10, 64);
    if(input_buf_queue == NULL){
        ESP_LOGE(TAG, "input_buf_queue creation failed");
    }


    xTaskCreate(stdin_read_task, "stdin_read_task", 4096, &input_buf_queue, 2, NULL);
    xTaskCreate(transaction_task, "transaction_task", 4096, &input_buf_queue, 2, NULL);
    xTaskCreate(create_dummy_task, "create_dummy_task", 4096, &input_buf_queue, 2, &create_dummy_task_handle);
}