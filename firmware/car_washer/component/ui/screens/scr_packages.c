#include "ui_screens.h"
#include "ui_theme.h"
#include <stdio.h>
#include <stdbool.h>

/*
 * Man chon goi (theo ban thiet ke): thanh tieu de co nut quay ve, ben duoi 2 goi / hang,
 * vua dung 2 hang tren man hinh. Moi the: thoi gian "05:00" (Bold 32) + gia "10.000 d" (Bold 20, xanh).
 * Nhieu hon 4 goi thi vuot de cuon, thanh cuon mong ben phai bao con goi o duoi.
 */

#define HEAD_H   44
#define GAP      8
#define COLS     2
#define CARD_W   ((UI_W - GAP * (COLS + 1)) / COLS)          /* 148 */
#define CARD_H   ((UI_H - HEAD_H - GAP * 3) / 2)             /* 86: dung 2 hang */
#define BACK_W   46                                          /* vung bam nut quay ve */

#define C_HEAD   lv_color_hex(0x1D2330)   /* nen thanh tieu de va the */
#define C_BG     lv_color_hex(0x0C1221)   /* nen man hinh */
#define C_BORDER lv_color_hex(0x5286E0)   /* vien the */

/* "10.000 d" (co dau cach truoc d nhu ban thiet ke) */
static void fmt_price(char *buf, size_t len, uint32_t vnd)
{
    char digits[16];
    int n = snprintf(digits, sizeof(digits), "%lu", (unsigned long)vnd);
    size_t o = 0;
    for (int i = 0; i < n && o + 5 < len; i++)
    {
        if (i > 0 && (n - i) % 3 == 0)
            buf[o++] = '.';
        buf[o++] = digits[i];
    }
    buf[o++] = ' ';
    buf[o++] = (char)0xC4; /* "d" gach = U+0111 */
    buf[o++] = (char)0x91;
    buf[o] = 0;
}

static void back_clicked(lv_event_t *e)
{
    (void)e;
    ui_on_back();
}

/* Nut mui ten quay ve man hinh dau, goc tren trai. Ve bang net (font khong co ky hieu mui ten). */
static void build_back(lv_obj_t *header)
{
    lv_obj_t *b = lv_button_create(header);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, BACK_W, HEAD_H);
    lv_obj_align(b, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(b, UI_C_SURFACE2, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_ext_click_area(b, 10);
    lv_obj_add_event_cb(b, back_clicked, LV_EVENT_CLICKED, NULL);

    /* Mui ten "<-": than ngang + 2 canh dau mui ten */
    static const lv_point_precise_t pts[] = {{22, 10}, {2, 10}, {10, 2}, {2, 10}, {10, 18}};
    lv_obj_t *ln = lv_line_create(b);
    lv_line_set_points(ln, pts, sizeof(pts) / sizeof(pts[0]));
    lv_obj_set_style_line_width(ln, 3, 0);
    lv_obj_set_style_line_color(ln, UI_C_TEXT, 0);
    lv_obj_set_style_line_rounded(ln, true, 0);
    lv_obj_center(ln);
}

static void card_clicked(lv_event_t *e)
{
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    ui_on_package_selected(index);
}

static void build_card(lv_obj_t *list, const devcfg_package_t *p, int index)
{
    lv_obj_t *card = lv_button_create(list);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, CARD_W, CARD_H);
    lv_obj_set_style_radius(card, 6, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(card, C_HEAD, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x2B3A57), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, C_BORDER, 0);
    lv_obj_set_style_border_width(card, 2, LV_STATE_PRESSED);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(card, 6, 0);
    lv_obj_add_flag(card, LV_OBJ_FLAG_EVENT_BUBBLE); /* keo tren the van cuon duoc danh sach */
    lv_obj_add_event_cb(card, card_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)index);

    char buf[24];
    snprintf(buf, sizeof(buf), "%02lu:%02lu", (unsigned long)(p->duration_s / 60u),
             (unsigned long)(p->duration_s % 60u)); /* "05:00", "07:30" */
    ui_label(card, &font_num_32, UI_C_TEXT, buf);

    fmt_price(buf, sizeof(buf), p->price);
    ui_label(card, &font_num_20, UI_C_WATER, buf);
}

void scr_packages_build(lv_obj_t *scr, const devcfg_package_t *pkgs, int count)
{
    ui_screen_base(scr, C_BG);

    /* Tieu de canh giua + nut quay ve */
    lv_obj_t *h = ui_panel(scr, C_HEAD, 0);
    lv_obj_set_size(h, UI_W, HEAD_H);
    lv_obj_align(h, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_t *ht = ui_label(h, &font_vn_22, UI_C_TEXT, "Chọn gói rửa xe");
    lv_obj_center(ht);
    build_back(h);

    if (count <= 0)
        return;
    if (count > DEVCFG_MAX_PACKAGES)
        count = DEVCFG_MAX_PACKAGES;

    /* Danh sach cuon doc, 2 the moi hang */
    lv_obj_t *list = lv_obj_create(scr);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, UI_W, UI_H - HEAD_H);
    lv_obj_set_pos(list, 0, HEAD_H);
    lv_obj_set_style_pad_all(list, GAP, 0);
    lv_obj_set_style_pad_row(list, GAP, 0);
    lv_obj_set_style_pad_column(list, GAP, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO); /* chi hien khi co hon 4 goi */
    /* Thanh cuon mong nam trong le phai (khong che the) */
    lv_obj_set_style_width(list, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_right(list, 2, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_ver(list, GAP, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(list, 2, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(list, C_BORDER, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, LV_PART_SCROLLBAR);

    for (int i = 0; i < count; i++)
        build_card(list, &pkgs[i], i);
}