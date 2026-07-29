#include "photo_web_pages.h"
#include "epdoptimize_bundle.h"
#include "opendisplay_bundle.h"
#include "wifi_manager.h"
#include "nvs_manager.h"
#include "display_bsp.h"
#include "sdcard_bsp.h"
#include "button_bsp.h"
#include "user_app.h"
#include "list.h"
#include <esp_http_server.h>
#include <esp_log.h>
#include <cJSON.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include <esp_timer.h>

static const char *TAG = "photoweb";
static httpd_handle_t server = NULL;
static void add_cors(httpd_req_t *req);

// Extern from PhotoFrame_mode.cpp
extern uint32_t photo_img_count;
extern uint32_t photo_img_index;
extern int      photo_interval;
extern bool     photo_running;
extern char     sleep_start[6];
extern char     sleep_end[6];
extern "C" bool photo_get_sensor(float *temp, float *rh);
extern CustomSDPort *SDPort;
extern ePaperPort ePaperDisplay;

/* ---- GET /api/status ---- */
static esp_err_t api_get_status(httpd_req_t *req)
{
    add_cors(req);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "wifi", wifi_manager_is_connected());
    float temp = 0, rh = 0;
    if (photo_get_sensor(&temp, &rh)) {
        cJSON_AddNumberToObject(root, "temperature", temp);
        cJSON_AddNumberToObject(root, "humidity", rh);
    } else {
        cJSON_AddNumberToObject(root, "temperature", 0);
        cJSON_AddNumberToObject(root, "humidity", 0);
    }
    cJSON_AddNumberToObject(root, "img_count", photo_img_count);
    cJSON_AddNumberToObject(root, "img_index", photo_img_index);
    cJSON_AddNumberToObject(root, "interval", photo_interval);
    cJSON_AddBoolToObject(root, "running", photo_running);
    cJSON_AddStringToObject(root, "sleep_start", sleep_start);
    cJSON_AddStringToObject(root, "sleep_end", sleep_end);
    char *str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, str, HTTPD_RESP_USE_STRLEN);
    free(str);
    cJSON_Delete(root);
    return ESP_OK;
}

/* ---- GET /api/photos ---- */
static esp_err_t api_photos_get(httpd_req_t *req)
{
    cJSON *root  = cJSON_CreateObject();
    cJSON *files = cJSON_CreateArray();
    list_t *host = SDPort->SDPort_GetListHost();
    if (host) {
        list_iterator_t *it = list_iterator_new(host, LIST_HEAD);
        list_node_t *node = list_iterator_next(it);
        while (node) {
            CustomSDPortNode_t *sd = (CustomSDPortNode_t *)node->val;
            const char *name = strrchr(sd->sdcard_name, '/');
            name = name ? name + 1 : sd->sdcard_name;
            cJSON_AddItemToArray(files, cJSON_CreateString(name));
            node = list_iterator_next(it);
        }
        list_iterator_destroy(it);
    }
    cJSON_AddItemToObject(root, "files", files);
    cJSON_AddNumberToObject(root, "total", photo_img_count);
    char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
    free(json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

/* ---- GET /api/wifi/scan ---- */
static esp_err_t api_wifi_scan(httpd_req_t *req)
{
    if (!wifi_manager_is_connected()) wifi_manager_start_ap();
    char ssids[32][33]; int count = 0;
    wifi_manager_scan(ssids, 32, &count);
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < count; i++) cJSON_AddItemToArray(arr, cJSON_CreateString(ssids[i]));
    cJSON_AddItemToObject(root, "networks", arr);
    char *str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, str, HTTPD_RESP_USE_STRLEN);
    free(str); cJSON_Delete(root);
    return ESP_OK;
}

/* ---- POST /api/wifi/connect ---- */
static esp_err_t api_wifi_connect(httpd_req_t *req)
{
    char buf[256]; int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data"); return ESP_FAIL; }
    buf[len] = '\0';
    cJSON *root = cJSON_Parse(buf);
    if (!root) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad JSON"); return ESP_FAIL; }
    cJSON *ssid = cJSON_GetObjectItem(root, "ssid");
    cJSON *pass = cJSON_GetObjectItem(root, "password");
    if (!ssid) { cJSON_Delete(root); httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing ssid"); return ESP_FAIL; }
    wifi_manager_set_credentials(ssid->valuestring, pass ? pass->valuestring : "");
    cJSON_Delete(root);
    wifi_manager_disconnect();
    wifi_manager_apply_credentials();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

/* ---- POST /api/wifi/reset ---- */
static esp_err_t api_wifi_reset(httpd_req_t *req) {
    wifi_manager_reset();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

/* ---- POST /api/upload ---- */
static esp_err_t api_upload(httpd_req_t *req)
{
    add_cors(req);
    mkdir("/sdcard/photos", 0777);
    if (req->content_len <= 0 || req->content_len > 2 * 1024 * 1024) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid size"); return ESP_FAIL;
    }
    uint8_t *buf = (uint8_t *)heap_caps_malloc(req->content_len, MALLOC_CAP_SPIRAM);
    if (!buf) { httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM"); return ESP_FAIL; }

    // httpd_req_recv 不保证一次返回全部数据，需循环读取
    int total = 0;
    while (total < req->content_len) {
        int ret = httpd_req_recv(req, (char *)buf + total, req->content_len - total);
        if (ret <= 0) { free(buf); httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Recv failed"); return ESP_FAIL; }
        total += ret;
    }

    // Use ?name= if provided, otherwise auto-number
    char filepath[256], qbuf[256], fname[128] = {0};
    if (httpd_req_get_url_query_str(req, qbuf, sizeof(qbuf)) == ESP_OK) {
        httpd_query_key_value(qbuf, "name", fname, sizeof(fname));
    }
    if (fname[0]) {
        // Basic sanitize: strip path separators
        for (char *p = fname; *p; p++) if (*p == '/' || *p == '\\') *p = '_';
        snprintf(filepath, sizeof(filepath), "/sdcard/photos/%s.bmp", fname);
    } else {
        snprintf(filepath, sizeof(filepath), "/sdcard/photos/%d.bmp", (int)photo_img_count);
    }

    int64_t t0 = esp_timer_get_time();
    esp_err_t ret = SDPort->SDPort_WriteFile(filepath, buf, req->content_len);
    ESP_LOGI(TAG, "SD write: %lld us", esp_timer_get_time() - t0);
    free(buf);
    vTaskDelay(pdMS_TO_TICKS(50)); // Ensure SD write is flushed before scan
    if (ret != ESP_OK) { httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "SD write failed"); return ESP_FAIL; }

    // 重新扫描以更新列表（ScanListDir 会先清空再扫描）
    int64_t t1 = esp_timer_get_time();
    SDPort->SDPort_ScanListDir("/sdcard/photos");
    ESP_LOGI(TAG, "SD scan: %lld us", esp_timer_get_time() - t1);
    photo_img_count = SDPort->SDPort_GetScanListValue();
    // Find actual index of newly uploaded file (readdir order is not creation order)
    photo_img_index = 0;
    list_t *host = SDPort->SDPort_GetListHost();
    if (host && photo_img_count > 0) {
        uint32_t idx = 0;
        list_iterator_t *it = list_iterator_new(host, LIST_HEAD);
        list_node_t *node = list_iterator_next(it);
        while (node) {
            CustomSDPortNode_t *sd = (CustomSDPortNode_t *)node->val;
            if (strcmp(sd->sdcard_name, filepath) == 0) { photo_img_index = idx; break; }
            idx++;
            node = list_iterator_next(it);
        }
        list_iterator_destroy(it);
    }
    ESP_LOGI(TAG, "上传完成: file=%s, count=%lu, showing index=%lu", filepath, photo_img_count, photo_img_index);
    xEventGroupSetBits(epaper_groups, set_bit_button(0));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

/* ---- POST /api/delete ---- */
static esp_err_t api_delete(httpd_req_t *req)
{
    char buf[128]; int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data"); return ESP_FAIL; }
    buf[len] = '\0';
    cJSON *json = cJSON_Parse(buf);
    if (!json) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad JSON"); return ESP_FAIL; }
    cJSON *name_item = cJSON_GetObjectItem(json, "name");
    if (!name_item || !name_item->valuestring) { cJSON_Delete(json); httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing name"); return ESP_FAIL; }
    const char *name = name_item->valuestring;
    char path[256];
    snprintf(path, sizeof(path), "/sdcard/photos/%s", name);
    unlink(path);
    // 重新扫描以更新列表（ScanListDir 会先清空再扫描）
    int64_t t1 = esp_timer_get_time();
    SDPort->SDPort_ScanListDir("/sdcard/photos");
    ESP_LOGI(TAG, "SD scan: %lld us", esp_timer_get_time() - t1);
    photo_img_count = SDPort->SDPort_GetScanListValue();
    if (photo_img_index >= photo_img_count && photo_img_count > 0)
        photo_img_index = photo_img_count - 1;
    cJSON_Delete(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

/* ---- POST /api/settings ---- */
static esp_err_t api_settings_post(httpd_req_t *req)
{
    char buf[256]; int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty"); return ESP_FAIL; }
    buf[len] = '\0';
    cJSON *json = cJSON_Parse(buf);
    if (!json) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad JSON"); return ESP_FAIL; }
    cJSON *item = cJSON_GetObjectItem(json, "interval");
    if (item && cJSON_IsNumber(item)) photo_interval = item->valueint;
    item = cJSON_GetObjectItem(json, "running");
    if (item) photo_running = cJSON_IsTrue(item);
    nvs_manager_set_str("photoframe", "interval", buf);
    cJSON_Delete(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

/* ---- GET /api/adjustments ---- */
static esp_err_t api_adjustments_get(httpd_req_t *req)
{
    char buf[256]; size_t len = sizeof(buf);
    if (nvs_manager_get_str("photoframe", "imgadj", buf, &len) == ESP_OK && len > 0) {
        buf[len] = '\0';
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    } else {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{}");
    }
    return ESP_OK;
}

/* ---- POST /api/adjustments ---- */
static esp_err_t api_adjustments_post(httpd_req_t *req)
{
    char buf[256]; int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty"); return ESP_FAIL; }
    buf[len] = '\0';
    nvs_manager_set_str("photoframe", "imgadj", buf);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

/* ---- POST /api/reboot ---- */
static esp_err_t api_reboot(httpd_req_t *req) {
    httpd_resp_sendstr(req, "{\"status\":\"rebooting\"}");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

/* ---- GET /lib/epdoptimize.js ---- */
static esp_err_t serve_epdoptimize(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/javascript; charset=utf-8");
    httpd_resp_send(req, EPDOPTIMIZE_JS, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* ---- GET /lib/opendisplay.js ---- */
static esp_err_t serve_opendisplay(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/javascript; charset=utf-8");
    httpd_resp_send(req, OPENDISPLAY_JS, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* ---- GET /api/photo ---- */
static esp_err_t api_photo_get(httpd_req_t *req)
{
    char qbuf[256], fname[128] = {0};
    if (httpd_req_get_url_query_str(req, qbuf, sizeof(qbuf)) == ESP_OK) {
        httpd_query_key_value(qbuf, "name", fname, sizeof(fname));
    }
    if (!fname[0]) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing name"); return ESP_FAIL; }
    char path[300];
    snprintf(path, sizeof(path), "/sdcard/photos/%s", fname);

    // Check for thumbnail request
    char thumb_str[4] = {0};
    bool want_thumb = false;
    if (httpd_req_get_url_query_str(req, qbuf, sizeof(qbuf)) == ESP_OK) {
        if (httpd_query_key_value(qbuf, "thumb", thumb_str, sizeof(thumb_str)) == ESP_OK) {
            want_thumb = (thumb_str[0] == '1');
        }
    }

    FILE *fp = fopen(path, "rb");
    if (!fp) { httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found"); return ESP_FAIL; }
    fseek(fp, 0, SEEK_END); long sz = ftell(fp); fseek(fp, 0, SEEK_SET);
    if (sz <= 0 || sz > 2*1024*1024) { fclose(fp); httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Bad file"); return ESP_FAIL; }

    uint8_t *buf = (uint8_t *)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
    if (!buf) { fclose(fp); httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM"); return ESP_FAIL; }
    fread(buf, 1, sz, fp); fclose(fp);

    if (!want_thumb) {
        httpd_resp_set_type(req, "image/bmp");
        httpd_resp_send(req, (const char *)buf, sz);
        free(buf);
        return ESP_OK;
    }

    // Generate thumbnail: subsample to max 200px wide
    if (sz < 54) { free(buf); httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Bad BMP"); return ESP_FAIL; }
    int orig_w = *(int32_t*)(buf + 18);
    int orig_h = *(int32_t*)(buf + 22);
    int bpp    = *(uint16_t*)(buf + 28);
    if (orig_w <= 0 || orig_h <= 0 || orig_h > 5000) orig_h = abs(orig_h);
    if (bpp != 24 || orig_w <= 0 || orig_h <= 0) {
        // Unsupported format, send original
        httpd_resp_set_type(req, "image/bmp");
        httpd_resp_send(req, (const char *)buf, sz);
        free(buf);
        return ESP_OK;
    }

    int tw = 400;
    if (orig_w <= tw) tw = orig_w;
    int th = (orig_h * tw) / orig_w;
    if (th < 1) th = 1;
    int row_sz = (tw * 3 + 3) & ~3;  // 4-byte aligned
    int pad_sz = row_sz * th;
    int thumb_sz = 54 + pad_sz;

    uint8_t *out = (uint8_t *)heap_caps_malloc(thumb_sz, MALLOC_CAP_SPIRAM);
    if (!out) {
        httpd_resp_set_type(req, "image/bmp");
        httpd_resp_send(req, (const char *)buf, sz);
        free(buf);
        return ESP_OK;
    }

    // BMP header
    memcpy(out, buf, 54);
    *(int32_t*)(out + 2)  = thumb_sz;   // file size
    *(int32_t*)(out + 18) = tw;         // width
    *(int32_t*)(out + 22) = th;         // height
    *(int32_t*)(out + 34) = pad_sz;     // image size

    // Subsample: nearest-neighbor
    int src_row_sz = (orig_w * 3 + 3) & ~3;
    uint32_t data_off = *(uint32_t*)(buf + 10);
    uint8_t *src = buf + data_off;
    uint8_t *dst = out + 54;

    for (int y = 0; y < th; y++) {
        int sy = (y * orig_h) / th;
        uint8_t *src_row = src + (orig_h - 1 - sy) * src_row_sz;  // BMP is bottom-up
        uint8_t *dst_row = dst + (th - 1 - y) * row_sz;
        for (int x = 0; x < tw; x++) {
            int sx = (x * orig_w) / tw;
            dst_row[x*3]   = src_row[sx*3];
            dst_row[x*3+1] = src_row[sx*3+1];
            dst_row[x*3+2] = src_row[sx*3+2];
        }
        // Padding bytes are already zero from memset-like... not guaranteed.
        // Zero the padding
        for (int p = tw*3; p < row_sz; p++) dst_row[p] = 0;
    }

    free(buf);
    httpd_resp_set_type(req, "image/bmp");
    httpd_resp_send(req, (const char *)out, thumb_sz);
    free(out);
    return ESP_OK;
}

/* ---- POST /api/switch ---- */
static int64_t last_switch_time = 0;
extern uint32_t photo_img_index;
extern EventGroupHandle_t epaper_groups;
static esp_err_t api_switch_post(httpd_req_t *req)
{
    int64_t now = esp_timer_get_time();
    if (now - last_switch_time < 15000000) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"ok\":false,\"msg\":\"请等待15秒后再切换\"}");
        return ESP_OK;
    }
    char buf[64]; int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data"); return ESP_FAIL; }
    buf[len] = '\0';
    cJSON *json = cJSON_Parse(buf);
    if (!json) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad JSON"); return ESP_FAIL; }
    cJSON *item = cJSON_GetObjectItem(json, "index");
    if (item && cJSON_IsNumber(item)) {
        photo_img_index = item->valueint;
        last_switch_time = now;
        xEventGroupSetBits(epaper_groups, set_bit_button(0));
    }
    cJSON_Delete(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

/* ---- GET / ---- */
static esp_err_t serve_index(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, WEB_INDEX_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static void add_cors(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "*");
}
static esp_err_t cors_options(httpd_req_t *req) {
    add_cors(req); httpd_resp_sendstr(req, ""); return ESP_OK;
}
static void register_get(const char *p, esp_err_t (*h)(httpd_req_t *)) {
    httpd_uri_t u = {.uri = p, .method = HTTP_GET, .handler = h}; httpd_register_uri_handler(server, &u);
}
static void register_post(const char *p, esp_err_t (*h)(httpd_req_t *)) {
    httpd_uri_t u = {.uri = p, .method = HTTP_POST, .handler = h}; httpd_register_uri_handler(server, &u);
}

extern "C" void photo_web_server_init(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 20;
    if (httpd_start(&server, &config) == ESP_OK) {
        ESP_LOGI(TAG, "Web 服务器已启动, 端口 %d", config.server_port);
        httpd_uri_t opt = {.uri = "/*", .method = HTTP_OPTIONS, .handler = cors_options};
        httpd_register_uri_handler(server, &opt);
        register_get("/lib/epdoptimize.js", serve_epdoptimize);
        register_get("/lib/opendisplay.js", serve_opendisplay);
        register_get("/", serve_index);
        register_get("/api/status", api_get_status);
        register_get("/api/photos", api_photos_get);
        register_get("/api/wifi/scan", api_wifi_scan);
        register_post("/api/wifi/connect", api_wifi_connect);
        register_post("/api/wifi/reset", api_wifi_reset);
        register_post("/api/upload", api_upload);
        register_post("/api/delete", api_delete);
        register_get("/api/adjustments", api_adjustments_get);
        register_post("/api/adjustments", api_adjustments_post);
        register_post("/api/settings", api_settings_post);
        register_get("/api/photo", api_photo_get);
        register_post("/api/switch", api_switch_post);
        register_post("/api/reboot", api_reboot);
    }
}
