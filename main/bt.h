#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// TODO let caller initialize and pass in queues
extern QueueHandle_t btRxQueue;
extern QueueHandle_t btTxQueue;

/**
 * @brief Initialize Bluetooth component
 */
void bt_init(void);
