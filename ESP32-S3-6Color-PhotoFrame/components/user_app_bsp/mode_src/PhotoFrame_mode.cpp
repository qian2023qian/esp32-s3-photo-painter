#include <stdio.h>
#include <string.h>
#include <cJSON.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <sys/stat.h>

#include "display_bsp.h"
#include "button_bsp.h"
#include "user_app.h"
#include "sdcard_bsp.h"
#include "list.h"
#include "i2c_equipment.h"

#include "wifi_manager.h"
#include "nvs_manager.h"

static const char *TAG = "photoframe";

// Shared with photo_web_server.cpp
uint32_t photo_img_count = 0;
uint32_t photo_img_index = 0;
int      photo_interval  = 60;  // minutes
bool     photo_running   = true;

static list_t *photoframe_list = NULL;
static Shtc3Port *shtc3 = NULL;

// Stub: used by server_app.cpp for NetworkMode display
uint8_t Get_CurrentlyNetworkMode(void) { return 0; }

extern "C" bool photo_get_sensor(float *temp, float *rh)
{
    if (!shtc3) return false;
    return shtc3->Shtc3_ReadTempHumi(temp, rh);
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
            if (node) {
                CustomSDPortNode_t *sd_node = (CustomSDPortNode_t *)node->val;
                ESP_LOGI(TAG, "Displaying [%lu/%lu]: %s", photo_img_index + 1, photo_img_count, sd_node->sdcard_name);
                ePaperDisplay.EPD_SDcardBmpShakingColor(sd_node->sdcard_name, 0, 0);
                ePaperDisplay.EPD_Display();
            }
        }

        xSemaphoreGive(epaper_gui_semapHandle);
        Green_led_arg = 0;
    }
}

/* ---- Slideshow Task ---- */
static void slideshow_task(void *arg)
{
    // 开机不自动刷新——墨水屏掉电不丢图，保留上次关机画面
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(photo_interval * 60 * 1000));
        if (!photo_running || photo_img_count == 0) continue;
        photo_img_index = (photo_img_index + 1) % photo_img_count;
        xEventGroupSetBits(epaper_groups, set_bit_button(0));
    }
}

/* ---- Button Task ---- */
static void button_task(void *arg)
{
    for (;;) {
        EventBits_t even = xEventGroupWaitBits(BootButtonGroups, set_bit_all, pdTRUE, pdFALSE, pdMS_TO_TICKS(2000));
        if (get_bit_button(even, 0)) {
            if (photo_img_count == 0) return;
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
    char nvs_buf[64]; size_t nvs_len = sizeof(nvs_buf);
    if (nvs_manager_get_str("photoframe", "interval", nvs_buf, &nvs_len) == ESP_OK) {
        cJSON *json = cJSON_Parse(nvs_buf);
        if (json) {
            cJSON *item = cJSON_GetObjectItem(json, "interval");
            if (item && cJSON_IsNumber(item)) photo_interval = item->valueint;
            item = cJSON_GetObjectItem(json, "running");
            if (item) photo_running = cJSON_IsTrue(item);
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

    // Init WiFi (STA try first, AP fallback)
    wifi_manager_init();
    if (!wifi_manager_connect()) {
        wifi_manager_start_ap();
    }

    // Start web server
    photo_web_server_init();

    // Create tasks
    xTaskCreate(gui_task, "photoframe_gui", 6 * 1024, NULL, 2, NULL);
    xTaskCreate(slideshow_task, "photoframe_slide", 4 * 1024, NULL, 2, NULL);
    xTaskCreate(button_task, "photoframe_btn", 3 * 1024, NULL, 3, NULL);

    ESP_LOGI(TAG, "相框模式初始化完成");
    ESP_LOGI(TAG, "WiFi 热点: PhotoFrame / 12345678 (仪表盘: 设备IP)");
}
