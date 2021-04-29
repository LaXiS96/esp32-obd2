/**
 * Implementation of LAWICEL Serial Line CAN protocol (http://www.can232.com/docs/canusb_manual.pdf)
 * Scope is to be compatible with Linux SocketCAN slcan driver, to allow usage of can-utils
 */

#include "slcan.h"

#include "config.h"
#include "message.h"
#include "can.h"

#include <string.h>
#include "FreeRTOS/task.h"
#include "esp_log.h"

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

static QueueHandle_t *_rxQueue;
static QueueHandle_t *_txQueue;

static twai_timing_config_t slcanTimingConfig50K = TWAI_TIMING_CONFIG_50KBITS();
static twai_timing_config_t slcanTimingConfig100K = TWAI_TIMING_CONFIG_100KBITS();
static twai_timing_config_t slcanTimingConfig125K = TWAI_TIMING_CONFIG_125KBITS();
static twai_timing_config_t slcanTimingConfig250K = TWAI_TIMING_CONFIG_250KBITS();
static twai_timing_config_t slcanTimingConfig500K = TWAI_TIMING_CONFIG_500KBITS();
static twai_timing_config_t slcanTimingConfig800K = TWAI_TIMING_CONFIG_800KBITS();
static twai_timing_config_t slcanTimingConfig1M = TWAI_TIMING_CONFIG_1MBITS();

static twai_timing_config_t *slcanChosenTimingConfig;

static void slcanTxMessage(char *data, size_t len)
{
    message_t msg = newMessage((uint8_t *)data, len);
    if (xQueueSend(_txQueue, &msg, 0) == errQUEUE_FULL)
        ESP_LOGE(TAG, "_txQueue FULL");
}

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
        slcanTxMessage(buf, strlen(buf));
    }
    else
    {
        ESP_LOGI(TAG, "respond ok");
        slcanTxMessage("\r", 1);
    }
}

/**
 * Send an error response (0x07)
 */
static void slcanRespondError(void)
{
    ESP_LOGI(TAG, "respond error");
    slcanTxMessage("\a", 1);
}

/**
 * Format received CAN frame for SLCAN output
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
static void slcanParseCommand(uint8_t *buf, size_t len)
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
                if (canTransmit(&frame) == ESP_OK)
                    slcanRespondOk("z");
                else
                {
                    ESP_LOGE(TAG, "canTransmit failed");
                    slcanRespondError();
                }
            }
            else
            {
                ESP_LOGE(TAG, "slcanParseFrame failed");
                slcanRespondError();
            }
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
                if (canTransmit(&frame) == ESP_OK)
                    slcanRespondOk("Z");
                else
                {
                    ESP_LOGE(TAG, "canTransmit failed");
                    slcanRespondError();
                }
            }
            else
            {
                ESP_LOGE(TAG, "slcanParseFrame failed");
                slcanRespondError();
            }
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
    message_t msg;
    uint8_t bufRemainder[SLCAN_MAX_CMD_LEN];
    size_t bufRemainderLen = 0;

    while (1)
    {
        xQueueReceive(_rxQueue, &msg, portMAX_DELAY);
        // ESP_LOG_BUFFER_HEXDUMP(TAG, msg.data, msg.length, ESP_LOG_INFO);

        uint8_t *pCmdStart = msg.data;                          // Command start position
        uint8_t *pCmdEnd = memchr(pCmdStart, '\r', msg.length); // Command end position (CR character)
        size_t cmdLen = 0;                                      // Command length

        // TODO
        // if (bufLen == sizeof(buf) && pCmdEnd == NULL)
        // { // TODO error: command longer than max command length, not permitted
        //     ESP_LOGE(TAG, "RX command buffer overrun");
        //     slcanRespondError();
        //     continue;
        // }

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

                slcanParseCommand(tmpBuf, tmpLen);

                free(tmpBuf);
                bufRemainderLen = 0;
            }
            else
            {
                slcanParseCommand(pCmdStart, cmdLen);
            }

            pCmdStart = pCmdEnd + 1;

            // Discard LF after CR
            if (*pCmdStart == '\n')
            {
                pCmdStart++;
                cmdLen++;
            }

            msg.length -= cmdLen;
            pCmdEnd = memchr(pCmdStart, '\r', msg.length);
        }

        // If buffer does not end with CR, save remaining characters for next iteration
        if (msg.length > 0)
        {
            memcpy(bufRemainder + bufRemainderLen, pCmdStart, msg.length);
            bufRemainderLen += msg.length;
        }

        free(msg.data);
    }
}

/**
 * Handle received CAN frames
 */
static void slcanTxTask(void *arg)
{
    while (1)
    {
        twai_message_t msg;
        xQueueReceive(canRxQueue, &msg, portMAX_DELAY);

        char out[32];
        slcanFormatFrame(&msg, out);
        slcanTxMessage(out, strlen(out));
    }
}

void slcanInit(QueueHandle_t *rxQueue, QueueHandle_t *txQueue)
{
    _rxQueue = rxQueue;
    _txQueue = txQueue;

    xTaskCreate(slcanRxTask, "slcanRx", 2048, NULL, SLCAN_RX_TASK_PRIO, NULL);
    xTaskCreate(slcanTxTask, "slcanTx", 2048, NULL, SLCAN_TX_TASK_PRIO, NULL);

    ESP_LOGI(TAG, "initialized");
}
