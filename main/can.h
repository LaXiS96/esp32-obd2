#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "hal/can_types.h"

// Received CAN frames
QueueHandle_t canRxQueue;

void canInit(void);
bool canIsOpen(void);
can_mode_t canGetMode(void);
esp_err_t canOpen(can_mode_t mode, can_timing_config_t *timingConfig);
esp_err_t canClose(void);
esp_err_t canTransmit(can_message_t *msg);
