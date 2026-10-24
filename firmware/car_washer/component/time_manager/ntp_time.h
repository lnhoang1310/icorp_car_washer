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
bool ntp_time_is_synced(void);

#ifdef __cplusplus
}
#endif

#endif