#include "message.h"

#include <stdlib.h>
#include <string.h>
#include "esp_log.h"

#define TAG "message"

message_t message_new(uint8_t *data, size_t length)
{
    message_t msg = {
        .length = length,
        .data = (uint8_t *)malloc(length),
    };
    // ESP_LOGI(TAG, "new %p %d", msg.data, msg.length);
    memcpy(msg.data, data, length);
    return msg;
}

void message_free(message_t *msg)
{
    // ESP_LOGI(TAG, "free %p %d", msg->data, msg->length);
    free(msg->data);
}
