#include "nvs_store.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include <string.h>
#include <stdbool.h>

static const char *TAG = "nvs_store";

typedef esp_err_t (*nvs_op_fn)(nvs_handle_t handle, const char *key, void *ctx);

static esp_err_t with_handle(const char *ns, nvs_open_mode_t mode,
                             const char *key, void *ctx, nvs_op_fn op,
                             bool commit_after)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(ns, mode, &handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "nvs_open(%s) failed: %s", ns, esp_err_to_name(err));
        return err;
    }

    err = op(handle, key, ctx);
    if (err == ESP_OK && commit_after)
    {
        esp_err_t cerr = nvs_commit(handle);
        if (cerr != ESP_OK)
        {
            ESP_LOGE(TAG, "nvs_commit(%s/%s) failed: %s", ns, key, esp_err_to_name(cerr));
            err = cerr;
        }
    }

    nvs_close(handle);
    return err;
}

/* ---------------- u8 ---------------- */
static esp_err_t op_set_u8(nvs_handle_t h, const char *key, void *ctx)
{
    return nvs_set_u8(h, key, *(uint8_t *)ctx);
}
static esp_err_t op_get_u8(nvs_handle_t h, const char *key, void *ctx)
{
    return nvs_get_u8(h, key, (uint8_t *)ctx);
}

esp_err_t nvs_store_set_u8(const char *ns, const char *key, uint8_t value)
{
    return with_handle(ns, NVS_READWRITE, key, &value, op_set_u8, true);
}
esp_err_t nvs_store_get_u8(const char *ns, const char *key, uint8_t *out_value)
{
    if (!out_value)
        return ESP_ERR_INVALID_ARG;
    return with_handle(ns, NVS_READONLY, key, out_value, op_get_u8, false);
}

/* ---------------- u32 ---------------- */
static esp_err_t op_set_u32(nvs_handle_t h, const char *key, void *ctx)
{
    return nvs_set_u32(h, key, *(uint32_t *)ctx);
}
static esp_err_t op_get_u32(nvs_handle_t h, const char *key, void *ctx)
{
    return nvs_get_u32(h, key, (uint32_t *)ctx);
}

esp_err_t nvs_store_set_u32(const char *ns, const char *key, uint32_t value)
{
    return with_handle(ns, NVS_READWRITE, key, &value, op_set_u32, true);
}
esp_err_t nvs_store_get_u32(const char *ns, const char *key, uint32_t *out_value)
{
    if (!out_value)
        return ESP_ERR_INVALID_ARG;
    return with_handle(ns, NVS_READONLY, key, out_value, op_get_u32, false);
}

/* ---------------- i32 ---------------- */
static esp_err_t op_set_i32(nvs_handle_t h, const char *key, void *ctx)
{
    return nvs_set_i32(h, key, *(int32_t *)ctx);
}
static esp_err_t op_get_i32(nvs_handle_t h, const char *key, void *ctx)
{
    return nvs_get_i32(h, key, (int32_t *)ctx);
}

esp_err_t nvs_store_set_i32(const char *ns, const char *key, int32_t value)
{
    return with_handle(ns, NVS_READWRITE, key, &value, op_set_i32, true);
}
esp_err_t nvs_store_get_i32(const char *ns, const char *key, int32_t *out_value)
{
    if (!out_value)
        return ESP_ERR_INVALID_ARG;
    return with_handle(ns, NVS_READONLY, key, out_value, op_get_i32, false);
}

/* ---------------- string ---------------- */
static esp_err_t op_set_str(nvs_handle_t h, const char *key, void *ctx)
{
    return nvs_set_str(h, key, (const char *)ctx);
}

esp_err_t nvs_store_set_str(const char *ns, const char *key, const char *value)
{
    if (!value)
        return ESP_ERR_INVALID_ARG;
    return with_handle(ns, NVS_READWRITE, key, (void *)value, op_set_str, true);
}

esp_err_t nvs_store_get_str(const char *ns, const char *key, char *out_buf, size_t buf_len)
{
    if (!out_buf || buf_len == 0)
        return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(ns, NVS_READONLY, &handle);
    if (err != ESP_OK)
        return err;

    size_t required = buf_len;
    err = nvs_get_str(handle, key, out_buf, &required);
    nvs_close(handle);

    if (err != ESP_OK)
        out_buf[0] = '\0';
    return err;
}

/* ---------------- blob ---------------- */
typedef struct
{
    const void *data;
    size_t size;
} blob_set_ctx_t;

static esp_err_t op_set_blob(nvs_handle_t h, const char *key, void *ctx)
{
    blob_set_ctx_t *c = (blob_set_ctx_t *)ctx;
    return nvs_set_blob(h, key, c->data, c->size);
}

esp_err_t nvs_store_set_blob(const char *ns, const char *key, const void *value, size_t size)
{
    if (!value)
        return ESP_ERR_INVALID_ARG;
    blob_set_ctx_t ctx = {.data = value, .size = size};
    return with_handle(ns, NVS_READWRITE, key, &ctx, op_set_blob, true);
}

esp_err_t nvs_store_get_blob(const char *ns, const char *key, void *out_value, size_t *inout_size)
{
    if (!out_value || !inout_size)
        return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(ns, NVS_READONLY, &handle);
    if (err != ESP_OK)
        return err;

    err = nvs_get_blob(handle, key, out_value, inout_size);
    nvs_close(handle);
    return err;
}

/* ---------------- erase ---------------- */
static esp_err_t op_erase_key(nvs_handle_t h, const char *key, void *ctx)
{
    (void)ctx;
    esp_err_t err = nvs_erase_key(h, key);
    return (err == ESP_ERR_NVS_NOT_FOUND) ? ESP_OK : err;
}

esp_err_t nvs_store_erase_key(const char *ns, const char *key)
{
    return with_handle(ns, NVS_READWRITE, key, NULL, op_erase_key, true);
}

esp_err_t nvs_store_erase_all(const char *ns)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &handle);
    if (err != ESP_OK)
        return err;

    err = nvs_erase_all(handle);
    if (err == ESP_OK)
        err = nvs_commit(handle);

    nvs_close(handle);
    return err;
}
