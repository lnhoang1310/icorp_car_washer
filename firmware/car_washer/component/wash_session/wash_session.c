#include "wash_session.h"
#include <string.h>

#define WS_WARN_MS 60000u
#define WS_MAX_DEADTIME_MS 5000u
#define WS_MAX_TOTAL_MS (6u * 3600u * 1000u) /* chan tren 6 gio, chong du lieu rac */

static inline uint32_t min_u32(uint32_t a, uint32_t b) { return a < b ? a : b; }

static inline uint32_t sat_add(uint32_t a, uint32_t b)
{
    uint32_t r = a + b;
    return (r < a) ? UINT32_MAX : r;
}

static void pumps_off(ws_session_t *s)
{
    if (s->active != WS_NONE)
        s->off_elapsed_ms = 0;
    s->active = WS_NONE;
    s->pending = WS_NONE;
}

static void finish(ws_session_t *s, ws_end_reason_t reason)
{
    pumps_off(s);
    s->state = WS_ST_FINISHED;
    s->end_reason = (uint8_t)reason;
}

const char *ws_kind_name(ws_kind_t k)
{
    switch (k)
    {
    case WS_FOAM:  return "FOAM";
    case WS_WATER: return "WATER";
    case WS_DRY:   return "DRY";
    default:       return "NONE";
    }
}

void ws_clear(ws_session_t *s)
{
    memset(s, 0, sizeof(*s));
    s->state = WS_ST_IDLE;
    s->active = WS_NONE;
    s->pending = WS_NONE;
}

bool ws_config_valid(const ws_config_t *cfg)
{
    if (!cfg || (cfg->mode != WS_MODE_RATE && cfg->mode != WS_MODE_SHARE))
        return false;
    uint32_t sum = 0;
    for (int k = 0; k < WS_KIND_COUNT; k++)
    {
        if (cfg->rate_pct[k] < WS_RATE_PCT_MIN || cfg->rate_pct[k] > WS_RATE_PCT_MAX)
            return false;
        sum += cfg->permille[k];
    }
    if (sum != 1000u)
        return false;
    return cfg->deadtime_ms <= WS_MAX_DEADTIME_MS && cfg->park_extra_ms <= WS_PARK_EXTRA_MAX_MS;
}

bool ws_start(ws_session_t *s, const ws_config_t *cfg, uint32_t total_ms, uint32_t park_total_ms,
              const char *order_id, uint32_t package_id, uint32_t start_epoch)
{
    if (!s || total_ms == 0 || total_ms > WS_MAX_TOTAL_MS || !ws_config_valid(cfg))
        return false;
    if (park_total_ms == 0)
        park_total_ms = total_ms + cfg->park_extra_ms;
    if (park_total_ms < total_ms || park_total_ms - total_ms > WS_PARK_EXTRA_MAX_MS)
        return false;

    ws_clear(s);
    s->magic = WS_SNAPSHOT_MAGIC;
    if (order_id)
        strncpy(s->order_id, order_id, WS_ORDER_ID_LEN - 1);
    s->package_id = package_id;
    s->start_epoch = start_epoch;

    s->mode = cfg->mode;
    s->total_ms = total_ms;
    s->session_left_ms = total_ms;
    s->park_total_ms = park_total_ms;
    s->park_left_ms = s->park_total_ms;

    if (s->mode == WS_MODE_SHARE)
    {
        /* Chia theo GIAY tron (de so tren man hinh khop nhau); loai cuoi nhan phan du */
        uint32_t total_s = total_ms / 1000u, assigned = 0;
        for (int k = 0; k < WS_KIND_COUNT - 1; k++)
        {
            s->budget_ms[k] = (uint32_t)(((uint64_t)total_s * cfg->permille[k]) / 1000u) * 1000u;
            assigned += s->budget_ms[k];
        }
        s->budget_ms[WS_KIND_COUNT - 1] = total_ms - assigned;
        for (int k = 0; k < WS_KIND_COUNT; k++)
            s->rate_pct[k] = 100;
    }
    else
    {
        for (int k = 0; k < WS_KIND_COUNT; k++)
        {
            s->budget_ms[k] = total_ms; /* khong gioi han rieng tung loai */
            s->rate_pct[k] = cfg->rate_pct[k];
        }
    }

    s->deadtime_ms = cfg->deadtime_ms;
    s->off_elapsed_ms = cfg->deadtime_ms; /* lan nhan dau tien bat ngay */
    s->state = WS_ST_RUNNING;
    return true;
}

uint32_t ws_kind_left_ms(const ws_session_t *s, ws_kind_t kind)
{
    if (kind >= WS_KIND_COUNT)
        return 0;
    uint32_t own = s->budget_ms[kind] > s->charged_ms[kind] ? s->budget_ms[kind] - s->charged_ms[kind] : 0;
    return min_u32(own, s->session_left_ms);
}

bool ws_is_available(const ws_session_t *s, ws_kind_t kind)
{
    return s->state == WS_ST_RUNNING && kind < WS_KIND_COUNT && ws_kind_left_ms(s, kind) > 0;
}

void ws_get_view(const ws_session_t *s, ws_view_t *v)
{
    memset(v, 0, sizeof(*v));
    v->mode = s->mode;
    v->park_s = (s->park_left_ms + 999u) / 1000u;

    if (s->mode == WS_MODE_SHARE)
    {
        for (int k = 0; k < WS_KIND_COUNT; k++)
        {
            uint32_t left = ws_kind_left_ms(s, (ws_kind_t)k);
            v->card_s[k] = (left + 999u) / 1000u;
            v->used_s[k] = s->budget_ms[k] / 1000u - v->card_s[k];
            v->exhausted[k] = (left == 0);
            v->left_s += v->card_s[k]; /* dong ho lon = tong 3 o -> luon khop */
        }
    }
    else
    {
        v->left_s = ws_left_shown_s(s);
        for (int k = 0; k < WS_KIND_COUNT; k++)
        {
            v->card_s[k] = s->shown_s[k];
            v->used_s[k] = s->shown_s[k];
        }
    }
}

ws_press_result_t ws_press(ws_session_t *s, ws_kind_t kind)
{
    if (s->state != WS_ST_RUNNING || kind >= WS_KIND_COUNT)
        return WS_PRESS_IGNORED;

    /* Nhan lai nut dang chay (hoac dang cho) -> dung */
    if (s->active == kind)
    {
        pumps_off(s);
        return WS_PRESS_STOPPED;
    }
    if (s->pending == kind)
    {
        s->pending = WS_NONE;
        return WS_PRESS_CANCELLED;
    }

    if (ws_kind_left_ms(s, kind) == 0)
        return WS_PRESS_EXHAUSTED;

    /* Chuyen bom: tat cai dang chay truoc */
    pumps_off(s);

    if (s->off_elapsed_ms >= s->deadtime_ms)
    {
        s->active = (uint8_t)kind;
        s->switch_count++;
        return WS_PRESS_STARTED;
    }

    s->pending = (uint8_t)kind;
    return WS_PRESS_PENDING;
}

uint32_t ws_tick(ws_session_t *s, uint32_t elapsed_ms)
{
    if (s->state != WS_ST_RUNNING || elapsed_ms == 0)
        return 0;

    uint32_t ev = 0;
    uint32_t use_before = s->session_left_ms;
    uint32_t park_before = s->park_left_ms;
    uint32_t d = min_u32(elapsed_ms, s->park_left_ms); /* khong tinh qua luc het gio do xe */

    if (s->active != WS_NONE)
    {
        /* Bom dang chay: tru thoi gian theo toc do cua loai do, khong vuot qua phan con lai cua no */
        ws_kind_t k = (ws_kind_t)s->active;
        uint32_t shown_before = ws_left_shown_s(s);
        uint32_t rate = s->rate_pct[k];
        uint32_t avail = ws_kind_left_ms(s, k);
        uint64_t acc = (uint64_t)d * rate + s->charge_rem;
        uint64_t charge = acc / 100u;

        if (charge >= avail)
        {
            /* Het phan giua chung: chi tinh phan bom thuc te da chay */
            uint64_t real = ((uint64_t)avail * 100u + rate - 1) / rate;
            if (real > d)
                real = d;
            s->used_ms[k] = sat_add(s->used_ms[k], (uint32_t)real);
            s->charged_ms[k] += avail;
            s->session_left_ms -= avail;
            s->charge_rem = 0;
            pumps_off(s);
            if (s->session_left_ms > 0)
                ev |= WS_EVT_PUMP_EXHAUSTED; /* kich ban 2: loai nay het, loai khac con */
        }
        else
        {
            s->used_ms[k] = sat_add(s->used_ms[k], d);
            s->charged_ms[k] += (uint32_t)charge;
            s->session_left_ms -= (uint32_t)charge;
            s->charge_rem = (uint16_t)(acc % 100u);
        }
        /* Kich ban 1: dong ho lon vua giam bao nhieu giay thi o cua bom dang chay tang dung bay nhieu */
        s->shown_s[k] += shown_before - ws_left_shown_s(s);
    }
    else
    {
        s->off_elapsed_ms = sat_add(s->off_elapsed_ms, d);
        if (s->pending != WS_NONE && s->off_elapsed_ms >= s->deadtime_ms)
        {
            ws_kind_t k = (ws_kind_t)s->pending;
            s->pending = WS_NONE;
            if (ws_kind_left_ms(s, k) > 0)
            {
                s->active = (uint8_t)k;
                s->switch_count++;
                ev |= WS_EVT_PUMP_ON;
            }
        }
    }

    s->park_left_ms -= d;

    if (use_before > WS_WARN_MS && s->session_left_ms <= WS_WARN_MS && s->session_left_ms > 0)
        ev |= WS_EVT_WARN_1MIN;
    if (park_before > WS_WARN_MS && s->park_left_ms <= WS_WARN_MS && s->park_left_ms > 0)
        ev |= WS_EVT_PARK_1MIN;

    if (s->session_left_ms == 0)
    {
        finish(s, WS_END_ALL_USED);
        ev |= WS_EVT_FINISHED;
    }
    else if (s->park_left_ms == 0)
    {
        finish(s, WS_END_TIME_UP);
        ev |= WS_EVT_FINISHED;
    }
    return ev;
}

void ws_pause(ws_session_t *s, bool active)
{
    if (active)
    {
        if (s->state == WS_ST_RUNNING)
        {
            pumps_off(s);
            s->off_elapsed_ms = 0;
            s->state = WS_ST_PAUSED;
        }
    }
    else if (s->state == WS_ST_PAUSED)
    {
        s->state = WS_ST_RUNNING; /* bom van tat, khach phai nhan lai */
    }
}

void ws_cancel(ws_session_t *s)
{
    if (s->state == WS_ST_RUNNING || s->state == WS_ST_PAUSED)
        finish(s, WS_END_CANCELLED);
}

bool ws_finish_early(ws_session_t *s)
{
    if (s->state != WS_ST_RUNNING && s->state != WS_ST_PAUSED)
        return false;
    finish(s, WS_END_USER_STOP);
    return true;
}

bool ws_validate(const ws_session_t *s)
{
    if (s->magic != WS_SNAPSHOT_MAGIC)
        return false;
    if (s->state > WS_ST_FINISHED)
        return false;
    if (s->total_ms == 0 || s->total_ms > WS_MAX_TOTAL_MS || s->session_left_ms > s->total_ms)
        return false;
    if (s->deadtime_ms > WS_MAX_DEADTIME_MS)
        return false;
    if (!(s->active < WS_KIND_COUNT || s->active == WS_NONE))
        return false;
    if (!(s->pending < WS_KIND_COUNT || s->pending == WS_NONE))
        return false;
    if (memchr(s->order_id, 0, WS_ORDER_ID_LEN) == NULL)
        return false;

    if (s->park_total_ms < s->total_ms || s->park_total_ms - s->total_ms > WS_PARK_EXTRA_MAX_MS ||
        s->park_left_ms > s->park_total_ms)
        return false;
    if (s->mode != WS_MODE_RATE && s->mode != WS_MODE_SHARE)
        return false;
    uint64_t sum = s->session_left_ms, shown = ws_left_shown_s(s), budget = 0;
    for (int k = 0; k < WS_KIND_COUNT; k++)
    {
        if (s->rate_pct[k] < WS_RATE_PCT_MIN || s->rate_pct[k] > WS_RATE_PCT_MAX)
            return false;
        if (s->charged_ms[k] > s->budget_ms[k] || s->budget_ms[k] > s->total_ms)
            return false;
        sum += s->charged_ms[k];
        shown += s->shown_s[k];
        budget += s->budget_ms[k];
    }
    if (s->mode == WS_MODE_SHARE && budget != s->total_ms)
        return false;
    if (sum != s->total_ms || shown != (s->total_ms + 999u) / 1000u)
        return false;
    return s->charge_rem < 100;
}

void ws_on_restore(ws_session_t *s)
{
    s->active = WS_NONE;
    s->pending = WS_NONE;
    s->off_elapsed_ms = s->deadtime_ms; /* relay da tat tu luc mat dien */
}