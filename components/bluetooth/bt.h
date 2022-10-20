#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// TODO refactor
QueueHandle_t btRxQueue;
QueueHandle_t btTxQueue;

/**
 * @brief Initialize Bluetooth component
 */
void bt_init(void);
