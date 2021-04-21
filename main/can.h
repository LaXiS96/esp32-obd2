#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "hal/twai_types.h"

// Received CAN frames
QueueHandle_t canRxQueue;

void canInit(void);
bool canIsOpen(void);
twai_mode_t canGetMode(void);
esp_err_t canOpen(twai_mode_t mode, twai_timing_config_t *timingConfig);
esp_err_t canClose(void);
esp_err_t canTransmit(twai_message_t *msg);
