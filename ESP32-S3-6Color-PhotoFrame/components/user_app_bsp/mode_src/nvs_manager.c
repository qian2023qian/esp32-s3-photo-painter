#include "nvs_manager.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "nvs";

void nvs_manager_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS 初始化完成");
}

esp_err_t nvs_manager_set_str(const char *ns, const char *key, const char *val)
{
    nvs_handle_t h;
    esp_err_t ret = nvs_open(ns, NVS_READWRITE, &h);
    if (ret != ESP_OK) return ret;
    ret = nvs_set_str(h, key, val);
    if (ret == ESP_OK) nvs_commit(h);
    nvs_close(h);
    return ret;
}

esp_err_t nvs_manager_get_str(const char *ns, const char *key, char *out, size_t *len)
{
    nvs_handle_t h;
    esp_err_t ret = nvs_open(ns, NVS_READONLY, &h);
    if (ret != ESP_OK) return ret;
    ret = nvs_get_str(h, key, out, len);
    nvs_close(h);
    return ret;
}

esp_err_t nvs_manager_erase_key(const char *ns, const char *key)
{
    nvs_handle_t h;
    esp_err_t ret = nvs_open(ns, NVS_READWRITE, &h);
    if (ret != ESP_OK) return ret;
    ret = nvs_erase_key(h, key);
    if (ret == ESP_OK) nvs_commit(h);
    nvs_close(h);
    return ret;
}
