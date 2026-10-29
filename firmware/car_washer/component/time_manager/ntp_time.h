#ifndef __NTP_TIME_H__
#define __NTP_TIME_H__

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

void ntp_time_init(void);
void ntp_time_start_sync(void);

void ntp_time_stop_sync(void);
/* true khi SNTP da dong bo thanh cong it nhat 1 lan tu luc khoi dong (gio RTC khong tinh) */
bool ntp_time_is_synced(void);

/* Goi cb (trong task esp_timer) moi lan SNTP dong bo thanh cong. Dung de chi gui Boot khi gio da dung. */
void ntp_time_on_sync(void (*cb)(void));

#ifdef __cplusplus
}
#endif

#endif