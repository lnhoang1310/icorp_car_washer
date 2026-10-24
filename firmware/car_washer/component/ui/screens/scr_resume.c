#include "ui_screens.h"
#include "ui_theme.h"
#include <stdio.h>

/*
 * Man hoi khi co dien lai giua phien. Ca 2 dong ho (su dung + do xe) van DUNG cho toi khi:
 *  - "Tiep tuc rua" -> APP_EVT_UI_RESUME
 *  - "Huy" -> hop xac nhan -> "Huy phien" -> APP_EVT_UI_END_SESSION (khong hoan tien)
 */

#define CONFIRM_TIMEOUT 10000 /* ms: khong ai bam thi tu dong dong hop xac nhan */

static lv_obj_t *s_confirm = NULL;
static uint32_t s_left_ms = 0;

static void confirm_deleted(lv_event_t *e)
{
    (void)e;
    s_confirm = NULL;
}

static void confirm_back(lv_event_t *e)
{
    (void)e;
    if (s_confirm)
        lv_obj_delete_async(s_confirm);
}

static void confirm_cancel(lv_event_t *e)
{
    lv_obj_add_state(lv_event_get_target_obj(e), LV_STATE_DISABLED); /* chong bam 2 lan */
    ui_on_end_session();
}

static void open_confirm(lv_event_t *e)
{
    if (s_confirm)
        return;
    lv_obj_t *scr = lv_obj_get_screen(lv_event_get_target_obj(e));

    char t[12], msg[96];
    ui_fmt_mpss(t, sizeof(t), s_left_ms);
    snprintf(msg, sizeof(msg), "Thời gian còn lại là %s sẽ không được hoàn tiền", t);
    s_confirm = ui_confirm_dialog(scr, "Vui lòng xác nhận hủy phiên rửa xe", msg,
                                  "Quay lại", UI_C_PRIMARY, confirm_back,
                                  "Hủy phiên", UI_C_DANGER, confirm_cancel, CONFIRM_TIMEOUT);
    lv_obj_add_event_cb(s_confirm, confirm_deleted, LV_EVENT_DELETE, NULL);
}

static void resume_clicked(lv_event_t *e)
{
    lv_obj_add_state(lv_event_get_target_obj(e), LV_STATE_DISABLED); /* chong bam 2 lan */
    ui_on_resume();
}

void scr_resume_build(lv_obj_t *scr, const ws_session_t *s)
{
    ui_screen_base(scr, UI_C_BG);
    s_confirm = NULL;
    s_left_ms = 0;
    if (s)
    {
        ws_view_t v;
        ws_get_view(s, &v);
        s_left_ms = v.left_s * 1000u; /* cung so voi dong ho lon o man dang rua */
    }

    lv_obj_t *t = ui_label(scr, &font_vn_22, UI_C_TEXT, "Bạn có muốn tiếp tục phiên rửa xe không?");
    lv_obj_set_width(t, UI_W - 24);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 8);

    lv_obj_t *cap = ui_label(scr, &font_vn_17, UI_C_TEXT, "Thời gian còn lại là");
    lv_obj_align(cap, LV_ALIGN_TOP_MID, 0, 74);

    char buf[12];
    ui_fmt_mmss(buf, sizeof(buf), s_left_ms);
    lv_obj_t *tm = ui_label(scr, &font_num_56, UI_C_TEXT, buf);
    lv_obj_align(tm, LV_ALIGN_TOP_MID, 0, 98);

    lv_obj_t *c = ui_button(scr, 146, 46, "Hủy", &font_vn_17, UI_C_DANGER, UI_C_TEXT, open_confirm);
    lv_obj_align(c, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_t *r = ui_button(scr, 146, 46, "Tiếp tục rửa", &font_vn_17, UI_C_PRIMARY, UI_C_TEXT, resume_clicked);
    lv_obj_align(r, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
}