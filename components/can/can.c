#include "can.h"

#include "config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/twai.h"

static const char *TAG = "CAN";

// TODO use task notifications instead
// Lock queue: empty = CAN closed; not empty = CAN open
static QueueHandle_t canOpenLockQueue;
static uint8_t canOpenLockDummy;

static twai_general_config_t *canGeneralConfig;

static void canRxTask(void *arg)
{
    while (1)
    {
        // Block task while CAN is not open
        if (xQueuePeek(canOpenLockQueue, &canOpenLockDummy, portMAX_DELAY) == pdTRUE)
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
    canOpenLockQueue = xQueueCreate(1, sizeof(canOpenLockDummy));

    xTaskCreate(canRxTask, "CAN RX", 4096, NULL, CONFIG_CAN_RX_TASK_PRIO, NULL);

    ESP_LOGI(TAG, "initialized");
}

bool can_isOpen(void)
{
    return xQueuePeek(canOpenLockQueue, &canOpenLockDummy, 0) == pdTRUE;
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

    xQueueSend(canOpenLockQueue, &(uint8_t){1}, portMAX_DELAY);

    ESP_LOGI(TAG, "opened");
    return ESP_OK;
}

esp_err_t can_close(void)
{
    if (!can_isOpen())
        return ESP_ERR_INVALID_STATE;

    ESP_LOGI(TAG, "closing");
    xQueueReceive(canOpenLockQueue, &canOpenLockDummy, portMAX_DELAY);

    twai_stop();
    twai_driver_uninstall();

    ESP_LOGI(TAG, "closed");
    return ESP_OK;
}

esp_err_t can_transmit(twai_message_t *msg)
{
    if (!can_isOpen())
        return ESP_ERR_INVALID_STATE;

    esp_err_t ret = twai_transmit(msg, pdMS_TO_TICKS(100));
    if (ret != ESP_OK)
        ESP_LOGE(TAG, "can_transmit: twai_transmit returned %d", ret);

    return ret;
}
