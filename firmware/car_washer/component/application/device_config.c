#include "device_config.h"
#include "nvs_store.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "devcfg";

#define DEVCFG_NS         "app"
#define DEVCFG_KEY        "devcfg"
#define DEVCFG_MAGIC      0x43464731u /* "CFG1" */
#define DEVCFG_LAYOUT_VER 8 /* 8: goi co packageDuration + parkingTime (giay) tu topic config; 7: den bat theo gio co dinh 18:00 -> 06:00; 6: 6 goi rua (them 25/30 phut); 5: them op_raw, do xe them 5 phut, QR 5 phut */
/* 4: toi da 12 goi. */
/* 3: 3: 2 kich ban tinh thoi gian (toc do / chia phan tram), chu ky bao tien do */

/* So dien thoai hien thi khi CHUA nhan duoc tu server. De rong thi man loi ghi "lien he quan ly". */
#ifndef DEVCFG_OWNER_PHONE_DEFAULT
#define DEVCFG_OWNER_PHONE_DEFAULT "0866129857"
#endif

static device_config_t s_cfg;

static void load_defaults(device_config_t *c)
{
    memset(c, 0, sizeof(*c));
    c->magic = DEVCFG_MAGIC;
    c->layout_ver = DEVCFG_LAYOUT_VER;
    c->config_ver = 0;

    /* Goi mac dinh khi CHUA nhan cau hinh tu server: {gia, thoi luong (phut), do xe (phut)}.
     * Server gui danh sach that qua topic config -> ghi de va luu NVS. */
    static const struct
    {
        uint32_t price;
        uint16_t minutes, park_minutes;
    } def_pkgs[] = {
        {10000, 5, 10},   {20000, 10, 15},  {30000, 15, 20},  {40000, 20, 25},
        {50000, 25, 30},  {60000, 30, 35},  {70000, 35, 40},  {80000, 40, 45},
        {90000, 45, 50},  {100000, 50, 55}, {110000, 55, 60}, {120000, 60, 65},
    };
    c->package_count = sizeof(def_pkgs) / sizeof(def_pkgs[0]);
    for (int i = 0; i < c->package_count; i++)
    {
        c->packages[i].price = def_pkgs[i].price;
        c->packages[i].duration_s = def_pkgs[i].minutes * 60u;
        c->packages[i].park_s = def_pkgs[i].park_minutes * 60u;
        device_config_fill_package(&c->packages[i], i);
    }

    ws_config_t w = WS_CONFIG_DEFAULT;
    c->wash = w;
    light_cfg_t l = LIGHT_CFG_DEFAULT;
    c->light = l;

    c->create_order_timeout_s = 10;
    c->qr_timeout_s = 300; /* tai lieu C06: da chot QR 5 phut */
    c->late_paid_grace_s = 120;
    c->paid_screen_s = 3;
    c->finished_screen_s = 5;
    c->notice_screen_s = 4;
    c->welcome_interval_s = 60;
    c->volume = 100;
    c->progress_interval_s = 10;
    c->select_timeout_s = 60;
    strlcpy(c->owner_phone, DEVCFG_OWNER_PHONE_DEFAULT, sizeof(c->owner_phone));
    c->app_qr[0] = 0;
    /* Khop WS_CONFIG_DEFAULT: SPEED bot x3, nuoc x1, khi (xit kho) x2; hut bui V1 chua co */
    c->op_raw[0] = 3;
    c->op_raw[1] = 1;
    c->op_raw[2] = 2;
    c->op_raw[3] = 1;
}

bool device_config_validate(const device_config_t *c)
{
    if (!c || c->magic != DEVCFG_MAGIC || c->layout_ver != DEVCFG_LAYOUT_VER)
        return false;
    if (c->package_count == 0 || c->package_count > DEVCFG_MAX_PACKAGES)
        return false;
    for (int i = 0; i < c->package_count; i++)
    {
        const devcfg_package_t *p = &c->packages[i];
        if (p->duration_s < DEVCFG_PKG_DURATION_MIN_S || p->duration_s > DEVCFG_PKG_DURATION_MAX_S || p->price == 0)
            return false;
        if (p->park_s < p->duration_s || p->park_s - p->duration_s > DEVCFG_PKG_PARK_EXTRA_MAX_S)
            return false;
        if (memchr(p->label, 0, DEVCFG_LABEL_LEN) == NULL)
            return false;
    }
    if (!ws_config_valid(&c->wash)) /* kich ban, toc do, ti le (tong 1000), dead-time, do xe them */
        return false;
    if (c->progress_interval_s != 0 && (c->progress_interval_s < 2 || c->progress_interval_s > 600))
        return false;
    if (memchr(c->owner_phone, 0, DEVCFG_PHONE_LEN) == NULL || memchr(c->app_qr, 0, DEVCFG_APP_QR_LEN) == NULL)
        return false;
    if (c->select_timeout_s < 10 || c->select_timeout_s > 600)
        return false;
    if (c->light.lat < -90 || c->light.lat > 90 || c->light.lon < -180 || c->light.lon > 180)
        return false;
    if (c->light.mode > LIGHT_MODE_ALWAYS_OFF)
        return false;
    if (c->light.fixed_on_min >= 1440 || c->light.fixed_off_min >= 1440) /* phut trong ngay 0..1439 */
        return false;
    if (c->create_order_timeout_s < 3 || c->create_order_timeout_s > 60)
        return false;
    if (c->qr_timeout_s < 30 || c->qr_timeout_s > 900)
        return false;
    if (c->volume > 100)
        return false;
    for (int i = 0; i < 4; i++)
        if (!(c->op_raw[i] >= 0 && c->op_raw[i] <= 1000)) /* chan ca NaN */
            return false;
    return true;
}

void device_config_init(void)
{
    device_config_t tmp;
    size_t len = sizeof(tmp);
    esp_err_t err = nvs_store_get_blob(DEVCFG_NS, DEVCFG_KEY, &tmp, &len);

    if (err == ESP_OK && len == sizeof(tmp) && device_config_validate(&tmp))
    {
        s_cfg = tmp;
        ESP_LOGI(TAG, "doc cau hinh NVS OK (config_ver=%lu, %u goi)",
                 (unsigned long)s_cfg.config_ver, s_cfg.package_count);
        return;
    }

    ESP_LOGW(TAG, "khong co / hong cau hinh trong NVS -> dung mac dinh");
    load_defaults(&s_cfg);
}

const device_config_t *device_config_get(void) { return &s_cfg; }

esp_err_t device_config_set(const device_config_t *cfg)
{
    if (!device_config_validate(cfg))
        return ESP_ERR_INVALID_ARG;
    esp_err_t err = nvs_store_set_blob(DEVCFG_NS, DEVCFG_KEY, cfg, sizeof(*cfg));
    if (err != ESP_OK)
        return err;
    s_cfg = *cfg;
    ESP_LOGI(TAG, "da luu cau hinh moi (config_ver=%lu)", (unsigned long)cfg->config_ver);
    return ESP_OK;
}

esp_err_t device_config_set_volatile(const device_config_t *cfg)
{
    if (!device_config_validate(cfg))
        return ESP_ERR_INVALID_ARG;
    s_cfg = *cfg;
    ESP_LOGW(TAG, "ap dung cau hinh TAM (khong luu NVS)");
    return ESP_OK;
}

void device_config_fill_package(devcfg_package_t *p, int index)
{
    unsigned long m = p->duration_s / 60u, s = p->duration_s % 60u;
    char tmp[48];
    p->id = (uint32_t)index + 1u;
    if (s)
        snprintf(tmp, sizeof(tmp), "Gói %lu phút %lu giây", m, s);
    else
        snprintf(tmp, sizeof(tmp), "Gói %lu phút", m);
    strlcpy(p->label, tmp, sizeof(p->label)); /* toi da "Gói 99 phút 59 giây" = 22 byte */
}

const devcfg_package_t *device_config_package(int index)
{
    if (index < 0 || index >= s_cfg.package_count)
        return NULL;
    return &s_cfg.packages[index];
}