#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "hal/twai_types.h"

/// @brief Check if CAN connection is open
bool can_isOpen(void);

/// @brief Get current CAN controller mode
twai_mode_t can_getMode(void);

/// @brief Open CAN connection
esp_err_t can_open(twai_mode_t mode, twai_timing_config_t *timingConfig);

/// @brief Close CAN connection
esp_err_t can_close(void);

/// @brief Read CAN message from RX queue
esp_err_t can_receive(twai_message_t *msg, TickType_t ticksToWait);

/// @brief Send CAN message
esp_err_t can_transmit(twai_message_t *msg, TickType_t ticksToWait);
