#include "sd.h"

#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

static const char *TAG = "SD";

void sdInit(void)
{
    // esp_err_t ret;

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 1;

    ESP_ERROR_CHECK(sdmmc_host_init());
    ESP_ERROR_CHECK(sdmmc_host_init_slot(SDMMC_HOST_SLOT_1, &slot_config));

    sdmmc_card_t card;
    ESP_ERROR_CHECK(sdmmc_card_init(&host, &card));

    ESP_LOG_BUFFER_HEXDUMP(TAG, &card, sizeof(sdmmc_card_t), ESP_LOG_INFO);

    // SD card was formatted as FAT with 16kB sectors
    // esp_vfs_fat_sdmmc_mount_config_t mount_config = {
    //     .format_if_mount_failed = false,
    //     .max_files = 5,
    //     .allocation_unit_size = 16 * 1024,
    // };

    // ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);
    // if (ret != ESP_OK)
    // {
    //     ESP_LOGE(TAG, "esp_vfs_fat_sdmmc_mount: %s", esp_err_to_name(ret));
    //     return;
    // }

    // sdmmc_card_print_info(stdout, card);
}
