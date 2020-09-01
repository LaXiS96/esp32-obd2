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

#define SLCAN_UART_NUM UART_NUM_2
#define SLCAN_UART_TXD_GPIO_NUM GPIO_NUM_17
#define SLCAN_UART_RXD_GPIO_NUM GPIO_NUM_16
#define SLCAN_UART_BAUDRATE 576000
#define SLCAN_UART_BUF_SIZE 1024
#define SLCAN_TX_TASK_PRIO 1  // TODO
#define SLCAN_CMD_BUF_SIZE 32 // TODO

static const char *TAG = "SLCAN";

// Hex to ASCII conversion function
#define HEX2ASCII(x) HEX2ASCII_LUT[(x)]
static const char *HEX2ASCII_LUT = "0123456789ABCDEF";

// clang-format off
// ASCII to hex conversion function
#define ASCII2HEX(x) ASCII2HEX_LUT[(x) - 0x30]
static const uint8_t ASCII2HEX_LUT[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F
};
// clang-format on

/**
 * Send an OK response, with optional data
 * @param data can be NULL
 */
static void slcanRespondOk(char *data)
{
    if (data != NULL)
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "%s\r", data);

        ESP_LOGI(TAG, "respond ok data=\"%s\"", data);
        uart_write_bytes(SLCAN_UART_NUM, buf, strlen(buf));
    }
    else
    {
        ESP_LOGI(TAG, "respond ok");
        uart_write_bytes(SLCAN_UART_NUM, "\r", 1);
    }
}

/**
 * Send an error response
 */
static void slcanRespondError(void)
{
    ESP_LOGI(TAG, "respond error");
    uart_write_bytes(SLCAN_UART_NUM, "\a", 1);
}

/**
 * Format received CAN frame for UART output
 * @param msg input frame
 * @param str output formatted string, must be at least TODO TBD long
 */
static void slcanFormatFrame(can_message_t *msg, char *str)
{
    if (msg->extd)
    {
        if (msg->rtr)
            *str++ = 'R';
        else
            *str++ = 'T';

        // 29bit identifier
        *str++ = HEX2ASCII(msg->identifier >> 28 & 0xF);
        *str++ = HEX2ASCII(msg->identifier >> 24 & 0xF);
        *str++ = HEX2ASCII(msg->identifier >> 20 & 0xF);
        *str++ = HEX2ASCII(msg->identifier >> 16 & 0xF);
        *str++ = HEX2ASCII(msg->identifier >> 12 & 0xF);
        *str++ = HEX2ASCII(msg->identifier >> 8 & 0xF);
        *str++ = HEX2ASCII(msg->identifier >> 4 & 0xF);
        *str++ = HEX2ASCII(msg->identifier & 0xF);
    }
    else
    {
        if (msg->rtr)
            *str++ = 'r';
        else
            *str++ = 't';

        // 11bit identifier
        *str++ = HEX2ASCII(msg->identifier >> 8 & 0xF);
        *str++ = HEX2ASCII(msg->identifier >> 4 & 0xF);
        *str++ = HEX2ASCII(msg->identifier & 0xF);
    }

    // Data Length Code
    *str++ = HEX2ASCII(msg->data_length_code & 0xF);

    // Data bytes
    for (int i = 0; i < msg->data_length_code; i++)
    {
        *str++ = HEX2ASCII(msg->data[i] >> 4);
        *str++ = HEX2ASCII(msg->data[i] & 0xF);
    }

    *str++ = '\r';
    *str++ = '\0';
}

/**
 * Parse t, T, r, R frame commands
 * @param str input command string
 * @param msg output parsed CAN frame
 */
static void slcanParseFrame(char *str, can_message_t *msg)
{
    size_t strLen = strlen(str);

    msg->flags = 0; // Reset all flags

    if (strLen > 0)
    {
        msg->extd = (str[0] == 'T' || str[0] == 'R') ? 1 : 0;
        msg->rtr = (str[0] == 'r' || str[0] == 'R') ? 1 : 0;

        // TODO
        // msg->identifier = ASCII2HEX();
    }
}

/**
 * Handle received SLCAN commands
 */
static void slcanRxTask(void *arg)
{
    static can_timing_config_t *slcanConfigTiming;

    while (1)
    {
        uint8_t cmd[SLCAN_CMD_BUF_SIZE];

        // Read from UART with a 10ms timeout
        // TODO is the timeout safe? better split commands by CR (would also be compatible with manual input)
        int readBytes = uart_read_bytes(SLCAN_UART_NUM, cmd, sizeof(cmd), pdMS_TO_TICKS(10));
        if (readBytes > 0)
        {
            ESP_LOGI(TAG, "command %.*s", readBytes, cmd);

            switch (cmd[0])
            {
            case 'S': // Set CAN standard bitrate
                if (canIsOpen())
                    slcanRespondError();
                else
                {
                    switch (cmd[1])
                    {
                    case '0':
                        // 10kbps unsupported
                        slcanRespondError();
                        break;
                    case '1':
                        // 20kbps unsupported
                        slcanRespondError();
                        break;
                    case '2':
                        slcanConfigTiming = &(can_timing_config_t)CAN_TIMING_CONFIG_50KBITS();
                        slcanRespondOk(NULL);
                        break;
                    case '3':
                        slcanConfigTiming = &(can_timing_config_t)CAN_TIMING_CONFIG_100KBITS();
                        slcanRespondOk(NULL);
                        break;
                    case '4':
                        slcanConfigTiming = &(can_timing_config_t)CAN_TIMING_CONFIG_125KBITS();
                        slcanRespondOk(NULL);
                        break;
                    case '5':
                        slcanConfigTiming = &(can_timing_config_t)CAN_TIMING_CONFIG_250KBITS();
                        slcanRespondOk(NULL);
                        break;
                    case '6':
                        slcanConfigTiming = &(can_timing_config_t)CAN_TIMING_CONFIG_500KBITS();
                        slcanRespondOk(NULL);
                        break;
                    case '7':
                        slcanConfigTiming = &(can_timing_config_t)CAN_TIMING_CONFIG_800KBITS();
                        slcanRespondOk(NULL);
                        break;
                    case '8':
                        slcanConfigTiming = &(can_timing_config_t)CAN_TIMING_CONFIG_1MBITS();
                        slcanRespondOk(NULL);
                        break;
                    default:
                        slcanRespondError();
                    }
                }
                break;
            case 'O': // Open CAN channel
                if (canIsOpen() || slcanConfigTiming == NULL)
                    slcanRespondError();
                else
                {
                    if (canOpen(CAN_MODE_NORMAL, slcanConfigTiming) == ESP_OK)
                        slcanRespondOk(NULL);
                    else
                        slcanRespondError();
                }
                break;
            case 'L': // Open CAN channel in listen-only mode
                if (canIsOpen() || slcanConfigTiming == NULL)
                    slcanRespondError();
                else
                {
                    if (canOpen(CAN_MODE_LISTEN_ONLY, slcanConfigTiming) == ESP_OK)
                        slcanRespondOk(NULL);
                    else
                        slcanRespondError();
                }
                break;
            case 'C': // Close CAN channel
                if (!canIsOpen())
                    slcanRespondError();
                else
                {
                    if (canClose() == ESP_OK)
                        slcanRespondOk(NULL);
                    else
                        slcanRespondError();
                }
                break;
            case 't': // Send standard frame
                if (!canIsOpen() || canGetMode() != CAN_MODE_NORMAL)
                    slcanRespondError();
                else
                {
                    // TODO
                }
                break;
            case 'T': // Send extended frame
                if (!canIsOpen() || canGetMode() != CAN_MODE_NORMAL)
                    slcanRespondError();
                else
                {
                    // TODO
                }
                break;
            case 'r': // Send standard remote frame
                if (!canIsOpen() || canGetMode() != CAN_MODE_NORMAL)
                    slcanRespondError();
                else
                {
                    // TODO
                }
                break;
            case 'R': // Send extended remote frame
                if (!canIsOpen() || canGetMode() != CAN_MODE_NORMAL)
                    slcanRespondError();
                else
                {
                    // TODO
                }
                break;
            case 'F': // Read and clear status flags
                if (!canIsOpen())
                    slcanRespondError();
                else
                {
                    // TODO
                }
                break;
            case 'V': // Query adapter version
                slcanRespondOk("V0000");
                break;
            case 'N': // Query adapter serial number (uses last 2 bytes of base MAC address)
            {
                uint8_t mac[6];
                esp_efuse_mac_get_default(mac);
                char sn[6];
                snprintf(sn, sizeof(sn), "N%.2X%.2X", mac[4], mac[5]);

                slcanRespondOk(sn);
                break;
            }
            default:
                slcanRespondError();
            }
        }
    }
}

/**
 * Handle received CAN frames
 */
static void slcanFramesTxTask(void *arg)
{
    while (1)
    {
        can_message_t msg;
        xQueueReceive(canRxQueue, &msg, portMAX_DELAY);

        char out[32];
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

    xTaskCreate(slcanRxTask, "SLCAN RX", 2048, NULL, SLCAN_TX_TASK_PRIO, NULL);
    xTaskCreate(slcanFramesTxTask, "SLCAN FRM TX", 2048, NULL, SLCAN_TX_TASK_PRIO, NULL);

    ESP_LOGI(TAG, "init completed");
}
