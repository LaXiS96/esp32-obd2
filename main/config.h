#pragma once

#define UART_PORT_NUM UART_NUM_0     // ESP console log moved to UART1 via menuconfig
#define UART_TXD_GPIO_NUM GPIO_NUM_1 // UART2: GPIO_NUM_17
#define UART_RXD_GPIO_NUM GPIO_NUM_3 // UART2: GPIO_NUM_16
#define UART_BAUDRATE 921600         // default CP2102 config also supports 1200000 and 1500000
#define UART_BUF_SIZE 128            // Must be >128 (ESP32 driver)
#define UART_QUEUES_LEN 8
#define UART_EVENT_TASK_PRIO 1
#define UART_TX_TASK_PRIO 1
