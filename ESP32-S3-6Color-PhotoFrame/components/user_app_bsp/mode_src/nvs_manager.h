#pragma once

#include "esp_err.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void nvs_manager_init(void);

esp_err_t nvs_manager_set_str(const char *ns, const char *key, const char *val);
esp_err_t nvs_manager_get_str(const char *ns, const char *key, char *out, size_t *len);
esp_err_t nvs_manager_erase_key(const char *ns, const char *key);

#ifdef __cplusplus
}
#endif
