#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// TODO refactor
extern QueueHandle_t btRxQueue;
extern QueueHandle_t btTxQueue;

/**
 * @brief Initialize Bluetooth component
 */
void bt_init(void);
