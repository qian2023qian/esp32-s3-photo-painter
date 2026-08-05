#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <cJSON.h>
#include <esp_log.h>
#include <esp_mac.h>
#include "mqtt_client.h"
#include "nvs_manager.h"
#include "wifi_manager.h"
#include "power_bsp.h"

#include "mqtt_ha.h"

static const char *TAG = "mqtt_ha";

// Extern from PhotoFrame_mode.cpp
extern uint32_t photo_img_count;
extern uint32_t photo_img_index;
extern int      photo_interval;
extern bool     photo_running;
extern "C" bool photo_get_sensor(float *temp, float *rh);
extern "C" int  photo_switch_to(uint32_t index);
extern "C" void photo_set_interval(int minutes);
extern "C" void photo_set_running(bool run);

// ---- config state ----
static bool         s_enabled  = false;
static char         s_host[128] = "";
static int          s_port      = 1883;
static char         s_user[64]  = "";
static char         s_pass[64]  = "";

static char         s_dev_id[16]    = "";
static char         s_base_topic[64]  = "";
static char         s_state_topic[80] = "";
static char         s_avail_topic[80] = "";

static esp_mqtt_client_handle_t s_client = NULL;
static TaskHandle_t             s_task   = NULL;
static bool                     s_connected = false;

// ---- NVS config ----
static void mqtt_load_config(void)
{
    s_enabled = false;
    s_host[0] = s_user[0] = s_pass[0] = '\0';
    s_port = 1883;

    char buf[512];
    size_t len = sizeof(buf);
    if (nvs_manager_get_str("photoframe", "mqtt", buf, &len) != ESP_OK || len == 0) {
        return;
    }
    cJSON *json = cJSON_Parse(buf);
    if (!json) return;
    cJSON *item = cJSON_GetObjectItem(json, "enabled");
    if (item) s_enabled = cJSON_IsTrue(item);
    item = cJSON_GetObjectItem(json, "host");
    if (item && cJSON_IsString(item)) strncpy(s_host, item->valuestring, sizeof(s_host) - 1);
    item = cJSON_GetObjectItem(json, "port");
    if (item && cJSON_IsNumber(item)) s_port = item->valueint;
    item = cJSON_GetObjectItem(json, "username");
    if (item && cJSON_IsString(item)) strncpy(s_user, item->valuestring, sizeof(s_user) - 1);
    item = cJSON_GetObjectItem(json, "password");
    if (item && cJSON_IsString(item)) strncpy(s_pass, item->valuestring, sizeof(s_pass) - 1);
    cJSON_Delete(json);
}

static void mqtt_compute_base(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_dev_id, sizeof(s_dev_id), "%02x%02x%02x", mac[3], mac[4], mac[5]);
    snprintf(s_base_topic, sizeof(s_base_topic), "photoframe/%s", s_dev_id);
    snprintf(s_state_topic, sizeof(s_state_topic), "%s/state", s_base_topic);
    snprintf(s_avail_topic, sizeof(s_avail_topic), "%s/available", s_base_topic);
}

// ---- state publish ----
static void mqtt_publish_state(void)
{
    if (!s_client || !s_connected) return;

    cJSON *root = cJSON_CreateObject();
    float temp = 0, rh = 0;
    if (photo_get_sensor(&temp, &rh)) {
        cJSON_AddNumberToObject(root, "temperature", temp);
        cJSON_AddNumberToObject(root, "humidity", rh);
    }
    PmicRegisterConfig pmic = Custom_PmicGetBatteryInfo();
    float volt = 0;
    int mv = 0, pct = 0;
    char *p = strrchr(pmic.batteryVoltage, ':');
    if (p) { mv = atoi(p + 1); volt = mv / 1000.0f; }
    p = strrchr(pmic.batteryPercent, ':');
    if (p) pct = atoi(p + 1);
    cJSON_AddNumberToObject(root, "battery_voltage", volt);
    cJSON_AddNumberToObject(root, "battery_percent", pct);
    bool charging = (strstr(pmic.isCharging, "Charging") != NULL)
                 && (strstr(pmic.isCharging, "Not Charging") == NULL);
    cJSON_AddStringToObject(root, "charging", charging ? "charging" : "not_charging");
    const char *cs = strrchr(pmic.chargeStatus, ':');
    cs = cs ? cs + 1 : pmic.chargeStatus;
    while (*cs == ' ') cs++;
    cJSON_AddStringToObject(root, "charge_status", cs);
    cJSON_AddNumberToObject(root, "img_count", photo_img_count);
    cJSON_AddNumberToObject(root, "img_index", photo_img_index);
    cJSON_AddNumberToObject(root, "interval", photo_interval);
    cJSON_AddBoolToObject(root, "running", photo_running);

    char *str = cJSON_PrintUnformatted(root);
    if (str) {
        esp_mqtt_client_publish(s_client, s_state_topic, str, 0, 0, 0);
        free(str);
    }
    cJSON_Delete(root);
}

// ---- HA discovery ----
static void pub_disc(const char *component, const char *entity, const char *name, const char *extra)
{
    char topic[160];
    char payload[640];
    snprintf(topic, sizeof(topic), "homeassistant/%s/%s_%s/config", component, s_dev_id, entity);
    snprintf(payload, sizeof(payload),
        "{\"name\":\"%s\",\"uniq_id\":\"%s_%s\",\"~\":\"%s\","
        "\"stat_t\":\"~/state\",\"avty_t\":\"~/available\",\"pl_avail\":\"online\",\"pl_not_avail\":\"offline\"%s,"
        "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"ESP32-S3 PhotoPainter\",\"manufacturer\":\"Waveshare\",\"model\":\"PhotoPainter 6-Color\"}}",
        name, s_dev_id, entity, s_base_topic, extra, s_dev_id);
    esp_mqtt_client_publish(s_client, topic, payload, 0, 0, 1);
}

static void mqtt_publish_discovery(void)
{
    if (!s_client || !s_connected) return;
    pub_disc("sensor", "temperature",    "温度",     ",\"dev_cla\":\"temperature\",\"unit_of_meas\":\"°C\",\"val_tpl\":\"{{ value_json.temperature }}\"");
    pub_disc("sensor", "humidity",       "湿度",     ",\"dev_cla\":\"humidity\",\"unit_of_meas\":\"%\",\"val_tpl\":\"{{ value_json.humidity }}\"");
    pub_disc("sensor", "battery_voltage","电池电压", ",\"dev_cla\":\"voltage\",\"unit_of_meas\":\"V\",\"val_tpl\":\"{{ value_json.battery_voltage }}\"");
    pub_disc("sensor", "battery_percent","电池电量", ",\"dev_cla\":\"battery\",\"unit_of_meas\":\"%\",\"val_tpl\":\"{{ value_json.battery_percent }}\"");
    pub_disc("binary_sensor", "charging", "充电中",  ",\"dev_cla\":\"battery_charging\",\"pl_on\":\"charging\",\"pl_off\":\"not_charging\",\"val_tpl\":\"{{ value_json.charging }}\"");
    pub_disc("sensor", "charge_status",   "充电状态", ",\"val_tpl\":\"{{ value_json.charge_status }}\"");
    pub_disc("sensor", "img_count",       "图片数量", ",\"val_tpl\":\"{{ value_json.img_count }}\"");
    pub_disc("sensor", "img_index",       "当前图片索引", ",\"val_tpl\":\"{{ value_json.img_index }}\"");
    pub_disc("number", "img_index",       "切换图片", ",\"cmd_t\":\"~/img_index/set\",\"min\":0,\"max\":1000,\"step\":1,\"mode\":\"box\",\"val_tpl\":\"{{ value_json.img_index }}\"");
    pub_disc("number", "interval",        "轮播间隔", ",\"cmd_t\":\"~/interval/set\",\"min\":1,\"max\":1440,\"step\":1,\"mode\":\"box\",\"unit_of_meas\":\"min\",\"val_tpl\":\"{{ value_json.interval }}\"");
    pub_disc("switch", "running",         "自动轮播", ",\"cmd_t\":\"~/running/set\",\"pl_on\":\"ON\",\"pl_off\":\"OFF\",\"val_tpl\":\"{{ 'ON' if value_json.running else 'OFF' }}\"");
    pub_disc("button", "next",            "下一张",   ",\"cmd_t\":\"~/next\"");
    pub_disc("button", "prev",            "上一张",   ",\"cmd_t\":\"~/prev\"");
}

// ---- command handling ----
static void mqtt_handle_command(const char *topic, const char *data)
{
    if (strcmp(topic, s_state_topic) == 0) return; // ignore state topic echo
    ESP_LOGI(TAG, "cmd: %s => %s", topic, data ? data : "");

    if (strstr(topic, "/img_index/set") && data) {
        long v = atol(data);
        if (v < 0) v = 0;
        if ((uint32_t)v >= photo_img_count) v = photo_img_count - 1;
        if (v >= 0) photo_switch_to((uint32_t)v);
    } else if (strstr(topic, "/interval/set") && data) {
        int v = atoi(data);
        if (v < 1) v = 1;
        if (v > 1440) v = 1440;
        photo_set_interval(v);
    } else if (strstr(topic, "/running/set") && data) {
        bool on = (strcmp(data, "ON") == 0 || strcmp(data, "on") == 0
                || strcmp(data, "true") == 0 || strcmp(data, "1") == 0);
        photo_set_running(on);
    } else if (strstr(topic, "/next")) {
        if (photo_img_count > 0) photo_switch_to((photo_img_index + 1) % photo_img_count);
    } else if (strstr(topic, "/prev")) {
        if (photo_img_count > 0) photo_switch_to((photo_img_index + photo_img_count - 1) % photo_img_count);
    }
    mqtt_publish_state();
}

// ---- mqtt event handler ----
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_t *event = (esp_mqtt_event_t *)event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        s_connected = true;
        ESP_LOGI(TAG, "MQTT connected to %s", s_host);
        esp_mqtt_client_publish(s_client, s_avail_topic, "online", 0, 0, 1);
        mqtt_publish_discovery();
        mqtt_publish_state();
        char sub[96];
        snprintf(sub, sizeof(sub), "%s/+/set", s_base_topic);
        esp_mqtt_client_subscribe(s_client, sub, 0);
        snprintf(sub, sizeof(sub), "%s/next", s_base_topic);
        esp_mqtt_client_subscribe(s_client, sub, 0);
        snprintf(sub, sizeof(sub), "%s/prev", s_base_topic);
        esp_mqtt_client_subscribe(s_client, sub, 0);
        break;
    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        ESP_LOGW(TAG, "MQTT disconnected");
        if (s_client) esp_mqtt_client_publish(s_client, s_avail_topic, "offline", 0, 0, 1);
        break;
    case MQTT_EVENT_DATA: {
        char topic[128], data[64];
        int tlen = event->topic_len < (int)sizeof(topic) - 1 ? event->topic_len : (int)sizeof(topic) - 1;
        memcpy(topic, event->topic, tlen); topic[tlen] = '\0';
        int dlen = event->data_len < (int)sizeof(data) - 1 ? event->data_len : (int)sizeof(data) - 1;
        memcpy(data, event->data, dlen); data[dlen] = '\0';
        mqtt_handle_command(topic, data);
        break;
    }
    default:
        break;
    }
}

// ---- client create / task ----
static void mqtt_client_start(void)
{
    if (!s_host[0]) return;
    esp_mqtt_client_config_t cfg = {};
    cfg.broker.address.hostname = s_host;
    cfg.broker.address.port = s_port;
    cfg.broker.address.transport = MQTT_TRANSPORT_OVER_TCP;
    if (s_user[0]) cfg.credentials.username = s_user;
    if (s_pass[0]) cfg.credentials.authentication.password = s_pass;
    cfg.session.keepalive = 60;
    cfg.network.reconnect_timeout_ms = 10000;
    cfg.buffer.size = 1024;
    cfg.buffer.out_size = 1024;

    s_client = esp_mqtt_client_init(&cfg);
    if (!s_client) return;
    esp_mqtt_client_register_event(s_client, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_client);
    ESP_LOGI(TAG, "MQTT client started -> %s:%d", s_host, s_port);
}

static void mqtt_ha_task(void *arg)
{
    mqtt_client_start();
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(30000));
        if (wifi_manager_is_connected() && s_client && s_connected) {
            mqtt_publish_state();
        }
    }
}

// ---- lifecycle ----
void mqtt_ha_start(void)
{
    mqtt_load_config();
    if (!s_enabled || s_task) return;
    mqtt_compute_base();
    xTaskCreate(mqtt_ha_task, "mqtt_ha", 6 * 1024, NULL, 2, &s_task);
    ESP_LOGI(TAG, "MQTT HA enabled, base=%s", s_base_topic);
}

void mqtt_ha_reconfigure(void)
{
    if (s_task) { vTaskDelete(s_task); s_task = NULL; }
    if (s_client) {
        esp_mqtt_client_stop(s_client);
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
    }
    mqtt_load_config();
    if (!s_enabled) { ESP_LOGI(TAG, "MQTT HA disabled"); return; }
    mqtt_compute_base();
    xTaskCreate(mqtt_ha_task, "mqtt_ha", 6 * 1024, NULL, 2, &s_task);
    ESP_LOGI(TAG, "MQTT HA reconfigured, base=%s", s_base_topic);
}

bool mqtt_ha_enabled(void)   { return s_enabled; }
const char *mqtt_ha_host(void)     { return s_host; }
int  mqtt_ha_port(void)            { return s_port; }
const char *mqtt_ha_username(void) { return s_user; }
bool mqtt_ha_connected(void)       { return s_connected; }
