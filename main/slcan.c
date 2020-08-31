/**
 * Implementation of LAWICEL Serial Line CAN protocol (http://www.can232.com/docs/canusb_manual.pdf)
 * Scope is to be compatible with Linux SocketCAN slcan driver, to allow usage of can-utils
 */

#include "slcan.h"

#include "can.h"

#include <string.h>
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/can.h"

#define SLCAN_UART_NUM UART_NUM_2
#define SLCAN_UART_TXD_GPIO_NUM GPIO_NUM_17
#define SLCAN_UART_RXD_GPIO_NUM GPIO_NUM_16
#define SLCAN_UART_BAUDRATE 576000
#define SLCAN_UART_BUF_SIZE 1024
#define SLCAN_TX_TASK_PRIO 1

static const char *TAG = "slcan";

/* Lookup table for HEX to ASCII conversion */
static const char *hexLut = "0123456789ABCDEF";

/**
 * Format CAN frame for UART output
 * @param buf must be at least TODO TBD long
 */
void slcanFormatFrame(can_message_t *msg, char *buf)
{
    if (msg->extd)
    {
        if (msg->rtr)
            *buf++ = 'R';
        else
            *buf++ = 'T';

        // TODO
    }
    else
    {
        if (msg->rtr)
            *buf++ = 'r';
        else
            *buf++ = 't';

        // 11bit identifier
        *buf++ = hexLut[msg->identifier >> 8 & 0xF];
        *buf++ = hexLut[msg->identifier >> 4 & 0xF];
        *buf++ = hexLut[msg->identifier & 0xF];
    }

    // Data Length Code
    *buf++ = hexLut[msg->data_length_code & 0xF];

    // Data
    for (int i = 0; i < msg->data_length_code; i++)
    {
        *buf++ = hexLut[msg->data[i] >> 4];
        *buf++ = hexLut[msg->data[i] & 0xF];
    }

    *buf++ = '\r';
    *buf++ = '\0';
}

/**
 * Parse CAN frame from UART input
 */
void slcanParseFrame(void)
{
    // TODO
}

static void slcanTransmitTask(void *arg)
{
    while (1)
    {
        can_message_t msg;
        xQueueReceive(canRxQueue, &msg, portMAX_DELAY);

        // char out[128];
        // snprintf(out, sizeof(out), "%.3X %d %.2X %.2X %.2X %.2X %.2X %.2X %.2X %.2X\r\n", msg.identifier, msg.data_length_code,
        //          msg.data[0], msg.data[1], msg.data[2], msg.data[3], msg.data[4], msg.data[5], msg.data[6], msg.data[7]);
        // uart_write_bytes(SLCAN_UART_NUM, out, strlen(out));

        char out[64];
        slcanFormatFrame(&msg, out);
        uart_write_bytes(SLCAN_UART_NUM, out, strlen(out));
    }
}

void slcanInit(void)
{
    uart_config_t uart_config = {
        .baud_rate = SLCAN_UART_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    ESP_ERROR_CHECK(uart_param_config(SLCAN_UART_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(SLCAN_UART_NUM, SLCAN_UART_TXD_GPIO_NUM, SLCAN_UART_RXD_GPIO_NUM, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(SLCAN_UART_NUM, SLCAN_UART_BUF_SIZE * 2, SLCAN_UART_BUF_SIZE * 2, 0, NULL, 0));

    xTaskCreate(slcanTransmitTask, "SLCAN TX", 2048, NULL, SLCAN_TX_TASK_PRIO, NULL);

    ESP_LOGI(TAG, "init completed");
}
