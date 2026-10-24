#ifndef __RTC_DS3231_H__
#define __RTC_DS3231_H__

#include "esp_err.h"
#include "driver/i2c_master.h"
#include <time.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif  

    typedef struct
    {
        i2c_port_t i2c_port;
        int sda_gpio;
        int scl_gpio;
        uint8_t i2c_addr_7bit;
    } rtc_ds3231_config_t;

    esp_err_t rtc_ds3231_init(const rtc_ds3231_config_t *cfg);
    bool rtc_ds3231_is_present(void);
    esp_err_t rtc_ds3231_get_time(struct tm *out);
    esp_err_t rtc_ds3231_set_time(const struct tm *t);
    bool rtc_ds3231_lost_power(void);

#ifdef __cplusplus
}
#endif

#endif