#pragma once

#define CAN_TX_GPIO_NUM GPIO_NUM_21
#define CAN_RX_GPIO_NUM GPIO_NUM_22
#define CAN_RX_TASK_PRIO 1

// QueueHandle_t canRxQueue;
// QueueHandle_t canTxQueue;

void can_init(void);
