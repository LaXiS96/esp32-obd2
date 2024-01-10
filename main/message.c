#include "message.h"

#include <stdlib.h>
#include <string.h>

message_t message_new(uint8_t *data, size_t length)
{
    message_t msg = {
        .length = length,
        .data = (uint8_t *)malloc(length),
    };
    memcpy(msg.data, data, length);
    return msg;
}

void message_free(message_t *msg)
{
    free(msg->data);
}
