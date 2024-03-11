#pragma once

#define APP_BT_TX_QUEUE_LEN 32          // Bluetooth message queue size
#define APP_BT_RX_QUEUE_LEN 128         // Bluetooth message queue size
#define APP_BT_TX_TASK_PRIO 1           // Bluetooth TX task priority
#define APP_CAN_TX_GPIO_NUM 21          // CAN TX GPIO number
#define APP_CAN_RX_GPIO_NUM 22          // CAN RX GPIO number
#define APP_SLCAN_SERIAL_RX_TASK_PRIO 1 // SLCAN serial RX task priority
#define APP_SLCAN_CAN_RX_TASK_PRIO 1    // SLCAN CAN RX task priority

#define UART_PORT_NUM UART_NUM_0 // ESP console moved from UART0 to UART1 via menuconfig (sdkconfig)
#define UART_TXD_GPIO_NUM GPIO_NUM_1
#define UART_RXD_GPIO_NUM GPIO_NUM_3
#define UART_BAUDRATE 921600 // Default CP2102 config also supports 1200000 and 1500000
#define UART_BUF_SIZE 128    // Must be at least 128 (ESP32 driver requirement)
#define UART_QUEUES_LEN 8
#define UART_EVENT_TASK_PRIO 1
#define UART_TX_TASK_PRIO 1
