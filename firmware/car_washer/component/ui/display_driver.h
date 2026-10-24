#ifndef __DISPLAY_DRIVER_H__
#define __DISPLAY_DRIVER_H__

/*
 * Man hinh 3.2" 240x320 (ILI9341/ILI9488) dat ngang 320x240 (SPI) + cam ung XPT2046 (chung bus SPI) + den nen PWM.
 * Dung esp_lvgl_port: LVGL chay trong task rieng, moi thao tac LVGL tu task khac
 * phai boc trong lvgl_port_lock() / lvgl_port_unlock().
 *
 * Chan va tham so: xem muc TFT/TOUCH trong app_config.h.
 */

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

esp_err_t display_init(void);

/* 0..100 % */
void display_set_backlight(uint8_t percent);

/* ===== Cam ung XPT2046 ===== */

/* Thong so quy doi ADC tho -> toa do man hinh.
 * Thu tu: (neu swap_xy thi doi 2 truc) -> keo gian [min..max] ra [0..W-1] -> (neu invert thi lat). */
typedef struct
{
    uint8_t swap_xy;
    uint8_t invert_x;
    uint8_t invert_y;
    uint8_t reserved;
    uint16_t x_min, x_max; /* ADC (sau khi swap) ung voi mep trai / phai */
    uint16_t y_min, y_max; /* ADC (sau khi swap) ung voi mep tren / duoi */
} display_touch_cal_t;

/* Cam ung khoi tao duoc va da gan vao LVGL chua */
bool display_touch_ok(void);

/* Bat/tat cam ung voi LVGL (tat khi dang hieu chinh de khong bam nham) */
void display_touch_set_enabled(bool enabled);

/* Doc ADC tho 1 lan, chua quy doi. Tra ve true neu dang cham.
 * Phai goi trong luc dang giu khoa LVGL (lvgl_port_lock) de khong tranh bus voi task LVGL. */
bool display_touch_read_raw(uint16_t *raw_x, uint16_t *raw_y, uint16_t *strength);

/* Quy doi ADC tho -> toa do man hinh theo hieu chinh dang dung */
void display_touch_map(uint16_t raw_x, uint16_t raw_y, int16_t *sx, int16_t *sy);

void display_touch_get_cal(display_touch_cal_t *cal);
/* Ap dung ngay; save = true thi luu NVS (khoi dong sau tu dung) */
esp_err_t display_touch_set_cal(const display_touch_cal_t *cal, bool save);
bool display_touch_has_saved_cal(void);
esp_err_t display_touch_clear_cal(void);

/* Chan doan phan cung cam ung (goi trong luc giu khoa LVGL) */
typedef enum
{
    TOUCH_DIAG_OK = 0,        /* chip co tra loi */
    TOUCH_DIAG_NO_RESPONSE,   /* MISO khong ai day: T_DO/T_CS/nguon cam ung */
    TOUCH_DIAG_STUCK,         /* MISO bi giu cung */
    TOUCH_DIAG_NOT_INIT,
} display_touch_diag_t;
display_touch_diag_t display_touch_diag(void);
/* In 1 dong gia tri tho z1/z2/x/y de xem co thay doi khi cham khong */
void display_touch_diag_log(void);

#endif