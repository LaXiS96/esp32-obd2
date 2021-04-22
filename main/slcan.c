/**
 * Implementation of LAWICEL Serial Line CAN protocol (http://www.can232.com/docs/canusb_manual.pdf)
 * Scope is to be compatible with Linux SocketCAN slcan driver, to allow usage of can-utils
 */

#include "slcan.h"

#include "util.h"
#include "can.h"

#include <string.h>
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/uart.h"

#define SLCAN_UART_NUM UART_NUM_0  // ESP console log moved to UART1 via menuconfig
#define SLCAN_UART_TXD_GPIO_NUM 1  // UART2: GPIO_NUM_17
#define SLCAN_UART_RXD_GPIO_NUM 3  // UART2: GPIO_NUM_16
#define SLCAN_UART_BAUDRATE 921600 // default CP2102 config also supports 1200000 and 1500000
#define SLCAN_UART_BUF_SIZE 1024
#define SLCAN_TX_TASK_PRIO 1 // TODO

#define SLCAN_MAX_CMD_LEN (strlen("T1FFFFFFF81122334455667788\r"))

static const char *TAG = "SLCAN";

/** Hex to ASCII conversion function */
#define HEX2ASCII(x) HEX2ASCII_LUT[(x)]
static const char *HEX2ASCII_LUT = "0123456789ABCDEF";

// clang-format off
/** ASCII to hex conversion function */
#define ASCII2HEX(x) ASCII2HEX_LUT[(x) - 0x30]
static const uint8_t ASCII2HEX_LUT[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
    [17] = 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    [49] = 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
};
// clang-format on

static twai_timing_config_t slcanTimingConfig50K = TWAI_TIMING_CONFIG_50KBITS();
static twai_timing_config_t slcanTimingConfig100K = TWAI_TIMING_CONFIG_100KBITS();
static twai_timing_config_t slcanTimingConfig125K = TWAI_TIMING_CONFIG_125KBITS();
static twai_timing_config_t slcanTimingConfig250K = TWAI_TIMING_CONFIG_250KBITS();
static twai_timing_config_t slcanTimingConfig500K = TWAI_TIMING_CONFIG_500KBITS();
static twai_timing_config_t slcanTimingConfig800K = TWAI_TIMING_CONFIG_800KBITS();
static twai_timing_config_t slcanTimingConfig1M = TWAI_TIMING_CONFIG_1MBITS();

static twai_timing_config_t *slcanChosenTimingConfig;

/**
 * Send an OK response (0x0D), with optional data
 * @param data string, can be NULL
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
 * Send an error response (0x07)
 */
static void slcanRespondError(void)
{
    ESP_LOGI(TAG, "respond error");
    uart_write_bytes(SLCAN_UART_NUM, "\a", 1);
}

/**
 * Format received CAN frame for UART output
 * @param msg input frame
 * @param str formatted output string, must be at least TODO long
 */
static void slcanFormatFrame(twai_message_t *msg, char *str)
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
 * @param str input command buffer
 * @param len input command buffer length
 * @param msg output parsed CAN frame
 */
static esp_err_t slcanParseFrame(uint8_t *buf, size_t len, twai_message_t *msg)
{
    if (len == 0)
        return ESP_FAIL;

    const uint8_t minStdLen = strlen("t1FF0\r");
    const uint8_t minExtLen = strlen("T1FFFFFFF0\r");
    uint8_t *pBuf = buf;

    msg->flags = 0;
    msg->extd = (*pBuf == 'T' || *pBuf == 'R') ? 1 : 0;
    msg->rtr = (*pBuf == 'r' || *pBuf == 'R') ? 1 : 0;

    pBuf++;

    msg->identifier = 0;
    if (msg->extd)
    {
        if (len < minExtLen)
            return ESP_FAIL;

        msg->identifier |= ASCII2HEX(*pBuf++) << 28;
        msg->identifier |= ASCII2HEX(*pBuf++) << 24;
        msg->identifier |= ASCII2HEX(*pBuf++) << 20;
        msg->identifier |= ASCII2HEX(*pBuf++) << 16;
        msg->identifier |= ASCII2HEX(*pBuf++) << 12;
        msg->identifier |= ASCII2HEX(*pBuf++) << 8;
        msg->identifier |= ASCII2HEX(*pBuf++) << 4;
        msg->identifier |= ASCII2HEX(*pBuf++);

        msg->data_length_code = ASCII2HEX(*pBuf++);

        if (len < minExtLen + msg->data_length_code * 2)
            return ESP_FAIL;
    }
    else
    {
        if (len < minStdLen)
            return ESP_FAIL;

        msg->identifier |= ASCII2HEX(*pBuf++) << 8;
        msg->identifier |= ASCII2HEX(*pBuf++) << 4;
        msg->identifier |= ASCII2HEX(*pBuf++);

        msg->data_length_code = ASCII2HEX(*pBuf++);

        if (len < minStdLen + msg->data_length_code * 2)
            return ESP_FAIL;
    }

    for (uint8_t i = 0; i < msg->data_length_code; i++)
    {
        if (len - 1 < pBuf - buf + 2) // At least 2 characters excluding CR
            return ESP_FAIL;

        msg->data[i] = 0;
        msg->data[i] |= ASCII2HEX(*pBuf++) << 4;
        msg->data[i] |= ASCII2HEX(*pBuf++);
    }

    return ESP_OK;
}

/**
 * Parse received command and perform requested action
 */
static void slcanExecuteCommand(uint8_t *buf, size_t len)
{
    ESP_LOGI(TAG, "command \"%.*s\"", len - 1, buf);

    if (len == 0)
    {
        slcanRespondError();
        return;
    }

    switch (buf[0])
    {
    case 'S': // Set CAN standard bitrate
        if (canIsOpen())
            slcanRespondError();
        else
        {
            switch (buf[1])
            {
            case '0':
            case '1':
                // 10kbps and 20kbps unsupported
                slcanRespondError();
                break;
            case '2':
                slcanChosenTimingConfig = &slcanTimingConfig50K;
                slcanRespondOk(NULL);
                break;
            case '3':
                slcanChosenTimingConfig = &slcanTimingConfig100K;
                slcanRespondOk(NULL);
                break;
            case '4':
                slcanChosenTimingConfig = &slcanTimingConfig125K;
                slcanRespondOk(NULL);
                break;
            case '5':
                slcanChosenTimingConfig = &slcanTimingConfig250K;
                slcanRespondOk(NULL);
                break;
            case '6':
                slcanChosenTimingConfig = &slcanTimingConfig500K;
                slcanRespondOk(NULL);
                break;
            case '7':
                slcanChosenTimingConfig = &slcanTimingConfig800K;
                slcanRespondOk(NULL);
                break;
            case '8':
                slcanChosenTimingConfig = &slcanTimingConfig1M;
                slcanRespondOk(NULL);
                break;
            default:
                slcanRespondError();
            }
        }
        break;
    case 'O': // Open CAN channel
        if (canIsOpen() || slcanChosenTimingConfig == NULL)
            slcanRespondError();
        else
        {
            if (canOpen(TWAI_MODE_NORMAL, slcanChosenTimingConfig) == ESP_OK)
                slcanRespondOk(NULL);
            else
                slcanRespondError();
        }
        break;
    case 'L': // Open CAN channel in listen-only mode
        if (canIsOpen() || slcanChosenTimingConfig == NULL)
            slcanRespondError();
        else
        {
            if (canOpen(TWAI_MODE_LISTEN_ONLY, slcanChosenTimingConfig) == ESP_OK)
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
    case 'r': // Send standard remote frame
        if (!canIsOpen() || canGetMode() != TWAI_MODE_NORMAL)
            slcanRespondError();
        else
        {
            twai_message_t frame;
            if (slcanParseFrame(buf, len, &frame) == ESP_OK)
            {
                if (canTransmit(&frame))
                    slcanRespondOk("z");
                else
                    slcanRespondError();
            }
            else
                slcanRespondError();
        }
        break;
    case 'T': // Send extended frame
    case 'R': // Send extended remote frame
        if (!canIsOpen() || canGetMode() != TWAI_MODE_NORMAL)
            slcanRespondError();
        else
        {
            twai_message_t frame;
            if (slcanParseFrame(buf, len, &frame) == ESP_OK)
            {
                if (canTransmit(&frame))
                    slcanRespondOk("Z");
                else
                    slcanRespondError();
            }
            else
                slcanRespondError();
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

/**
 * Handle received SLCAN commands
 */
static void slcanRxTask(void *arg)
{
    uint8_t bufRemainder[SLCAN_MAX_CMD_LEN];
    size_t bufRemainderLen = 0;

    while (1)
    {
        uint8_t buf[SLCAN_MAX_CMD_LEN]; // Received data buffer
        size_t bufLen = 0;              // Received data length

        // Read from UART every 100ms and split commands by CR
        bufLen = uart_read_bytes(SLCAN_UART_NUM, buf, sizeof(buf), pdMS_TO_TICKS(100));
        if (bufLen > 0)
        {
            uint8_t *pCmdStart = buf;                           // Current command start position
            uint8_t *pCmdEnd = memchr(pCmdStart, '\r', bufLen); // Current command end position (CR character)
            size_t cmdLen = 0;                                  // Current command length

            if (bufLen == sizeof(buf) && pCmdEnd == NULL)
            { // TODO error: command longer than max command length, not permitted
                ESP_LOGE(TAG, "RX command buffer overrun");
                slcanRespondError();
                continue;
            }

            while (pCmdEnd != NULL)
            {
                cmdLen = pCmdEnd - pCmdStart + 1;

                if (bufRemainderLen > 0)
                {
                    size_t tmpLen = bufRemainderLen + cmdLen;

                    // Concatenate remainder with current command and parse it
                    uint8_t *tmpBuf = malloc(tmpLen);
                    memcpy(tmpBuf, bufRemainder, bufRemainderLen);
                    memcpy(tmpBuf + bufRemainderLen, pCmdStart, cmdLen);

                    slcanExecuteCommand(tmpBuf, tmpLen);

                    free(tmpBuf);
                    bufRemainderLen = 0;
                }
                else
                {
                    slcanExecuteCommand(pCmdStart, cmdLen);
                }

                pCmdStart = pCmdEnd + 1;
                bufLen -= cmdLen;
                pCmdEnd = memchr(pCmdStart, '\r', bufLen);
            }

            // If buffer does not end with CR, save remaining characters for next iteration
            if (bufLen > 0)
            {
                memcpy(bufRemainder, pCmdStart, bufLen);
                bufRemainderLen += bufLen;
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
        twai_message_t msg;
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
