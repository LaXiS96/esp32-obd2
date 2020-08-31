#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

QueueHandle_t canRxQueue;
// QueueHandle_t canTxQueue;

void canInit(void);
