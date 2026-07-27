#include "wifi_manager.h"
#include "nvs_manager.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "wifi";
static bool connected = false;
static bool ap_active = false;
static bool sta_configured = false;
static int sta_retry_count = 0;
static bool in_scan = false;

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_START) {
            if (sta_configured) esp_wifi_connect();
        } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
            if (in_scan) return;
            if (!sta_configured) return;
            connected = false;
            sta_retry_count++;
            if (sta_retry_count >= 3) {
                sta_retry_count = 0;
                if (!ap_active) {
                    ESP_LOGW(TAG, "WiFi 连接失败 3 次，启动 AP 配网");
                    wifi_manager_start_ap();
                    return;
                }
                ESP_LOGW(TAG, "WiFi 断开，30秒后重试...");
                vTaskDelay(pdMS_TO_TICKS(30000));
                esp_wifi_connect();
            } else {
                ESP_LOGW(TAG, "WiFi 断开，重连中 (%d/3)...", sta_retry_count);
                vTaskDelay(pdMS_TO_TICKS(5000));
                esp_wifi_connect();
            }
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "已获取 IP: " IPSTR, IP2STR(&ev->ip_info.ip));
        connected = true;
        sta_retry_count = 0;
        wifi_manager_stop_ap();
    }
}

void wifi_manager_init(void)
{
    esp_netif_init();
    esp_event_loop_create_default(); // safe if already created by main.cc

    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                     &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                     &wifi_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));
    ESP_LOGI(TAG, "WiFi 初始化完成");
}

bool wifi_manager_connect(void)
{
    char ssid[33] = {0}, pass[65] = {0};
    size_t slen = sizeof(ssid), plen = sizeof(pass);
    if (!wifi_manager_get_credentials(ssid, slen, pass, plen)) {
        ESP_LOGW(TAG, "无已保存的 WiFi 凭据");
        return false;
    }
    if (ssid[0] == '\0') {
        ESP_LOGW(TAG, "WiFi SSID 为空");
        return false;
    }

    wifi_config_t cfg = {0};
    strncpy((char *)cfg.sta.ssid, ssid, 32);
    strncpy((char *)cfg.sta.password, pass, 64);

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    esp_wifi_start();
    sta_configured = true;

    ESP_LOGI(TAG, "正在连接 %s...", ssid);
    return true;
}

static bool apply_credentials(void)
{
    char ssid[33] = {0}, pass[65] = {0};
    size_t slen = sizeof(ssid), plen = sizeof(pass);
    if (!wifi_manager_get_credentials(ssid, slen, pass, plen)) return false;
    if (ssid[0] == '\0') return false;

    wifi_config_t cfg = {0};
    strncpy((char *)cfg.sta.ssid, ssid, 32);
    strncpy((char *)cfg.sta.password, pass, 64);
    esp_wifi_set_config(WIFI_IF_STA, &cfg);

    if (ap_active) {
        sta_configured = true;
        esp_wifi_connect();
    } else {
        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_wifi_start();
        sta_configured = true;
    }

    ESP_LOGI(TAG, "正在连接 %s...", ssid);
    return true;
}

bool wifi_manager_is_connected(void) { return connected; }

void wifi_manager_start_ap(void)
{
    wifi_config_t ap_cfg = {
        .ap = {
            .ssid = WIFI_AP_SSID,
            .password = WIFI_AP_PASS,
            .ssid_len = strlen(WIFI_AP_SSID),
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        }
    };

    if (ap_active) {
        esp_wifi_set_mode(WIFI_MODE_APSTA);
        return;
    }

    esp_wifi_set_mode(WIFI_MODE_APSTA);

    if (!sta_configured) {
        wifi_config_t sta_cfg = {0};
        esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
    }

    esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
    esp_wifi_start();
    ap_active = true;
    ESP_LOGI(TAG, "AP 已启动: %s (密码: %s)", WIFI_AP_SSID, WIFI_AP_PASS);
}

void wifi_manager_stop_ap(void)
{
    if (!ap_active) return;
    if (sta_configured) {
        esp_wifi_set_mode(WIFI_MODE_STA);
    } else {
        esp_wifi_set_mode(WIFI_MODE_NULL);
        esp_wifi_stop();
    }
    ap_active = false;
    ESP_LOGI(TAG, "AP 已关闭");
}

void wifi_manager_disconnect(void)
{
    esp_wifi_disconnect();
    connected = false;
}

void wifi_manager_scan(char (*ssids)[33], int max, int *count)
{
    *count = 0;
    in_scan = true;
    wifi_scan_config_t scan_cfg = {
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };
    esp_err_t err = esp_wifi_scan_start(&scan_cfg, true);
    in_scan = false;
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WiFi 扫描失败: %s (0x%X)", esp_err_to_name(err), err);
        return;
    }

    uint16_t num = 0;
    esp_wifi_scan_get_ap_num(&num);
    wifi_ap_record_t *aps = calloc(num, sizeof(wifi_ap_record_t));
    if (!aps) return;
    esp_wifi_scan_get_ap_records(&num, aps);

    *count = (num < max) ? num : max;
    for (int i = 0; i < *count; i++) {
        strncpy(ssids[i], (char *)aps[i].ssid, 32);
        ssids[i][32] = '\0';
    }
    free(aps);
}

bool wifi_manager_set_credentials(const char *ssid, const char *pass)
{
    nvs_manager_set_str(NVS_NS_WIFI, "ssid", ssid);
    nvs_manager_set_str(NVS_NS_WIFI, "pass", pass);
    return true;
}

bool wifi_manager_get_credentials(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    esp_err_t e1 = nvs_manager_get_str(NVS_NS_WIFI, "ssid", ssid, &ssid_len);
    esp_err_t e2 = nvs_manager_get_str(NVS_NS_WIFI, "pass", pass, &pass_len);
    return (e1 == ESP_OK && e2 == ESP_OK);
}

void wifi_manager_clear_credentials(void)
{
    nvs_manager_erase_key(NVS_NS_WIFI, "ssid");
    nvs_manager_erase_key(NVS_NS_WIFI, "pass");

    wifi_config_t cfg = {0};
    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    sta_configured = false;
}

bool wifi_manager_apply_credentials(void)
{
    return apply_credentials();
}

/* ── 重置 WiFi: 清除凭据 → 断开 STA → 启动 AP ── */
void wifi_manager_reset(void)
{
    ESP_LOGI(TAG, "重置 WiFi — 清除凭据并启动 AP");
    wifi_manager_clear_credentials();
    esp_wifi_disconnect();
    connected = false;
    sta_retry_count = 0;
    esp_wifi_set_mode(WIFI_MODE_NULL);
    esp_wifi_stop();
    vTaskDelay(pdMS_TO_TICKS(500));
    wifi_manager_start_ap();
}

/* ── BOOT 按钮 (GPIO0) 长按 5 秒检测 ── */
#define BOOT_BTN_GPIO   GPIO_NUM_0
#define LONG_PRESS_MS   5000

static void boot_button_task(void *arg)
{
    gpio_set_direction(BOOT_BTN_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BOOT_BTN_GPIO, GPIO_PULLUP_ONLY);

    uint32_t press_ms = 0;
    bool prev = true;
    bool triggered = false;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(100));
        bool now = gpio_get_level(BOOT_BTN_GPIO);

        if (!now && prev) {
            /* 刚按下 */
            press_ms = 0;
            triggered = false;
        } else if (!now && !prev) {
            /* 持续按住 */
            press_ms += 100;
            if (press_ms >= LONG_PRESS_MS && !triggered) {
                triggered = true;
                ESP_LOGI(TAG, "BOOT 长按 %d ms — 重置 WiFi", press_ms);
                wifi_manager_reset();
            }
        }
        prev = now;
    }
}

void wifi_manager_button_init(void)
{
    xTaskCreate(boot_button_task, "boot_btn", 2048, NULL, 1, NULL);
}
