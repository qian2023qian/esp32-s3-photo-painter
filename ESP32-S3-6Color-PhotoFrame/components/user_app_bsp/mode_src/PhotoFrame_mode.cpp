#include <stdio.h>
#include <string.h>
#include <cJSON.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <esp_sntp.h>
#include <esp_timer.h>
#include <time.h>
#include <sys/stat.h>

#include "display_bsp.h"
#include "button_bsp.h"
#include "user_app.h"
#include "sdcard_bsp.h"
#include "list.h"
#include "i2c_equipment.h"

#include "wifi_manager.h"
#include "nvs_manager.h"
#include "mqtt_ha.h"
#include "power_bsp.h"

static const char *TAG = "photoframe";

// 低电量阈值：<ENTER 进入电量页暂停轮播，≥EXIT 恢复（回滞防反复切换）
#define LOW_BATTERY_ENTER  10
#define LOW_BATTERY_EXIT   15

static bool low_battery_active = false;
static bool battery_page_dirty = false;

// Shared with photo_web_server.cpp
uint32_t photo_img_count = 0;
uint32_t photo_img_index = 0;
int      photo_interval  = 60;  // minutes
bool     photo_running   = true;  // 用户“自动轮播”开关，持久化到 NVS（低电量临时暂停不写这里）
char     sleep_start[6]  = "23:00";
char     sleep_end[6]    = "07:00";

static list_t *photoframe_list = NULL;
static Shtc3Port *shtc3 = NULL;

// Stub: used by server_app.cpp for NetworkMode display
uint8_t Get_CurrentlyNetworkMode(void) { return 0; }

extern "C" bool photo_get_sensor(float *temp, float *rh)
{
    if (!shtc3) return false;
    return shtc3->Shtc3_ReadTempHumi(temp, rh);
}

/* ---- Shared control helpers (web server + MQTT share the same 15s throttle) ---- */
static portMUX_TYPE g_switch_mux = portMUX_INITIALIZER_UNLOCKED;
static int64_t      g_last_switch_us = 0;

extern "C" void photo_persist_settings(void);

extern "C" int photo_switch_to(uint32_t index)
{
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&g_switch_mux);
    if (now - g_last_switch_us < 15000000) { portEXIT_CRITICAL(&g_switch_mux); return 0; }
    g_last_switch_us = now;
    portEXIT_CRITICAL(&g_switch_mux);
    if (photo_img_count == 0) return 0;
    if (index >= photo_img_count) index = photo_img_count - 1;
    photo_img_index = index;
    xEventGroupSetBits(epaper_groups, set_bit_button(0));
    ESP_LOGI(TAG, "switch to [%lu/%lu]", photo_img_index + 1, photo_img_count);
    return 1;
}

extern "C" void photo_set_interval(int minutes)
{
    photo_interval = minutes;
    photo_persist_settings();
}

extern "C" void photo_set_running(bool run)
{
    photo_running = run;
    photo_persist_settings();
}

extern "C" void photo_persist_settings(void)
{
    char buf[256];
    snprintf(buf, sizeof(buf), "{\"interval\":%d,\"running\":%s,\"sleep_start\":\"%s\",\"sleep_end\":\"%s\"}",
             photo_interval, photo_running ? "true" : "false", sleep_start, sleep_end);
    nvs_manager_set_str("photoframe", "interval", buf);
}

/* ---- 中文电量页（BOOT 双击 / 低电量自动显示共用）；返回是否成功上屏 ---- */
extern "C" bool photo_show_battery_page(bool low)
{
    if (pdTRUE != xSemaphoreTake(epaper_gui_semapHandle, 2000)) return false;

    PmicRegisterConfig pmic = Custom_PmicGetBatteryInfo();
    int mv  = Custom_PmicGetBatteryVoltage();
    int pct = Custom_PmicGetBatteryPercent();

    ePaperDisplay.EPD_DispClear(ColorWhite);
    int y = 150;
    if (low) {
        ePaperDisplay.EPD_DrawStringCN(250, y, "低电量 请充电", &FontBatteryCN, ColorRed, ColorWhite);
        y += 55;
    }

    const char *chg = "充电状态：未充电";
    if (strstr(pmic.isCharging, "Charging") && !strstr(pmic.isCharging, "Not Charging"))
        chg = "充电状态：充电中";

    const char *stage = "充电阶段：未充电";
    if      (strstr(pmic.chargeStatus, "Tri"))            stage = "充电阶段：涓流充电";
    else if (strstr(pmic.chargeStatus, "Pre"))            stage = "充电阶段：预充电";
    else if (strstr(pmic.chargeStatus, "Constant_Charge")) stage = "充电阶段：恒流充电";
    else if (strstr(pmic.chargeStatus, "Constant_Voltage"))stage = "充电阶段：恒压充电";
    else if (strstr(pmic.chargeStatus, "Done"))            stage = "充电阶段：已充满";

    char volt[32], pct_str[32];
    snprintf(volt, sizeof(volt), "电池电压：%dmV", mv);
    snprintf(pct_str, sizeof(pct_str), "电池电量：%d%%", pct);

    ePaperDisplay.EPD_DrawStringCN(200, y,      chg,     &FontBatteryCN, ColorBlack, ColorWhite);
    ePaperDisplay.EPD_DrawStringCN(200, y + 40, stage,   &FontBatteryCN, ColorBlack, ColorWhite);
    ePaperDisplay.EPD_DrawStringCN(200, y + 80, volt,    &FontBatteryCN, ColorBlack, ColorWhite);
    ePaperDisplay.EPD_DrawStringCN(200, y + 120, pct_str, &FontBatteryCN, ColorBlack, ColorWhite);
    ePaperDisplay.Set_Rotation(2);   // 180°，与横屏图片一致（图片加载 BMP 时也会设 2/3）
    ePaperDisplay.EPD_Display();
    ePaperDisplay.Set_Rotation(0);   // 恢复默认，后续图片显示时会自行设置

    xSemaphoreGive(epaper_gui_semapHandle);
    return true;
}

/* ---- 状态提示页文本宽度（中文 24px/字，ASCII 用字库 ASCII_Width）---- */
static int photo_text_width_cn(const char *s)
{
    int w = 0;
    while (s && *s) {
        if ((unsigned char)*s <= 0xE0) { w += FontBatteryCN.ASCII_Width; s += 1; }
        else                           { w += FontBatteryCN.Width;       s += 3; }
    }
    return w;
}

/* ---- 状态提示页（无图片 / 存储卡未挂载 / 图片读取失败）----
   注意：调用方必须已持有 epaper_gui_semapHandle，互斥锁非递归，本函数不再重复获取 */
static void photo_draw_status_page_locked(const char *title, const char *hint, bool warn)
{
    ePaperDisplay.EPD_DispClear(ColorWhite);

    const int y = 170;
    int w1 = photo_text_width_cn(title);
    ePaperDisplay.EPD_DrawStringCN((800 - w1) / 2, y, title, &FontBatteryCN,
                                   warn ? ColorRed : ColorBlack, ColorWhite);
    if (hint && hint[0]) {
        int w2 = photo_text_width_cn(hint);
        ePaperDisplay.EPD_DrawStringCN((800 - w2) / 2, y + 46, hint, &FontBatteryCN,
                                       ColorBlack, ColorWhite);
    }

    ePaperDisplay.Set_Rotation(2);      // 与横屏图片 / 电量页一致
    ePaperDisplay.EPD_Display();
    ePaperDisplay.Set_Rotation(0);
    ESP_LOGW(TAG, "status page: %s | %s", title ? title : "", hint ? hint : "");
}

/* ---- ePaper GUI Task ---- */
static void gui_task(void *arg)
{
    ePaperDisplay.EPD_Init();
    for (;;) {
        EventBits_t even = xEventGroupWaitBits(epaper_groups, set_bit_all, pdTRUE, pdFALSE, pdMS_TO_TICKS(1000));
        if (!get_bit_button(even, 0)) continue;
        if (pdTRUE != xSemaphoreTake(epaper_gui_semapHandle, 2000)) continue;

        xEventGroupSetBits(Green_led_Mode_queue, set_bit_button(6));
        Green_led_arg = 1;

        if (photoframe_list && photo_img_count > 0) {
            list_node_t *node = list_at(photoframe_list, photo_img_index);
            bool drawn = false;
            if (node) {
                CustomSDPortNode_t *sd_node = (CustomSDPortNode_t *)node->val;
                ESP_LOGI(TAG, "Displaying [%lu/%lu]: %s", photo_img_index + 1, photo_img_count, sd_node->sdcard_name);
                drawn = ePaperDisplay.EPD_SDcardBmpShakingColor(sd_node->sdcard_name, 0, 0);
                if (drawn) ePaperDisplay.EPD_Display();
            }
            if (!drawn) {
                /* 列表里有文件但读不出来（卡被拔出 / 文件损坏 / 非 24bit BMP）：
                   给一页可见提示，别让屏幕停在旧画面看起来像死机 */
                photo_draw_status_page_locked("图片读取失败", "请检查存储卡", true);
            }
        } else {
            /* 一张图都没有：区分“SD 未挂载”和“目录为空”，两者都给出可见提示 */
            if (SDPort && SDPort->SDPort_GetSdcardInitOK())
                photo_draw_status_page_locked("未找到图片", "请上传图片", false);
            else
                photo_draw_status_page_locked("存储卡未检测到", "请检查存储卡", true);
        }
        battery_page_dirty = true;   // 低电量态下被图片覆盖后，下个周期重新显示电量页

        xSemaphoreGive(epaper_gui_semapHandle);
        Green_led_arg = 0;
    }
}

/* ---- Sleep window check ---- */
static bool is_sleep_time(void)
{
    time_t now; struct tm ti;
    time(&now); localtime_r(&now, &ti);
    int cur = ti.tm_hour * 60 + ti.tm_min;
    int start = ((sleep_start[0]-'0')*10 + (sleep_start[1]-'0')) * 60
              + ((sleep_start[3]-'0')*10 + (sleep_start[4]-'0'));
    int end   = ((sleep_end[0]-'0')*10 + (sleep_end[1]-'0')) * 60
              + ((sleep_end[3]-'0')*10 + (sleep_end[4]-'0'));
    if (start <= end) return (cur >= start && cur < end);        // 同日 23:00~07:00
    else              return (cur >= start || cur < end);         // 跨日
}

/* ---- Slideshow Task ---- */
static void slideshow_task(void *arg)
{
    // 每分钟检查一次，避免长 vTaskDelay 导致休眠结束后无法及时恢复
    int tick_minute = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(60 * 1000));

        /* 低电量检测（独立于轮播开关）：<10% 暂停轮播并显示电量页，充电≥15% 恢复。
           注意：**充电中完全不做低电量处理**。
           之前的实现只在“重显提醒”那一支加了 !charging，而“首次进入低电量”那一支没有，
           于是开机后第一次检查（最多 60s）时，即使正在充电也会无条件刷出电量页，
           把用户刚切换/上传的图片覆盖掉。已经插上电再提示“请充电”本身也没有意义。*/
        int pct = Custom_PmicGetBatteryPercent();
        bool charging = Custom_PmicGetCharging();
        if (charging) {
            if (low_battery_active) {          // 插电后立即恢复：清掉电量页并放行自动轮播
                low_battery_active = false;
                battery_page_dirty = false;
                xEventGroupSetBits(epaper_groups, set_bit_button(0));
            }
        } else if (pct >= 0 && pct < LOW_BATTERY_ENTER && !low_battery_active) {
            // 低电量只置 low_battery_active 做“临时暂停”，不动 photo_running：
            // photo_running 是用户的自动轮播开关（会持久化），不能被临时状态顶掉
            low_battery_active = true;
            battery_page_dirty = false;
            if (!photo_show_battery_page(true)) battery_page_dirty = true;   // 上屏失败则下周期重试
        } else if (low_battery_active && pct >= LOW_BATTERY_EXIT) {
            low_battery_active = false;
            xEventGroupSetBits(epaper_groups, set_bit_button(0));   // 覆盖电量页；轮播按用户开关恢复
        } else if (low_battery_active && battery_page_dirty) {
            // 未充电时被切图覆盖后重显提醒（充电中不会走到这里）
            if (photo_show_battery_page(true)) battery_page_dirty = false;
        }

        if (!photo_running || low_battery_active || photo_img_count == 0) { tick_minute = 0; continue; }
        if (is_sleep_time()) { tick_minute = 0; continue; }
        tick_minute++;
        if (tick_minute >= photo_interval) {
            tick_minute = 0;
            photo_img_index = (photo_img_index + 1) % photo_img_count;
            xEventGroupSetBits(epaper_groups, set_bit_button(0));
        }
    }
}

/* ---- Button Task ---- */
static void button_task(void *arg)
{
    for (;;) {
        EventBits_t even = xEventGroupWaitBits(BootButtonGroups, set_bit_all, pdTRUE, pdFALSE, pdMS_TO_TICKS(2000));
        if (get_bit_button(even, 0)) {
            if (photo_img_count == 0) continue;   // 无图片：保持任务存活（原来 return 会让按键任务永久退出）
            photo_img_index = (photo_img_index + 1) % photo_img_count;
            xEventGroupSetBits(epaper_groups, set_bit_button(0));
        }
    }
}

/* ---- Web server (from photo_web_server.cpp) ---- */
extern "C" void photo_web_server_init(void);

/* ---- Init ---- */
void User_PhotoFrame_mode_app_init(void)
{
    xEventGroupSetBits(Red_led_Mode_queue, set_bit_button(0));

    // Init SHTC3 sensor
    shtc3 = new Shtc3Port(I2cBus);

    // Load settings from NVS (saved via web dashboard)
    nvs_manager_init();
    char nvs_buf[256]; size_t nvs_len = sizeof(nvs_buf);
    if (nvs_manager_get_str("photoframe", "interval", nvs_buf, &nvs_len) == ESP_OK) {
        cJSON *json = cJSON_Parse(nvs_buf);
        if (json) {
            cJSON *item = cJSON_GetObjectItem(json, "interval");
            if (item && cJSON_IsNumber(item)) photo_interval = item->valueint;
            item = cJSON_GetObjectItem(json, "running");
            if (item) photo_running = cJSON_IsTrue(item);
            item = cJSON_GetObjectItem(json, "sleep_start");
            if (item && cJSON_IsString(item)) strncpy(sleep_start, item->valuestring, 5);
            item = cJSON_GetObjectItem(json, "sleep_end");
            if (item && cJSON_IsString(item)) strncpy(sleep_end, item->valuestring, 5);
            cJSON_Delete(json);
        }
    }
    ESP_LOGI(TAG, "轮播间隔: %d 分钟, 运行: %d", photo_interval, photo_running);

    // Scan SD card photos directory
    mkdir("/sdcard/photos", 0777);
    SDPort->SDPort_ScanListDir("/sdcard/photos");
    photoframe_list = SDPort->SDPort_GetListHost();
    photo_img_count = SDPort->SDPort_GetScanListValue();
    ESP_LOGI(TAG, "SD 卡找到 %lu 张图片", photo_img_count);

    /* 一张图都没有（含 SD 未挂载）时，开机就主动刷一页提示：
       否则屏幕会一直停在断电前的旧画面，无法区分“正常运行”和“没读到卡” */
    if (photo_img_count == 0) {
        xEventGroupSetBits(epaper_groups, set_bit_button(0));
    }

    // Init WiFi (STA try first, AP fallback)
    wifi_manager_init();
    if (!wifi_manager_connect()) {
        wifi_manager_start_ap();
    }

    // SNTP time sync (non-blocking)
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    // Start web server
    photo_web_server_init();

    // MQTT -> Home Assistant (enabled via dashboard config in NVS)
    mqtt_ha_start();

    // Create tasks
    xTaskCreate(gui_task, "photoframe_gui", 6 * 1024, NULL, 2, NULL);
    xTaskCreate(slideshow_task, "photoframe_slide", 4 * 1024, NULL, 2, NULL);
    xTaskCreate(button_task, "photoframe_btn", 3 * 1024, NULL, 3, NULL);

    ESP_LOGI(TAG, "相框模式初始化完成");
    ESP_LOGI(TAG, "WiFi 热点: PhotoFrame / 12345678 (仪表盘: 设备IP)");
}
