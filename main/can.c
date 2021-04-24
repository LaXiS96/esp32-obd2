#include "can.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/twai.h"

#define CAN_TX_GPIO_NUM GPIO_NUM_21
#define CAN_RX_GPIO_NUM GPIO_NUM_22
#define CAN_RX_TASK_PRIO 1

static const char *TAG = "CAN";

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

            // Try receiving for 100ms max
            if (twai_receive(&msg, pdMS_TO_TICKS(100)) == ESP_OK)
                xQueueSend(canRxQueue, &msg, portMAX_DELAY);
        }
    }
}

void canInit(void)
{
    canRxQueue = xQueueCreate(8, sizeof(twai_message_t));
    canOpenLockQueue = xQueueCreate(1, sizeof(canOpenLockDummy));

    xTaskCreate(canRxTask, "CAN RX", 4096, NULL, CAN_RX_TASK_PRIO, NULL);

    ESP_LOGI(TAG, "init completed");
}

bool canIsOpen(void)
{
    return xQueuePeek(canOpenLockQueue, &canOpenLockDummy, 0) == pdTRUE;
}

twai_mode_t canGetMode(void)
{
    return (canGeneralConfig != NULL) ? canGeneralConfig->mode : -1;
}

esp_err_t canOpen(twai_mode_t mode, twai_timing_config_t *timingConfig)
{
    if (canIsOpen())
        return ESP_ERR_INVALID_STATE;

    ESP_LOGI(TAG, "opening");

    canGeneralConfig = &(twai_general_config_t)TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_GPIO_NUM, CAN_RX_GPIO_NUM, mode);
    const twai_filter_config_t filterConfig = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    ESP_ERROR_CHECK(twai_driver_install(canGeneralConfig, timingConfig, &filterConfig));
    ESP_ERROR_CHECK(twai_start());

    xQueueSend(canOpenLockQueue, &(uint8_t){1}, portMAX_DELAY);

    ESP_LOGI(TAG, "opened");
    return ESP_OK;
}

esp_err_t canClose(void)
{
    if (!canIsOpen())
        return ESP_ERR_INVALID_STATE;

    ESP_LOGI(TAG, "closing");
    xQueueReceive(canOpenLockQueue, &canOpenLockDummy, portMAX_DELAY);

    twai_stop();
    twai_driver_uninstall();

    ESP_LOGI(TAG, "closed");
    return ESP_OK;
}

esp_err_t canTransmit(twai_message_t *msg)
{
    if (!canIsOpen())
        return ESP_ERR_INVALID_STATE;

    return twai_transmit(msg, pdMS_TO_TICKS(100));
}
