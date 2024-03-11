#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

QueueHandle_t uartRxQueue;
QueueHandle_t uartTxQueue;

void uartInit(void);
