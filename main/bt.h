#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// TODO let caller initialize and pass in queues
// TODO maybe it's better if queues are handled internally like twai does, exposing only transmit and receive functions
extern QueueHandle_t btRxQueue;
extern QueueHandle_t btTxQueue;

/**
 * @brief Initialize Bluetooth component
 */
void bt_init(void);
