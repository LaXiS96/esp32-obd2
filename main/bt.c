#include "bt.h"

#include "message.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_spp_api.h"

#define TAG "BT"

QueueHandle_t btRxQueue;
QueueHandle_t btTxQueue;

static uint32_t sppHandle = 0;
static SemaphoreHandle_t sppWriteLock = NULL;
static message_t sppMessage; // Message to be written to SPP

static char *bda2str(uint8_t *bda, char *str, size_t size)
{
    if (bda == NULL || str == NULL || size < 18)
        return NULL;

    uint8_t *p = bda;
    sprintf(str, "%02x:%02x:%02x:%02x:%02x:%02x", p[0], p[1], p[2], p[3], p[4], p[5]);
    return str;
}

static void gapCallback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    char bda_str[18] = {0};

    switch (event)
    {
    case ESP_BT_GAP_AUTH_CMPL_EVT:
        ESP_LOGI(TAG, "ESP_BT_GAP_AUTH_CMPL_EVT status:%d device_name:%s bda:[%s]",
                 param->auth_cmpl.stat,
                 param->auth_cmpl.device_name,
                 bda2str(param->auth_cmpl.bda, bda_str, sizeof(bda_str)));
        break;
    default:
        ESP_LOGI(TAG, "GAP event:%d", event);
    }
}

static void sppCallback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
{
    char bda_str[18] = {0};

    switch (event)
    {
    case ESP_SPP_INIT_EVT:
        if (param->init.status == ESP_SPP_SUCCESS)
        {
            ESP_LOGI(TAG, "ESP_SPP_INIT_EVT");
            esp_spp_start_srv(ESP_SPP_SEC_AUTHENTICATE, ESP_SPP_ROLE_SLAVE, 0, "SPP");
        }
        else
        {
            ESP_LOGE(TAG, "ESP_SPP_INIT_EVT status:%d", param->init.status);
        }
        break;
    case ESP_SPP_CLOSE_EVT:
        ESP_LOGI(TAG, "ESP_SPP_CLOSE_EVT");
        sppHandle = 0;
        break;
    case ESP_SPP_START_EVT:
        if (param->start.status == ESP_SPP_SUCCESS)
        {
            ESP_LOGI(TAG, "ESP_SPP_START_EVT handle:%lu sec_id:%u scn:%u",
                     param->start.handle, param->start.sec_id, param->start.scn);
            esp_bt_dev_set_device_name("ESP32 OBD-II");
            esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
        }
        else
        {
            ESP_LOGE(TAG, "ESP_SPP_START_EVT status:%d", param->start.status);
        }
        break;
    case ESP_SPP_DATA_IND_EVT:
        ESP_LOGI(TAG, "ESP_SPP_DATA_IND_EVT length:%d", param->data_ind.len);
        message_t msg = message_new(param->data_ind.data, param->data_ind.len);
        if (xQueueSend(btRxQueue, &msg, 0) == errQUEUE_FULL)
            ESP_LOGE(TAG, "btRxQueue FULL");
        break;
    case ESP_SPP_CONG_EVT:
        ESP_LOGW(TAG, "ESP_SPP_CONG_EVT status:%d cong:%d", param->cong.status, param->cong.cong);
        if (!param->cong.cong) // Congestion resolved, allow new writes
            xSemaphoreGive(sppWriteLock);
        break;
    case ESP_SPP_WRITE_EVT:
        if (param->write.status != ESP_SPP_SUCCESS || param->write.cong)
            ESP_LOGW(TAG, "ESP_SPP_WRITE_EVT status:%d cong:%d len:%d", param->write.status, param->write.cong, param->write.len);

        // Allow new writes only if there is no congestion (ESP_SPP_CONG_EVT event will arrive otherwise)
        // TODO is cong valid if status != success?
        if (!param->write.cong)
            xSemaphoreGive(sppWriteLock);

        // TODO maybe it makes sense to resend if the write was not successful
        message_free(&sppMessage);
        break;
    case ESP_SPP_SRV_OPEN_EVT:
        ESP_LOGI(TAG, "ESP_SPP_SRV_OPEN_EVT status:%d handle:%lu rem_bda:[%s]",
                 param->srv_open.status,
                 param->srv_open.handle,
                 bda2str(param->srv_open.rem_bda, bda_str, sizeof(bda_str)));
        sppHandle = param->srv_open.handle;
        break;
    default:
        ESP_LOGI(TAG, "SPP event:%d", event);
    }
}

static void txTask(void *arg)
{
    while (1)
    {
        // Wait for previous write to finish
        xSemaphoreTake(sppWriteLock, portMAX_DELAY);

        // Read multiple messages from queue and send them at once
        message_t msg;
        uint8_t buf[512]; // TODO test size, look at stack usage
        uint8_t *pBuf = buf;
        uint8_t received = 0;
        while (xQueueReceive(btTxQueue, &msg, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            uint8_t free = buf + sizeof(buf) - pBuf;
            // ESP_LOGI(TAG, "free:%d", free);
            if (msg.length <= free)
            {
                memcpy(pBuf, msg.data, msg.length);
                pBuf += msg.length;
                received++;
                message_free(&msg);
            }
            else
            {
                // TODO store msg for next loop
                ESP_LOGW(TAG, "discarded message length:%d free:%d", msg.length, free);
                message_free(&msg);
                break;
            }
        }

        if (received > 0)
        {
            size_t len = pBuf - buf;

            if (sppHandle > 0)
            {
                sppMessage = message_new(buf, len);
                ESP_LOGI(TAG, "write messages:%d bytes:%d", received, sppMessage.length);
                // ESP_LOG_BUFFER_HEX(TAG, sppMessage.data, sppMessage.length);
                esp_spp_write(sppHandle, sppMessage.length, sppMessage.data);
                // sppMessage will be freed and sppWriteLock given in SPP callbacks
            }
            else
            {
                // SPP not connected, discard message
                // ESP_LOGI(TAG, "discard messages:%d bytes:%d", received, len);
                xSemaphoreGive(sppWriteLock);
            }
        }
        else
            xSemaphoreGive(sppWriteLock);
    }
}

void bt_init(void)
{
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_spp_cfg_t spp_cfg = {
        .mode = ESP_SPP_MODE_CB,
        .enable_l2cap_ertm = true,
        .tx_buffer_size = 0,
    };

    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());
    ESP_ERROR_CHECK(esp_bt_gap_register_callback(gapCallback));
    ESP_ERROR_CHECK(esp_spp_register_callback(sppCallback));
    ESP_ERROR_CHECK(esp_spp_enhanced_init(&spp_cfg));
    ESP_ERROR_CHECK(esp_bt_gap_set_pin(ESP_BT_PIN_TYPE_FIXED, 4, (esp_bt_pin_code_t){'0', '0', '0', '0'}));

    btRxQueue = xQueueCreate(CONFIG_APP_BT_QUEUES_LEN, sizeof(message_t));
    btTxQueue = xQueueCreate(CONFIG_APP_BT_QUEUES_LEN, sizeof(message_t));

    sppWriteLock = xSemaphoreCreateBinary();
    xSemaphoreGive(sppWriteLock);

    xTaskCreate(txTask, "btTx", 4096, NULL, CONFIG_APP_BT_TX_TASK_PRIO, NULL);

    ESP_LOGI(TAG, "initialized");
}
