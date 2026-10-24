#include "sun_time.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define DEG2RAD (M_PI / 180.0)
#define RAD2DEG (180.0 / M_PI)
#define ZENITH_OFFICIAL 90.833 /* tinh ca khuc xa khi quyen va ban kinh dia mat troi */

static double norm(double v, double range)
{
    v = fmod(v, range);
    return (v < 0) ? v + range : v;
}

static bool is_leap(int y) { return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0); }

static int day_of_year(int y, int m, int d)
{
    static const int cum[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    int n = cum[m - 1] + d;
    if (m > 2 && is_leap(y))
        n++;
    return n;
}

/* Tra ve gio UT (0..24), hoac -1 = khong moc, -2 = khong lan */
static double calc_ut(int n, double lat, double lon, bool rising)
{
    double lng_hour = lon / 15.0;
    double t = n + ((rising ? 6.0 : 18.0) - lng_hour) / 24.0;

    double M = 0.9856 * t - 3.289;
    double L = norm(M + 1.916 * sin(M * DEG2RAD) + 0.020 * sin(2 * M * DEG2RAD) + 282.634, 360.0);

    double RA = norm(RAD2DEG * atan(0.91764 * tan(L * DEG2RAD)), 360.0);
    RA += floor(L / 90.0) * 90.0 - floor(RA / 90.0) * 90.0; /* dua RA ve cung goc phan tu voi L */
    RA /= 15.0;

    double sin_dec = 0.39782 * sin(L * DEG2RAD);
    double cos_dec = cos(asin(sin_dec));

    double cos_h = (cos(ZENITH_OFFICIAL * DEG2RAD) - sin_dec * sin(lat * DEG2RAD)) /
                   (cos_dec * cos(lat * DEG2RAD));
    if (cos_h > 1.0)
        return -1.0;
    if (cos_h < -1.0)
        return -2.0;

    double H = rising ? 360.0 - RAD2DEG * acos(cos_h) : RAD2DEG * acos(cos_h);
    H /= 15.0;

    double T = H + RA - 0.06571 * t - 6.622;
    return norm(T - lng_hour, 24.0);
}

static int16_t to_local_min(double ut, double tz)
{
    double local = norm(ut + tz, 24.0);
    int m = (int)lround(local * 60.0);
    return (int16_t)(m % 1440);
}

bool sun_time_calc(int year, int month, int day, double lat, double lon, double tz_hours, sun_times_t *out)
{
    if (!out || month < 1 || month > 12 || day < 1 || day > 31 ||
        lat < -90 || lat > 90 || lon < -180 || lon > 180)
        return false;

    int n = day_of_year(year, month, day);
    double rise = calc_ut(n, lat, lon, true);
    double set = calc_ut(n, lat, lon, false);

    out->polar_day = (rise == -2.0 || set == -2.0);
    out->polar_night = (rise == -1.0 || set == -1.0);
    out->sunrise_min = (rise >= 0) ? to_local_min(rise, tz_hours) : -1;
    out->sunset_min = (set >= 0) ? to_local_min(set, tz_hours) : -1;
    return true;
}