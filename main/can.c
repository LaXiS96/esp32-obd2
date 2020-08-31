#include "can.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/can.h"

#define CAN_TX_GPIO_NUM GPIO_NUM_21
#define CAN_RX_GPIO_NUM GPIO_NUM_22
#define CAN_RX_TASK_PRIO 1

static const char *TAG = "can";

static const can_general_config_t g_config = CAN_GENERAL_CONFIG_DEFAULT(CAN_TX_GPIO_NUM, CAN_RX_GPIO_NUM, CAN_MODE_LISTEN_ONLY);
static const can_timing_config_t t_config = CAN_TIMING_CONFIG_500KBITS();
static const can_filter_config_t f_config = CAN_FILTER_CONFIG_ACCEPT_ALL();

static void canReceiveTask(void *arg)
{
    while (1)
    {
        can_message_t msg;
        ESP_ERROR_CHECK(can_receive(&msg, portMAX_DELAY));

        // ESP_LOGI(TAG, "%.3x %d %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x", msg.identifier, msg.data_length_code,
        //          msg.data[0], msg.data[1], msg.data[2], msg.data[3], msg.data[4], msg.data[5], msg.data[6], msg.data[7]);

        xQueueSendToBack(canRxQueue, &msg, portMAX_DELAY);

        // Delay for 1 tick (10ms), should avoid starving IDLE task
        // vTaskDelay(1);
    }
}

void canInit(void)
{
    ESP_ERROR_CHECK(can_driver_install(&g_config, &t_config, &f_config));
    ESP_ERROR_CHECK(can_start());

    canRxQueue = xQueueCreate(8, sizeof(can_message_t));

    xTaskCreate(canReceiveTask, "CAN RX", 4096, NULL, CAN_RX_TASK_PRIO, NULL);

    ESP_LOGI(TAG, "init completed");
}
