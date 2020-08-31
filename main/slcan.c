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
#define SLCAN_TX_TASK_PRIO 1  // TODO
#define SLCAN_CMD_BUF_SIZE 32 // TODO

static struct slcan_state
{
    bool isOpen;
    enum slcan_mode
    {
        SLCAN_MODE_NORMAL = 1,
        SLCAN_MODE_LISTEN_ONLY = 2
    } mode;
    uint32_t bitrate;
} slcan_state = {
    .isOpen = false,
    .mode = SLCAN_MODE_NORMAL,
    .bitrate = 125000,
};

static const char *TAG = "slcan";

/* Hex to ASCII conversion lookup table */
static const char *HEX2ASCII = "0123456789ABCDEF";

/**
 * Return an OK response, with optional data
 * @param data can be NULL
 */
static void slcanRespondOk(char *data)
{
    if (data != NULL)
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "%s\r", data);
        uart_write_bytes(SLCAN_UART_NUM, buf, strlen(buf));
    }
    else
    {
        uart_write_bytes(SLCAN_UART_NUM, "\r", 1);
    }
}

static void slcanRespondError(void)
{
    uart_write_bytes(SLCAN_UART_NUM, "\a", 1);
}

/**
 * Format received CAN frame for UART output
 * @param buf must be at least TODO TBD long
 */
static void slcanFormatFrame(can_message_t *msg, char *buf)
{
    if (msg->extd)
    {
        if (msg->rtr)
            *buf++ = 'R';
        else
            *buf++ = 'T';

        // 29bit identifier
        *buf++ = HEX2ASCII[msg->identifier >> 28 & 0xF];
        *buf++ = HEX2ASCII[msg->identifier >> 24 & 0xF];
        *buf++ = HEX2ASCII[msg->identifier >> 20 & 0xF];
        *buf++ = HEX2ASCII[msg->identifier >> 16 & 0xF];
        *buf++ = HEX2ASCII[msg->identifier >> 12 & 0xF];
        *buf++ = HEX2ASCII[msg->identifier >> 8 & 0xF];
        *buf++ = HEX2ASCII[msg->identifier >> 4 & 0xF];
        *buf++ = HEX2ASCII[msg->identifier & 0xF];
    }
    else
    {
        if (msg->rtr)
            *buf++ = 'r';
        else
            *buf++ = 't';

        // 11bit identifier
        *buf++ = HEX2ASCII[msg->identifier >> 8 & 0xF];
        *buf++ = HEX2ASCII[msg->identifier >> 4 & 0xF];
        *buf++ = HEX2ASCII[msg->identifier & 0xF];
    }

    // Data Length Code
    *buf++ = HEX2ASCII[msg->data_length_code & 0xF];

    // Data bytes
    for (int i = 0; i < msg->data_length_code; i++)
    {
        *buf++ = HEX2ASCII[msg->data[i] >> 4];
        *buf++ = HEX2ASCII[msg->data[i] & 0xF];
    }

    *buf++ = '\r';
    *buf++ = '\0';
}

/**
 * Handle received SLCAN commands
 */
static void slcanRxTask(void *arg)
{
    while (1)
    {
        uint8_t cmd[SLCAN_CMD_BUF_SIZE];

        // Read from UART with a 10ms timeout
        int readBytes = uart_read_bytes(SLCAN_UART_NUM, cmd, sizeof(cmd), 10 / portTICK_PERIOD_MS);
        if (readBytes > 0)
        {
            switch (cmd[0])
            {
            case 'S': // Set CAN standard bitrate
                if (slcan_state.isOpen)
                    slcanRespondError();
                else
                {
                    switch (cmd[1])
                    {
                    case '0':
                        slcan_state.bitrate = 10000;
                        slcanRespondOk(NULL);
                        break;
                    case '1':
                        slcan_state.bitrate = 20000;
                        slcanRespondOk(NULL);
                        break;
                    case '2':
                        slcan_state.bitrate = 50000;
                        slcanRespondOk(NULL);
                        break;
                    case '3':
                        slcan_state.bitrate = 100000;
                        slcanRespondOk(NULL);
                        break;
                    case '4':
                        slcan_state.bitrate = 125000;
                        slcanRespondOk(NULL);
                        break;
                    case '5':
                        slcan_state.bitrate = 250000;
                        slcanRespondOk(NULL);
                        break;
                    case '6':
                        slcan_state.bitrate = 500000;
                        slcanRespondOk(NULL);
                        break;
                    case '7':
                        slcan_state.bitrate = 800000;
                        slcanRespondOk(NULL);
                        break;
                    case '8':
                        slcan_state.bitrate = 1000000;
                        slcanRespondOk(NULL);
                        break;
                    default:
                        slcanRespondError();
                    }
                }
                break;
            case 'O': // Open CAN channel
                if (slcan_state.isOpen)
                    slcanRespondError();
                else
                {
                    slcan_state.mode = SLCAN_MODE_NORMAL;
                    // TODO init can using config from slcan_state
                }
                break;
            case 'L': // Open CAN channel in listen-only mode
                if (slcan_state.isOpen)
                    slcanRespondError();
                else
                {
                    slcan_state.mode = SLCAN_MODE_LISTEN_ONLY;
                    // TODOinit can using config from slcan_state
                }
                break;
            case 'C': // Close CAN channel
                if (!slcan_state.isOpen)
                    slcanRespondError();
                else
                {
                    // TODO
                }
                break;
            case 't': // Send standard frame
                if (!slcan_state.isOpen || slcan_state.mode != SLCAN_MODE_NORMAL)
                    slcanRespondError();
                else
                {
                    // TODO
                }
                break;
            case 'T': // Send extended frame
                if (!slcan_state.isOpen || slcan_state.mode != SLCAN_MODE_NORMAL)
                    slcanRespondError();
                else
                {
                    // TODO
                }
                break;
            case 'r': // Send standard remote frame
                if (!slcan_state.isOpen || slcan_state.mode != SLCAN_MODE_NORMAL)
                    slcanRespondError();
                else
                {
                    // TODO
                }
                break;
            case 'R': // Send extended remote frame
                if (!slcan_state.isOpen || slcan_state.mode != SLCAN_MODE_NORMAL)
                    slcanRespondError();
                else
                {
                    // TODO
                }
                break;
            case 'F': // Read and clear status flags
                if (!slcan_state.isOpen)
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
