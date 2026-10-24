#include "light_control.h"
#include <string.h>

static int wrap_min(int m)
{
    m %= 1440;
    return (m < 0) ? m + 1440 : m;
}

bool light_in_window(int m, int start, int end)
{
    m = wrap_min(m);
    start = wrap_min(start);
    end = wrap_min(end);
    if (start == end)
        return false;
    if (start < end)
        return m >= start && m < end;
    return m >= start || m < end; /* qua nua dem */
}

void light_ctrl_set_config(light_ctrl_t *lc, const light_cfg_t *cfg)
{
    lc->cfg = *cfg;
    lc->sun_valid = false; /* toa do co the doi -> tinh lai */
    lc->cache_y = lc->cache_m = lc->cache_d = 0;
}

void light_ctrl_init(light_ctrl_t *lc, const light_cfg_t *cfg)
{
    memset(lc, 0, sizeof(*lc));
    light_ctrl_set_config(lc, cfg);
}

static void refresh_sun(light_ctrl_t *lc, const light_inputs_t *in)
{
    if (lc->sun_valid && lc->cache_y == in->year && lc->cache_m == in->month && lc->cache_d == in->day)
        return;
    lc->sun_valid = sun_time_calc(in->year, in->month, in->day,
                                  lc->cfg.lat, lc->cfg.lon, lc->cfg.tz_hours, &lc->sun);
    lc->cache_y = in->year;
    lc->cache_m = in->month;
    lc->cache_d = in->day;
}

static bool calc_dark(light_ctrl_t *lc, const light_inputs_t *in)
{
    const light_cfg_t *c = &lc->cfg;

    switch (c->mode)
    {
    case LIGHT_MODE_ALWAYS_ON:
        return true;
    case LIGHT_MODE_ALWAYS_OFF:
        return false;
    default:
        break;
    }

    if (!in->time_valid)
        return true; /* khong biet gio -> uu tien an toan cho nguoi rua xe */

    if (c->mode == LIGHT_MODE_AUTO_SUN)
    {
        refresh_sun(lc, in);
        if (lc->sun_valid)
        {
            if (lc->sun.polar_night)
                return true;
            if (lc->sun.polar_day)
                return false;
            int start = lc->sun.sunset_min - c->on_before_sunset_min;
            int end = lc->sun.sunrise_min + c->off_after_sunrise_min;
            return light_in_window(in->minute_of_day, start, end);
        }
        /* tinh loi (toa do sai) -> roi xuong khung gio co dinh */
    }

    return light_in_window(in->minute_of_day, c->fixed_on_min, c->fixed_off_min);
}

bool light_ctrl_update(light_ctrl_t *lc, const light_inputs_t *in, uint32_t elapsed_ms)
{
    lc->is_dark = calc_dark(lc, in);

    bool presence = in->motion || in->session_active;
    if (presence)
        lc->hold_left_ms = (uint32_t)lc->cfg.hold_s * 1000u;
    else
        lc->hold_left_ms = (lc->hold_left_ms > elapsed_ms) ? lc->hold_left_ms - elapsed_ms : 0;

    if (lc->cfg.mode == LIGHT_MODE_ALWAYS_ON)
        lc->on = true;
    else if (lc->cfg.mode == LIGHT_MODE_ALWAYS_OFF)
        lc->on = false;
    else if (lc->cfg.mode == LIGHT_MODE_FIXED)
        lc->on = lc->is_dark; /* bat ca khung gio, khong phu thuoc co nguoi */
    else
        lc->on = lc->is_dark && (presence || lc->hold_left_ms > 0);

    return lc->on;
}