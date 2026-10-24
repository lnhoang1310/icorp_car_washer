#ifndef __SUN_TIME_H__
#define __SUN_TIME_H__

/*
 * Tinh gio mat troi moc / lan theo toa do va ngay (thuat toan NOAA / Almanac for Computers).
 * Khong can mang, sai so ~1-2 phut o vi do Viet Nam. Thuan C, test duoc tren may tinh.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    int16_t sunrise_min;  /* phut tinh tu 00:00 gio dia phuong */
    int16_t sunset_min;
    bool polar_day;       /* mat troi khong lan ca ngay (khong xay ra o VN) */
    bool polar_night;     /* mat troi khong moc ca ngay */
} sun_times_t;

/*
 * year: vd 2026, month: 1..12, day: 1..31
 * lat: vi do (+ Bac), lon: kinh do (+ Dong), tz_hours: mui gio (VN = 7)
 * Tra ve false neu tham so sai.
 */
bool sun_time_calc(int year, int month, int day, double lat, double lon, double tz_hours, sun_times_t *out);

#ifdef __cplusplus
}
#endif

#endif