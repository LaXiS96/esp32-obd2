#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "hal/twai_types.h"

// TODO dynamic queue allocation by caller in can_init
// Received CAN frames
extern QueueHandle_t can_rxQueue;

/// @brief Initialize CAN component
void can_init(void);

/// @brief Check if CAN connection is open
bool can_isOpen(void);

/// @brief Get current CAN controller mode
twai_mode_t can_getMode(void);

/// @brief Open CAN connection
/// @param mode CAN mode
/// @param timingConfig CAN timing configuration
/// @return @ref ESP_OK if successful
esp_err_t can_open(twai_mode_t mode, twai_timing_config_t *timingConfig);

/// @brief Close CAN connection
/// @return @ref ESP_OK if successful
esp_err_t can_close(void);

/// @brief Send CAN message
/// @param msg Message to send
/// @return @ref ESP_OK if successful
esp_err_t can_transmit(twai_message_t *msg);
