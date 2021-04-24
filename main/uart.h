#pragma once

#include "config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

QueueHandle_t uartRxQueue;
QueueHandle_t uartTxQueue;

typedef struct
{
    size_t length;
    uint8_t *data; // dynamically allocated, must be freed after usage
} uart_message_t;

void uartInit(void);
uart_message_t uartNewMessage(uint8_t *data, size_t length);
