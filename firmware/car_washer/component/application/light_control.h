#ifndef __LIGHT_CTRL_H__
#define __LIGHT_CTRL_H__

/*
 * Quyet dinh bat/tat den. THUAN C, khong dieu khien relay truc tiep:
 * app_fsm goi light_ctrl_update() moi 1 giay roi ghi ket qua ra io_light_set().
 *
 * LIGHT_MODE_FIXED    : bat SUOT khung gio [fixed_on_min, fixed_off_min), khong can co nguoi
 *                       (mac dinh 18:00 bat -> 06:00 tat).
 * LIGHT_MODE_AUTO_SUN : bat khi TROI TOI  va  (co nguoi  hoac  dang co phien rua  hoac  chua het thoi gian giu).
 *
 * "Troi toi" theo che do:
 *   LIGHT_MODE_AUTO_SUN : tu (lan - on_before_sunset) den (moc + off_after_sunrise)
 *   LIGHT_MODE_FIXED    : khung gio co dinh, cho phep qua nua dem (vd 18:00 -> 06:00)
 *   -> muon doi gio bat/tat: sua fixed_on_min / fixed_off_min trong LIGHT_CFG_DEFAULT
 *      (nho tang DEVCFG_LAYOUT_VER trong device_config.c de thiet bi nap lai mac dinh)
 *   LIGHT_MODE_ALWAYS_ON / ALWAYS_OFF : bao tri
 * Neu chua co gio hop le (mat NTP va RTC hong): coi nhu troi toi -> co nguoi la bat den.
 */

#include <stdbool.h>
#include <stdint.h>
#include "sun_time.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    LIGHT_MODE_AUTO_SUN = 0,
    LIGHT_MODE_FIXED,
    LIGHT_MODE_ALWAYS_ON,
    LIGHT_MODE_ALWAYS_OFF,
} light_mode_t;

typedef struct
{
    uint8_t mode;               /* light_mode_t */
    double lat, lon;            /* toa do tram */
    double tz_hours;            /* VN = 7 */
    int16_t on_before_sunset_min;  /* bat som truoc khi mat troi lan */
    int16_t off_after_sunrise_min; /* coi la con toi them sau khi mat troi moc */
    uint16_t fixed_on_min;      /* che do FIXED: phut trong ngay, vd 18*60 */
    uint16_t fixed_off_min;     /* vd 6*60 */
    uint16_t hold_s;            /* giu den sau khi het nguoi / het phien */
} light_cfg_t;

/* Toa do mac dinh o giua Viet Nam -- PHAI sua theo vi tri that cua tram (lay tu server/NVS) */
#define LIGHT_CFG_DEFAULT {                  \
    .mode = LIGHT_MODE_FIXED,                \
    .lat = 16.0, .lon = 108.0, .tz_hours = 7.0, \
    .on_before_sunset_min = 20,              \
    .off_after_sunrise_min = 20,             \
    .fixed_on_min = 18 * 60,  /* 18:00 bat */ \
    .fixed_off_min = 6 * 60,  /* 06:00 tat */ \
    .hold_s = 120 }

typedef struct
{
    bool time_valid;
    int year, month, day;       /* ngay dia phuong */
    int minute_of_day;          /* 0..1439 gio dia phuong */
    bool motion;                /* cam bien dang bao co nguoi */
    bool session_active;        /* dang trong phien rua (ke ca dung khan) */
} light_inputs_t;

typedef struct
{
    light_cfg_t cfg;
    /* cache gio mat troi theo ngay */
    int cache_y, cache_m, cache_d;
    sun_times_t sun;
    bool sun_valid;
    /* trang thai */
    uint32_t hold_left_ms;
    bool is_dark;
    bool on;
} light_ctrl_t;

void light_ctrl_init(light_ctrl_t *lc, const light_cfg_t *cfg);
void light_ctrl_set_config(light_ctrl_t *lc, const light_cfg_t *cfg);

/* Goi dinh ky; tra ve true neu den nen BAT */
bool light_ctrl_update(light_ctrl_t *lc, const light_inputs_t *in, uint32_t elapsed_ms);

/* Tien ich: kiem tra phut m co nam trong [start, end) quay vong qua nua dem */
bool light_in_window(int m, int start, int end);

#ifdef __cplusplus
}
#endif

#endif