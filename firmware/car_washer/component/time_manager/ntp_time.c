#include "ntp_time.h"
#include "rtc_ds3231.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "esp_log.h"
#include <time.h>
#include <sys/time.h>
#include <string.h>

static const char *TAG = "ntp_time";

#define VALID_TIME_THRESHOLD 1672531200
#define MAX_VALID_TIME 2051222400UL /* ~01/01/2035 -- gioi han TREN hop ly, tranh nhan gio loi (vd RTC doc glitch, SNTP loi) nhay vao tuong lai xa nhu 2070 */

static bool s_sntp_synced = false;

static bool system_time_valid(void)
{
    time_t now = time(NULL);
    return now > VALID_TIME_THRESHOLD;
}

static void set_system_time_from_rtc(void)
{
    struct tm rtc_time;
    if (rtc_ds3231_get_time(&rtc_time) != ESP_OK)
    {
        ESP_LOGW(TAG, "Doc RTC that bai -- bo qua, cho SNTP");
        return;
    }

    if (rtc_time.tm_year < (2020 - 1900) || rtc_time.tm_year > (2035 - 1900) || rtc_time.tm_mon < 0 ||
        rtc_time.tm_mon > 11 || rtc_time.tm_mday < 1 || rtc_time.tm_mday > 31)
    {
        ESP_LOGW(TAG, "Gio doc tu RTC khong hop le (co the do nhieu I2C lam sai lech du lieu) -- bo qua, cho SNTP");
        return;
    }

    time_t t = mktime(&rtc_time);
    struct timeval tv = {.tv_sec = t, .tv_usec = 0};
    settimeofday(&tv, NULL);

    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &rtc_time);
    ESP_LOGI(TAG, "Da dat gio he thong TAM THOI tu RTC: %s (cho SNTP xac nhan lai)", buf);
}

static void rtc_sync_from_system(void)
{
    if (!rtc_ds3231_is_present())
        return;

    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);

    if (rtc_ds3231_set_time(&tm_now) == ESP_OK)
        ESP_LOGI(TAG, "Da ghi gio he thong xuong RTC");
    else
        ESP_LOGW(TAG, "Ghi gio xuong RTC that bai");
}

static void time_sync_notification_cb(struct timeval *tv)
{
    (void)tv;

    time_t now = time(NULL);

    if (now > (time_t)MAX_VALID_TIME || now < (time_t)VALID_TIME_THRESHOLD)
    {
        ESP_LOGW(TAG, "SNTP tra ve gio KHONG HOP LE (epoch=%lld) -- BO QUA, KHONG ghi xuong RTC, giu gio cu",
                 (long long)now);
        return;
    }

    s_sntp_synced = true;

    struct tm tm_now;
    localtime_r(&now, &tm_now);

    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_now);
    ESP_LOGI(TAG, "SNTP dong bo thanh cong: %s", buf);

    rtc_sync_from_system();
}

void ntp_time_init(void)
{
    setenv("TZ", "ICT-7", 1); /* Gio Viet Nam, UTC+7 */
    tzset();

    if (!system_time_valid())
    {
        ESP_LOGW(TAG, "Dong ho he thong chua hop le (vua boot/reset)");
        if (rtc_ds3231_is_present())
            set_system_time_from_rtc();
        else
            ESP_LOGW(TAG, "Khong co RTC ngoai -- gio se sai cho toi khi SNTP dong bo duoc");
    }
    else
    {
        ESP_LOGI(TAG, "Dong ho he thong da hop le san");
    }
}

void ntp_time_start_sync(void)
{
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(
        3, ESP_SNTP_SERVER_LIST("vn.pool.ntp.org", "time.google.com", "pool.ntp.org"));

    config.sync_cb = time_sync_notification_cb;
    esp_netif_sntp_init(&config);
    sntp_set_sync_interval(15 * 60 * 1000); /* 15 phut */
    esp_netif_sntp_start();

    ESP_LOGI(TAG, "SNTP started");
}

void ntp_time_stop_sync(void)
{
    esp_netif_sntp_deinit();
}

bool ntp_time_is_synced(void) { return s_sntp_synced; }