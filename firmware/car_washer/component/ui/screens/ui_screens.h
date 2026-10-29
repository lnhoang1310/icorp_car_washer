#ifndef __UI_SCREENS_H__
#define __UI_SCREENS_H__

/*
 * Moi man hinh la 1 file scr_*.c, ve len man hinh chung "scr" (da xoa sach truoc khi goi).
 * Chi ui_manager.c goi cac ham nay, va luon trong luc dang giu khoa LVGL.
 * Ham *_update chi duoc goi khi man hinh tuong ung dang hien thi.
 */

#include "lvgl.h"
#include "device_config.h"
#include "cloud_manager.h"
#include "wash_session.h"
#include <stdbool.h>

/* Man dau: trai = ma QR mo app, phai = nut "Bam vao day" de tra bang tai khoan ngan hang */
void scr_home_build(lv_obj_t *scr, const char *app_qr);
void scr_packages_build(lv_obj_t *scr, const devcfg_package_t *pkgs, int count);
void scr_qr_loading_build(lv_obj_t *scr, const devcfg_package_t *pkg);
void scr_qr_build(lv_obj_t *scr, const cloud_qr_t *qr, const devcfg_package_t *pkg);
void scr_qr_update(uint32_t sec_left);
void scr_paid_build(lv_obj_t *scr, const devcfg_package_t *pkg);
void scr_washing_build(lv_obj_t *scr, const ws_session_t *s);
void scr_washing_update(const ws_session_t *s);
void scr_washing_warn(const char *text); /* bang canh bao tam thoi, tu an */
/* Loi thiet bi (mat lien lac bus I/O): hien so dien thoai chu cua hang */
void scr_fault_build(lv_obj_t *scr, const char *phone);
void scr_resume_build(lv_obj_t *scr, const ws_session_t *s);
void scr_finished_build(lv_obj_t *scr, const ws_session_t *s);
void scr_notice_build(lv_obj_t *scr, const char *title, const char *message);
void scr_offline_build(lv_obj_t *scr, const char *phone);
/* Vua khoi dong, dang ket noi server lan dau (trong scr_offline.c) */
void scr_connecting_build(lv_obj_t *scr);

/* Thao tac nguoi dung -- trien khai trong ui_manager.c */
void ui_on_manual(void);            /* bam "Bam vao day" o man dau */
void ui_on_back(void);              /* bam mui ten quay ve o man chon goi */
void ui_on_package_selected(int index);
void ui_on_cancel(void);
void ui_on_end_session(void);
void ui_on_resume(void);

#endif