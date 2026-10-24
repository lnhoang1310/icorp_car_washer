#include "session_store.h"
#include "fram.h"
#include "crc.h"
#include "esp_log.h"
#include <string.h>
#include <stddef.h>

static const char *TAG = "session_store";

typedef struct
{
    uint32_t seq;
    ws_session_t s;
    uint32_t crc; /* CRC32 cua seq + s */
} ss_record_t;

_Static_assert(sizeof(ss_record_t) <= SESSION_STORE_SLOT_SIZE, "ss_record_t vuot qua kich thuoc slot");

static uint32_t s_last_seq = 0;

static uint16_t slot_addr(int slot)
{
    return (uint16_t)(SESSION_STORE_FRAM_BASE + slot * SESSION_STORE_SLOT_SIZE);
}

static uint32_t record_crc(const ss_record_t *r)
{
    return crc_compute((const uint8_t *)r, offsetof(ss_record_t, crc));
}

static bool read_slot(int slot, ss_record_t *r)
{
    if (fram_read(slot_addr(slot), r, sizeof(*r)) != ESP_OK)
        return false;
    if (record_crc(r) != r->crc)
        return false;
    return ws_validate(&r->s);
}

esp_err_t session_store_save(const ws_session_t *s)
{
    if (!fram_is_ready())
        return ESP_ERR_INVALID_STATE;

    ss_record_t r;
    memset(&r, 0, sizeof(r));
    r.seq = s_last_seq + 1;
    memcpy(&r.s, s, sizeof(*s));
    r.crc = record_crc(&r);

    esp_err_t err = fram_write(slot_addr(r.seq & 1u), &r, sizeof(r));
    if (err == ESP_OK)
        s_last_seq = r.seq;
    else
        ESP_LOGW(TAG, "ghi FRAM that bai: %s", esp_err_to_name(err));
    return err;
}

bool session_store_load(ws_session_t *out)
{
    if (!fram_is_ready())
        return false;

    ss_record_t a, b;
    bool ok_a = read_slot(0, &a);
    bool ok_b = read_slot(1, &b);

    const ss_record_t *best = NULL;
    if (ok_a && ok_b)
        best = ((int32_t)(a.seq - b.seq) > 0) ? &a : &b; /* chiu duoc tran so seq */
    else if (ok_a)
        best = &a;
    else if (ok_b)
        best = &b;

    if (!best)
        return false;

    s_last_seq = best->seq;
    memcpy(out, &best->s, sizeof(*out));
    ESP_LOGI(TAG, "khoi phuc phien order=%s state=%u con %lu ms (seq=%lu)",
             out->order_id, out->state, (unsigned long)out->session_left_ms, (unsigned long)best->seq);
    return true;
}

esp_err_t session_store_clear(void)
{
    if (!fram_is_ready())
        return ESP_ERR_INVALID_STATE;

    ss_record_t z;
    memset(&z, 0, sizeof(z)); /* crc = 0 khong khop -> slot bi coi la trong */
    esp_err_t e1 = fram_write(slot_addr(0), &z, sizeof(z));
    esp_err_t e2 = fram_write(slot_addr(1), &z, sizeof(z));
    return (e1 != ESP_OK) ? e1 : e2;
}