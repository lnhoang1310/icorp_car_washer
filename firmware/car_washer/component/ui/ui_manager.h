#ifndef __UI_MANAGER_H__
#define __UI_MANAGER_H__

/*
 * API man hinh cho app_fsm. Goi duoc tu task bat ky (ban that se tu khoa LVGL).
 * Thao tac nguoi dung tren man hinh duoc gui nguoc ve bang app_post_event():
 *   APP_EVT_UI_MANUAL, APP_EVT_UI_SELECT_PACKAGE, APP_EVT_UI_CANCEL, APP_EVT_UI_END_SESSION,
 *   APP_EVT_UI_RESUME, APP_EVT_UI_TOUCH
 *
 * Du lieu truyen vao chi can hop le trong luc goi ham (UI tu sao chep).
 */

#include "esp_err.h"
#include "device_config.h"
#include "cloud_manager.h"
#include "wash_session.h"
#include <stdbool.h>
#include <stdint.h>

esp_err_t ui_init(void);

/* Man hinh dau: ma QR mo app (trai) + nut tra bang tai khoan ngan hang (phai) */
void ui_show_home(const char *app_qr);
void ui_show_packages(const devcfg_package_t *pkgs, int count);
void ui_show_qr_loading(const devcfg_package_t *pkg);
void ui_show_qr(const cloud_qr_t *qr, const devcfg_package_t *pkg);
void ui_update_qr_countdown(uint32_t sec_left);
void ui_show_paid(const devcfg_package_t *pkg);
void ui_show_washing(const ws_session_t *s);
void ui_update_washing(const ws_session_t *s);
/* Canh bao "thao tac qua nhanh" tren man dang rua, tu an sau UI_WARN_SHOW_MS */
void ui_warn_too_fast(void);
/* Loi thiet bi (mat lien lac bus I/O): man do kem so dien thoai chu cua hang (rong -> "lien he quan ly") */
void ui_show_fault(const char *phone);
/* Man hoi "Tiep tuc phien rua?" khi co dien lai giua phien, dong ho phien van dung */
void ui_show_resume_ask(const ws_session_t *s);
void ui_show_finished(const ws_session_t *s);
void ui_show_notice(const char *title, const char *message);
/* Mat ket noi server: kem so dien thoai chu cua hang (rong -> "lien he quan ly") */
void ui_show_offline(const char *phone);

/* Co nguoi/cham man hinh: bat den nen day du */
void ui_wake(void);

/* So ms tu lan cuoi co nguoi cham man hinh (hoac ui_wake: bam nut, cam bien chuyen dong) */
uint32_t ui_inactive_ms(void);

#endif