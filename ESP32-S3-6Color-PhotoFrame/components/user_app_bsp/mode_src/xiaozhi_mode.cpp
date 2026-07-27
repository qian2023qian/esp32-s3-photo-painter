#include <stdio.h>
#include <string.h>
#include <esp_heap_caps.h>
#include <nvs_flash.h>
#include <driver/rtc_io.h>
#include "user_app.h"
#include "application.h"
#include "client_app.h"
#include "weather_app.h"
#include "button_bsp.h"
#include "list.h"
#include "i2c_equipment.h"

static const char *TAG = "xiaozhi_mode";
WeatherPort WeaPort;
WeatherData_t *WeatherData = NULL;
static list_t *ListHost = NULL;
Shtc3Port *PeraPort = NULL;

char THData[40];
int  sdcard_bmp_Quantity = 0;
int  sdcard_doc_count    = 0;

char sleep_buff[18];
SemaphoreHandle_t ai_img_while_semap;
EventGroupHandle_t ai_IMG_LoopGroup;
int img_loopTimer = 1 * 60 * 1000;
int img_loopCount = 0;


void xiaozhi_init_received(const char *arg1)
{
    static uint8_t Oneime = 0;
    if (Oneime) return;
    if (strstr(arg1, "版本") != NULL) {
        Oneime = 1;
        const char *str = get_weather_data();
        xEventGroupSetBits(Red_led_Mode_queue, set_bit_button(0));
        if (str == NULL) {
            ESP_LOGE("xiaozhi_init","json decoding failed");
            return;
        }
        WeatherData = WeaPort.WeatherPort_DecodingSring(str);
        if (WeatherData == NULL) {
            ESP_LOGE("xiaozhi_init","WeatherData is NULL");
            return;
        }
        xEventGroupSetBits(epaper_groups, set_bit_button(0));
    }
}

void xiaozhi_application_received(const char *str) {
    static bool is_led_flag = false;
    strcpy(sleep_buff, str);
    if (is_led_flag) {
        if (strstr(sleep_buff, "idle") != NULL) {
            gpio_set_level((gpio_num_t) 45, 1);
            is_led_flag = false;
        }
    } else {
        if ((strstr(sleep_buff, "listening") != NULL) || (strstr(sleep_buff, "speaking") != NULL)) {
            gpio_set_level((gpio_num_t) 45, 0);
            is_led_flag = true;
        }
    }
}

void xiaozhi_ai_Message(const char *arg1, const char *arg2)
{
    // Reserved: text callback from XiaoZhi AI conversation.
    // AI image generation has been removed.
}

static void gui_user_Task(void *arg) {
    int *sdcard_doc = (int *) arg;
    ePaperDisplay.EPD_Init();
    for (;;) {
        EventBits_t even = xEventGroupWaitBits(epaper_groups, set_bit_all, pdTRUE, pdFALSE, portMAX_DELAY);
        if (pdTRUE == xSemaphoreTake(epaper_gui_semapHandle, 2000)) {
            xEventGroupSetBits(Green_led_Mode_queue, set_bit_button(6));
            Green_led_arg = 1;

            if (get_bit_button(even, 0)) {
                // Weather display
                vTaskDelay(pdMS_TO_TICKS(3000));
                char *strURL = NULL;
                WeatherAqi_t aqi_data;
                uint16_t x_or = 0;
                ePaperDisplay.EPD_SDcardBmpShakingColor("/sdcard/01_sys_init_img/00_init.bmp",0,0);
                strURL = WeaPort.WeatherPort_GetSdCardImageName(WeatherData->td_type);
                ePaperDisplay.EPD_SDcardBmpShakingColor(strURL, 86, 92);
                strURL = WeaPort.WeatherPort_GetSdCardImageName(WeatherData->tmr_type);
                ePaperDisplay.EPD_SDcardBmpShakingColor(strURL, 274, 92);
                strURL = WeaPort.WeatherPort_GetSdCardImageName(WeatherData->tdat_type);
                ePaperDisplay.EPD_SDcardBmpShakingColor(strURL, 462, 92);
                strURL = WeaPort.WeatherPort_GetSdCardImageName(WeatherData->stdat_type);
                ePaperDisplay.EPD_SDcardBmpShakingColor(strURL, 650, 92);

                ePaperDisplay.EPD_DrawStringCN(82, 34, WeatherData->td_weather, &Font14CN, ColorBlack, ColorWhite);
                ePaperDisplay.EPD_DrawStringCN(84, 58, WeatherData->td_week, &Font14CN, ColorBlack, ColorWhite);
                ePaperDisplay.EPD_DrawStringCN(74, 176, WeatherData->td_Temp, &Font14CN, ColorBlack, ColorWhite);
                x_or = WeaPort.WeatherPort_ReassignCoordinates(74, WeatherData->td_type);
                ePaperDisplay.EPD_DrawStringCN(x_or, 208, WeatherData->td_type, &Font14CN, ColorBlack, ColorWhite);
                x_or = WeaPort.WeatherPort_ReassignCoordinates(74, WeatherData->td_fx);
                ePaperDisplay.EPD_DrawStringCN(x_or, 234, WeatherData->td_fx, &Font14CN, ColorBlack, ColorWhite);
                aqi_data = WeaPort.WeatherPort_GetWeatherAQI(WeatherData->td_aqi);
                x_or = WeaPort.WeatherPort_ReassignCoordinates(74, aqi_data.str);
                ePaperDisplay.EPD_DrawStringCN(x_or, 264, aqi_data.str, &Font14CN, ColorWhite, aqi_data.color);

                ePaperDisplay.EPD_DrawStringCN(270, 34, WeatherData->tmr_weather, &Font14CN, ColorBlack, ColorWhite);
                ePaperDisplay.EPD_DrawStringCN(272, 58, WeatherData->tmr_week, &Font14CN, ColorBlack, ColorWhite);
                ePaperDisplay.EPD_DrawStringCN(262, 176, WeatherData->tmr_Temp, &Font14CN, ColorBlack, ColorWhite);
                x_or = WeaPort.WeatherPort_ReassignCoordinates(262, WeatherData->tmr_type);
                ePaperDisplay.EPD_DrawStringCN(x_or, 208, WeatherData->tmr_type, &Font14CN, ColorBlack, ColorWhite);
                x_or = WeaPort.WeatherPort_ReassignCoordinates(262, WeatherData->tmr_fx);
                ePaperDisplay.EPD_DrawStringCN(x_or, 234, WeatherData->tmr_fx, &Font14CN, ColorBlack, ColorWhite);
                aqi_data = WeaPort.WeatherPort_GetWeatherAQI(WeatherData->tmr_aqi);
                x_or = WeaPort.WeatherPort_ReassignCoordinates(262, aqi_data.str);
                ePaperDisplay.EPD_DrawStringCN(x_or, 264, aqi_data.str, &Font14CN, ColorWhite, aqi_data.color);

                ePaperDisplay.EPD_DrawStringCN(458, 34, WeatherData->tdat_weather, &Font14CN, ColorBlack, ColorWhite);
                ePaperDisplay.EPD_DrawStringCN(460, 58, WeatherData->tdat_week, &Font14CN, ColorBlack, ColorWhite);
                ePaperDisplay.EPD_DrawStringCN(450, 176, WeatherData->tdat_Temp, &Font14CN, ColorBlack, ColorWhite);
                x_or = WeaPort.WeatherPort_ReassignCoordinates(450, WeatherData->tdat_type);
                ePaperDisplay.EPD_DrawStringCN(x_or, 208, WeatherData->tdat_type, &Font14CN, ColorBlack, ColorWhite);
                x_or = WeaPort.WeatherPort_ReassignCoordinates(450, WeatherData->tdat_fx);
                ePaperDisplay.EPD_DrawStringCN(x_or, 234, WeatherData->tdat_fx, &Font14CN, ColorBlack, ColorWhite);
                aqi_data = WeaPort.WeatherPort_GetWeatherAQI(WeatherData->tdat_aqi);
                x_or = WeaPort.WeatherPort_ReassignCoordinates(450, aqi_data.str);
                ePaperDisplay.EPD_DrawStringCN(x_or, 264, aqi_data.str, &Font14CN, ColorWhite, aqi_data.color);

                ePaperDisplay.EPD_DrawStringCN(646, 34, WeatherData->stdat_weather, &Font14CN, ColorBlack, ColorWhite);
                ePaperDisplay.EPD_DrawStringCN(648, 58, WeatherData->stdat_week, &Font14CN, ColorBlack, ColorWhite);
                ePaperDisplay.EPD_DrawStringCN(638, 176, WeatherData->stdat_Temp, &Font14CN, ColorBlack, ColorWhite);
                x_or = WeaPort.WeatherPort_ReassignCoordinates(638, WeatherData->stdat_type);
                ePaperDisplay.EPD_DrawStringCN(x_or, 208, WeatherData->stdat_type, &Font14CN, ColorBlack, ColorWhite);
                x_or = WeaPort.WeatherPort_ReassignCoordinates(638, WeatherData->stdat_fx);
                ePaperDisplay.EPD_DrawStringCN(x_or, 234, WeatherData->stdat_fx, &Font14CN, ColorBlack, ColorWhite);
                aqi_data = WeaPort.WeatherPort_GetWeatherAQI(WeatherData->stdat_aqi);
                x_or = WeaPort.WeatherPort_ReassignCoordinates(638, aqi_data.str);
                ePaperDisplay.EPD_DrawStringCN(x_or, 264, aqi_data.str, &Font14CN, ColorWhite, aqi_data.color);

                ePaperDisplay.EPD_DrawStringCN(44, 367, WeatherData->calendar, &Font22CN, ColorBlack, ColorWhite);
                ePaperDisplay.EPD_DrawStringCN(118, 410, WeatherData->td_week, &Font18CN, ColorBlack, ColorWhite);

                ePaperDisplay.EPD_Display();
            } else if (get_bit_button(even, 1)) {
                // Previous image
                xEventGroupClearBits(ai_IMG_LoopGroup, 0x01);
                *sdcard_doc -= 1;
                list_node_t *sdcard_node = list_at(ListHost, *sdcard_doc);
                if (sdcard_node != NULL) {
                    CustomSDPortNode_t *sdcard_Name_node = (CustomSDPortNode_t *) sdcard_node->val;
                    SDPort->SDPort_SetCurrentlyNode(sdcard_node);
                    ePaperDisplay.EPD_SDcardScaleIMGShakingColor(sdcard_Name_node->sdcard_name,0,0);
                    ePaperDisplay.EPD_Display();
                }
            } else if (get_bit_button(even, 2)) {
                // Switch to specific image (via MCP tool)
                list_node_t *node = list_at(ListHost, -1);
                if (node != NULL) {
                    CustomSDPortNode_t *sdcard_node = (CustomSDPortNode_t *) node->val;
                    SDPort->SDPort_SetCurrentlyNode(node);
                    ePaperDisplay.EPD_SDcardScaleIMGShakingColor(sdcard_node->sdcard_name,0,0);
                    ePaperDisplay.EPD_Display();
                }
            } else if (get_bit_button(even, 3)) {
                // Image loop
                img_loopCount--;
                list_node_t *node = list_at(ListHost, img_loopCount);
                if (node != NULL) {
                    CustomSDPortNode_t *sdcard_node = (CustomSDPortNode_t *) node->val;
                    SDPort->SDPort_SetCurrentlyNode(node);
                    ePaperDisplay.EPD_SDcardScaleIMGShakingColor(sdcard_node->sdcard_name,0,0);
                    ePaperDisplay.EPD_Display();
                }
                if (img_loopCount == 0) img_loopCount = sdcard_bmp_Quantity;
            }
            xSemaphoreGive(epaper_gui_semapHandle);
            Green_led_arg = 0;
        }
    }
}

void key_wakeUp_user_Task(void *arg) {
    for (;;) {
        EventBits_t even = xEventGroupWaitBits(GP4ButtonGroups, (0x01), pdTRUE, pdFALSE, pdMS_TO_TICKS(2000));
        if (even & 0x01) {
            if (strstr(sleep_buff, "idle") != NULL) {
                gpio_set_level((gpio_num_t) 45, 0);
                std::string wake_word = "你好小智";
                Application::GetInstance().WakeWordInvoke(wake_word);
            }
        }
    }
}

void bool_long_press_sleep_Task(void *arg) {
    for (;;) {
        EventBits_t even = xEventGroupWaitBits(BootButtonGroups, (0x02), pdTRUE, pdFALSE, pdMS_TO_TICKS(2000));
        if (even & 0x02) {
            gpio_set_level((gpio_num_t) 45, 1);
        }
    }
}

char* Get_TemperatureHumidity(void) {
    float t,h;
    if(PeraPort->Shtc3_ReadTempHumi(&t,&h)) {
        snprintf(THData,40,"温度:%.2f,湿度:%.2f",t,h);
        return THData;
    }
    return NULL;
}

void ai_IMG_LoopTask(void *arg) {
    for (;;) {
        EventBits_t even = xEventGroupWaitBits(ai_IMG_LoopGroup, (0x01), pdFALSE, pdFALSE, portMAX_DELAY);
        if (get_bit_button(even, 0)) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            xEventGroupSetBits(epaper_groups, set_bit_button(3));
        }
        vTaskDelay(pdMS_TO_TICKS(img_loopTimer));
    }
}

void User_xiaozhi_app_init(void)
{
    PeraPort = new Shtc3Port(I2cBus);
    ListHost = SDPort->SDPort_GetListHost();
    gpio_set_level((gpio_num_t) 45, 0);
    ai_img_while_semap = xSemaphoreCreateBinary();
    ai_IMG_LoopGroup   = xEventGroupCreate();
    SDPort->SDPort_ScanListDir("/sdcard/05_user_ai_img");
    sdcard_bmp_Quantity = SDPort->SDPort_GetScanListValue();
    img_loopCount = sdcard_bmp_Quantity;
    xTaskCreate(gui_user_Task, "gui_user_Task", 6 * 1024, &sdcard_doc_count, 2, NULL);
    xTaskCreate(ai_IMG_LoopTask, "ai_IMG_LoopTask", 4 * 1024, NULL, 2, NULL);
    xTaskCreate(key_wakeUp_user_Task, "key_wakeUp_user_Task", 4 * 1024, NULL, 3, NULL);
    xTaskCreate(bool_long_press_sleep_Task, "bool_long_press_sleep_Task", 4 * 1024, NULL, 3, NULL);
}
