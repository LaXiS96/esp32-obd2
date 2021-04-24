#pragma once

#include "FreeRTOS/FreeRTOS.h"
#include "FreeRTOS/queue.h"

void slcanInit(QueueHandle_t *rxQueue, QueueHandle_t *txQueue);
