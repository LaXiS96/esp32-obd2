#include "esp_system.h"
#include "nvs_flash.h"

#include "uart.h"
#include "bt.h"
#include "wifi.h"
#include "can.h"
#include "slcan.h"
#include "sd.h"

// TODO capture FreeRTOS statistics and optimize task stack sizes, etc...

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
}
