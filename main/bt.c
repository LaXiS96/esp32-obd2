#include "bt.h"

#include "message.h"

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
static SemaphoreHandle_t sppWriteLock;
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
    switch (event)
    {
    case ESP_BT_GAP_AUTH_CMPL_EVT:
        ESP_LOGI(TAG, "ESP_BT_GAP_AUTH_CMPL_EVT");
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS)
        {
            ESP_LOGI(TAG, "authentication success: %s", param->auth_cmpl.device_name);
            esp_log_buffer_hex(TAG, param->auth_cmpl.bda, ESP_BD_ADDR_LEN);
        }
        else
        {
            ESP_LOGE(TAG, "authentication failed, status:%d", param->auth_cmpl.stat);
        }
        break;
    default:
        ESP_LOGI(TAG, "GAP event:%d", event);
    }
}

static void sppCallback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
{
    static bool writeFailed = false;
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
            ESP_LOGI(TAG, "ESP_SPP_START_EVT handle:%" PRIu32 " sec_id:%d scn:%d", param->start.handle, param->start.sec_id, param->start.scn);
            esp_bt_dev_set_device_name("ESP32 OBD-II");
            esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
        }
        else
        {
            ESP_LOGE(TAG, "ESP_SPP_START_EVT status:%d", param->start.status);
        }
        break;
    case ESP_SPP_DATA_IND_EVT:
        // ESP_LOGI(TAG, "ESP_SPP_DATA_IND_EVT length:%d", param->data_ind.len);
        message_t msg = message_new(param->data_ind.data, param->data_ind.len);
        if (xQueueSend(btRxQueue, &msg, 0) == errQUEUE_FULL)
            ESP_LOGE(TAG, "btRxQueue FULL");
        break;
    case ESP_SPP_CONG_EVT:
        ESP_LOGW(TAG, "ESP_SPP_CONG_EVT status:%d cong:%d", param->cong.status, param->cong.cong);
        if (!param->cong.cong)
        {
            if (writeFailed)
            {
                // Resend the current message
                writeFailed = false;
                esp_spp_write(sppHandle, sppMessage.length, sppMessage.data);
            }
            else
            {
                // Congestion resolved, clean up and allow new writes
                message_free(&sppMessage);
                xSemaphoreGive(sppWriteLock);
            }
        }
        break;
    case ESP_SPP_WRITE_EVT:
        if (param->write.status != ESP_SPP_SUCCESS || param->write.cong || param->write.len == -1)
            ESP_LOGW(TAG, "ESP_SPP_WRITE_EVT status:%d cong:%d len:%d", param->write.status, param->write.cong, param->write.len);

        if (param->write.status == ESP_SPP_SUCCESS)
        {
            if (!param->write.cong)
            {
                // Successful write without congestion, clean up and allow new writes
                message_free(&sppMessage);
                xSemaphoreGive(sppWriteLock);
            }
        }
        else if (param->write.len == -1)
        {
            // Write failed for congestion? Message will be resent once congestion is resolved
            writeFailed = true;
        }
        break;
    case ESP_SPP_SRV_OPEN_EVT:
        ESP_LOGI(TAG, "ESP_SPP_SRV_OPEN_EVT status:%d handle:%" PRIu32 ", rem_bda:[%s]",
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
        // Dequeue new message once previous write is finished
        xSemaphoreTake(sppWriteLock, portMAX_DELAY);
        xQueueReceive(btTxQueue, &sppMessage, portMAX_DELAY);

        if (sppHandle > 0)
        {
            esp_spp_write(sppHandle, sppMessage.length, sppMessage.data);
            // Message will be freed in SPP callback
        }
        else
        {
            // SPP not connected, discard message
            message_free(&sppMessage);
            xSemaphoreGive(sppWriteLock);
        }
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

    xTaskCreate(txTask, "btTx", 2048, NULL, CONFIG_APP_BT_TX_TASK_PRIO, NULL);

    ESP_LOGI(TAG, "initialized");
}
