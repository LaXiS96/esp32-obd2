#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/// @brief Initialize SLCAN component
/// @param serialRxQueue @ref message_t queue of received serial messages
/// @param serialTxQueue @ref message_t queue for sending serial messages
void slcan_init(QueueHandle_t *serialRxQueue, QueueHandle_t *serialTxQueue);
