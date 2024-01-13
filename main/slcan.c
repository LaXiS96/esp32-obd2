/*
Implementation of LAWICEL Serial Line CAN protocol (http://www.can232.com/docs/canusb_manual.pdf)
Target is to be compatible with Linux SocketCAN slcan driver, to allow usage of can-utils
*/

#include "slcan.h"

#include "config.h"
#include "message.h"
#include "can.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_mac.h"

#define SLCAN_MIN_STD_CMD_LEN (strlen("t1FF0\r"))
#define SLCAN_MIN_EXT_CMD_LEN (strlen("T1FFFFFFF0\r"))
#define SLCAN_MAX_CMD_LEN (strlen("T1FFFFFFF81122334455667788\r"))

static const char *TAG = "SLCAN";

/// @brief Hex to ASCII conversion function
#define HEX2ASCII(x) HEX2ASCII_LUT[(x)]
static const char *HEX2ASCII_LUT = "0123456789ABCDEF";

// clang-format off

/// @brief ASCII to hex conversion function
#define ASCII2HEX(x) ASCII2HEX_LUT[(x) - 0x30]
static const uint8_t ASCII2HEX_LUT[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
    [17] = 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    [49] = 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
};
// clang-format on

static QueueHandle_t *_rxQueue;
static QueueHandle_t *_txQueue;

static twai_timing_config_t timingConfig = {};

static void sendSerialMessage(char *data, size_t len)
{
    message_t msg = message_new((uint8_t *)data, len);
    if (xQueueSend(*_txQueue, &msg, 0) == errQUEUE_FULL)
        ESP_LOGE(TAG, "_txQueue full");
}

/// @brief Send an OK response (0x0D), with optional data
/// @param data reply string, can be NULL
static void sendOkResponse(char *data)
{
    if (data != NULL)
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "%s\r", data);

        ESP_LOGI(TAG, "respond ok data=\"%s\"", data);
        sendSerialMessage(buf, strlen(buf));
    }
    else
    {
        ESP_LOGI(TAG, "respond ok");
        sendSerialMessage("\r", 1);
    }
}

/// @brief Send an error response (0x07)
static void sendErrorResponse(void)
{
    ESP_LOGI(TAG, "respond error");
    sendSerialMessage("\a", 1);
}

/// @brief Format received CAN frame for SLCAN output
/// @param msg input frame
/// @param str formatted output string, must be at least SLCAN_MAX_CMD_LEN+1 long
static void formatFrame(twai_message_t *msg, char *str)
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

/// @brief Parse t, T, r, R frame commands
/// @param str input command buffer
/// @param len input command buffer length
/// @param msg output parsed CAN frame
static esp_err_t parseFrame(uint8_t *buf, size_t len, twai_message_t *msg)
{
    if (len == 0)
        return ESP_FAIL;

    uint8_t *pBuf = buf;

    msg->flags = 0;
    msg->extd = (*pBuf == 'T' || *pBuf == 'R') ? 1 : 0;
    msg->rtr = (*pBuf == 'r' || *pBuf == 'R') ? 1 : 0;

    pBuf++;

    msg->identifier = 0;
    if (msg->extd)
    {
        if (len < SLCAN_MIN_EXT_CMD_LEN)
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

        if (len < SLCAN_MIN_EXT_CMD_LEN + msg->data_length_code * 2)
            return ESP_FAIL;
    }
    else
    {
        if (len < SLCAN_MIN_STD_CMD_LEN)
            return ESP_FAIL;

        msg->identifier |= ASCII2HEX(*pBuf++) << 8;
        msg->identifier |= ASCII2HEX(*pBuf++) << 4;
        msg->identifier |= ASCII2HEX(*pBuf++);

        msg->data_length_code = ASCII2HEX(*pBuf++);

        if (len < SLCAN_MIN_STD_CMD_LEN + msg->data_length_code * 2)
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

/// @brief Parse received command and perform requested action
static void parseCommand(uint8_t *buf, size_t len)
{
    ESP_LOGI(TAG, "command \"%.*s\"", len - 1, buf);

    if (len == 0)
    {
        sendErrorResponse();
        return;
    }

    switch (buf[0])
    {
    case 'S': // Set CAN standard bitrate
        if (can_isOpen())
            sendErrorResponse();
        else
        {
            switch (buf[1])
            {
            case '0':
            case '1':
                // 10kbps and 20kbps unsupported
                sendErrorResponse();
                break;
            case '2':
                timingConfig = (twai_timing_config_t)TWAI_TIMING_CONFIG_50KBITS();
                sendOkResponse(NULL);
                break;
            case '3':
                timingConfig = (twai_timing_config_t)TWAI_TIMING_CONFIG_100KBITS();
                sendOkResponse(NULL);
                break;
            case '4':
                timingConfig = (twai_timing_config_t)TWAI_TIMING_CONFIG_125KBITS();
                sendOkResponse(NULL);
                break;
            case '5':
                timingConfig = (twai_timing_config_t)TWAI_TIMING_CONFIG_250KBITS();
                sendOkResponse(NULL);
                break;
            case '6':
                timingConfig = (twai_timing_config_t)TWAI_TIMING_CONFIG_500KBITS();
                sendOkResponse(NULL);
                break;
            case '7':
                timingConfig = (twai_timing_config_t)TWAI_TIMING_CONFIG_800KBITS();
                sendOkResponse(NULL);
                break;
            case '8':
                timingConfig = (twai_timing_config_t)TWAI_TIMING_CONFIG_1MBITS();
                sendOkResponse(NULL);
                break;
            default:
                sendErrorResponse();
            }
        }
        break;
    case 'O': // Open CAN channel
              // TODO check if timingConfig was set previously
        if (can_isOpen())
        {
            ESP_LOGE(TAG, "already open or brp:%" PRIu32, timingConfig.brp);
            sendErrorResponse();
        }
        else
        {
            esp_err_t res = can_open(TWAI_MODE_NORMAL, &timingConfig);
            if (res == ESP_OK)
                sendOkResponse(NULL);
            else
            {
                ESP_LOGE(TAG, "can_open returned %s", esp_err_to_name(res));
                sendErrorResponse();
            }
        }
        break;
    case 'L': // Open CAN channel in listen-only mode
        if (can_isOpen() || timingConfig.brp == 0) // TODO
            sendErrorResponse();
        else
        {
            if (can_open(TWAI_MODE_LISTEN_ONLY, &timingConfig) == ESP_OK)
                sendOkResponse(NULL);
            else
                sendErrorResponse();
        }
        break;
    case 'C': // Close CAN channel
        if (!can_isOpen())
            sendErrorResponse();
        else
        {
            if (can_close() == ESP_OK)
                sendOkResponse(NULL);
            else
                sendErrorResponse();
        }
        break;
    case 't': // Send standard frame
    case 'r': // Send standard remote frame
        if (!can_isOpen() || can_getMode() != TWAI_MODE_NORMAL)
            sendErrorResponse();
        else
        {
            twai_message_t frame;
            if (parseFrame(buf, len, &frame) == ESP_OK)
            {
                if (can_transmit(&frame) == ESP_OK)
                    sendOkResponse("z");
                else
                {
                    ESP_LOGE(TAG, "can_transmit failed");
                    sendErrorResponse();
                }
            }
            else
            {
                ESP_LOGE(TAG, "parseFrame failed");
                sendErrorResponse();
            }
        }
        break;
    case 'T': // Send extended frame
    case 'R': // Send extended remote frame
        if (!can_isOpen() || can_getMode() != TWAI_MODE_NORMAL)
            sendErrorResponse();
        else
        {
            twai_message_t frame;
            if (parseFrame(buf, len, &frame) == ESP_OK)
            {
                if (can_transmit(&frame) == ESP_OK)
                    sendOkResponse("Z");
                else
                {
                    ESP_LOGE(TAG, "can_transmit failed");
                    sendErrorResponse();
                }
            }
            else
            {
                ESP_LOGE(TAG, "parseFrame failed");
                sendErrorResponse();
            }
        }
        break;
    case 'F': // Read and clear status flags
        if (!can_isOpen())
            sendErrorResponse();
        else
        {
            // TODO
        }
        break;
    case 'V': // Query adapter version
        sendOkResponse("V0000");
        break;
    case 'N': // Query adapter serial number (uses last 2 bytes of base MAC address)
    {
        uint8_t mac[6];
        esp_efuse_mac_get_default(mac);
        char sn[6];
        snprintf(sn, sizeof(sn), "N%.2X%.2X", mac[4], mac[5]);

        sendOkResponse(sn);
        break;
    }
    default:
        sendErrorResponse();
    }
}

/// @brief Handle received SLCAN commands
static void serialRxTask(void *arg)
{
    message_t msg;
    uint8_t bufRemainder[SLCAN_MAX_CMD_LEN];
    size_t bufRemainderLen = 0;

    while (1)
    {
        xQueueReceive(*_rxQueue, &msg, portMAX_DELAY);
        // ESP_LOG_BUFFER_HEXDUMP(TAG, msg.data, msg.length, ESP_LOG_INFO);

        uint8_t *pCmdStart = msg.data;                          // Command start position
        uint8_t *pCmdEnd = memchr(pCmdStart, '\r', msg.length); // Command end position (CR character)
        size_t cmdLen = 0;                                      // Command length

        // TODO
        // if (bufLen == sizeof(buf) && pCmdEnd == NULL)
        // { // TODO error: command longer than max command length, not permitted
        //     ESP_LOGE(TAG, "RX command buffer overrun");
        //     sendErrorResponse();
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

                parseCommand(tmpBuf, tmpLen);

                free(tmpBuf);
                bufRemainderLen = 0;
            }
            else
            {
                parseCommand(pCmdStart, cmdLen);
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

        message_free(&msg);
    }
}

/// @brief Handle received CAN frames
static void canRxTask(void *arg)
{
    while (1)
    {
        twai_message_t msg;
        xQueueReceive(can_rxQueue, &msg, portMAX_DELAY);
        // ESP_LOGW(TAG, "received from can_rxQueue id:%" PRIu32, msg.identifier);

        char out[32];
        formatFrame(&msg, out);
        sendSerialMessage(out, strlen(out));
    }
}

void slcan_init(QueueHandle_t *rxQueue, QueueHandle_t *txQueue)
{
    _rxQueue = rxQueue;
    _txQueue = txQueue;

    xTaskCreate(serialRxTask, "slcan serialRx", 3072, NULL, CONFIG_APP_SLCAN_SERIAL_RX_TASK_PRIO, NULL);
    xTaskCreate(canRxTask, "slcan canRx", 2048, NULL, CONFIG_APP_SLCAN_CAN_RX_TASK_PRIO, NULL);

    ESP_LOGI(TAG, "initialized");
}
