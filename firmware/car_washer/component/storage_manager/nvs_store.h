
#ifndef __NVS_STORE_H__
#define __NVS_STORE_H__

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

esp_err_t nvs_store_set_u8(const char *ns, const char *key, uint8_t value);
esp_err_t nvs_store_get_u8(const char *ns, const char *key, uint8_t *out_value);

esp_err_t nvs_store_set_u32(const char *ns, const char *key, uint32_t value);
esp_err_t nvs_store_get_u32(const char *ns, const char *key, uint32_t *out_value);

esp_err_t nvs_store_set_i32(const char *ns, const char *key, int32_t value);
esp_err_t nvs_store_get_i32(const char *ns, const char *key, int32_t *out_value);

esp_err_t nvs_store_set_str(const char *ns, const char *key, const char *value);
esp_err_t nvs_store_get_str(const char *ns, const char *key, char *out_buf, size_t buf_len);

esp_err_t nvs_store_set_blob(const char *ns, const char *key, const void *value, size_t size);
esp_err_t nvs_store_get_blob(const char *ns, const char *key, void *out_value, size_t *inout_size);

esp_err_t nvs_store_erase_key(const char *ns, const char *key);
esp_err_t nvs_store_erase_all(const char *ns);

#endif
