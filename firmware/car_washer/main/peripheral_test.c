#include "peripheral_test.h"
#include "app_config.h"

#include "fram.h"
#include "rtc_ds3231.h"
#include "io_expander.h"
#include "app_fsm.h"
#include "cloud_manager.h"
#include "display_driver.h"
#include "audio_player.h"
#include "ui_theme.h"
#include "esp_lvgl_port.h"

#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>
#include <stdlib.h>

static const char *TAG = "peripheral_test";

/* ================= CHON BAI TEST (1 = chay, 0 = bo qua) ================= */
#define PT_TEST_FRAM     1
#define PT_TEST_DS3231   1
#define PT_TEST_RELAY    1
#define PT_TEST_LCD_PINS 0   /* do day man hinh bang dong ho van nang (xem test_lcd_pins) */

/* Test relay: bat TAT CA relay PT_RELAY_ON_MS, tat PT_RELAY_OFF_MS, lap PT_RELAY_CYCLES lan.
 * PT_RELAY_CYCLES = 0: lap mai mai (tram khong chay tiep vao che do binh thuong).
 * CANH BAO: 3 bom se chay cung luc -> thao bom hoac cat nguon bom truoc khi test. */
#define PT_RELAY_ON_MS   1000
#define PT_RELAY_OFF_MS  1000
#define PT_RELAY_CYCLES  5

/* ================= THU THAO TAC KHONG CAN SERVER =================
 * PT_DEMO_PAYMENT = 1: tram coi nhu da ket noi server -> hien man chon goi, nhan khach.
 *   Chon goi -> sau PT_DEMO_QR_DELAY_MS hien ma QR thu (quet ra chu "DEMO", khong co tien that)
 *   -> sau PT_DEMO_PAY_AFTER_S giay tu bao ket qua PT_DEMO_RESULT -> vao rua xe nhu that.
 * Cac kich ban:
 *   - Thanh toan thanh cong : PT_DEMO_RESULT = CLOUD_ORDER_PAID (mac dinh)
 *   - Khach bam Huy         : bam "Huy" truoc khi het PT_DEMO_PAY_AFTER_S
 *   - QR het han            : PT_DEMO_PAY_AFTER_S = 0 (khong bao gio tra tien) -> cho het qr_timeout
 *   - Thanh toan loi        : PT_DEMO_RESULT = CLOUD_ORDER_FAILED
 *   - Khong tao duoc QR     : PT_DEMO_NO_QR = 1 -> het create_order_timeout
 * NHO DAT VE 0 TRUOC KHI LAP DAT THAT. */
#define PT_DEMO_PAYMENT      0
#define PT_DEMO_QR_DELAY_MS  2000
#define PT_DEMO_PAY_AFTER_S  8
#define PT_DEMO_RESULT       CLOUD_ORDER_PAID
#define PT_DEMO_NO_QR        0

/* ================= HIEU CHINH CAM UNG =================
 * Chay sau khi man hinh khoi tao xong (peripheral_test_after_ui).
 * Man hinh hien dau "+" lan luot o 4 goc -> cham dung tam dau "+" (dung dau but cam ung/que nhua
 * cho chinh xac) -> tu tinh va luu vao NVS -> buoc cham thu: cham xanh phai nam duoi ngon tay.
 *   0 = khong chay
 *   1 = chi chay khi chua co hieu chinh trong NVS (mac dinh)
 *   2 = chay moi lan khoi dong (muon hieu chinh lai thi dat 2, nap, xong dat lai 1) */
#define PT_TOUCH_CALIB       1
#define PT_TOUCH_WAIT_S      60   /* khong cham trong ngan nay giay -> bo qua hieu chinh */
#define PT_TOUCH_VERIFY_S    15   /* thoi gian buoc cham thu */
#define PT_TOUCH_MARGIN      24   /* khoang cach dau "+" toi mep man hinh (px) */

/* ================= TEST LOA =================
 * > 0: sau khi khoi dong ~6 giay, loa keu "tut" 1 kHz lien tuc chung nay giay (am luong toi da)
 *      de nghe thu va do dien ap cac chan ampli (gia tri can do in ra log).
 * 0  : tat (dat lai 0 khi test xong). */
#define PT_TEST_AUDIO_TONE_S 0

/* ================= THU KICH BAN TINH THOI GIAN =================
 * Khi chua co server de cau hinh:
 *   0 = dung theo cau hinh dang luu (mac dinh la kich ban 1)
 *   1 = ep kich ban 1: tru theo toc do (bot x3, xit kho x2, nuoc x1)
 *   2 = ep kich ban 2: chia phan tram (bot 20%, nuoc 50%, xit kho 30%)
 * Chi ap dung tam trong lan chay nay (khong luu NVS). Muon doi he so: sua PT_DEMO_RATE / PT_DEMO_PERMILLE. */
#define PT_DEMO_WASH_MODE    0
#define PT_DEMO_RATE         {300, 100, 200}   /* bot, nuoc, xit kho (%) */
#define PT_DEMO_PERMILLE     {200, 500, 300}   /* bot, nuoc, xit kho (phan nghin, tong = 1000) */

/* ================= THU LUONG "SU DUNG APP" =================
 * > 0: sau khi khoi dong chung nay giay, gia lap server bao "khach da chon goi + tra tien tren App"
 *      -> tram vao thang man dang rua voi goi PT_DEMO_APP_MINUTES phut (tram phai dang o man hinh dau).
 * 0  : tat. Chi co tac dung khi PT_DEMO_PAYMENT = 1. */
#define PT_DEMO_APP_ORDER_AFTER_S 0
#define PT_DEMO_APP_MINUTES       30

/* Test day man hinh: lan luot dua TUNG chan len 3.3V, cac chan con lai 0V, moi chan giu
 * PT_LCD_PIN_HOLD_MS, lap PT_LCD_PIN_CYCLES vong. Do tai DAU CHAN TREN MODULE MAN HINH
 * (que den vao chan, que den GND): dung chan dang bao "CAO" phai ~3.3V, chan khac ~0V. */
#define PT_LCD_PIN_HOLD_MS  3000
#define PT_LCD_PIN_CYCLES   2

/* ================= FRAM ================= */
static bool test_fram(void)
{
    ESP_LOGI(TAG, "===== TEST FRAM =====");

    fram_config_t cfg = {
        .i2c_port = FRAM_I2C_PORT,
        .sda_gpio = FRAM_SDA_GPIO,
        .scl_gpio = FRAM_SCL_GPIO,
        .clk_speed_hz = 0,
        .i2c_addr_7bit = FRAM_I2C_ADDR,
        .capacity_bytes = FRAM_CAPACITY_BYTES,
    };

    esp_err_t err = fram_init(&cfg);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "fram_init loi: %s", esp_err_to_name(err));
        return false;
    }

    if (!fram_self_test())
    {
        ESP_LOGE(TAG, "fram_self_test THAT BAI -- kiem tra chan I2C (sda=%d scl=%d port=%d), "
                      "dia chi 0x%02X, hoac chip co gan dung khong",
                 FRAM_SDA_GPIO, FRAM_SCL_GPIO, FRAM_I2C_PORT, FRAM_I2C_ADDR);
        return false;
    }
    ESP_LOGI(TAG, "fram_self_test OK");

    const uint16_t test_addr = 0x0100;
    uint32_t pattern = 0xDEADBEEF;
    uint32_t readback = 0;

    if (fram_write(test_addr, &pattern, sizeof(pattern)) != ESP_OK)
    {
        ESP_LOGE(TAG, "fram_write tai 0x%04X that bai", test_addr);
        return false;
    }
    if (fram_read(test_addr, &readback, sizeof(readback)) != ESP_OK)
    {
        ESP_LOGE(TAG, "fram_read tai 0x%04X that bai", test_addr);
        return false;
    }
    if (readback != pattern)
    {
        ESP_LOGE(TAG, "FRAM doc/ghi KHONG khop: ghi 0x%08" PRIX32 " doc lai 0x%08" PRIX32,
                 pattern, readback);
        return false;
    }

    ESP_LOGI(TAG, "Ghi/doc pattern tai 0x%04X khop -> FRAM (cap=%u byte): PASS",
             test_addr, (unsigned)fram_get_capacity());
    return true;
}

/* ================= DS3231 ================= */
static bool test_ds3231(void)
{
    ESP_LOGI(TAG, "===== TEST DS3231 =====");

    rtc_ds3231_config_t cfg = {
        .i2c_port = DS3231_I2C_PORT,
        .sda_gpio = DS3231_SDA_GPIO,
        .scl_gpio = DS3231_SCL_GPIO,
        .i2c_addr_7bit = DS3231_I2C_ADDR,
    };

    esp_err_t err = rtc_ds3231_init(&cfg);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "rtc_ds3231_init loi: %s", esp_err_to_name(err));
        return false;
    }

    if (!rtc_ds3231_is_present())
    {
        ESP_LOGE(TAG, "Khong phat hien DS3231 -- kiem tra chan I2C (sda=%d scl=%d port=%d), "
                      "dia chi 0x%02X, hoac chip co gan dung khong",
                 DS3231_SDA_GPIO, DS3231_SCL_GPIO, DS3231_I2C_PORT, DS3231_I2C_ADDR);
        return false;
    }
    ESP_LOGI(TAG, "DS3231 connected");

    if (rtc_ds3231_lost_power())
        ESP_LOGW(TAG, "DS3231 bao mat dien (bit OSF) -- gio hien tai trong chip co the KHONG dang tin cay");

    struct tm before = {0};
    esp_err_t rerr = rtc_ds3231_get_time(&before);
    if (rerr == ESP_OK)
    {
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &before);
        ESP_LOGI(TAG, "Gio dang luu trong RTC: %s", buf);
    }
    else
    {
        ESP_LOGW(TAG, "Doc gio RTC that bai (co the chip moi, chua tung set gio)");
    }

    struct tm test_time = {
        .tm_year = 2025 - 1900,
        .tm_mon = 0, /* thang 1 */
        .tm_mday = 1,
        .tm_hour = 12,
        .tm_min = 0,
        .tm_sec = 0,
        .tm_wday = 3,
    };

    if (rtc_ds3231_set_time(&test_time) != ESP_OK)
    {
        ESP_LOGE(TAG, "rtc_ds3231_set_time that bai");
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(1200));

    struct tm readback = {0};
    if (rtc_ds3231_get_time(&readback) != ESP_OK)
    {
        ESP_LOGE(TAG, "Doc lai gio sau khi set that bai");
        return false;
    }

    time_t t_set = mktime(&test_time);
    time_t t_read = mktime(&readback);
    long diff = (long)(t_read - t_set);
    if (diff < 0)
        diff = -diff;

    if (diff > 2)
    {
        ESP_LOGE(TAG, "Gio doc lai lech qua nhieu so voi gio vua set (lech %ld giay)", diff);
        return false;
    }

    char buf2[32];
    strftime(buf2, sizeof(buf2), "%Y-%m-%d %H:%M:%S", &readback);
    ESP_LOGI(TAG, "Set 2025-01-01 12:00:00 -> doc lai %s (lech %ld giay) -> DS3231: PASS", buf2, diff);

    if (rerr == ESP_OK)
    {
        rtc_ds3231_set_time(&before);
        ESP_LOGI(TAG, "Da khoi phuc lai gio RTC nhu truoc luc test");
    }
    else
    {
        ESP_LOGW(TAG, "Khong the khoi phuc gio cu (luc dau doc that bai) -- RTC dang giu gio test "
                      "2025-01-01 12:00:xx, nho chinh lai qua SNTP/tay sau nay");
    }

    return true;
}

/* ================= RELAY ================= */
static const char *relay_block_reason(void)
{
    if (io_input_is_active(IO_IN_BUS_FAULT))
        return "mat lien lac PCF input";
    return "?";
}

static bool test_relay(void)
{
    ESP_LOGI(TAG, "===== TEST RELAY =====");
    ESP_LOGW(TAG, "Bat TAT CA relay %d ms / tat %d ms, %s -- thao bom hoac cat nguon bom truoc khi test!",
             PT_RELAY_ON_MS, PT_RELAY_OFF_MS, PT_RELAY_CYCLES > 0 ? "lap co gioi han" : "lap mai mai");

    /* Cho io_expander doc chip input lan dau (toi da 1 giay) */
    for (int i = 0; i < 20 && !io_expander_is_healthy(); i++)
        vTaskDelay(pdMS_TO_TICKS(50));
    vTaskDelay(pdMS_TO_TICKS(IN_DEBOUNCE_MS + 20));

    const uint8_t all = (uint8_t)((1u << IO_RELAY_COUNT) - 1);
    bool ok = true;

    for (int n = 1; PT_RELAY_CYCLES <= 0 || n <= PT_RELAY_CYCLES; n++)
    {
        esp_err_t err = io_relay_write_raw(all);
        if (err == ESP_OK)
        {
            ESP_LOGI(TAG, "[%d] BAT: bot + nuoc + kho + den", n);
        }
        else if (err == ESP_ERR_INVALID_STATE)
        {
            ESP_LOGW(TAG, "[%d] BAT: chi relay DEN -- relay bom bi chan: %s", n, relay_block_reason());
            ok = false;
        }
        else
        {
            ESP_LOGE(TAG, "[%d] BAT: ghi PCF output 0x%02X loi: %s", n, PCF8574AT_OUT_I2C_ADDR, esp_err_to_name(err));
            ok = false;
        }
        vTaskDelay(pdMS_TO_TICKS(PT_RELAY_ON_MS));

        err = io_relay_write_raw(0);
        if (err == ESP_OK)
            ESP_LOGI(TAG, "[%d] TAT", n);
        else
        {
            ESP_LOGE(TAG, "[%d] TAT: ghi PCF output loi: %s", n, esp_err_to_name(err));
            ok = false;
        }
        vTaskDelay(pdMS_TO_TICKS(PT_RELAY_OFF_MS));
    }

    ESP_LOGI(TAG, "Test relay xong -> %s (PASS = ESP32 ghi duoc lenh; con relay co dong that hay khong thi nghe/nhin tren bo)",
             ok ? "PASS" : "FAIL");
    return ok;
}

/* ================= DAY MAN HINH ================= */
#if PT_TEST_LCD_PINS
static void test_lcd_pins(void)
{
    ESP_LOGI(TAG, "===== TEST DAY MAN HINH =====");
    static const struct
    {
        int gpio;
        const char *name;
    } pins[] = {
        {TFT_SCK_GPIO, "SCK"},
        {TFT_MOSI_GPIO, "SDI(MOSI)"},
        {TFT_CS_GPIO, "CS"},
        {TFT_DC_GPIO, "DC/RS"},
        {TFT_RST_GPIO, "RESET"},
    };
    const int n = sizeof(pins) / sizeof(pins[0]);

    uint64_t mask = 0;
    for (int i = 0; i < n; i++)
        mask |= 1ULL << pins[i].gpio;
    gpio_config_t cfg = {.pin_bit_mask = mask, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&cfg);

    ESP_LOGW(TAG, "Do dien ap TAI CHAN MODULE MAN HINH (khong do tai ESP32): chan bao CAO ~3.3V, cac chan khac ~0V");
    for (int c = 1; c <= PT_LCD_PIN_CYCLES; c++)
    {
        for (int i = 0; i < n; i++)
        {
            for (int k = 0; k < n; k++)
                gpio_set_level(pins[k].gpio, k == i);
            ESP_LOGI(TAG, "[%d] chan %-9s (GPIO%d) = CAO, con lai = THAP", c, pins[i].name, pins[i].gpio);
            vTaskDelay(pdMS_TO_TICKS(PT_LCD_PIN_HOLD_MS));
        }
    }

    /* Tra chan ve mac dinh de driver man hinh cau hinh lai */
    for (int i = 0; i < n; i++)
        gpio_reset_pin(pins[i].gpio);
    ESP_LOGI(TAG, "Test day man hinh xong (ket qua do bang dong ho, firmware khong tu biet duoc)");
}
#endif

/* ================= THU THAO TAC KHONG CAN SERVER ================= */

static esp_timer_handle_t s_demo_qr_timer = NULL;
static esp_timer_handle_t s_demo_pay_timer = NULL;
static char s_demo_order[CLOUD_ORDER_ID_LEN];
static uint32_t s_demo_amount = 0;

bool pt_demo_enabled(void)
{
    return PT_DEMO_PAYMENT != 0;
}

static void demo_pay_cb(void *arg)
{
    (void)arg;
    app_evt_t e = {.type = APP_EVT_CLOUD_ORDER_RESULT};
    strlcpy(e.data.result.order_id, s_demo_order, sizeof(e.data.result.order_id));
    e.data.result.status = PT_DEMO_RESULT;
    ESP_LOGW(TAG, "[DEMO] don %s: gia lap ket qua thanh toan = %d", s_demo_order, (int)PT_DEMO_RESULT);
    app_post_event(&e);
}

static void demo_qr_cb(void *arg)
{
    (void)arg;
    cloud_qr_t *qr = calloc(1, sizeof(cloud_qr_t));
    if (!qr)
        return;
    strlcpy(qr->order_id, s_demo_order, sizeof(qr->order_id));
    snprintf(qr->qr, sizeof(qr->qr), "DEMO - KHONG THANH TOAN THAT - DON %s - %lu VND",
             s_demo_order, (unsigned long)s_demo_amount);
    qr->amount = s_demo_amount;
    qr->expire_in_s = 0; /* 0 = dung qr_timeout_s trong cau hinh */
    strlcpy(qr->bank_name, "NGAN HANG DEMO", sizeof(qr->bank_name));
    strlcpy(qr->account_name, "TRAM RUA XE DEMO", sizeof(qr->account_name));

    app_evt_t e = {.type = APP_EVT_CLOUD_QR, .data.qr = qr};
    if (!app_post_event(&e))
    {
        free(qr);
        return;
    }
    ESP_LOGW(TAG, "[DEMO] don %s: da gui ma QR thu", s_demo_order);

    if (PT_DEMO_PAY_AFTER_S > 0 && s_demo_pay_timer)
    {
        esp_timer_stop(s_demo_pay_timer);
        esp_timer_start_once(s_demo_pay_timer, (uint64_t)PT_DEMO_PAY_AFTER_S * 1000000ULL);
        ESP_LOGW(TAG, "[DEMO] tu bao thanh toan sau %d s", PT_DEMO_PAY_AFTER_S);
    }
}

esp_err_t pt_demo_create_order(const char *order_id, const devcfg_package_t *pkg)
{
    if (!s_demo_qr_timer)
    {
        esp_timer_create_args_t a = {.callback = demo_qr_cb, .name = "demo_qr"};
        esp_timer_create(&a, &s_demo_qr_timer);
        esp_timer_create_args_t b = {.callback = demo_pay_cb, .name = "demo_pay"};
        esp_timer_create(&b, &s_demo_pay_timer);
    }
    esp_timer_stop(s_demo_qr_timer);
    esp_timer_stop(s_demo_pay_timer);

    strlcpy(s_demo_order, order_id, sizeof(s_demo_order));
    s_demo_amount = pkg ? pkg->price : 0;
    ESP_LOGW(TAG, "[DEMO] tao don %s, %lu d", order_id, (unsigned long)s_demo_amount);

#if PT_DEMO_NO_QR
    ESP_LOGW(TAG, "[DEMO] PT_DEMO_NO_QR = 1 -> khong tra ma QR");
#else
    esp_timer_start_once(s_demo_qr_timer, (uint64_t)PT_DEMO_QR_DELAY_MS * 1000ULL);
#endif
    return ESP_OK;
}

void pt_demo_cancel_order(const char *order_id)
{
    if (s_demo_qr_timer && strcmp(order_id, s_demo_order) == 0)
    {
        esp_timer_stop(s_demo_qr_timer);
        esp_timer_stop(s_demo_pay_timer);
        ESP_LOGW(TAG, "[DEMO] huy don %s", order_id);
    }
}

void pt_demo_query_order(const char *order_id)
{
    ESP_LOGW(TAG, "[DEMO] hoi trang thai don %s -> bo qua", order_id);
}

/* ================= HIEU CHINH CAM UNG ================= */
#if PT_TOUCH_CALIB

typedef enum
{
    TP_OK = 0,
    TP_TIMEOUT, /* khong cham gi */
    TP_STUCK,   /* luc nao cung bao dang cham */
} tp_result_t;

static struct
{
    lv_obj_t *root, *msg, *hint, *line_h, *line_v, *ring, *dot;
} s_tc;

static int64_t now_us(void) { return esp_timer_get_time(); }

static lv_obj_t *tc_box(lv_obj_t *parent, int w, int h, lv_color_t c, int radius)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    return o;
}

/* Cac ham tc_* duoi day goi trong luc dang giu khoa LVGL */
static void tc_build(void)
{
    s_tc.root = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_tc.root);
    lv_obj_set_size(s_tc.root, UI_W, UI_H);
    lv_obj_set_style_bg_color(s_tc.root, UI_C_BG, 0);
    lv_obj_set_style_bg_opa(s_tc.root, LV_OPA_COVER, 0);

    s_tc.msg = ui_label(s_tc.root, &font_vn_17, UI_C_TEXT, "");
    lv_obj_set_width(s_tc.msg, UI_W - 2 * (PT_TOUCH_MARGIN + 30));
    lv_obj_set_style_text_align(s_tc.msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_tc.msg, LV_ALIGN_CENTER, 0, -14);

    s_tc.hint = ui_label(s_tc.root, &font_vn_14, UI_C_TEXT2, "");
    lv_obj_set_width(s_tc.hint, UI_W - 2 * (PT_TOUCH_MARGIN + 30));
    lv_obj_set_style_text_align(s_tc.hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_tc.hint, LV_ALIGN_CENTER, 0, 34);

    s_tc.line_h = tc_box(s_tc.root, 31, 3, UI_C_WARNING, 0);
    s_tc.line_v = tc_box(s_tc.root, 3, 31, UI_C_WARNING, 0);
    s_tc.ring = tc_box(s_tc.root, 18, 18, UI_C_BG, LV_RADIUS_CIRCLE);
    lv_obj_set_style_bg_opa(s_tc.ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_tc.ring, 2, 0);
    lv_obj_set_style_border_color(s_tc.ring, UI_C_WARNING, 0);
    s_tc.dot = tc_box(s_tc.root, 14, 14, UI_C_SUCCESS, LV_RADIUS_CIRCLE);
}

static void tc_text(const char *msg, const char *hint)
{
    lv_label_set_text(s_tc.msg, msg ? msg : "");
    lv_label_set_text(s_tc.hint, hint ? hint : "");
}

static void tc_target(int x, int y, bool show)
{
    lv_obj_t *parts[] = {s_tc.line_h, s_tc.line_v, s_tc.ring};
    for (int i = 0; i < 3; i++)
    {
        if (show)
            lv_obj_remove_flag(parts[i], LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(parts[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_pos(s_tc.line_h, x - 15, y - 1);
    lv_obj_set_pos(s_tc.line_v, x - 1, y - 15);
    lv_obj_set_pos(s_tc.ring, x - 9, y - 9);
}

static bool tc_read(uint16_t *x, uint16_t *y, uint16_t *z)
{
    lvgl_port_lock(0);
    bool p = display_touch_read_raw(x, y, z);
    lvgl_port_unlock();
    return p;
}

/* Cho 1 lan cham: lay trung binh cac mau trong luc giu tay, tra ve khi da nha tay */
static tp_result_t tc_wait_point(uint16_t *out_x, uint16_t *out_y)
{
    uint16_t x, y, z;
    int64_t t0 = now_us();

    /* Neu dang bi bao cham san (chua nha tay tu diem truoc) thi cho nha */
    while (tc_read(&x, &y, &z))
    {
        if (now_us() - t0 > 10 * 1000000LL)
            return TP_STUCK;
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    uint32_t sx = 0, sy = 0;
    int n = 0, settle = 0;
    int64_t last_log = 0, last_diag = now_us();
    t0 = now_us();
    for (;;)
    {
        bool p = tc_read(&x, &y, &z);
        if (p)
        {
            if (now_us() - last_log > 500000)
            {
                last_log = now_us();
                ESP_LOGI(TAG, "  cham: adc x=%u y=%u luc=%u", x, y, z);
            }
            if (++settle > 3 && n < 16) /* bo vai mau dau luc tay moi dat xuong */
            {
                sx += x;
                sy += y;
                n++;
            }
            if (n >= 16)
                break;
        }
        else
        {
            if (n >= 6)
                break; /* nha tay som nhung da du mau */
            settle = 0;
            n = 0;
            sx = sy = 0;
            if (now_us() - last_diag > 3000000) /* chua thay cham: in so lieu tho de xem chip co phan ung */
            {
                last_diag = now_us();
                lvgl_port_lock(0);
                display_touch_diag_log();
                lvgl_port_unlock();
            }
        }
        if (now_us() - t0 > (int64_t)PT_TOUCH_WAIT_S * 1000000LL)
            return TP_TIMEOUT;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    *out_x = (uint16_t)(sx / n);
    *out_y = (uint16_t)(sy / n);

    /* Cho nha tay */
    t0 = now_us();
    while (tc_read(&x, &y, &z))
    {
        if (now_us() - t0 > 10 * 1000000LL)
            return TP_STUCK;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return TP_OK;
}

/* Tinh 1 truc: a_lo = ADC trung binh tai toa do man hinh s_lo, a_hi tai s_hi, man rong len_px */
static bool tc_axis(int a_lo, int a_hi, int s_lo, int s_hi, int len_px, uint16_t *mn, uint16_t *mx, uint8_t *inv)
{
    if (abs(a_hi - a_lo) < 300)
        return false; /* 2 dau truc gan nhu bang nhau -> cham sai */
    float slope = (float)(a_hi - a_lo) / (float)(s_hi - s_lo);
    float at0 = a_lo - slope * s_lo;
    float atN = a_lo + slope * (len_px - 1 - s_lo);
    float lo = at0 < atN ? at0 : atN, hi = at0 < atN ? atN : at0;
    if (lo < 0) lo = 0;
    if (hi > 4095) hi = 4095;
    *inv = at0 > atN;
    *mn = (uint16_t)lo;
    *mx = (uint16_t)hi;
    return *mx > *mn;
}

static bool tc_compute(const uint16_t rx[4], const uint16_t ry[4], display_touch_cal_t *c)
{
    const int M = PT_TOUCH_MARGIN;
    /* Diem 0: tren trai, 1: tren phai, 2: duoi phai, 3: duoi trai */
    int dx_from_x = abs((rx[1] + rx[2]) - (rx[0] + rx[3])); /* kenh X cua chip doi theo chieu ngang? */
    int dx_from_y = abs((ry[1] + ry[2]) - (ry[0] + ry[3]));
    memset(c, 0, sizeof(*c));
    c->swap_xy = dx_from_y > dx_from_x;

    int a[4], b[4]; /* a: ADC theo truc ngang man hinh, b: theo truc doc */
    for (int i = 0; i < 4; i++)
    {
        a[i] = c->swap_xy ? ry[i] : rx[i];
        b[i] = c->swap_xy ? rx[i] : ry[i];
    }
    ESP_LOGI(TAG, "kenh ngang: thay doi %d (kenh X chip) / %d (kenh Y chip) -> swap_xy=%d",
             dx_from_x, dx_from_y, c->swap_xy);

    bool okx = tc_axis((a[0] + a[3]) / 2, (a[1] + a[2]) / 2, M, UI_W - 1 - M, UI_W, &c->x_min, &c->x_max, &c->invert_x);
    bool oky = tc_axis((b[0] + b[1]) / 2, (b[2] + b[3]) / 2, M, UI_H - 1 - M, UI_H, &c->y_min, &c->y_max, &c->invert_y);
    return okx && oky;
}

static void tc_verify(void)
{
    char buf[64];
    int64_t end = now_us() + (int64_t)PT_TOUCH_VERIFY_S * 1000000LL;
    lvgl_port_lock(0);
    tc_target(0, 0, false);
    tc_text("Chạm thử khắp màn hình.\nChấm xanh phải nằm ngay dưới ngón tay.", "");
    lvgl_port_unlock();

    int last_s = -1;
    while (now_us() < end)
    {
        uint16_t x, y, z;
        lvgl_port_lock(0);
        bool p = display_touch_read_raw(&x, &y, &z);
        if (p)
        {
            int16_t sx, sy;
            display_touch_map(x, y, &sx, &sy);
            lv_obj_set_pos(s_tc.dot, sx - 7, sy - 7);
            lv_obj_remove_flag(s_tc.dot, LV_OBJ_FLAG_HIDDEN);
        }
        int s = (int)((end - now_us()) / 1000000LL) + 1;
        if (s != last_s)
        {
            last_s = s;
            snprintf(buf, sizeof(buf), "Tự thoát sau %d giây", s);
            lv_label_set_text(s_tc.hint, buf);
        }
        lvgl_port_unlock();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void touch_calibration_run(void)
{
    const int M = PT_TOUCH_MARGIN;
    const int tx[4] = {M, UI_W - 1 - M, UI_W - 1 - M, M};
    const int ty[4] = {M, M, UI_H - 1 - M, UI_H - 1 - M};
    static const char *names[4] = {"trên trái", "trên phải", "dưới phải", "dưới trái"};

    ESP_LOGI(TAG, "===== HIEU CHINH CAM UNG =====");
    lvgl_port_lock(0);
    display_touch_set_enabled(false);
    tc_build();
    display_touch_diag_t diag = display_touch_diag();
    lvgl_port_unlock();
    // display_set_backlight(100);
    if (diag == TOUCH_DIAG_NO_RESPONSE || diag == TOUCH_DIAG_STUCK)
    {
        lvgl_port_lock(0);
        tc_text(diag == TOUCH_DIAG_NO_RESPONSE ? "Chip cảm ứng không trả lời" : "Đường MISO bị giữ cứng",
                "Kiểm tra dây T_DO→GPIO18, T_CS→GPIO21 và nguồn. Xem log.");
        lvgl_port_unlock();
        vTaskDelay(pdMS_TO_TICKS(6000));
        lvgl_port_lock(0);
        lv_obj_delete(s_tc.root);
        memset(&s_tc, 0, sizeof(s_tc));
        display_touch_set_enabled(true);
        lv_display_trigger_activity(NULL);
        lvgl_port_unlock();
        return;
    }

    bool done = false;
    for (int attempt = 1; attempt <= 3 && !done; attempt++)
    {
        uint16_t rx[4], ry[4];
        tp_result_t r = TP_OK;
        for (int i = 0; i < 4 && r == TP_OK; i++)
        {
            char msg[64], hint[64];
            snprintf(msg, sizeof(msg), "Chạm vào tâm dấu + (góc %s)", names[i]);
            snprintf(hint, sizeof(hint), "Hiệu chỉnh cảm ứng  •  điểm %d/4", i + 1);
            lvgl_port_lock(0);
            tc_text(msg, hint);
            tc_target(tx[i], ty[i], true);
            lvgl_port_unlock();
            ESP_LOGI(TAG, "cho cham diem %d/4 (%s) tai man hinh x=%d y=%d", i + 1, names[i], tx[i], ty[i]);

            r = tc_wait_point(&rx[i], &ry[i]);
            if (r == TP_OK)
                ESP_LOGI(TAG, "diem %d: adc x=%u y=%u", i + 1, rx[i], ry[i]);
        }

        if (r == TP_TIMEOUT)
        {
            ESP_LOGE(TAG, "%d giay khong nhan duoc cham nao -> bo qua hieu chinh. Kiem tra day cam ung: "
                          "T_CS->GPIO%d, T_CLK->GPIO%d, T_DIN->GPIO%d, T_DO->GPIO%d",
                     PT_TOUCH_WAIT_S, TOUCH_CS_GPIO, TOUCH_SCK_GPIO, TOUCH_MOSI_GPIO, TOUCH_MISO_GPIO);
            lvgl_port_lock(0);
            tc_target(0, 0, false);
            tc_text("Không nhận được chạm nào", "Kiểm tra dây T_CS, T_CLK, T_DIN, T_DO của cảm ứng");
            lvgl_port_unlock();
            vTaskDelay(pdMS_TO_TICKS(4000));
            break;
        }
        if (r == TP_STUCK)
        {
            ESP_LOGE(TAG, "cam ung luc nao cung bao dang cham -> bo qua hieu chinh. Thuong do T_DO (MISO) "
                          "hoac T_CS noi sai/ho, hoac mat cam ung bi de/ep vien");
            lvgl_port_lock(0);
            tc_target(0, 0, false);
            tc_text("Cảm ứng luôn báo đang chạm", "Kiểm tra dây T_DO, T_CS và viền ép mặt cảm ứng");
            lvgl_port_unlock();
            vTaskDelay(pdMS_TO_TICKS(4000));
            break;
        }

        display_touch_cal_t cal;
        if (!tc_compute(rx, ry, &cal))
        {
            ESP_LOGW(TAG, "lan %d: diem cham khong hop le (cham lech dau +?) -> lam lai", attempt);
            lvgl_port_lock(0);
            tc_target(0, 0, false);
            tc_text("Điểm chạm không hợp lệ, làm lại", "Chạm đúng tâm dấu +");
            lvgl_port_unlock();
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        esp_err_t err = display_touch_set_cal(&cal, true);
        ESP_LOGI(TAG, "hieu chinh xong%s. Neu muon lam mac dinh, chep vao app_config.h:",
                 err == ESP_OK ? ", da luu NVS" : " (LUU NVS LOI)");
        ESP_LOGI(TAG, "#define TOUCH_SWAP_XY          (%d)", cal.swap_xy);
        ESP_LOGI(TAG, "#define TOUCH_INVERT_X         (%d)", cal.invert_x);
        ESP_LOGI(TAG, "#define TOUCH_INVERT_Y         (%d)", cal.invert_y);
        ESP_LOGI(TAG, "#define TOUCH_RAW_X_MIN        (%u)", cal.x_min);
        ESP_LOGI(TAG, "#define TOUCH_RAW_X_MAX        (%u)", cal.x_max);
        ESP_LOGI(TAG, "#define TOUCH_RAW_Y_MIN        (%u)", cal.y_min);
        ESP_LOGI(TAG, "#define TOUCH_RAW_Y_MAX        (%u)", cal.y_max);
        tc_verify();
        done = true;
    }

    lvgl_port_lock(0);
    lv_obj_delete(s_tc.root);
    memset(&s_tc, 0, sizeof(s_tc));
    display_touch_set_enabled(true);
    lv_display_trigger_activity(NULL); /* tranh bi giam sang ngay sau khi hieu chinh */
    lvgl_port_unlock();
}
#endif /* PT_TOUCH_CALIB */

#if PT_TEST_AUDIO_TONE_S > 0
static void audio_tone_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(6000)); /* cho audio_init() va cau thong bao luc khoi dong xong */
    audio_test_tone(PT_TEST_AUDIO_TONE_S);
    vTaskDelete(NULL);
}
#endif

#if PT_DEMO_PAYMENT && PT_DEMO_APP_ORDER_AFTER_S > 0
static void demo_app_order_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(PT_DEMO_APP_ORDER_AFTER_S * 1000));
    app_evt_t e = {.type = APP_EVT_CLOUD_APP_ORDER};
    snprintf(e.data.app_order.order_id, sizeof(e.data.app_order.order_id), "APPDEMO-%lu",
             (unsigned long)(esp_timer_get_time() / 1000000));
    e.data.app_order.package_id = 0;
    e.data.app_order.minutes = PT_DEMO_APP_MINUTES;
    ESP_LOGW(TAG, "[DEMO] gia lap don tu App: %s, %d phut", e.data.app_order.order_id, PT_DEMO_APP_MINUTES);
    app_post_event(&e);
    vTaskDelete(NULL);
}
#endif

void peripheral_test_after_ui(void)
{
#if PT_DEMO_WASH_MODE
    {
        static const uint16_t rate[WS_KIND_COUNT] = PT_DEMO_RATE, pm[WS_KIND_COUNT] = PT_DEMO_PERMILLE;
        device_config_t c = *device_config_get();
        c.wash.mode = PT_DEMO_WASH_MODE;
        for (int k = 0; k < WS_KIND_COUNT; k++)
        {
            c.wash.rate_pct[k] = rate[k];
            c.wash.permille[k] = pm[k];
        }
        esp_err_t err = device_config_set_volatile(&c);
        ESP_LOGW(TAG, "[DEMO] ep kich ban tinh thoi gian = %d: %s", PT_DEMO_WASH_MODE,
                 err == ESP_OK ? "OK" : "CAU HINH SAI (kiem tra PT_DEMO_RATE / PT_DEMO_PERMILLE)");
    }
#endif
#if PT_DEMO_PAYMENT && PT_DEMO_APP_ORDER_AFTER_S > 0
    xTaskCreate(demo_app_order_task, "pt_app", 3072, NULL, 3, NULL);
#endif
#if PT_TEST_AUDIO_TONE_S > 0
    xTaskCreate(audio_tone_task, "pt_tone", 2560, NULL, 3, NULL);
#endif
#if PT_TOUCH_CALIB
    if (!display_touch_ok())
    {
        ESP_LOGW(TAG, "cam ung chua khoi tao duoc -> bo qua hieu chinh");
        return;
    }
    if (PT_TOUCH_CALIB == 2 || !display_touch_has_saved_cal())
        touch_calibration_run();
    else
        ESP_LOGI(TAG, "cam ung da co hieu chinh trong NVS (muon lam lai: dat PT_TOUCH_CALIB = 2)");
#endif
}

/* ================= RUN ALL ================= */
void peripheral_test_run(void)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "########## BAT DAU TEST NGOAI VI ##########");

    // bool ok_sd = test_sd_card();
    // ESP_LOGI(TAG, "");
    bool ok_fram = true, ok_rtc = true, ok_relay = true;
#if PT_TEST_FRAM
    ok_fram = test_fram();
    ESP_LOGI(TAG, "");
#endif
#if PT_TEST_DS3231
    ok_rtc = test_ds3231();
    ESP_LOGI(TAG, "");
#endif
#if PT_TEST_LCD_PINS
    test_lcd_pins();
    ESP_LOGI(TAG, "");
#endif
#if PT_TEST_RELAY
    ok_relay = test_relay(); /* PT_RELAY_CYCLES = 0 thi khong bao gio ra khoi ham nay */
#endif

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "########## KET QUA TEST NGOAI VI ##########");
    // ESP_LOGI(TAG, "SD CARD : %s", ok_sd ? "PASS" : "FAIL");
    ESP_LOGI(TAG, "FRAM    : %s", PT_TEST_FRAM ? (ok_fram ? "PASS" : "FAIL") : "bo qua");
    ESP_LOGI(TAG, "DS3231  : %s", PT_TEST_DS3231 ? (ok_rtc ? "PASS" : "FAIL") : "bo qua");
    ESP_LOGI(TAG, "RELAY   : %s", PT_TEST_RELAY ? (ok_relay ? "PASS" : "FAIL") : "bo qua");
    ESP_LOGI(TAG, "############################################");
}