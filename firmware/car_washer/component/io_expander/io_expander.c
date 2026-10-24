#include "io_expander.h"
#include "app_config.h"
#include "i2c_bus.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "io_exp";

#define IO_POLL_MS          (10)   /* poll du phong ke ca khi co INT */
#define IO_I2C_TIMEOUT_MS   (50)
#define IO_FAULT_THRESHOLD  (3)    /* so lan doc loi lien tiep -> bao loi bus */
#define IO_RETRY_MS         (1000) /* dang mat lien lac: 1 giay thu lai 1 lan */
#define IO_TASK_STACK       (3072)
#define IO_TASK_PRIO        (10)

/* Dung khan cap cua tram = ngat thang nguon toan bo (phan cung), firmware khong doc nut nay. */

#define PUMP_RELAY_MASK ((1u << IO_RELAY_FOAM) | (1u << IO_RELAY_WATER) | (1u << IO_RELAY_DRY))

static const uint8_t k_in_bit[IO_IN_COUNT] = {
    [IO_IN_BTN_FOAM] = IN_BIT_BTN_FOAM,
    [IO_IN_BTN_WATER] = IN_BIT_BTN_WATER,
    [IO_IN_BTN_DRY] = IN_BIT_BTN_DRY,
    [IO_IN_MOTION] = IN_BIT_MOTION,
};

static const uint8_t k_out_bit[IO_RELAY_COUNT] = {
    [IO_RELAY_FOAM] = OUT_BIT_RELAY_FOAM,
    [IO_RELAY_WATER] = OUT_BIT_RELAY_WATER,
    [IO_RELAY_DRY] = OUT_BIT_RELAY_DRY,
    [IO_RELAY_LIGHT] = OUT_BIT_RELAY_LIGHT,
};

#define IN_EFFECTIVE_ACTIVE_LOW (IN_ACTIVE_LOW_MASK)

static i2c_master_bus_handle_t s_bus = NULL;
static i2c_master_dev_handle_t s_in_dev = NULL;
static i2c_master_dev_handle_t s_out_dev = NULL;
static bool s_out_present = false; /* chip output dang tra loi */
static SemaphoreHandle_t s_out_lock = NULL;
static TaskHandle_t s_task = NULL;

static io_input_cb_t s_cb = NULL;
static void *s_cb_ctx = NULL;

static uint8_t s_relay_mask = 0;                 /* logic, bit = io_relay_t */
static volatile bool s_stable[IO_IN_COUNT];      /* trang thai da chong doi */
static volatile bool s_healthy = false;

/* ================= OUTPUT ================= */

static uint8_t mask_to_pins(uint8_t mask)
{
    uint8_t pins = 0xFF; /* bit khong dung de HIGH (trang thai mac dinh cua PCF) */
    for (int r = 0; r < IO_RELAY_COUNT; r++)
    {
        bool on = mask & (1u << r);
#if OUT_RELAY_ACTIVE_LOW
        if (on)
            pins &= ~(1u << k_out_bit[r]);
#else
        if (!on)
            pins &= ~(1u << k_out_bit[r]);
#endif
    }
    return pins;
}

/* Goi khi dang giu s_out_lock */
static esp_err_t write_mask_locked(uint8_t mask)
{
    /* Chip dang vang mat: hoi tham lang le truoc, tranh driver in 3 dong loi moi lan */
    if (!s_out_present && i2c_master_probe(s_bus, PCF8574AT_OUT_I2C_ADDR, IO_I2C_TIMEOUT_MS) != ESP_OK)
        return ESP_ERR_NOT_FOUND;

    uint8_t pins = mask_to_pins(mask);
    esp_err_t err = ESP_FAIL;
    for (int i = 0; i < 3 && err != ESP_OK; i++)
        err = i2c_master_transmit(s_out_dev, &pins, 1, IO_I2C_TIMEOUT_MS);
    if (err == ESP_OK)
    {
        if (!s_out_present)
            ESP_LOGI(TAG, "PCF output 0x%02X OK", PCF8574AT_OUT_I2C_ADDR);
        s_out_present = true;
        s_relay_mask = mask;
    }
    else
    {
        if (s_out_present)
            ESP_LOGE(TAG, "mat lien lac PCF output 0x%02X: %s", PCF8574AT_OUT_I2C_ADDR, esp_err_to_name(err));
        s_out_present = false;
    }
    return err;
}

static void force_pumps_off(const char *why)
{
    xSemaphoreTake(s_out_lock, portMAX_DELAY);
    if (s_relay_mask & PUMP_RELAY_MASK)
    {
        ESP_LOGW(TAG, "TAT BOM KHAN CAP: %s", why);
        write_mask_locked(s_relay_mask & ~PUMP_RELAY_MASK);
    }
    xSemaphoreGive(s_out_lock);
}

esp_err_t io_pump_select(int pump)
{
    if (!s_out_lock)
        return ESP_ERR_INVALID_STATE;
    if (pump >= 0 && pump > IO_RELAY_DRY)
        return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(s_out_lock, portMAX_DELAY);

    uint8_t cur_pumps = s_relay_mask & PUMP_RELAY_MASK;
    uint8_t want_pumps = (pump >= 0) ? (uint8_t)(1u << pump) : 0;

    /* Lop tren (wash_session) da dam bao dead-time. Day la lop bao ve thu 2:
     * neu bi yeu cau chuyen thang A -> B thi tat A, cho dead-time roi moi bat B. */
    if (cur_pumps && want_pumps && cur_pumps != want_pumps)
    {
        ESP_LOGW(TAG, "chuyen bom truc tiep -> ep dead-time %d ms", OUT_PUMP_DEADTIME_MS);
        write_mask_locked(s_relay_mask & ~PUMP_RELAY_MASK);
        xSemaphoreGive(s_out_lock);
        vTaskDelay(pdMS_TO_TICKS(OUT_PUMP_DEADTIME_MS));
        xSemaphoreTake(s_out_lock, portMAX_DELAY);
    }

    /* Kiem tra an toan SAU khi (co the) da cho, vi bus co the loi trong luc cho */
    if (want_pumps && !s_healthy)
    {
        write_mask_locked(s_relay_mask & ~PUMP_RELAY_MASK);
        xSemaphoreGive(s_out_lock);
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = write_mask_locked((s_relay_mask & ~PUMP_RELAY_MASK) | want_pumps);
    xSemaphoreGive(s_out_lock);
    return err;
}

esp_err_t io_light_set(bool on)
{
    if (!s_out_lock)
        return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_out_lock, portMAX_DELAY);
    uint8_t m = on ? (s_relay_mask | (1u << IO_RELAY_LIGHT)) : (s_relay_mask & ~(1u << IO_RELAY_LIGHT));
    esp_err_t err = (m == s_relay_mask) ? ESP_OK : write_mask_locked(m);
    xSemaphoreGive(s_out_lock);
    return err;
}

esp_err_t io_relay_all_off(void)
{
    if (!s_out_lock)
        return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_out_lock, portMAX_DELAY);
    esp_err_t err = write_mask_locked(0);
    xSemaphoreGive(s_out_lock);
    return err;
}

esp_err_t io_relay_refresh(void)
{
    if (!s_out_lock)
        return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_out_lock, portMAX_DELAY);
    esp_err_t err = write_mask_locked(s_relay_mask);
    xSemaphoreGive(s_out_lock);
    return err;
}

uint8_t io_relay_get_mask(void) { return s_relay_mask; }

esp_err_t io_relay_write_raw(uint8_t mask)
{
    if (!s_out_lock)
        return ESP_ERR_INVALID_STATE;

    mask &= (uint8_t)((1u << IO_RELAY_COUNT) - 1);
    bool blocked = (mask & PUMP_RELAY_MASK) && !s_healthy;
    if (blocked)
        mask &= ~PUMP_RELAY_MASK;

    xSemaphoreTake(s_out_lock, portMAX_DELAY);
    esp_err_t err = write_mask_locked(mask);
    xSemaphoreGive(s_out_lock);

    if (err != ESP_OK)
        return err;
    return blocked ? ESP_ERR_INVALID_STATE : ESP_OK;
}

/* ================= INPUT ================= */

bool io_input_is_active(io_input_t in)
{
    if (in == IO_IN_BUS_FAULT)
        return !s_healthy;
    return (in < IO_IN_COUNT) ? s_stable[in] : false;
}

bool io_expander_is_healthy(void) { return s_healthy; }

static void IRAM_ATTR int_isr(void *arg)
{
    BaseType_t hp = pdFALSE;
    if (s_task)
        vTaskNotifyGiveFromISR(s_task, &hp);
    portYIELD_FROM_ISR(hp);
}

static void emit(io_input_t in, bool active)
{
    if (s_cb)
        s_cb(in, active, s_cb_ctx);
}

static void io_task(void *arg)
{
    int64_t change_since[IO_IN_COUNT] = {0};
    bool changing[IO_IN_COUNT] = {0};
    int fail_count = 0;

    bool fault_reported = false;

    for (;;)
    {
        /* Binh thuong doc moi 10 ms; da bao loi thi 1 giay thu lai 1 lan */
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(fault_reported ? IO_RETRY_MS : IO_POLL_MS));

        uint8_t raw = 0xFF;
        esp_err_t err;
        if (!s_healthy && (err = i2c_master_probe(s_bus, PCF8574AT_IN_I2C_ADDR, IO_I2C_TIMEOUT_MS)) != ESP_OK)
        {
            /* chua nhan / mat lien lac: hoi tham lang le, khong in loi cua driver */
        }
        else
        {
            err = i2c_master_receive(s_in_dev, &raw, 1, IO_I2C_TIMEOUT_MS);
        }

        if (err != ESP_OK)
        {
            if (++fail_count >= IO_FAULT_THRESHOLD && !fault_reported)
            {
                fault_reported = true;
                ESP_LOGE(TAG, "mat lien lac PCF input (0x%02X): %s -- thu lai moi %d ms",
                         PCF8574AT_IN_I2C_ADDR, esp_err_to_name(err), IO_RETRY_MS);
                s_healthy = false;
                force_pumps_off("loi bus input");
                emit(IO_IN_BUS_FAULT, true);
            }
            continue;
        }
        fail_count = 0;
        if (!s_healthy)
        {
            ESP_LOGI(TAG, "PCF input 0x%02X OK", PCF8574AT_IN_I2C_ADDR);
            s_healthy = true;
            if (fault_reported)
                emit(IO_IN_BUS_FAULT, false);
            fault_reported = false;
        }

        int64_t now = esp_timer_get_time() / 1000;
        for (int i = 0; i < IO_IN_COUNT; i++)
        {
#if !IN_USE_MOTION_SENSOR
            if (i == IO_IN_MOTION)
                continue; /* khong lap cam bien: luon coi la khong co nguoi */
#endif
            uint8_t bit = k_in_bit[i];
            bool level = raw & (1u << bit);
            bool active = (IN_EFFECTIVE_ACTIVE_LOW & (1u << bit)) ? !level : level;

            if (active == s_stable[i])
            {
                changing[i] = false;
                continue;
            }
            if (!changing[i])
            {
                changing[i] = true;
                change_since[i] = now;
            }

            bool commit = (now - change_since[i] >= IN_DEBOUNCE_MS);
            if (!commit)
                continue;

            changing[i] = false;
            s_stable[i] = active;

            ESP_LOGD(TAG, "input %d -> %d", i, active);
            emit((io_input_t)i, active);
        }
    }
}

/* ================= INIT ================= */

/* In ra moi dia chi dang tra loi tren bus, de biet chip PCF thuc te o dia chi nao */
static void scan_bus(void)
{
    char line[160];
    int n = 0, found = 0;
    for (uint8_t a = 0x08; a <= 0x77; a++)
    {
        if (i2c_master_probe(s_bus, a, 20) == ESP_OK)
        {
            n += snprintf(line + n, sizeof(line) - n, " 0x%02X", a);
            found++;
            if (n >= (int)sizeof(line) - 6)
                break;
        }
    }
    ESP_LOGW(TAG, "quet I2C: %d thiet bi tra loi:%s", found, found ? line : " (khong co)");
    ESP_LOGW(TAG, "  goi y: 0x50 = FRAM, 0x68 = DS3231, 0x57 = EEPROM tren module DS3231");
    ESP_LOGW(TAG, "  PCF8574AT: 0x38..0x3F, PCF8574T (khong co chu A): 0x20..0x27 -- tuy chan A0/A1/A2");
}

esp_err_t io_expander_init(io_input_cb_t cb, void *ctx)
{
    if (s_task)
        return ESP_OK;

    s_cb = cb;
    s_cb_ctx = ctx;
    memset((void *)s_stable, 0, sizeof(s_stable));

    s_out_lock = xSemaphoreCreateMutex();
    if (!s_out_lock)
        return ESP_ERR_NO_MEM;

    esp_err_t err = i2c_bus_init(PCF8574AT_I2C_PORT, PCF8574AT_SDA_GPIO, PCF8574AT_SCL_GPIO, 7);
    if (err != ESP_OK)
        return err;

    i2c_device_config_t dcfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .scl_speed_hz = PCF8574AT_CLK_HZ,
    };
    i2c_master_bus_handle_t bus = i2c_bus_get_handle(PCF8574AT_I2C_PORT);
    s_bus = bus;

    bool out_ok = i2c_master_probe(bus, PCF8574AT_OUT_I2C_ADDR, IO_I2C_TIMEOUT_MS) == ESP_OK;
    bool in_ok = i2c_master_probe(bus, PCF8574AT_IN_I2C_ADDR, IO_I2C_TIMEOUT_MS) == ESP_OK;
    if (!out_ok)
        ESP_LOGE(TAG, "KHONG thay PCF output o dia chi 0x%02X", PCF8574AT_OUT_I2C_ADDR);
    if (!in_ok)
        ESP_LOGE(TAG, "KHONG thay PCF input o dia chi 0x%02X", PCF8574AT_IN_I2C_ADDR);
    if (!out_ok || !in_ok)
        scan_bus();

    dcfg.device_address = PCF8574AT_OUT_I2C_ADDR;
    err = i2c_master_bus_add_device(bus, &dcfg, &s_out_dev);
    if (err != ESP_OK)
        return err;

    dcfg.device_address = PCF8574AT_IN_I2C_ADDR;
    err = i2c_master_bus_add_device(bus, &dcfg, &s_in_dev);
    if (err != ESP_OK)
        return err;

    /* 1. Tat het relay NGAY LAP TUC */
    s_out_present = out_ok;
    xSemaphoreTake(s_out_lock, portMAX_DELAY);
    write_mask_locked(0);
    xSemaphoreGive(s_out_lock);

    /* 2. Dua PCF input ve che do doc (ghi 1 len moi chan) */
    uint8_t ff = 0xFF;
    if (in_ok)
        i2c_master_transmit(s_in_dev, &ff, 1, IO_I2C_TIMEOUT_MS);

    /* 3. Chan INT (open-drain, active-low) */
    if (PCF8574AT_IN_INT_GPIO >= 0)
    {
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << PCF8574AT_IN_INT_GPIO,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .intr_type = GPIO_INTR_NEGEDGE,
        };
        gpio_config(&io);
        esp_err_t isr_err = gpio_install_isr_service(0);
        if (isr_err != ESP_OK && isr_err != ESP_ERR_INVALID_STATE) /* da cai boi module khac thi van OK */
            return isr_err;
        gpio_isr_handler_add(PCF8574AT_IN_INT_GPIO, int_isr, NULL);
    }

    if (xTaskCreate(io_task, "io_exp", IO_TASK_STACK, NULL, IO_TASK_PRIO, &s_task) != pdPASS)
        return ESP_ERR_NO_MEM;

    if (out_ok && in_ok)
        ESP_LOGI(TAG, "init OK (out=0x%02X in=0x%02X int=GPIO%d)",
                 PCF8574AT_OUT_I2C_ADDR, PCF8574AT_IN_I2C_ADDR, PCF8574AT_IN_INT_GPIO);
    else
        ESP_LOGE(TAG, "init: thieu chip PCF -> bom/den/nut KHONG hoat dong, se tu ket noi lai khi chip tra loi");
    return ESP_OK;
}