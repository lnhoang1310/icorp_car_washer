#include "display_driver.h"
#include "app_config.h"

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch_xpt2046.h"
#include "nvs.h"
#include "esp_timer.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>

#if LCD_CONTROLLER == LCD_CTRL_ILI9488
#include "esp_lcd_ili9488.h"
#define LCD_CTRL_NAME   "ILI9488"
#define LCD_BYTES_PX    3   /* ILI9488 qua SPI chi chay 18-bit: 3 byte/diem */
#elif LCD_CONTROLLER == LCD_CTRL_ILI9341
#include "esp_lcd_ili9341.h"
#define LCD_CTRL_NAME   "ILI9341"
#define LCD_BYTES_PX    2   /* RGB565 */
#else
#error "LCD_CONTROLLER khong hop le (xem app_config.h)"
#endif
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "display";

#define BL_LEDC_TIMER   LEDC_TIMER_0
#define BL_LEDC_CHANNEL LEDC_CHANNEL_0
#define BL_LEDC_RES     LEDC_TIMER_10_BIT
#define BL_LEDC_MAX     ((1 << 10) - 1)

#define LCD_BUF_PIXELS  (LCD_H_RES * LCD_DRAW_BUF_LINES)

static esp_lcd_panel_io_handle_t s_lcd_io = NULL;
static esp_lcd_panel_handle_t s_panel = NULL;
static esp_lcd_panel_io_handle_t s_touch_io = NULL;
static esp_lcd_touch_handle_t s_touch = NULL;
static lv_indev_t *s_indev = NULL;

/* Man hinh va cam ung dung chung 1 bus SPI. De khong bao gio doc cam ung trong luc DMA
 * dang day hinh ra man (de gay treo LVGL), danh dau "dang day hinh" va chi doc cam ung khi ranh. */
static volatile bool s_lcd_busy = false;
static SemaphoreHandle_t s_flush_sem = NULL;
#define LCD_FLUSH_TIMEOUT_MS 300

/* Hieu chinh cam ung dang dung: mac dinh lay tu app_config.h, co ban da luu trong NVS thi dung ban do */
static display_touch_cal_t s_cal = {
    .swap_xy = TOUCH_SWAP_XY,
    .invert_x = TOUCH_INVERT_X,
    .invert_y = TOUCH_INVERT_Y,
    .x_min = TOUCH_RAW_X_MIN,
    .x_max = TOUCH_RAW_X_MAX,
    .y_min = TOUCH_RAW_Y_MIN,
    .y_max = TOUCH_RAW_Y_MAX,
};
static bool s_cal_saved = false;
static volatile bool s_touch_raw_mode = false; /* true: tra ve ADC tho, khong quy doi (dung khi hieu chinh) */

#define TOUCH_NVS_NS   "touch"
#define TOUCH_NVS_KEY  "cal"
#define TOUCH_CAL_MAGIC 0x54430001u
static lv_display_t *s_disp = NULL;

/* ================= Den nen ================= */

static esp_err_t backlight_init(void)
{
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << TFT_BACKLIGHT_GPIO,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if(gpio_config(&io_conf) != ESP_OK)
        return ESP_FAIL;
    return gpio_set_level(TFT_BACKLIGHT_GPIO, 1);
}

/* ================= Cam ung: hieu chinh ADC -> toa do man hinh ================= */

void display_touch_map(uint16_t raw_x, uint16_t raw_y, int16_t *sx_out, int16_t *sy_out)
{
    const display_touch_cal_t *c = &s_cal;
    int32_t rx = raw_x, ry = raw_y;
    if (c->swap_xy)
    {
        int32_t t = rx;
        rx = ry;
        ry = t;
    }
    int32_t dx = (int32_t)c->x_max - c->x_min, dy = (int32_t)c->y_max - c->y_min;
    if (dx <= 0) dx = 1;
    if (dy <= 0) dy = 1;
    int32_t sx = (rx - c->x_min) * (LCD_H_RES - 1) / dx;
    int32_t sy = (ry - c->y_min) * (LCD_V_RES - 1) / dy;
    if (sx < 0) sx = 0;
    if (sx > LCD_H_RES - 1) sx = LCD_H_RES - 1;
    if (sy < 0) sy = 0;
    if (sy > LCD_V_RES - 1) sy = LCD_V_RES - 1;
    /* Hieu chinh luu theo man lap xuoi; man lap nguoc 180 do -> dao them ca 2 truc */
    if (c->invert_x ^ LCD_ROTATE_180)
        sx = LCD_H_RES - 1 - sx;
    if (c->invert_y ^ LCD_ROTATE_180)
        sy = LCD_V_RES - 1 - sy;
    *sx_out = (int16_t)sx;
    *sy_out = (int16_t)sy;
}

static void touch_calibrate(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y,
                            uint16_t *strength, uint8_t *point_num, uint8_t max_point_num)
{
    (void)tp;
    (void)strength;
    (void)max_point_num;

    for (int i = 0; i < *point_num; i++)
    {
        int32_t rx = x[i], ry = y[i];
#if CONFIG_XPT2046_CONVERT_ADC_TO_COORDS
        /* Driver da quy doi ADC ra 0..max -> doi nguoc ve ADC de hieu chinh chinh xac */
        rx = rx * 4096 / tp->config.x_max;
        ry = ry * 4096 / tp->config.y_max;
#endif
        if (s_touch_raw_mode)
        {
            x[i] = (uint16_t)rx; /* ADC tho, chua doi truc */
            y[i] = (uint16_t)ry;
            continue;
        }
#if TOUCH_CALIB_LOG
        ESP_LOGI(TAG, "touch raw x=%ld y=%ld", (long)rx, (long)ry);
#endif
        int16_t sx, sy;
        display_touch_map((uint16_t)rx, (uint16_t)ry, &sx, &sy);
        x[i] = (uint16_t)sx;
        y[i] = (uint16_t)sy;
    }
}

static void touch_load_cal(void)
{
    nvs_handle_t h;
    if (nvs_open(TOUCH_NVS_NS, NVS_READONLY, &h) != ESP_OK)
        return;
    struct
    {
        uint32_t magic;
        display_touch_cal_t cal;
    } blob;
    size_t len = sizeof(blob);
    if (nvs_get_blob(h, TOUCH_NVS_KEY, &blob, &len) == ESP_OK && len == sizeof(blob) &&
        blob.magic == TOUCH_CAL_MAGIC && blob.cal.x_max > blob.cal.x_min && blob.cal.y_max > blob.cal.y_min)
    {
        s_cal = blob.cal;
        s_cal_saved = true;
        ESP_LOGI(TAG, "cam ung: dung hieu chinh da luu (swap=%d inv_x=%d inv_y=%d x=%u..%u y=%u..%u)",
                 s_cal.swap_xy, s_cal.invert_x, s_cal.invert_y, s_cal.x_min, s_cal.x_max, s_cal.y_min, s_cal.y_max);
    }
    nvs_close(h);
}

/* ================= API cam ung (cho phan hieu chinh) ================= */

bool display_touch_ok(void)
{
    return s_touch != NULL && s_indev != NULL;
}

void display_touch_set_enabled(bool enabled)
{
    if (s_indev)
        lv_indev_enable(s_indev, enabled);
}

static bool xpt_sample(uint16_t *x, uint16_t *y, uint16_t *z, int threshold);

bool display_touch_read_raw(uint16_t *raw_x, uint16_t *raw_y, uint16_t *strength)
{
    if (!s_touch)
        return false;
    for (int i = 0; i < 50 && s_lcd_busy; i++) /* cho man hinh day hinh xong */
        vTaskDelay(pdMS_TO_TICKS(2));
    uint16_t x = 0, y = 0, z = 0;
    bool pressed = xpt_sample(&x, &y, &z, TOUCH_Z_PRESS);
    if (raw_x) *raw_x = x;
    if (raw_y) *raw_y = y;
    if (strength) *strength = z;
    return pressed;
}

/* s_cal / NVS luon luu theo huong man lap XUOI. Ben ngoai (hieu chinh tai cho) lam viec theo huong
 * dang hien thi -> doi qua lai khi LCD_ROTATE_180 = 1. Nho vay doi LCD_ROTATE_180 khong can hieu chinh lai. */
static void cal_toggle_rotation(display_touch_cal_t *c)
{
    c->invert_x ^= LCD_ROTATE_180;
    c->invert_y ^= LCD_ROTATE_180;
}

void display_touch_get_cal(display_touch_cal_t *cal)
{
    *cal = s_cal;
    cal_toggle_rotation(cal);
}

bool display_touch_has_saved_cal(void)
{
    return s_cal_saved;
}

esp_err_t display_touch_set_cal(const display_touch_cal_t *cal, bool save)
{
    if (!cal || cal->x_max <= cal->x_min || cal->y_max <= cal->y_min)
        return ESP_ERR_INVALID_ARG;
    display_touch_cal_t native = *cal; /* huong dang hien thi -> huong lap xuoi */
    cal_toggle_rotation(&native);
    s_cal = native;
    if (!save)
        return ESP_OK;

    nvs_handle_t h;
    esp_err_t err = nvs_open(TOUCH_NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK)
        return err;
    struct
    {
        uint32_t magic;
        display_touch_cal_t cal;
    } blob = {.magic = TOUCH_CAL_MAGIC, .cal = native};
    err = nvs_set_blob(h, TOUCH_NVS_KEY, &blob, sizeof(blob));
    if (err == ESP_OK)
        err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK)
        s_cal_saved = true;
    return err;
}

esp_err_t display_touch_clear_cal(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(TOUCH_NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK)
        return err;
    nvs_erase_key(h, TOUCH_NVS_KEY);
    err = nvs_commit(h);
    nvs_close(h);
    s_cal_saved = false;
    return err;
}

/* ================= Chan doan cam ung: doc thang thanh ghi XPT2046 ================= */

/* Lenh doc XPT2046 (giong driver: 12 bit, vi sai, PD = 01) */
#define XPT_CMD_Z1 0xB1
#define XPT_CMD_Z2 0xC1
#define XPT_CMD_X  0xD1
#define XPT_CMD_Y  0x91

/* Cho DMA day hinh ra man xong. Bat ky lan doc cam ung nao xen vao giua luc man hinh dang nhan
 * hinh deu co the lam hong lenh cua man hinh (man trang, soc) -> moi lan doc deu phai cho. */
static void wait_lcd_idle(void)
{
    for (int i = 0; i < 100 && s_lcd_busy; i++)
        vTaskDelay(1);
}

static uint16_t xpt_read(uint8_t cmd)
{
    uint8_t b[2] = {0};
    wait_lcd_idle();
    if (esp_lcd_panel_io_rx_param(s_touch_io, cmd, b, 2) != ESP_OK)
        return 0xFFFF;
    return (uint16_t)(((b[0] << 8) | b[1]) >> 3); /* 0..4095 neu chip tra loi dung */
}

static void xpt_read4(uint16_t v[4])
{
    v[0] = xpt_read(XPT_CMD_Z1);
    v[1] = xpt_read(XPT_CMD_Z2);
    v[2] = xpt_read(XPT_CMD_X);
    v[3] = xpt_read(XPT_CMD_Y);
}

static uint16_t median3(uint16_t a, uint16_t b, uint16_t c)
{
    if (a > b) { uint16_t t = a; a = b; b = t; }
    if (b > c) { uint16_t t = b; b = c; c = t; }
    if (a > b) { uint16_t t = a; a = b; b = t; }
    return b;
}

/* Doc 1 mau cam ung (ADC tho). Tra ve true neu luc an z >= threshold va toa do hop le.
 * Tu doc thanh ghi thay cho driver de chinh duoc nguong luc an (TOUCH_Z_PRESS/RELEASE)
 * va loc nhieu bang trung vi 3 lan doc. */
static bool xpt_sample(uint16_t *x, uint16_t *y, uint16_t *z, int threshold)
{
    if (!s_touch_io)
        return false;
    uint16_t z1 = xpt_read(XPT_CMD_Z1);
    uint16_t z2 = xpt_read(XPT_CMD_Z2);
    if (z1 > 4095 || z2 > 4095)
        return false; /* loi doc */
    int zz = (int)z1 + (4095 - (int)z2);
    if (zz < 0)
        zz = 0;
    *z = (uint16_t)zz;
    if (zz < threshold)
        return false;

    (void)xpt_read(XPT_CMD_X); /* mau dau thuong lech, bo */
    uint16_t xs[3], ys[3];
    for (int i = 0; i < 3; i++)
    {
        xs[i] = xpt_read(XPT_CMD_X);
        ys[i] = xpt_read(XPT_CMD_Y);
    }
    uint16_t mx = median3(xs[0], xs[1], xs[2]);
    uint16_t my = median3(ys[0], ys[1], ys[2]);
    if (mx < 50 || mx > 4045 || my < 50 || my > 4045)
        return false;
    /* 3 lan doc lech nhau qua nhieu = nhieu dien, khong phai ngon tay that */
    uint16_t xmin = xs[0], xmax = xs[0], ymin = ys[0], ymax = ys[0];
    for (int i = 1; i < 3; i++)
    {
        if (xs[i] < xmin) xmin = xs[i];
        if (xs[i] > xmax) xmax = xs[i];
        if (ys[i] < ymin) ymin = ys[i];
        if (ys[i] > ymax) ymax = ys[i];
    }
    if (xmax - xmin > TOUCH_MAX_SPREAD || ymax - ymin > TOUCH_MAX_SPREAD)
        return false;
    *x = mx;
    *y = my;
    return true;
}

void display_touch_diag_log(void)
{
    if (!s_touch_io)
        return;
    uint16_t v[4];
    xpt_read4(v);
    int z = (int)v[0] + (4096 - (int)v[1]);
    ESP_LOGI(TAG, "XPT2046 tho: z1=%u z2=%u x=%u y=%u -> luc z=%d (nguong %d)",
             v[0], v[1], v[2], v[3], z, CONFIG_XPT2046_Z_THRESHOLD);
}

display_touch_diag_t display_touch_diag(void)
{
    if (!s_touch_io)
        return TOUCH_DIAG_NOT_INIT;

    /* Doc 2 lan: keo MISO len roi keo xuong. Neu so doc duoc chay theo dien tro keo
     * -> khong co ai day chan MISO -> chip cam ung khong tra loi. */
    uint16_t up[4], dn[4];
    gpio_set_pull_mode(TOUCH_MISO_GPIO, GPIO_PULLUP_ONLY);
    vTaskDelay(pdMS_TO_TICKS(5));
    xpt_read4(up);
    gpio_set_pull_mode(TOUCH_MISO_GPIO, GPIO_PULLDOWN_ONLY);
    vTaskDelay(pdMS_TO_TICKS(5));
    xpt_read4(dn);
    gpio_set_pull_mode(TOUCH_MISO_GPIO, GPIO_FLOATING);

    ESP_LOGI(TAG, "XPT2046 (MISO keo LEN):  z1=%u z2=%u x=%u y=%u", up[0], up[1], up[2], up[3]);
    ESP_LOGI(TAG, "XPT2046 (MISO keo XUONG): z1=%u z2=%u x=%u y=%u", dn[0], dn[1], dn[2], dn[3]);

    bool all_hi = true, all_lo = true, same = true;
    for (int i = 0; i < 4; i++)
    {
        if (up[i] < 4000)
            all_hi = false;
        if (dn[i] > 0)
            all_lo = false;
        if (abs((int)up[i] - (int)dn[i]) > 64)
            same = false;
    }
    if (all_hi && all_lo)
    {
        ESP_LOGE(TAG, "=> CAM UNG KHONG TRA LOI: chan MISO (GPIO%d) khong co ai day. Kiem tra day T_DO -> GPIO%d, "
                      "T_CS -> GPIO%d, nguon cua module cam ung", TOUCH_MISO_GPIO, TOUCH_MISO_GPIO, TOUCH_CS_GPIO);
        return TOUCH_DIAG_NO_RESPONSE;
    }
    if (same && (up[2] >= 4000 || up[2] == 0) && (up[3] >= 4000 || up[3] == 0) && up[0] == up[1])
    {
        ESP_LOGE(TAG, "=> MISO bi giu cung o 1 muc: co the dang chap voi chan khac, hoac SDO man hinh khong nha bus");
        return TOUCH_DIAG_STUCK;
    }
    ESP_LOGI(TAG, "=> chip cam ung CO tra loi (so lieu thay doi hop ly)");
    return TOUCH_DIAG_OK;
}

/* ================= Dong bo man hinh <-> cam ung tren bus SPI chung ================= */

/* ISR: DMA day xong 1 vung hinh */
static bool lcd_flush_done_cb(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    (void)io;
    (void)edata;
    BaseType_t woken = pdFALSE;
    s_lcd_busy = false;
    lv_display_flush_ready((lv_display_t *)user_ctx);
    if (s_flush_sem)
        xSemaphoreGiveFromISR(s_flush_sem, &woken);
    return woken == pdTRUE;
}

/* LVGL bat dau day 1 vung hinh */
static void lcd_flush_start_cb(lv_event_t *e)
{
    (void)e;
    if (s_flush_sem)
        xSemaphoreTake(s_flush_sem, 0); /* xoa tin hieu cu */
    s_lcd_busy = true;
}

/* LVGL cho day hinh xong: ngu cho thay vi quay vong, co thoi han de khong bao gio treo */
static void lcd_flush_wait_cb(lv_display_t *disp)
{
    (void)disp;
    if (xSemaphoreTake(s_flush_sem, pdMS_TO_TICKS(LCD_FLUSH_TIMEOUT_MS)) != pdTRUE)
    {
        static uint32_t n = 0;
        if ((n++ % 20) == 0)
            ESP_LOGE(TAG, "day hinh ra man qua %d ms khong xong (lan %lu) -- bo qua de khong treo",
                     LCD_FLUSH_TIMEOUT_MS, (unsigned long)n);
        /* Khong xoa co ban: neu DMA van con chay thi doc cam ung luc nay se pha hinh */
    }
}

/* Doc cam ung cho LVGL: chi doc khi bus ranh, dang ban thi giu trang thai lan truoc */
static void touch_indev_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    static int16_t lx = 0, ly = 0;
    static bool pressed = false, first_logged = false;
    static uint8_t miss = 0, hits = 0;
    static uint16_t px = 0, py = 0; /* mau truoc, de so sanh khi chua tinh la cham */

    if (!s_lcd_busy)
    {
        uint16_t rx, ry, z;
        /* Tre (hysteresis): bat dau cham can luc >= TOUCH_Z_PRESS, dang cham thi chi can >= TOUCH_Z_RELEASE */
        int thr = pressed ? TOUCH_Z_RELEASE : TOUCH_Z_PRESS;
        bool ok = xpt_sample(&rx, &ry, &z, thr);
        if (ok && !pressed)
        {
            /* Chong cham ao: phai co TOUCH_PRESS_READS mau lien tiep, vi tri gan nhau, moi tinh la cham */
            if (hits > 0 && (abs((int)rx - (int)px) > TOUCH_MAX_SPREAD || abs((int)ry - (int)py) > TOUCH_MAX_SPREAD))
                hits = 0;
            px = rx;
            py = ry;
            if (++hits < TOUCH_PRESS_READS)
                ok = false; /* chua du, cho lan doc sau */
#if TOUCH_CALIB_LOG
            if (!ok)
                ESP_LOGI(TAG, "touch nghi ngo: adc x=%u y=%u luc=%u (mau %u/%d)", rx, ry, z, hits, TOUCH_PRESS_READS);
#endif
        }
        else if (!ok)
        {
            hits = 0;
        }

        if (ok)
        {
            display_touch_map(rx, ry, &lx, &ly);
            pressed = true;
            miss = 0;
            hits = 0;
            if (!first_logged)
            {
                first_logged = true;
                ESP_LOGI(TAG, "cam ung: nhan duoc cham dau tien tai x=%d y=%d (luc %u)", lx, ly, z);
            }
#if TOUCH_CALIB_LOG
            ESP_LOGI(TAG, "touch adc x=%u y=%u luc=%u -> man hinh x=%d y=%d", rx, ry, z, lx, ly);
#endif
        }
        else if (pressed && ++miss < TOUCH_RELEASE_READS)
        {
            /* cham nhe hay bi mat 1-2 lan doc: giu trang thai dang cham, khong tinh la nha tay */
        }
        else
        {
            pressed = false;
            miss = 0;
        }
    }
    data->point.x = lx;
    data->point.y = ly;
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

/* ================= Chan doan: doc ID chip va to mau ================= */

/* Reset cung roi doc lenh 0xD3 (Read ID4) o toc do thap.
 * Can chan SDO(MISO) cua man hinh noi vao TFT_MISO_GPIO. Ghi ra log chip that su la gi. */
static void lcd_probe_id(void)
{
    gpio_config_t rst = {
        .pin_bit_mask = 1ULL << TFT_RST_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&rst);
    gpio_set_level(TFT_RST_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(TFT_RST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(150));

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t cfg = {
        .cs_gpio_num = TFT_CS_GPIO,
        .dc_gpio_num = TFT_DC_GPIO,
        .spi_mode = 0,
        .pclk_hz = 2 * 1000 * 1000, /* doc phai cham: ILI9341/9488 doc toi da ~6 MHz */
        .trans_queue_depth = 2,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)TFT_SPI_HOST, &cfg, &io) != ESP_OK)
        return;

    uint8_t id[5] = {0};
    esp_err_t err = esp_lcd_panel_io_rx_param(io, 0xD3, id, sizeof(id));
    esp_lcd_panel_io_del(io);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "doc ID man hinh loi: %s", esp_err_to_name(err));
        return;
    }

    /* Tim ma 9488 / 9341 o moi do lech bit (o che do SPI co chip chen 1 bit dummy) */
    uint64_t v = 0;
    for (int i = 0; i < (int)sizeof(id); i++)
        v = (v << 8) | id[i];
    uint16_t found = 0;
    for (int sh = 0; sh <= 24 && !found; sh++)
    {
        uint16_t w = (uint16_t)(v >> sh);
        if (w == 0x9488 || w == 0x9341 || w == 0x7796 || w == 0x7789)
            found = w;
    }

    ESP_LOGI(TAG, "doc ID man hinh (0xD3): %02X %02X %02X %02X %02X", id[0], id[1], id[2], id[3], id[4]);
    if (found == 0x9488 || found == 0x9341)
    {
        uint16_t want = (LCD_CONTROLLER == LCD_CTRL_ILI9488) ? 0x9488 : 0x9341;
        if (found == want)
            ESP_LOGI(TAG, "chip man hinh: ILI%04X -- khop cau hinh", found);
        else
            ESP_LOGE(TAG, "chip man hinh la ILI%04X nhung LCD_CONTROLLER dang dat %s -> doi LCD_CONTROLLER trong app_config.h",
                     found, LCD_CTRL_NAME);
    }
    else if (found)
    {
        ESP_LOGE(TAG, "chip man hinh la ST%04X -- firmware chua ho tro chip nay", found);
    }
    else if ((id[1] | id[2] | id[3] | id[4]) == 0x00 || (id[1] & id[2] & id[3] & id[4]) == 0xFF)
    {
        ESP_LOGW(TAG, "man hinh khong tra loi: chan SDO/MISO chua noi, hoac chip chua nhan duoc lenh "
                      "(kiem tra day CS/DC/RST/SCK/SDI va nguon man hinh)");
    }
    else
    {
        ESP_LOGW(TAG, "khong nhan ra ID -- gui dong log nay de doi chieu");
    }
}

#if LCD_SELF_TEST
/* To ca man bang tung mau, khong qua LVGL: thay mau doi tung buoc = day + chip deu dung */
static void lcd_self_test(void)
{
    static const struct { uint16_t c; const char *name; } steps[] = {
        {0xF800, "DO"}, {0x07E0, "XANH LA"}, {0x001F, "XANH DUONG"}, {0xFFFF, "TRANG"}, {0x0000, "DEN"},
    };
    const size_t n = (size_t)LCD_H_RES * LCD_DRAW_BUF_LINES;
    uint16_t *buf = heap_caps_malloc(n * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!buf)
        return;

    for (size_t k = 0; k < sizeof(steps) / sizeof(steps[0]); k++)
    {
        uint16_t c = steps[k].c;
#if LCD_CONTROLLER == LCD_CTRL_ILI9341
        c = (uint16_t)((c >> 8) | (c << 8)); /* ILI9341 nhan byte cao truoc */
#endif
        for (size_t i = 0; i < n; i++)
            buf[i] = c;
        for (int y = 0; y < LCD_V_RES; y += LCD_DRAW_BUF_LINES)
        {
            int y2 = y + LCD_DRAW_BUF_LINES;
            if (y2 > LCD_V_RES)
                y2 = LCD_V_RES;
            esp_lcd_panel_draw_bitmap(s_panel, 0, y, LCD_H_RES, y2, buf);
        }
        ESP_LOGI(TAG, "self test: man hinh phai dang mau %s", steps[k].name);
        vTaskDelay(pdMS_TO_TICKS(700));
    }
    heap_caps_free(buf);
}
#endif

/* ================= Khoi tao ================= */

esp_err_t display_init(void)
{
    ESP_RETURN_ON_ERROR(backlight_init(), TAG, "backlight");

    /* 1. Bus SPI dung chung cho TFT + cam ung */
    spi_bus_config_t bus = {
        .sclk_io_num = TFT_SCK_GPIO,
        .mosi_io_num = TFT_MOSI_GPIO,
        .miso_io_num = TFT_MISO_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_BUF_PIXELS * LCD_BYTES_PX + 64,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(TFT_SPI_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "spi bus");

    /* Chan doan: chip man hinh that su la gi */
    lcd_probe_id();

    /* 2. Panel */
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = TFT_CS_GPIO,
        .dc_gpio_num = TFT_DC_GPIO,
        .spi_mode = 0,
        .pclk_hz = TFT_SPI_MHZ * 1000 * 1000,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)TFT_SPI_HOST, &io_cfg, &s_lcd_io),
                        TAG, "lcd io");

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = TFT_RST_GPIO,
        .rgb_ele_order = LCD_BGR_ORDER ? LCD_RGB_ELEMENT_ORDER_BGR : LCD_RGB_ELEMENT_ORDER_RGB,
#if LCD_CONTROLLER == LCD_CTRL_ILI9488
        .bits_per_pixel = 18, /* ILI9488 qua SPI bat buoc 18-bit, driver tu chuyen tu RGB565 */
#else
        .bits_per_pixel = 16,
#endif
    };
#if LCD_CONTROLLER == LCD_CTRL_ILI9488
    /* Tham so 3: buffer chuyen mau trong RAM noi, phai >= buffer ve cua LVGL */
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ili9488(s_lcd_io, &panel_cfg, LCD_BUF_PIXELS, &s_panel), TAG, "ili9488");
#else
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ili9341(s_lcd_io, &panel_cfg, &s_panel), TAG, "ili9341");
#endif

    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);
    esp_lcd_panel_invert_color(s_panel, LCD_INVERT_COLOR);
    esp_lcd_panel_swap_xy(s_panel, LCD_SWAP_XY);
    esp_lcd_panel_mirror(s_panel, LCD_MIRROR_X, LCD_MIRROR_Y);
    esp_lcd_panel_set_gap(s_panel, LCD_OFFSET_X, LCD_OFFSET_Y);
    esp_lcd_panel_disp_on_off(s_panel, true);

#if LCD_SELF_TEST
    lcd_self_test();
#endif

    /* 3. LVGL */
    lvgl_port_cfg_t lv_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lv_cfg.task_priority = 4;
    lv_cfg.task_affinity = 1; /* core 1, tach khoi mang/io o core 0 */
    lv_cfg.task_stack = 8192;
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lv_cfg), TAG, "lvgl port");

    lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = s_lcd_io,
        .panel_handle = s_panel,
        .buffer_size = LCD_BUF_PIXELS,
        .double_buffer = true,
        .hres = LCD_H_RES,
        .vres = LCD_V_RES,
        .monochrome = false,
        .rotation = {
            .swap_xy = LCD_SWAP_XY,
            .mirror_x = LCD_MIRROR_X,
            .mirror_y = LCD_MIRROR_Y,
        },
        .color_format = LV_COLOR_FORMAT_RGB565,
#if LCD_CONTROLLER == LCD_CTRL_ILI9488
        .flags = {
            .buff_dma = 0,
            .buff_spiram = 1, /* driver ILI9488 chep sang buffer DMA rieng nen buffer LVGL de o PSRAM */
            .swap_bytes = 0,
        },
#else
        .flags = {
            .buff_dma = 1,    /* ILI9341 gui thang buffer LVGL qua DMA -> phai o RAM noi */
            .buff_spiram = 0,
            .swap_bytes = 1,  /* ILI9341 nhan RGB565 byte cao truoc */
        },
#endif
    };
    s_disp = lvgl_port_add_disp(&disp_cfg);
    ESP_RETURN_ON_FALSE(s_disp, ESP_FAIL, TAG, "lvgl_port_add_disp");

    /* Thay co che bao "day hinh xong" cua esp_lvgl_port bang ban co danh dau ban/ranh + thoi han */
    s_flush_sem = xSemaphoreCreateBinary();
    esp_lcd_panel_io_callbacks_t io_cbs = {.on_color_trans_done = lcd_flush_done_cb};
    esp_lcd_panel_io_register_event_callbacks(s_lcd_io, &io_cbs, s_disp);
    lvgl_port_lock(0);
    lv_display_add_event_cb(s_disp, lcd_flush_start_cb, LV_EVENT_FLUSH_START, NULL);
    lv_display_set_flush_wait_cb(s_disp, lcd_flush_wait_cb);
    lvgl_port_unlock();

    /* 4. Cam ung XPT2046 (cung bus, toc do thap) */
    esp_lcd_panel_io_spi_config_t tio = ESP_LCD_TOUCH_IO_SPI_XPT2046_CONFIG(TOUCH_CS_GPIO);
    tio.pclk_hz = TOUCH_SPI_MHZ * 1000 * 1000;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)TFT_SPI_HOST, &tio, &s_touch_io),
                        TAG, "touch io");

    esp_lcd_touch_config_t tcfg = {
        .x_max = LCD_H_RES,
        .y_max = LCD_V_RES,
        .rst_gpio_num = GPIO_NUM_NC,
#if TOUCH_USE_IRQ
        .int_gpio_num = TOUCH_IRQ_GPIO,
#else
        .int_gpio_num = GPIO_NUM_NC, /* doc dinh ky, khong dung chan IRQ (xem TOUCH_USE_IRQ) */
#endif
        .levels = {.reset = 0, .interrupt = 0},
        .flags = {.swap_xy = 0, .mirror_x = 0, .mirror_y = 0}, /* xu ly het trong touch_calibrate */
        .process_coordinates = touch_calibrate,
    };
    esp_err_t err = esp_lcd_touch_new_spi_xpt2046(s_touch_io, &tcfg, &s_touch);
    if (err == ESP_OK)
    {
        touch_load_cal();
        /* Tu tao indev (khong dung lvgl_port_add_touch) de chi doc cam ung khi bus SPI ranh */
        lvgl_port_lock(0);
        s_indev = lv_indev_create();
        lv_indev_set_type(s_indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(s_indev, touch_indev_read_cb);
        lv_indev_set_display(s_indev, s_disp);
        lvgl_port_unlock();
        if (!s_indev)
            ESP_LOGE(TAG, "tao indev cam ung that bai");
        else
            ESP_LOGI(TAG, "cam ung XPT2046 OK (doc dinh ky, CS=GPIO%d)", TOUCH_CS_GPIO);
    }
    else
    {
        ESP_LOGE(TAG, "khoi tao XPT2046 loi: %s -- man hinh van hien thi, khong cam ung", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "display OK %s %dx%d, SPI %d MHz", LCD_CTRL_NAME, LCD_H_RES, LCD_V_RES, TFT_SPI_MHZ);
    return ESP_OK;
}