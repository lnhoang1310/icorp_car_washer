#include "ui_screens.h"
#include "ui_theme.h"
#include <string.h>

/*
 * Man hinh dau tien: khach chon 1 trong 2 cach
 *   Trai : quet ma QR bang App ICORP Energy (chon goi + thanh toan tren dien thoai)
 *   Phai : bam "Bam vao day" -> chon goi tren man hinh, tra bang tai khoan ngan hang
 */

#define GAP     6
#define LEFT_W  166                       /* khung QR rong hon de ma QR to, de quet */
#define RIGHT_W (UI_W - GAP * 3 - LEFT_W)
#define PANEL_H (UI_H - GAP * 2)
#define QR_BOX  142
#define QR_SIZE 130

static void manual_clicked(lv_event_t *e)
{
    (void)e;
    ui_on_manual();
}

static lv_obj_t *panel(lv_obj_t *scr, int x, int w)
{
    lv_obj_t *p = ui_panel(scr, UI_C_SURFACE, 8);
    lv_obj_set_size(p, w, PANEL_H);
    lv_obj_set_pos(p, x, GAP);
    lv_obj_set_style_border_width(p, 1, 0);
    lv_obj_set_style_border_color(p, UI_C_BORDER, 0);
    return p;
}

void scr_home_build(lv_obj_t *scr, const char *app_qr)
{
    ui_screen_base(scr, UI_C_BG);

    /* ---- Trai: dung App ---- */
    lv_obj_t *l = panel(scr, GAP, LEFT_W);
    /* Ngat dong co chu y: 3 dong can doi, khong de chu "xe" rot xuong 1 minh */
    lv_obj_t *lt = ui_label(l, &font_vn_14, UI_C_TEXT, "Sử dụng App ICORP\nEnergy và quét mã QR\nbên dưới để rửa xe");
    lv_obj_set_width(lt, LEFT_W - 8);
    lv_obj_set_style_text_align(lt, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(lt, 0, 0);
    lv_obj_align(lt, LV_ALIGN_TOP_MID, 0, 6);

    lv_obj_t *box = ui_panel(l, lv_color_white(), 6);
    lv_obj_set_size(box, QR_BOX, QR_BOX);
    lv_obj_align(box, LV_ALIGN_BOTTOM_MID, 0, -6);
    if (app_qr && app_qr[0])
    {
        lv_obj_t *code = lv_qrcode_create(box);
        lv_qrcode_set_size(code, QR_SIZE);
        lv_qrcode_set_dark_color(code, lv_color_black());
        lv_qrcode_set_light_color(code, lv_color_white());
        lv_qrcode_update(code, app_qr, strlen(app_qr));
        lv_obj_center(code);
    }

    /* ---- Phai: khong dung App. Ca khung deu bam duoc cho de cham ---- */
    lv_obj_t *r = panel(scr, GAP * 2 + LEFT_W, RIGHT_W);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(r, UI_C_SURFACE2, LV_STATE_PRESSED);
    lv_obj_add_event_cb(r, manual_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *rt = ui_label(r, &font_vn_14, UI_C_TEXT,
                            "Bạn chưa có App ?\nVui lòng tải app ICORP Energy hoặc sử dụng tài khoản ngân hàng để thanh toán.");
    lv_obj_set_width(rt, RIGHT_W - 10);
    lv_obj_set_style_text_align(rt, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(rt, 0, 0);
    lv_obj_align(rt, LV_ALIGN_TOP_MID, 0, 8);

    lv_obj_t *b = ui_button(r, RIGHT_W - 14, 46, "Bấm vào đây", &font_vn_17, UI_C_SUCCESS, UI_C_TEXT, manual_clicked);
    lv_obj_align(b, LV_ALIGN_BOTTOM_MID, 0, -10);
}