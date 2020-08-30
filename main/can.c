#include "can.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/can.h"

static const char *TAG = "can";

static const can_general_config_t g_config = CAN_GENERAL_CONFIG_DEFAULT(CAN_TX_GPIO_NUM, CAN_RX_GPIO_NUM, CAN_MODE_NORMAL);
static const can_timing_config_t t_config = CAN_TIMING_CONFIG_500KBITS();
static const can_filter_config_t f_config = CAN_FILTER_CONFIG_ACCEPT_ALL();

static void can_receive_task(void *arg)
{
    while (1)
    {
        can_message_t msg;
        ESP_ERROR_CHECK(can_receive(&msg, portMAX_DELAY));

        ESP_LOGI(TAG, "%.3x %d %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x", msg.identifier, msg.data_length_code,
                 msg.data[0], msg.data[1], msg.data[2], msg.data[3], msg.data[4], msg.data[5], msg.data[6], msg.data[7]);

        // TODO forward received message to queue
    }
}

void can_init(void)
{
    ESP_ERROR_CHECK(can_driver_install(&g_config, &t_config, &f_config));
    ESP_ERROR_CHECK(can_start());

    xTaskCreate(can_receive_task, "CAN RX", 4096, NULL, CAN_RX_TASK_PRIO, NULL);

    ESP_LOGI(TAG, "can_init finished");
}
