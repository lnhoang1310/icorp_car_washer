/*
 * Phat thong bao: file WAV trong phan vung "storage" (LittleFS) -> I2S -> MAX98357A.
 *
 *  - WAV ho tro: PCM 16-bit, mono hoac stereo (tu tron ve mono), 8..48 kHz.
 *    Khuyen dung 16 kHz mono de nhe bo nho.
 *  - File nam trong assets/audio/, ten theo cot 2 cua audio_clips.h. Khi build, thu muc
 *    nay duoc dong goi thanh anh LittleFS va nap cung firmware (xem main/CMakeLists.txt).
 *  - Thieu file / chua nap phan vung: phat tieng "ding" tao san trong code de van
 *    kiem tra duoc loa va biet la dang co thong bao.
 *  - Cau moi chen ngang cau dang phat (hang doi 1 phan tu, luon giu yeu cau moi nhat).
 *  - MAX98357A khong co dieu chinh am luong -> nhan he so bang phan mem.
 *  - Chan SD_MODE: keo xuong khi khong phat de tat ampli (het tieng "xe", tiet kiem dien).
 */
#include "audio_player.h"
#include "app_config.h"

#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

static const char *TAG = "audio";

#define AUDIO_BASE_PATH     "/audio"
#define AUDIO_PARTITION     "storage"
#define AUDIO_CHUNK_FRAMES  512
#define AUDIO_TASK_STACK    4096
#define AUDIO_TASK_PRIO     6
#define AUDIO_AMP_ON_MS     10   /* cho ampli on dinh sau khi bat SD_MODE */
#define AUDIO_TAIL_MS       30   /* phat im lang cuoi cau truoc khi tat ampli, tranh "bup" */

/* Muc logic tren GPIO SD_MODE de BAT ampli.
 * Mach nay: GPIO -> R 1k -> transistor NPN (Q6) -> chan SD_MODE cua MAX98357 (keo len 5V qua 680k).
 *   GPIO = 1 -> Q6 dan -> SD_MODE = 0 V  -> ampli TAT
 *   GPIO = 0 -> Q6 ngat -> SD_MODE ~0,64 V -> ampli BAT (che do (L+R)/2)
 * => bat ampli bang muc 0. Neu noi GPIO thang vao SD_MODE (khong qua transistor) thi dat = 1
 * (dinh nghia lai trong app_config.h). */
#ifndef MAX98357_SD_ON_LEVEL
#define MAX98357_SD_ON_LEVEL (0)
#endif

typedef enum
{
    AUDIO_CMD_PLAY = 0,
    AUDIO_CMD_STOP,
    AUDIO_CMD_TONE, /* test loa: clip = so giay */
} audio_cmd_t;

typedef struct
{
    uint8_t cmd;
    uint8_t clip;
} audio_msg_t;

static const char *const k_text[AUDIO_CLIP_COUNT] = {
#define X_TEXT(id, file, text) [id] = text,
    AUDIO_CLIP_LIST(X_TEXT)
#undef X_TEXT
};

static const char *const k_file[AUDIO_CLIP_COUNT] = {
#define X_FILE(id, file, text) [id] = file,
    AUDIO_CLIP_LIST(X_FILE)
#undef X_FILE
};

static QueueHandle_t s_queue = NULL;
static i2s_chan_handle_t s_tx = NULL;
static uint32_t s_rate = 0;
static volatile uint8_t s_volume = 70;
static bool s_fs_ok = false;
static int16_t s_buf[AUDIO_CHUNK_FRAMES * 2]; /* du cho stereo truoc khi tron */

/* ================= Tien ich ================= */

const char *audio_clip_text(audio_clip_t clip)
{
    return (clip < AUDIO_CLIP_COUNT) ? k_text[clip] : "?";
}

const char *audio_clip_file(audio_clip_t clip)
{
    return (clip < AUDIO_CLIP_COUNT) ? k_file[clip] : NULL;
}

static inline bool interrupted(void)
{
    return uxQueueMessagesWaiting(s_queue) > 0;
}

static void amp_enable(bool on)
{
    if (MAX98357_SD_MODE_GPIO >= 0)
        gpio_set_level(MAX98357_SD_MODE_GPIO, on ? MAX98357_SD_ON_LEVEL : !MAX98357_SD_ON_LEVEL);
}

/* He so am luong 0..256, theo binh phuong cho tai nguoi nghe deu hon */
static int32_t gain_q8(void)
{
    uint32_t v = s_volume;
    return (int32_t)(v * v * 256u / 10000u);
}

static esp_err_t set_rate(uint32_t rate)
{
    if (rate == s_rate)
        return ESP_OK;
    i2s_channel_disable(s_tx);
    i2s_std_clk_config_t clk = I2S_STD_CLK_DEFAULT_CONFIG(rate);
    esp_err_t err = i2s_channel_reconfig_std_clock(s_tx, &clk);
    i2s_channel_enable(s_tx);
    if (err == ESP_OK)
        s_rate = rate;
    return err;
}

static void write_samples(int16_t *pcm, size_t frames)
{
    int32_t g = gain_q8();
    for (size_t i = 0; i < frames; i++)
        pcm[i] = (int16_t)(((int32_t)pcm[i] * g) >> 8);
    size_t written = 0;
    i2s_channel_write(s_tx, pcm, frames * sizeof(int16_t), &written, 1000);
}

static void write_silence(uint32_t ms)
{
    size_t frames = (size_t)s_rate * ms / 1000;
    memset(s_buf, 0, sizeof(s_buf));
    while (frames)
    {
        size_t n = frames > AUDIO_CHUNK_FRAMES ? AUDIO_CHUNK_FRAMES : frames;
        size_t written = 0;
        i2s_channel_write(s_tx, s_buf, n * sizeof(int16_t), &written, 1000);
        frames -= n;
    }
}

/* ================= Tieng "ding" du phong ================= */

static void play_tone(float freq, uint32_t ms)
{
    const uint32_t rate = s_rate;
    size_t total = (size_t)rate * ms / 1000;
    size_t fade = rate / 200; /* 5 ms len/xuong de khong bi "tach" */
    size_t done = 0;
    while (done < total && !interrupted())
    {
        size_t n = total - done > AUDIO_CHUNK_FRAMES ? AUDIO_CHUNK_FRAMES : total - done;
        for (size_t i = 0; i < n; i++)
        {
            size_t k = done + i;
            float env = 1.0f;
            if (k < fade)
                env = (float)k / fade;
            else if (total - k < fade)
                env = (float)(total - k) / fade;
            s_buf[i] = (int16_t)(12000.0f * env * sinf(2.0f * (float)M_PI * freq * (float)k / (float)rate));
        }
        write_samples(s_buf, n);
        done += n;
    }
}

static void play_ding(void)
{
    set_rate(16000);
    play_tone(880.0f, 120);
    play_tone(1320.0f, 180);
}

/* Test loa: 1 kHz, bien do lon, KHONG kiem tra chen ngang, KHONG phu thuoc am luong */
static void play_test_tone(uint32_t seconds)
{
    set_rate(16000);
    ESP_LOGW(TAG, "===== TEST LOA: phat 1 kHz trong %lu giay =====", (unsigned long)seconds);
    ESP_LOGW(TAG, "Do trong luc dang keu (que den vao GND):");
    ESP_LOGW(TAG, "  GPIO%d (SD_MODE, truoc R56)   : ~%s", MAX98357_SD_MODE_GPIO,
             MAX98357_SD_ON_LEVEL ? "3.3 V" : "0 V");
    ESP_LOGW(TAG, "  chan 4 MAX98357 (SD_MODE)     : ~0.6 V (0 V = ampli dang bi TAT)");
    ESP_LOGW(TAG, "  GPIO%d (BCLK), GPIO%d (LRCLK)  : ~1.6 V moi chan", MAX98357_BCLK_GPIO, MAX98357_LRCLK_GPIO);
    ESP_LOGW(TAG, "  GPIO%d (DIN)                  : dao dong quanh 1-2 V", MAX98357_DIN_GPIO);
    ESP_LOGW(TAG, "  OUTP, OUTN (chan 9, 10)       : ~2.5 V moi chan (0 V = ampli khong chay)");

    amp_enable(true);
    vTaskDelay(pdMS_TO_TICKS(AUDIO_AMP_ON_MS));
    const uint32_t rate = s_rate;
    size_t total = (size_t)rate * seconds, done = 0;
    while (done < total)
    {
        size_t n = total - done > AUDIO_CHUNK_FRAMES ? AUDIO_CHUNK_FRAMES : total - done;
        for (size_t i = 0; i < n; i++)
            s_buf[i] = (int16_t)(20000.0f * sinf(2.0f * (float)M_PI * 1000.0f * (float)(done + i) / (float)rate));
        size_t written = 0;
        esp_err_t err = i2s_channel_write(s_tx, s_buf, n * sizeof(int16_t), &written, 1000);
        if (err != ESP_OK || written != n * sizeof(int16_t))
        {
            ESP_LOGE(TAG, "i2s_channel_write loi: %s (ghi %u/%u byte)", esp_err_to_name(err),
                     (unsigned)written, (unsigned)(n * sizeof(int16_t)));
            break;
        }
        done += n;
    }
    write_silence(AUDIO_TAIL_MS);
    amp_enable(false);
    ESP_LOGW(TAG, "===== TEST LOA: xong (da ghi %u mau ra I2S) =====", (unsigned)done);
}

/* ================= Doc WAV ================= */

typedef struct
{
    uint16_t channels;
    uint32_t rate;
    uint32_t data_bytes;
} wav_info_t;

static bool wav_open(FILE *f, wav_info_t *wi)
{
    uint8_t hdr[12];
    if (fread(hdr, 1, 12, f) != 12 || memcmp(hdr, "RIFF", 4) || memcmp(hdr + 8, "WAVE", 4))
        return false;

    bool have_fmt = false;
    uint16_t fmt = 0, bits = 0;
    for (;;)
    {
        uint8_t ch[8];
        if (fread(ch, 1, 8, f) != 8)
            return false;
        uint32_t size = ch[4] | (ch[5] << 8) | (ch[6] << 16) | ((uint32_t)ch[7] << 24);

        if (!memcmp(ch, "fmt ", 4))
        {
            uint8_t b[16];
            if (size < 16 || fread(b, 1, 16, f) != 16)
                return false;
            fmt = b[0] | (b[1] << 8);
            wi->channels = b[2] | (b[3] << 8);
            wi->rate = b[4] | (b[5] << 8) | (b[6] << 16) | ((uint32_t)b[7] << 24);
            bits = b[14] | (b[15] << 8);
            have_fmt = true;
            if (fseek(f, (long)(size - 16 + (size & 1)), SEEK_CUR))
                return false;
        }
        else if (!memcmp(ch, "data", 4))
        {
            wi->data_bytes = size;
            break;
        }
        else if (fseek(f, (long)(size + (size & 1)), SEEK_CUR))
        {
            return false;
        }
    }

    if (!have_fmt || fmt != 1 || bits != 16 || wi->channels < 1 || wi->channels > 2 ||
        wi->rate < 8000 || wi->rate > 48000)
    {
        ESP_LOGW(TAG, "WAV khong ho tro (format=%u, %u-bit, %u kenh, %lu Hz) -- can PCM 16-bit",
                 fmt, bits, wi->channels, (unsigned long)wi->rate);
        return false;
    }
    return true;
}

static bool play_file(audio_clip_t clip)
{
    if (!s_fs_ok)
        return false;

    char path[48];
    snprintf(path, sizeof(path), AUDIO_BASE_PATH "/%s", k_file[clip]);
    FILE *f = fopen(path, "rb");
    if (!f)
    {
        ESP_LOGW(TAG, "thieu file %s", path);
        return false;
    }

    wav_info_t wi = {0};
    bool ok = wav_open(f, &wi);
    if (ok && set_rate(wi.rate) != ESP_OK)
        ok = false;

    if (ok)
    {
        uint32_t left = wi.data_bytes;
        size_t frame_bytes = wi.channels * sizeof(int16_t);
        while (left >= frame_bytes && !interrupted())
        {
            size_t want = AUDIO_CHUNK_FRAMES * frame_bytes;
            if (want > left)
                want = left - (left % frame_bytes);
            size_t got = fread(s_buf, 1, want, f);
            size_t frames = got / frame_bytes;
            if (frames == 0)
                break;
            if (wi.channels == 2)
                for (size_t i = 0; i < frames; i++)
                    s_buf[i] = (int16_t)(((int32_t)s_buf[2 * i] + s_buf[2 * i + 1]) / 2);
            write_samples(s_buf, frames);
            left -= got;
        }
    }
    fclose(f);
    return ok;
}

/* ================= Task ================= */

static void audio_task(void *arg)
{
    audio_msg_t m;
    for (;;)
    {
        if (xQueueReceive(s_queue, &m, portMAX_DELAY) != pdTRUE)
            continue;
        if (m.cmd == AUDIO_CMD_TONE)
        {
            play_test_tone(m.clip);
            continue;
        }
        if (m.cmd != AUDIO_CMD_PLAY || m.clip >= AUDIO_CLIP_COUNT || s_volume == 0)
        {
            amp_enable(false); /* lenh dung (co the dang chen ngang 1 cau) -> tat ampli */
            continue;
        }

        ESP_LOGI(TAG, "phat: \"%s\"", k_text[m.clip]);
        amp_enable(true);
        vTaskDelay(pdMS_TO_TICKS(AUDIO_AMP_ON_MS));

        if (!play_file((audio_clip_t)m.clip))
            play_ding();

        if (!interrupted())
        {
            write_silence(AUDIO_TAIL_MS);
            amp_enable(false);
        }
        /* bi chen ngang: giu ampli bat, vong lap lay ngay yeu cau moi */
    }
}

/* ================= API ================= */

static void mount_fs(void)
{
    esp_vfs_littlefs_conf_t conf = {
        .base_path = AUDIO_BASE_PATH,
        .partition_label = AUDIO_PARTITION,
        .format_if_mount_failed = false, /* khong tu xoa: du lieu do may tinh dong goi san */
        .read_only = true,
    };
    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "khong mount duoc phan vung '%s' (%s) -- se phat tieng ding thay cau noi",
                 AUDIO_PARTITION, esp_err_to_name(err));
        return;
    }
    s_fs_ok = true;

    size_t total = 0, used = 0;
    esp_littlefs_info(AUDIO_PARTITION, &total, &used);
    ESP_LOGI(TAG, "phan vung am thanh: dung %u/%u KB", (unsigned)(used / 1024), (unsigned)(total / 1024));

    int missing = 0;
    for (int i = 0; i < AUDIO_CLIP_COUNT; i++)
    {
        char path[48];
        snprintf(path, sizeof(path), AUDIO_BASE_PATH "/%s", k_file[i]);
        FILE *f = fopen(path, "rb");
        if (f)
            fclose(f);
        else
            missing++;
    }
    if (missing)
        ESP_LOGW(TAG, "thieu %d/%d file am thanh trong assets/audio", missing, AUDIO_CLIP_COUNT);
}

esp_err_t audio_init(void)
{
    if (s_queue)
        return ESP_OK;

    /* Tat ampli truoc tien */
    if (MAX98357_SD_MODE_GPIO >= 0)
    {
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << MAX98357_SD_MODE_GPIO,
            .mode = GPIO_MODE_OUTPUT,
        };
        gpio_config(&io);
        amp_enable(false);
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true; /* het du lieu thi tu gui 0 -> im lang, khong lap lai tieng cu */
    esp_err_t err = i2s_new_channel(&chan_cfg, &s_tx, NULL);
    if (err != ESP_OK)
        return err;

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = MAX98357_BCLK_GPIO,
            .ws = MAX98357_LRCLK_GPIO,
            .dout = MAX98357_DIN_GPIO,
            .din = I2S_GPIO_UNUSED,
        },
    };
    /* Mono gui ra CA 2 kenh trai/phai: MAX98357A lay kenh nao (tuy chan SD_MODE) cung co tieng */
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;

    err = i2s_channel_init_std_mode(s_tx, &std_cfg);
    if (err != ESP_OK)
        return err;
    err = i2s_channel_enable(s_tx);
    if (err != ESP_OK)
        return err;
    s_rate = AUDIO_SAMPLE_RATE;

    mount_fs();

    s_queue = xQueueCreate(1, sizeof(audio_msg_t));
    if (!s_queue)
        return ESP_ERR_NO_MEM;
    if (xTaskCreatePinnedToCore(audio_task, "audio", AUDIO_TASK_STACK, NULL, AUDIO_TASK_PRIO, NULL, 1) != pdPASS)
        return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "audio OK (I2S %lu Hz, BCLK=%d WS=%d DIN=%d SD=%d)", (unsigned long)s_rate,
             MAX98357_BCLK_GPIO, MAX98357_LRCLK_GPIO, MAX98357_DIN_GPIO, MAX98357_SD_MODE_GPIO);
    return ESP_OK;
}

void audio_play(audio_clip_t clip)
{
    if (!s_queue || clip >= AUDIO_CLIP_COUNT)
        return;
    audio_msg_t m = {.cmd = AUDIO_CMD_PLAY, .clip = (uint8_t)clip};
    xQueueOverwrite(s_queue, &m); /* luon giu yeu cau moi nhat */
}

void audio_stop(void)
{
    if (!s_queue)
        return;
    audio_msg_t m = {.cmd = AUDIO_CMD_STOP};
    xQueueOverwrite(s_queue, &m);
}

void audio_set_volume(uint8_t percent)
{
    s_volume = percent > 100 ? 100 : percent;
}

void audio_test_tone(uint32_t seconds)
{
    if (!s_queue || seconds == 0)
        return;
    if (seconds > 120)
        seconds = 120;
    audio_msg_t m = {.cmd = AUDIO_CMD_TONE, .clip = (uint8_t)seconds};
    xQueueOverwrite(s_queue, &m);
}