#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/// @brief Initialize SLCAN component
/// @param rxQueue @ref message_t queue of received serial messages
/// @param txQueue @ref message_t queue for sending serial messages
void slcan_init(QueueHandle_t *rxQueue, QueueHandle_t *txQueue);
