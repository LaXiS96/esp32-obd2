#pragma once

// Adapted from esp_err.h
#ifdef NDEBUG
#define UTIL_CHECK_RETURN(x) ({ \
    esp_err_t __err_rc = (x);   \
    __err_rc;                   \
})
#else
#define UTIL_CHECK_RETURN(x, ret_val) ({                      \
    esp_err_t __err_rc = (x);                                 \
    if (__err_rc != ESP_OK)                                   \
    {                                                         \
        _esp_error_check_failed_without_abort(                \
            __err_rc, __FILE__, __LINE__, __ASSERT_FUNC, #x); \
        return (ret_val);                                     \
    }                                                         \
    __err_rc;                                                 \
})
#endif //NDEBUG
