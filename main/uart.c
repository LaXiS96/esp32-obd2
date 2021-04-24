#include "uart.h"

#include <string.h>
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/uart.h"

static const char *TAG = "UART";

static QueueHandle_t uartEventQueue;

uart_message_t uartNewMessage(uint8_t *data, size_t length)
{
    uart_message_t msg = {
        .length = length,
        .data = malloc(length),
    };
    memcpy(msg.data, data, length);
    return msg;
}

static void uartEventTask(void *arg)
{
    uart_event_t event;
    uint8_t buf[UART_BUF_SIZE];

    while (1)
    {
        if (xQueueReceive(uartEventQueue, &event, portMAX_DELAY))
        {
            switch (event.type)
            {
            case UART_DATA:
                ESP_LOGI(TAG, "UART_DATA size:%d", event.size);
                uart_read_bytes(UART_PORT_NUM, buf, event.size, portMAX_DELAY);
                // ESP_LOG_BUFFER_HEXDUMP(TAG, buf, sizeof(buf), ESP_LOG_INFO);

                uart_message_t msg = uartNewMessage(buf, event.size);
                if (xQueueSend(uartRxQueue, &msg, 0) == errQUEUE_FULL)
                    ESP_LOGE(TAG, "uartRxQueue FULL");
                break;
            case UART_BREAK:
                ESP_LOGI(TAG, "UART_BREAK");
                break;
            case UART_BUFFER_FULL:
                ESP_LOGW(TAG, "UART_BUFFER_FULL");
                break;
            case UART_FIFO_OVF:
                ESP_LOGW(TAG, "UART_FIFO_OVF");
                break;
            case UART_FRAME_ERR:
                ESP_LOGW(TAG, "UART_FRAME_ERR");
                break;
            case UART_PARITY_ERR:
                ESP_LOGW(TAG, "UART_PARITY_ERR");
                break;
            case UART_DATA_BREAK:
                ESP_LOGI(TAG, "UART_DATA_BREAK");
                break;
            case UART_PATTERN_DET:
                ESP_LOGI(TAG, "UART_PATTERN_DET");
                break;
            case UART_EVENT_MAX:
                ESP_LOGW(TAG, "UART_EVENT_MAX");
                break;
            }
        }
    }
}

static void uartTxTask(void *arg)
{
    uart_message_t msg;

    while (1)
    {
        xQueueReceive(uartTxQueue, &msg, portMAX_DELAY);

        uart_write_bytes(UART_PORT_NUM, (const char *)msg.data, msg.length);
    }
}

void uartInit(void)
{
    uart_config_t uart_config = {
        .baud_rate = UART_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };

    ESP_ERROR_CHECK(uart_driver_install(UART_PORT_NUM, UART_BUF_SIZE * 2, UART_BUF_SIZE * 2, 16, &uartEventQueue, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT_NUM, UART_TXD_GPIO_NUM, UART_RXD_GPIO_NUM, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    uartRxQueue = xQueueCreate(UART_QUEUES_LEN, sizeof(uart_message_t));
    uartTxQueue = xQueueCreate(UART_QUEUES_LEN, sizeof(uart_message_t));

    xTaskCreate(uartEventTask, "uartEvent", 2048, NULL, UART_EVENT_TASK_PRIO, NULL);
    xTaskCreate(uartTxTask, "uartTx", 2048, NULL, UART_TX_TASK_PRIO, NULL);

    ESP_LOGI(TAG, "init completed");
}
