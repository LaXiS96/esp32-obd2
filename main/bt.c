#include "bt.h"

#include "config.h"
#include "message.h"

#include "FreeRTOS/task.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_spp_api.h"

static const char *TAG = "BT";

static uint32_t btCurrentHandle = 0;

static void btGapCallback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event)
    {
    case ESP_BT_GAP_DISC_RES_EVT:
        ESP_LOGI(TAG, "ESP_BT_GAP_DISC_RES_EVT");
        break;
    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
        ESP_LOGI(TAG, "ESP_BT_GAP_DISC_STATE_CHANGED_EVT");
        break;
    case ESP_BT_GAP_RMT_SRVCS_EVT:
        ESP_LOGI(TAG, "ESP_BT_GAP_RMT_SRVCS_EVT");
        break;
    case ESP_BT_GAP_RMT_SRVC_REC_EVT:
        ESP_LOGI(TAG, "ESP_BT_GAP_RMT_SRVC_REC_EVT");
        break;
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
    case ESP_BT_GAP_PIN_REQ_EVT:
        ESP_LOGI(TAG, "ESP_BT_GAP_PIN_REQ_EVT min_16_digit:%d", param->pin_req.min_16_digit);
        if (param->pin_req.min_16_digit)
        {
            ESP_LOGI(TAG, "Input pin code: 0000 0000 0000 0000");
            esp_bt_pin_code_t pin_code = {0};
            esp_bt_gap_pin_reply(param->pin_req.bda, true, 16, pin_code);
        }
        else
        {
            ESP_LOGI(TAG, "Input pin code: 0000");
            esp_bt_pin_code_t pin_code = {0};
            esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin_code);
        }
        break;
#if (CONFIG_BT_SSP_ENABLED == true)
    case ESP_BT_GAP_CFM_REQ_EVT:
        ESP_LOGI(TAG, "ESP_BT_GAP_CFM_REQ_EVT Please compare the numeric value: %d", param->cfm_req.num_val);
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
        break;
    case ESP_BT_GAP_KEY_NOTIF_EVT:
        ESP_LOGI(TAG, "ESP_BT_GAP_KEY_NOTIF_EVT passkey:%d", param->key_notif.passkey);
        break;
    case ESP_BT_GAP_KEY_REQ_EVT:
        ESP_LOGI(TAG, "ESP_BT_GAP_KEY_REQ_EVT Please enter passkey!");
        break;
#endif
    case ESP_BT_GAP_READ_RSSI_DELTA_EVT:
        ESP_LOGI(TAG, "ESP_BT_GAP_READ_RSSI_DELTA_EVT");
        break;
    case ESP_BT_GAP_CONFIG_EIR_DATA_EVT:
        ESP_LOGI(TAG, "ESP_BT_GAP_CONFIG_EIR_DATA_EVT");
        break;
    case ESP_BT_GAP_SET_AFH_CHANNELS_EVT:
        ESP_LOGI(TAG, "ESP_BT_GAP_SET_AFH_CHANNELS_EVT");
        break;
    case ESP_BT_GAP_READ_REMOTE_NAME_EVT:
        ESP_LOGI(TAG, "ESP_BT_GAP_READ_REMOTE_NAME_EVT");
        break;
    case ESP_BT_GAP_REMOVE_BOND_DEV_COMPLETE_EVT:
        ESP_LOGI(TAG, "ESP_BT_GAP_REMOVE_BOND_DEV_COMPLETE_EVT");
        break;
    default:
        ESP_LOGI(TAG, "GAP event: %d", event);
    }
}

static void btSppCallback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
{
    switch (event)
    {
    case ESP_SPP_INIT_EVT:
        ESP_LOGI(TAG, "ESP_SPP_INIT_EVT");
        esp_bt_dev_set_device_name("ESP32 OBD-II");
        esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
        esp_spp_start_srv(ESP_SPP_SEC_AUTHENTICATE, ESP_SPP_ROLE_SLAVE, 0, "SPP");
        break;
    case ESP_SPP_UNINIT_EVT:
        ESP_LOGI(TAG, "ESP_SPP_UNINIT_EVT");
        break;
    case ESP_SPP_DISCOVERY_COMP_EVT:
        ESP_LOGI(TAG, "ESP_SPP_DISCOVERY_COMP_EVT");
        break;
    case ESP_SPP_OPEN_EVT:
        ESP_LOGI(TAG, "ESP_SPP_OPEN_EVT");
        break;
    case ESP_SPP_CLOSE_EVT:
        ESP_LOGI(TAG, "ESP_SPP_CLOSE_EVT");
        btCurrentHandle = 0;
        break;
    case ESP_SPP_START_EVT:
        ESP_LOGI(TAG, "ESP_SPP_START_EVT");
        break;
    case ESP_SPP_CL_INIT_EVT:
        ESP_LOGI(TAG, "ESP_SPP_CL_INIT_EVT");
        break;
    case ESP_SPP_DATA_IND_EVT:
        ESP_LOGI(TAG, "ESP_SPP_DATA_IND_EVT length:%d", param->data_ind.len);
        message_t msg = newMessage(param->data_ind.data, param->data_ind.len);
        if (xQueueSend(btRxQueue, &msg, 0) == errQUEUE_FULL)
            ESP_LOGE(TAG, "btRxQueue FULL");
        break;
    case ESP_SPP_CONG_EVT:
        ESP_LOGI(TAG, "ESP_SPP_CONG_EVT");
        break;
    case ESP_SPP_WRITE_EVT:
        ESP_LOGI(TAG, "ESP_SPP_WRITE_EVT");
        break;
    case ESP_SPP_SRV_OPEN_EVT:
        ESP_LOGI(TAG, "ESP_SPP_SRV_OPEN_EVT handle:%d", param->open.handle);
        btCurrentHandle = param->open.handle;
        break;
    case ESP_SPP_SRV_STOP_EVT:
        ESP_LOGI(TAG, "ESP_SPP_SRV_STOP_EVT");
        break;
    default:
        ESP_LOGI(TAG, "SPP event: %d", event);
    }
}

static void btTxTask(void *arg)
{
    message_t msg;

    while (1)
    {
        xQueueReceive(btTxQueue, &msg, portMAX_DELAY);

        if (btCurrentHandle > 0)
        {
            // TODO I recall reading that SPP max data length is 128
            esp_spp_write(btCurrentHandle, msg.length, msg.data);
        }

        free(msg.data);
    }
}

void btInit(void)
{
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());
    ESP_ERROR_CHECK(esp_bt_gap_register_callback(btGapCallback));
    ESP_ERROR_CHECK(esp_spp_register_callback(btSppCallback));
    ESP_ERROR_CHECK(esp_spp_init(ESP_SPP_MODE_CB));

#if (CONFIG_BT_SSP_ENABLED == true)
    // Secure Simple Pairing
    esp_bt_sp_param_t param_type = ESP_BT_SP_IOCAP_MODE;
    esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_IO;
    esp_bt_gap_set_security_param(param_type, &iocap, sizeof(uint8_t));
#endif

    // Legacy pairing
    esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_VARIABLE;
    esp_bt_pin_code_t pin_code;
    esp_bt_gap_set_pin(pin_type, 0, pin_code);

    btRxQueue = xQueueCreate(BT_QUEUES_LEN, sizeof(message_t));
    btTxQueue = xQueueCreate(BT_QUEUES_LEN, sizeof(message_t));

    xTaskCreate(btTxTask, "btTx", 2048, NULL, BT_TX_TASK_PRIO, NULL);

    ESP_LOGI(TAG, "initialized");
}
