#include "can.h"

#include "config.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/twai.h"

#define TAG "CAN"

static twai_general_config_t *canGeneralConfig;
static bool isOpen = false;

bool can_isOpen(void)
{
    return isOpen;
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

    twai_general_config_t generalConfig = TWAI_GENERAL_CONFIG_DEFAULT(CONFIG_APP_CAN_TX_GPIO_NUM, CONFIG_APP_CAN_RX_GPIO_NUM, mode);
    canGeneralConfig = malloc(sizeof(generalConfig));
    memcpy(canGeneralConfig, &generalConfig, sizeof(generalConfig));

    const twai_filter_config_t filterConfig = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    
    ESP_ERROR_CHECK(twai_driver_install(canGeneralConfig, timingConfig, &filterConfig));
    ESP_ERROR_CHECK(twai_start());

    isOpen = true;
    ESP_LOGI(TAG, "opened");
    return ESP_OK;
}

esp_err_t can_close(void)
{
    if (!can_isOpen())
        return ESP_ERR_INVALID_STATE;

    ESP_LOGI(TAG, "closing");
    isOpen = false;
    ESP_ERROR_CHECK(twai_stop());
    ESP_ERROR_CHECK(twai_driver_uninstall());
    ESP_LOGI(TAG, "closed");
    return ESP_OK;
}

esp_err_t can_receive(twai_message_t *msg, TickType_t ticksToWait)
{
    if (!can_isOpen())
        return ESP_ERR_INVALID_STATE;

    esp_err_t ret = twai_receive(msg, ticksToWait);
    if (ret != ESP_OK)
        ESP_LOGE(TAG, "can_receive: twai_receive returned %s", esp_err_to_name(ret));

    return ret;
}

esp_err_t can_transmit(twai_message_t *msg, TickType_t ticksToWait)
{
    if (!can_isOpen())
        return ESP_ERR_INVALID_STATE;

    esp_err_t ret = twai_transmit(msg, ticksToWait);
    if (ret != ESP_OK)
        ESP_LOGE(TAG, "can_transmit: twai_transmit returned %s", esp_err_to_name(ret));

    return ret;
}
