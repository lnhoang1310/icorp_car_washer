#include "cloud_protocol.h"
#include "esp_log.h"
#include "esp_random.h"
#include "cJSON.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

static const char *TAG = "cloud_proto";

/* ================= TIEN ICH ================= */

bool cp_mac_normalize(const char *in, char out[CP_MAC_LEN])
{
    int n = 0;
    for (; in && *in; in++)
    {
        if (*in == ':' || *in == '-')
            continue;
        if (!isxdigit((unsigned char)*in) || n >= CP_MAC_LEN - 1)
        {
            out[0] = 0;
            return false;
        }
        out[n++] = (char)tolower((unsigned char)*in);
    }
    out[n] = 0;
    return n == CP_MAC_LEN - 1;
}

/* So ngay tu 1970-01-01 (thuat toan days_from_civil) */
static int64_t days_from_civil(int y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

int64_t cp_iso_parse(const char *s)
{
    int y, mo, d, h, mi, se;
    if (!s || sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d", &y, &mo, &d, &h, &mi, &se) != 6)
        return -1;
    int64_t t = days_from_civil(y, (unsigned)mo, (unsigned)d) * 86400 + h * 3600 + mi * 60 + se;
    const char *p = s + 19;
    if (*p == '.')
        while (*++p >= '0' && *p <= '9')
            ;
    if (*p == '+' || *p == '-')
    {
        int oh = 0, om = 0;
        if (sscanf(p + 1, "%2d:%2d", &oh, &om) >= 1)
            t += (*p == '+' ? -1 : 1) * (oh * 3600 + om * 60);
    }
    return t;
}

uint32_t cp_txn_from_str(const char *s)
{
    if (!s || !*s)
        return 0;
    char *end = NULL;
    unsigned long v = strtoul(s, &end, 10);
    return (end && *end == 0 && v > 0 && v <= UINT32_MAX) ? (uint32_t)v : 0;
}

void cp_free(char *json)
{
    if (json)
        cJSON_free(json);
}

/* UTC ISO 8601: "2026-10-08T02:00:00Z" */
static void iso_now(char out[24])
{
    time_t t = time(NULL);
    struct tm tm;
    gmtime_r(&t, &tm);
    strftime(out, 24, "%Y-%m-%dT%H:%M:%SZ", &tm);
}

/* packageId: UUID v4 ngau nhien -> khong trung qua reboot, khong trung giua cac chuong */
static void make_package_id(char out[37])
{
    uint8_t b[16];
    esp_fill_random(b, sizeof(b));
    b[6] = (b[6] & 0x0F) | 0x40;
    b[8] = (b[8] & 0x3F) | 0x80;
    snprintf(out, 37, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
}

/* ================= GOI LEN ================= */

/* Bao ngoai {type, packageId, timestamp, payload}. *payload = object de them truong. */
static cJSON *envelope(const char *type, cJSON **payload)
{
    char pid[37], ts[24];
    make_package_id(pid);
    iso_now(ts);
    cJSON *root = cJSON_CreateObject();
    if (!root)
        return NULL;
    cJSON_AddStringToObject(root, "type", type);
    cJSON_AddStringToObject(root, "packageId", pid);
    cJSON_AddStringToObject(root, "timestamp", ts);
    *payload = cJSON_AddObjectToObject(root, "payload");
    return root;
}

/* Bao ngoai co bayMac + bayNumber */
static cJSON *envelope_bay(const char *type, const char *bay_mac, int bay_number, cJSON **payload)
{
    cJSON *root = envelope(type, payload);
    if (root)
    {
        cJSON_AddStringToObject(*payload, "bayMac", bay_mac);
        cJSON_AddNumberToObject(*payload, "bayNumber", bay_number);
    }
    return root;
}

static char *finish(cJSON *root)
{
    if (!root)
        return NULL;
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return s;
}

char *cp_build_boot(const char *mac, const char *fw_ver, const char *hw_ver, uint8_t wash_mode, const float raw[4])
{
    cJSON *p;
    cJSON *root = envelope("Boot", &p);
    if (!root)
        return NULL;
    cJSON_AddStringToObject(p, "mac", mac);
    cJSON_AddStringToObject(p, "firmwareVersion", fw_ver);
    cJSON_AddStringToObject(p, "hardwareVersion", hw_ver);
    /* Cau hinh dang AP DUNG (khong phai cau hinh dang cho), object day du */
    cJSON *om = cJSON_AddObjectToObject(p, "operationMode");
    if (om)
    {
        static const char *const keys[4] = {"foam", "water", "air", "vacuum"};
        cJSON_AddStringToObject(om, "mode", wash_mode == WS_MODE_SHARE ? "RATIO" : "SPEED");
        for (int i = 0; i < 4; i++)
            cJSON_AddNumberToObject(om, keys[i], round((double)raw[i] * 1000.0) / 1000.0);
    }
    return finish(root);
}

char *cp_build_init(const char *bay_mac, int bay_number, uint32_t duration_s, uint32_t price)
{
    cJSON *p;
    cJSON *root = envelope_bay("InitTransaction", bay_mac, bay_number, &p);
    if (!root)
        return NULL;
    cJSON_AddNumberToObject(p, "packageDuration", duration_s);
    cJSON_AddNumberToObject(p, "price", price);
    return finish(root);
}

char *cp_build_cancel(const char *bay_mac, int bay_number, uint32_t txn)
{
    cJSON *p;
    cJSON *root = envelope_bay("CancelPayment", bay_mac, bay_number, &p);
    if (!root)
        return NULL;
    cJSON_AddNumberToObject(p, "transactionId", txn);
    return finish(root);
}

static const char *end_reason_str(uint8_t r)
{
    switch (r)
    {
    case WS_END_USER_STOP:
        return "USER_STOP";
    case WS_END_CANCELLED:
        return "REMOTE_STOP"; /* tram chi huy phien khi server gui RemoteStopTransaction */
    default:
        return "NORMAL_FINISHED"; /* het thoi gian su dung / het thoi gian do xe */
    }
}

char *cp_build_meter(const char *bay_mac, int bay_number, const ws_session_t *s)
{
    cJSON *p;
    cJSON *root = envelope_bay("Meter", bay_mac, bay_number, &p);
    if (!root)
        return NULL;

    bool in_session = s && (s->state == WS_ST_RUNNING || s->state == WS_ST_PAUSED || s->state == WS_ST_FINISHED);
    bool washing = in_session && s->state != WS_ST_FINISHED;
    uint32_t txn = in_session ? cp_txn_from_str(s->order_id) : 0;
    if (in_session && !txn) /* vd don che do thu "APPDEMO-..." */
        ESP_LOGD(TAG, "phien '%s' khong co transactionId so -> bao 0", s->order_id);

    int on = (washing && s->state == WS_ST_RUNNING) ? (int)s->active : -1;
    cJSON_AddStringToObject(p, "status", washing ? "WASHING" : "NORMAL");
    cJSON_AddNumberToObject(p, "transactionId", txn);
    cJSON_AddBoolToObject(p, "foamOn", on == WS_FOAM);
    cJSON_AddBoolToObject(p, "waterOn", on == WS_WATER);
    cJSON_AddBoolToObject(p, "airOn", on == WS_DRY); /* "khi" = xit kho */
    cJSON_AddBoolToObject(p, "vacuumOn", false);     /* V1 chua co hut bui */
    if (in_session)
    {
        if (!washing)
            cJSON_AddStringToObject(p, "reason", end_reason_str(s->end_reason));
        /* Giay bom THUC TE cua tung chuc nang trong phien */
        cJSON_AddNumberToObject(p, "foamSeconds", s->used_ms[WS_FOAM] / 1000);
        cJSON_AddNumberToObject(p, "waterSeconds", s->used_ms[WS_WATER] / 1000);
        cJSON_AddNumberToObject(p, "airSeconds", s->used_ms[WS_DRY] / 1000);
        cJSON_AddNumberToObject(p, "vacuumSeconds", 0);
        /* Thoi luong do xe TOI DA cua phien (goi + thoi gian them), khong doi trong phien */
        cJSON_AddNumberToObject(p, "parkingTime", s->park_total_ms / 1000);
    }
    return finish(root);
}

/* ================= GOI XUONG ================= */

static bool get_pos_number(const cJSON *obj, const char *key, uint32_t *out)
{
    const cJSON *j = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsNumber(j) || j->valuedouble < 1 || j->valuedouble > UINT32_MAX)
        return false;
    *out = (uint32_t)j->valuedouble;
    return true;
}

void cp_parse_down(const char *data, int len, cp_down_t *out)
{
    memset(out, 0, sizeof(*out));
    out->type = CP_DOWN_INVALID;
    out->expire_at = -1;

    cJSON *root = cJSON_ParseWithLength(data, len);
    if (!root)
        return;
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    const cJSON *p = cJSON_GetObjectItemCaseSensitive(root, "payload");
    if (!cJSON_IsString(type) || !cJSON_IsObject(p))
        goto done;
    strlcpy(out->type_name, type->valuestring, sizeof(out->type_name));

    /* Tai lieu: "bayMac". Nhan ca "bayMAC" (ban nhap cu) cho chac */
    const cJSON *m = cJSON_GetObjectItemCaseSensitive(p, "bayMac");
    if (!m)
        m = cJSON_GetObjectItemCaseSensitive(p, "bayMAC");
    if (cJSON_IsString(m))
        cp_mac_normalize(m->valuestring, out->bay_mac);

    if (!strcmp(type->valuestring, "ShowPaymentCode"))
    {
        const cJSON *code = cJSON_GetObjectItemCaseSensitive(p, "code");
        if (!cJSON_IsString(code) || !code->valuestring[0] || !get_pos_number(p, "transactionId", &out->txn))
            goto done;
        out->code_truncated = strlcpy(out->code, code->valuestring, sizeof(out->code)) >= sizeof(out->code);
        get_pos_number(p, "price", &out->price);
        const cJSON *exp = cJSON_GetObjectItemCaseSensitive(p, "expireAt");
        if (cJSON_IsString(exp))
            out->expire_at = cp_iso_parse(exp->valuestring);
        out->type = CP_DOWN_SHOW_QR;
    }
    else if (!strcmp(type->valuestring, "StartTransaction"))
    {
        if (!get_pos_number(p, "transactionId", &out->txn) || !get_pos_number(p, "packageDuration", &out->duration_s))
            goto done;
        out->type = CP_DOWN_START;
    }
    else if (!strcmp(type->valuestring, "RemoteStopTransaction"))
    {
        if (!get_pos_number(p, "transactionId", &out->txn))
            goto done;
        out->type = CP_DOWN_REMOTE_STOP;
    }
    else
    {
        out->type = CP_DOWN_UNKNOWN;
    }
done:
    cJSON_Delete(root);
}

/* ================= CAU HINH ================= */

/* Doi 4 gia tri goc (bot, nuoc, khi, hut bui) sang tham so wash_session. is_speed: SPEED / RATIO */
static bool fill_op_mode(bool is_speed, const double v[4], cp_op_mode_t *out)
{
    for (int i = 0; i < 4; i++)
    {
        if (!isfinite(v[i]) || v[i] <= 0)
            return false;
        out->raw[i] = (float)v[i];
    }
    if (is_speed)
    {
        /* He so x100 (3 -> 300%). Chi so wash_session: 0 bot, 1 nuoc, 2 kho (= air). */
        out->mode = WS_MODE_RATE;
        for (int k = 0; k < WS_KIND_COUNT; k++)
        {
            long r = lround(v[k] * 100.0);
            if (r < WS_RATE_PCT_MIN || r > WS_RATE_PCT_MAX)
                return false;
            out->rate_pct[k] = (uint16_t)r;
        }
        return true;
    }
    /* RATIO: so nguyen > 0, tong 100. V1 chua co hut bui -> phan hut bui chia lai cho
     * bot/nuoc/kho theo dung ti le cua chung, vd 20/50/20/10 -> 22.2% / 55.6% / 22.2% */
    out->mode = WS_MODE_SHARE;
    long sum = 0;
    for (int i = 0; i < 4; i++)
    {
        if (v[i] != floor(v[i]))
            return false;
        sum += (long)v[i];
    }
    if (sum != 100)
        return false;
    double s3 = v[0] + v[1] + v[2];
    int acc = 0;
    for (int k = 0; k < WS_KIND_COUNT - 1; k++)
    {
        out->permille[k] = (uint16_t)lround(v[k] * 1000.0 / s3);
        acc += out->permille[k];
    }
    out->permille[WS_KIND_COUNT - 1] = (uint16_t)(1000 - acc);
    return true;
}

/* Tach chuoi theo ky tu sep thanh toi da max phan (sua tai cho). Tra ve so phan, -1 neu qua max */
static int split(char *s, char sep, char *parts[], int max)
{
    int n = 0;
    for (char *p = s;; p++)
    {
        if (n >= max)
            return -1;
        parts[n++] = p;
        p = strchr(p, sep);
        if (!p)
            return n;
        *p = '\0';
    }
}

/* So thuc hop le (chap nhan khoang trang hai dau) */
static bool parse_num(const char *s, double *out)
{
    char *end;
    while (isspace((unsigned char)*s))
        s++;
    if (!*s)
        return false;
    *out = strtod(s, &end);
    while (isspace((unsigned char)*end))
        end++;
    return *end == '\0' && isfinite(*out);
}

/* So nguyen duong <= max */
static bool parse_uint(const char *s, uint32_t max, uint32_t *out)
{
    double d;
    if (!parse_num(s, &d) || d != floor(d) || d <= 0 || d > max)
        return false;
    *out = (uint32_t)d;
    return true;
}

bool cp_parse_mode_str(const char *str, cp_op_mode_t *out)
{
    memset(out, 0, sizeof(*out));
    char buf[64];
    if (!str || strlen(str) >= sizeof(buf))
        return false;
    strcpy(buf, str);
    char *f[6];
    int n = split(buf, '#', f, 6);
    if (n != 5) /* mode#foam#water#air#vacuum: dung 4 gia tri */
        return false;

    bool is_speed;
    while (isspace((unsigned char)*f[0]))
        f[0]++;
    if (!strcasecmp(f[0], "S") || !strcasecmp(f[0], "SPEED"))
        is_speed = true;
    else if (!strcasecmp(f[0], "R") || !strcasecmp(f[0], "RATIO"))
        is_speed = false;
    else
        return false;

    double v[4];
    for (int i = 0; i < 4; i++)
        if (!parse_num(f[i + 1], &v[i]))
            return false;
    return fill_op_mode(is_speed, v, out);
}

bool cp_parse_packages_str(const char *str, cp_pkg_list_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!str)
        return false;
    size_t len = strlen(str);
    char *buf = malloc(len + 1);
    if (!buf)
        return false;
    memcpy(buf, str, len + 1);

    char *items[CP_MAX_PACKAGES + 1];
    int n = split(buf, '|', items, CP_MAX_PACKAGES + 1);
    bool ok = n >= 1 && n <= CP_MAX_PACKAGES;
    for (int i = 0; ok && i < n; i++)
    {
        char *f[4];
        cp_pkg_t *p = &out->p[i];
        ok = split(items[i], '#', f, 4) == 3 &&
             parse_uint(f[0], CP_PKG_PRICE_MAX, &p->price) &&
             parse_uint(f[1], CP_PKG_DURATION_MAX_S, &p->duration_s) &&
             parse_uint(f[2], CP_PKG_DURATION_MAX_S + CP_PKG_PARK_EXTRA_MAX_S, &p->park_s) &&
             p->duration_s >= CP_PKG_DURATION_MIN_S &&
             p->park_s >= p->duration_s &&                         /* do xe bao gom thoi gian rua */
             p->park_s - p->duration_s <= CP_PKG_PARK_EXTRA_MAX_S;
        if (!ok)
            ESP_LOGW(TAG, "goi thu %d khong hop le (price#packageDuration#parkingTime)", i + 1);
    }
    if (ok)
        out->count = (uint8_t)n;
    free(buf);
    return ok;
}

bool cp_parse_hotline(const char *str, char out[DEVCFG_PHONE_LEN])
{
    out[0] = '\0';
    if (!str)
        return false;
    while (isspace((unsigned char)*str))
        str++;
    size_t n = strlen(str);
    while (n && isspace((unsigned char)str[n - 1]))
        n--;
    if (n >= DEVCFG_PHONE_LEN)
        return false;
    for (size_t i = 0; i < n; i++)
        if (!isdigit((unsigned char)str[i]) && !strchr("+-.() ", str[i]))
            return false;
    memcpy(out, str, n);
    out[n] = '\0';
    return true;
}

void cp_parse_config(const char *data, int len, cp_config_t *out)
{
    memset(out, 0, sizeof(*out));
    cJSON *root = cJSON_ParseWithLength(data, len);
    if (!cJSON_IsObject(root))
    {
        cJSON_Delete(root);
        return;
    }
    out->json_ok = true;
    const cJSON *m = cJSON_GetObjectItemCaseSensitive(root, "mode");
    const cJSON *p = cJSON_GetObjectItemCaseSensitive(root, "packages");
    out->has_mode = cJSON_IsString(m);
    out->has_packages = cJSON_IsString(p);
    out->mode_ok = out->has_mode && cp_parse_mode_str(m->valuestring, &out->mode);
    out->packages_ok = out->has_packages && cp_parse_packages_str(p->valuestring, &out->packages);
    const cJSON *h = cJSON_GetObjectItemCaseSensitive(root, "hotline");
    out->has_hotline = cJSON_IsString(h);
    out->hotline_ok = out->has_hotline && cp_parse_hotline(h->valuestring, out->hotline);
    const cJSON *mi = cJSON_GetObjectItemCaseSensitive(root, "meterInterval");
    out->has_meter_interval = (mi != NULL);
    if (cJSON_IsNumber(mi))
    {
        double v = mi->valuedouble;
        if (v == (double)(uint32_t)v && v >= CLOUD_METER_INTERVAL_MIN_S && v <= CLOUD_METER_INTERVAL_MAX_S)
        {
            out->meter_interval_ok = true;
            out->meter_interval_s = (uint32_t)v;
        }
    }
    cJSON_Delete(root);
}