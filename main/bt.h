#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

QueueHandle_t btRxQueue;
QueueHandle_t btTxQueue;

void btInit(void);
