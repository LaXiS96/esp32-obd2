#include "esp_system.h"
#include "nvs_flash.h"

#include "uart.h"
#include "bt.h"
#include "wifi.h"
#include "can.h"
#include "slcan.h"
#include "sd.h"

// TODO capture FreeRTOS statistics and optimize task stack sizes, etc...

#include "message.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#define TESTPATTERN "12345678987654321"

static char taskList[1024];
void testBtTask(void *arg)
{
    while (1)
    {
        // message_t msg = message_new((uint8_t *)TESTPATTERN, sizeof(TESTPATTERN));
        // if (xQueueSend(btTxQueue, &msg, 0) == errQUEUE_FULL)
        // {
        //     ESP_LOGE("testBtTask", "queue full");
        //     message_free(&msg);
        // }

        vTaskList(taskList);
        ESP_LOGW("task", "%s", taskList);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // uartInit();
    bt_init();
    // wifiInit();
    can_init();
    slcan_init(&btRxQueue, &btTxQueue);
    // sdInit();

    // xTaskCreate(testBtTask, "testBtTask", 2048, NULL, 1, NULL);
}
