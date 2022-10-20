#pragma once

#include <stddef.h>
#include <stdint.h>

/// @brief Dynamically allocated message buffer
typedef struct
{
    size_t length;
    uint8_t *data;
} message_t;

/// @brief Allocate and initialize a new @ref message_t instance with the given data
/// @param data Message data
/// @param length Data length in bytes
message_t message_new(uint8_t *data, size_t length);

/// @brief Free an allocated message
/// @param msg The message to free
void message_free(message_t *msg);
