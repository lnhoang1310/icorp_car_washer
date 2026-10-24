#include "mqtt_tx_queue.h"
#include "fram.h"
#include "fram_map.h"
#include "crc.h"
#include "esp_log.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "txq";

#define TXQ_MAGIC 0x51585431u /* "TXQ1" */

typedef struct
{
    uint32_t magic;
    uint32_t seq;  /* thu tu them vao, 0 = slot trong */
    uint16_t len;  /* do dai JSON (khong tinh '\0') */
    uint16_t rsv;
    uint32_t crc;  /* CRC32 cua magic..rsv + du lieu */
} txq_hdr_t;

_Static_assert(sizeof(txq_hdr_t) == 16, "txq header");
_Static_assert(TXQ_SLOTS * TXQ_SLOT_SIZE <= FRAM_MAP_OUTBOX_SIZE, "txq vuot vung outbox FRAM");

static uint32_t s_seq[TXQ_SLOTS];  /* ban sao trong RAM: 0 = trong */
static char *s_ram[TXQ_SLOTS];     /* chi dung khi FRAM loi */
static uint32_t s_next_seq = 1;
static bool s_use_fram = false;

static uint16_t slot_addr(int i) { return (uint16_t)(FRAM_MAP_OUTBOX_BASE + i * TXQ_SLOT_SIZE); }

static uint32_t slot_crc(const txq_hdr_t *h, const char *data)
{
    /* CRC noi tiep: header (tru crc) + du lieu */
    uint8_t tmp[TXQ_SLOT_SIZE];
    memcpy(tmp, h, offsetof(txq_hdr_t, crc));
    memcpy(tmp + offsetof(txq_hdr_t, crc), data, h->len);
    return crc_compute(tmp, offsetof(txq_hdr_t, crc) + h->len);
}

static bool read_slot(int i, txq_hdr_t *h, char *data)
{
    if (fram_read(slot_addr(i), h, sizeof(*h)) != ESP_OK)
        return false;
    if (h->magic != TXQ_MAGIC || h->seq == 0 || h->len == 0 || h->len > TXQ_MAX_LEN)
        return false;
    if (fram_read(slot_addr(i) + sizeof(*h), data, h->len) != ESP_OK)
        return false;
    return slot_crc(h, data) == h->crc;
}

void txq_init(void)
{
    memset(s_seq, 0, sizeof(s_seq));
    s_use_fram = fram_is_ready();
    if (!s_use_fram)
    {
        ESP_LOGW(TAG, "FRAM khong san sang -> hang doi chi trong RAM (mat khi mat dien)");
        return;
    }

    static char data[TXQ_SLOT_SIZE];
    int n = 0;
    for (int i = 0; i < TXQ_SLOTS; i++)
    {
        txq_hdr_t h;
        if (read_slot(i, &h, data))
        {
            s_seq[i] = h.seq;
            if (h.seq >= s_next_seq)
                s_next_seq = h.seq + 1;
            n++;
        }
    }
    if (n)
        ESP_LOGW(TAG, "con %d ban tin chua gui duoc tu lan chay truoc", n);
}

int txq_push(const char *json)
{
    size_t len = strlen(json);
    if (len == 0 || len > TXQ_MAX_LEN)
    {
        ESP_LOGE(TAG, "ban tin dai %u byte, khong luu duoc", (unsigned)len);
        return -1;
    }

    int slot = -1;
    for (int i = 0; i < TXQ_SLOTS && slot < 0; i++)
        if (!s_seq[i])
            slot = i;
    if (slot < 0)
    {
        ESP_LOGE(TAG, "hang doi day (%d ban tin chua duoc server xac nhan)", TXQ_SLOTS);
        return -1;
    }

    uint32_t seq = s_next_seq++;
    if (s_use_fram)
    {
        /* fram_write toi da 256 byte/lan: ghi du lieu truoc, header (co magic + CRC) ghi SAU CUNG
         * -> mat dien giua chung thi slot van bi coi la trong, khong doc ra ban tin do dang */
        txq_hdr_t h = {.magic = TXQ_MAGIC, .seq = seq, .len = (uint16_t)len};
        h.crc = slot_crc(&h, json);
        esp_err_t err = ESP_OK;
        for (size_t off = 0; off < len && err == ESP_OK; off += 256)
        {
            size_t n = len - off > 256 ? 256 : len - off;
            err = fram_write(slot_addr(slot) + sizeof(h) + off, json + off, n);
        }
        if (err == ESP_OK)
            err = fram_write(slot_addr(slot), &h, sizeof(h));
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "ghi FRAM loi");
            return -1;
        }
    }
    else
    {
        s_ram[slot] = strdup(json);
        if (!s_ram[slot])
            return -1;
    }
    s_seq[slot] = seq;
    return slot;
}

bool txq_read(int slot, char *buf, size_t buf_len)
{
    if (slot < 0 || slot >= TXQ_SLOTS || !s_seq[slot] || buf_len < 2)
        return false;
    if (!s_use_fram)
    {
        strlcpy(buf, s_ram[slot], buf_len);
        return true;
    }
    txq_hdr_t h;
    static char data[TXQ_SLOT_SIZE];
    if (!read_slot(slot, &h, data) || h.len >= buf_len)
    {
        ESP_LOGE(TAG, "slot %d hong -> bo", slot);
        txq_remove(slot);
        return false;
    }
    memcpy(buf, data, h.len);
    buf[h.len] = 0;
    return true;
}

void txq_remove(int slot)
{
    if (slot < 0 || slot >= TXQ_SLOTS || !s_seq[slot])
        return;
    s_seq[slot] = 0;
    if (s_use_fram)
    {
        txq_hdr_t z;
        memset(&z, 0, sizeof(z));
        fram_write(slot_addr(slot), &z, sizeof(z));
    }
    else
    {
        free(s_ram[slot]);
        s_ram[slot] = NULL;
    }
}

int txq_list(int out[TXQ_SLOTS])
{
    int n = 0;
    for (int i = 0; i < TXQ_SLOTS; i++)
        if (s_seq[i])
            out[n++] = i;
    /* sap xep theo seq tang dan (n nho -> chen) */
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && s_seq[out[j - 1]] > s_seq[out[j]]; j--)
        {
            int t = out[j];
            out[j] = out[j - 1];
            out[j - 1] = t;
        }
    return n;
}

int txq_count(void)
{
    int n = 0;
    for (int i = 0; i < TXQ_SLOTS; i++)
        n += s_seq[i] != 0;
    return n;
}