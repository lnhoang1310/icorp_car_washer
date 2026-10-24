#ifndef __OTA_H__
#define __OTA_H__

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        OTA_STATE_START,
        OTA_STATE_UPDATING,
        OTA_STATE_SUCCESS,
        OTA_STATE_FAILED,
    } ota_state_t;

    typedef void (*ota_progress_cb_t)(const char *state_str, uint8_t percent, void *ctx);

    void ota_manager_init(ota_progress_cb_t cb, void *ctx);
    esp_err_t ota_manager_start(const char *url);
    bool ota_manager_is_running(void);
    uint8_t ota_manager_get_percent(void);

#ifdef __cplusplus
}
#endif

#endif