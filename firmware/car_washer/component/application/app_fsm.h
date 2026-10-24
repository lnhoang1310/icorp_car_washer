#ifndef __APP_FSM_H__
#define __APP_FSM_H__

/*
 * May trang thai trung tam cua tram rua xe.
 *
 *   IDLE (chon goi) -> CREATING_QR -> WAIT_PAYMENT -> PAID -> WASHING -> FINISHED -> IDLE
 *   Nhanh phu: NOTICE (thong bao loi/huy, tu ve IDLE), OUT_OF_SERVICE (khong ket noi server).
 *   Co dien lai giua phien (dung khan cap = ngat nguon) / loi bus I/O: phien tam dung, dong ho dung.
 *
 * Trinh tu goi:
 *   app_fsm_init()   -- tao hang doi (goi TRUOC io_expander_init de khong mat su kien)
 *   io_expander_init(app_fsm_on_input, NULL)
 *   ... ui_init, audio_init, mang, cloud_init ...
 *   app_fsm_start()  -- khoi phuc phien do dang (neu co) + chay task
 */

#include "esp_err.h"
#include "app_events.h"
#include <stdbool.h>

esp_err_t app_fsm_init(void);
esp_err_t app_fsm_start(void);

/* Gui su kien vao app_fsm, khong chan. false = hang doi day (nguoi goi tu don dep, vd free qr) */
bool app_post_event(const app_evt_t *evt);

/* Callback dang ky cho io_expander_init() */
void app_fsm_on_input(io_input_t in, bool active, void *ctx);

#endif