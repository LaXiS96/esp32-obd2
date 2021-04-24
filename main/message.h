#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct
{
    size_t length;
    uint8_t *data; // dynamically allocated, must be freed after usage
} message_t;

message_t newMessage(uint8_t *data, size_t length);
