#include "message.h"

#include <stdlib.h>
#include <string.h>

/**
 * Construct a message_t object.
 * The `data` member must be freed after usage.
 */
message_t newMessage(uint8_t *data, size_t length)
{
    message_t msg = {
        .length = length,
        .data = (uint8_t *)malloc(length),
    };
    memcpy(msg.data, data, length);
    return msg;
}
