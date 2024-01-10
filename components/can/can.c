#include "can.h"

#include "config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/twai.h"

#define TAG "CAN"
#define NOTIFY_OPEN (1 << 0)

QueueHandle_t can_rxQueue;

static twai_general_config_t *canGeneralConfig;
static TaskHandle_t rxTaskHandle = NULL;

static void canRxTask(void *arg)
{
    uint32_t notified = 0;

    while (1)
    {
        // Block task while CAN is not open
        xTaskNotifyWait(0, 0, &notified, portMAX_DELAY);
        if ((notified & NOTIFY_OPEN) != 0)
        {
            twai_message_t msg;

            // Try receiving for 100ms max and sending to queue for 100ms if full
            if (twai_receive(&msg, pdMS_TO_TICKS(100)) == ESP_OK)
                if (xQueueSend(can_rxQueue, &msg, pdMS_TO_TICKS(100)) == errQUEUE_FULL)
                    ESP_LOGE(TAG, "can_rxQueue full");
        }
    }
}

void can_init(void)
{
    can_rxQueue = xQueueCreate(8, sizeof(twai_message_t));

    xTaskCreate(canRxTask, "CAN RX", 4096, NULL, CONFIG_CAN_RX_TASK_PRIO, &rxTaskHandle);

    ESP_LOGI(TAG, "initialized");
}

bool can_isOpen(void)
{
    if (rxTaskHandle == NULL)
        return false;

    uint32_t notified = ulTaskNotifyValueClear(rxTaskHandle, 0);
    return (notified & NOTIFY_OPEN) != 0;
}

twai_mode_t can_getMode(void)
{
    return (canGeneralConfig != NULL) ? canGeneralConfig->mode : -1;
}

esp_err_t can_open(twai_mode_t mode, twai_timing_config_t *timingConfig)
{
    if (can_isOpen())
        return ESP_ERR_INVALID_STATE;

    ESP_LOGI(TAG, "opening");

    canGeneralConfig = &(twai_general_config_t)TWAI_GENERAL_CONFIG_DEFAULT(CONFIG_CAN_TX_GPIO_NUM, CONFIG_CAN_RX_GPIO_NUM, mode);
    const twai_filter_config_t filterConfig = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    ESP_ERROR_CHECK(twai_driver_install(canGeneralConfig, timingConfig, &filterConfig));
    ESP_ERROR_CHECK(twai_start());

    xTaskNotify(rxTaskHandle, NOTIFY_OPEN, eSetBits);

    ESP_LOGI(TAG, "opened");
    return ESP_OK;
}

esp_err_t can_close(void)
{
    if (!can_isOpen())
        return ESP_ERR_INVALID_STATE;

    ESP_LOGI(TAG, "closing");
    ulTaskNotifyValueClear(rxTaskHandle, NOTIFY_OPEN);
    ESP_ERROR_CHECK(twai_stop());
    ESP_ERROR_CHECK(twai_driver_uninstall());

    ESP_LOGI(TAG, "closed");
    return ESP_OK;
}

esp_err_t can_transmit(twai_message_t *msg)
{
    if (!can_isOpen())
        return ESP_ERR_INVALID_STATE;

    esp_err_t ret = twai_transmit(msg, pdMS_TO_TICKS(100));
    if (ret != ESP_OK)
        ESP_LOGE(TAG, "can_transmit: twai_transmit returned %s", esp_err_to_name(ret));

    return ret;
}
