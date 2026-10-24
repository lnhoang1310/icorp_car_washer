#ifndef __UI_THEME_H__
#define __UI_THEME_H__

/*
 * Mau sac, font va ham tien ich dung chung cho moi man hinh.
 * Nen toi, chu sang: de doc ngoai troi va duoi den tram rua xe.
 */

#include "lvgl.h"
#include "wash_session.h"
#include <stddef.h>
#include <stdint.h>

/* Man 3.2" 240x320 dat nam ngang */
#define UI_W 320
#define UI_H 240

/* ---- Font (sinh tu Be Vietnam Pro, co dau tieng Viet) ---- */
LV_FONT_DECLARE(font_vn_14);   /* Regular 14: chu phu */
LV_FONT_DECLARE(font_vn_17);   /* SemiBold 17: noi dung, nut */
LV_FONT_DECLARE(font_vn_22);   /* Bold 22: tieu de */
LV_FONT_DECLARE(font_num_32);  /* Bold 32: chi so, "phut", ".", "d" */
LV_FONT_DECLARE(font_num_20);  /* Bold 20: gia goi "10.000 d" (chi so, ".", ",", " ", "d") */
LV_FONT_DECLARE(font_num_56);  /* Bold 56: chi so va ":" cho dong ho lon, bieu tuong lon */

/* ---- Mau ---- */
#define UI_C_BG        lv_color_hex(0x0F172A)
#define UI_C_SURFACE   lv_color_hex(0x1E293B)
#define UI_C_SURFACE2  lv_color_hex(0x334155)
#define UI_C_TEXT      lv_color_hex(0xF8FAFC)
#define UI_C_TEXT2     lv_color_hex(0x94A3B8)
#define UI_C_ACCENT    lv_color_hex(0x38BDF8)
#define UI_C_SUCCESS   lv_color_hex(0x22C55E)
#define UI_C_DANGER    lv_color_hex(0xEF4444)
#define UI_C_WARNING   lv_color_hex(0xF59E0B)
#define UI_C_ERROR_BG  lv_color_hex(0x6E231C)   /* nen do cua moi man loi */
#define UI_C_PRIMARY   lv_color_hex(0x3B82F6)   /* nut chinh, the dang chay */
#define UI_C_BORDER    lv_color_hex(0x2F4A8A)   /* vien the */

#define UI_C_FOAM      lv_color_hex(0xC084FC)
#define UI_C_WATER     lv_color_hex(0x60A5FA)
#define UI_C_DRY       lv_color_hex(0xFBBF24)

/* Bieu tuong FontAwesome co trong font_vn_* */
#define UI_SYM_OK      "\xEF\x80\x8C" /* F00C */
#define UI_SYM_CLOSE   "\xEF\x80\x8D" /* F00D */
#define UI_SYM_WARNING "\xEF\x81\xB1" /* F071 */
#define UI_SYM_WIFI    "\xEF\x87\xAB" /* F1EB */

void ui_theme_init(void);

/* Dat nen cho man hinh, tat cuon */
void ui_screen_base(lv_obj_t *scr, lv_color_t bg);

/* Tao label nhanh */
lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text);

/* Thanh tieu de phia tren, cao UI_HEADER_H */
#define UI_HEADER_H 38
lv_obj_t *ui_header(lv_obj_t *scr, const char *title);

/* Khung bo goc, khong cuon, khong nhan cham */
lv_obj_t *ui_panel(lv_obj_t *parent, lv_color_t bg, int radius);

/* "mm:ss", lam tron len theo giay */
void ui_fmt_mmss(char *buf, size_t len, uint32_t ms);

/* "30.000đ" */
void ui_fmt_money(char *buf, size_t len, uint32_t vnd);

/* Nut bam bo goc, chu canh giua. cb nhan LV_EVENT_CLICKED */
lv_obj_t *ui_button(lv_obj_t *parent, int w, int h, const char *text, const lv_font_t *font,
                    lv_color_t bg, lv_color_t fg, lv_event_cb_t cb);

/* Man loi / canh bao nen do: bieu tuong tam giac, tieu de vang, noi dung trang,
 * dong nhan manh (vd so dien thoai) chu dam. message / strong co the NULL hoac "" */
void ui_error_screen(lv_obj_t *scr, const char *title, const char *message, const char *strong);

/* Hop xac nhan phu len man hinh: tieu de, noi dung, 2 nut + nut X goc phai.
 * Tu dong sau timeout_ms (0 = khong). Bam X / het gio -> chi dong hop.
 * Tra ve doi tuong lop phu; xoa no (lv_obj_delete) de dong hop. */
lv_obj_t *ui_confirm_dialog(lv_obj_t *scr, const char *title, const char *message,
                            const char *left_text, lv_color_t left_bg, lv_event_cb_t left_cb,
                            const char *right_text, lv_color_t right_bg, lv_event_cb_t right_cb,
                            uint32_t timeout_ms);

/* "mm:ss" lam tron XUONG: dung cho so dem len (thoi gian da dung) */
void ui_fmt_mmss_up(char *buf, size_t len, uint32_t ms);

/* "2:17" (phut khong co so 0 dung truoc): thoi gian con lai viet trong cau */
void ui_fmt_mpss(char *buf, size_t len, uint32_t ms);

lv_color_t ui_kind_color(ws_kind_t k);
const char *ui_kind_name(ws_kind_t k);

#endif