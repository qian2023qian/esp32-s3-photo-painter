#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <esp_log.h>
#include "display_bsp.h"

ePaperPort::ePaperPort(ImgDecodeDither &dither,int mosi, int scl, int dc, int cs, int rst, int busy, uint16_t width, uint16_t height,uint16_t scale_MaxWidth, uint16_t scale_MaxHeight, spi_host_device_t spihost) : 
dither_(dither),
mosi_(mosi), 
scl_(scl), 
dc_(dc), 
cs_(cs), 
rst_(rst), 
busy_(busy), 
width_(width), 
height_(height),
scale_MaxWidth_(scale_MaxWidth),
scale_MaxHeight_(scale_MaxHeight) {
    esp_err_t        ret;
    spi_bus_config_t buscfg   = {};
    int              transfer = width_ * height_;
    DisplayLen                = transfer / 2; //(1byte 2ipex)
    DispBuffer                = (uint8_t *) heap_caps_malloc(DisplayLen, MALLOC_CAP_SPIRAM);
    assert(DispBuffer);
    RotationBuffer             = (uint8_t *) heap_caps_malloc(DisplayLen, MALLOC_CAP_SPIRAM);
    assert(RotationBuffer);
    BmpSrcBuffer              = (uint8_t *) heap_caps_malloc(width_ * height_ * 3, MALLOC_CAP_SPIRAM); 
    assert(BmpSrcBuffer);
    buscfg.miso_io_num                   = -1;
    buscfg.mosi_io_num                   = mosi;
    buscfg.sclk_io_num                   = scl;
    buscfg.quadwp_io_num                 = -1;
    buscfg.quadhd_io_num                 = -1;
    buscfg.max_transfer_sz               = transfer;
    spi_device_interface_config_t devcfg = {};
    devcfg.spics_io_num                  = -1;
    devcfg.clock_speed_hz                = 40 * 1000 * 1000;    // Clock out at 40 MHz
    devcfg.mode                          = 0;                   // SPI mode 0
    devcfg.queue_size                    = 7;                   // We want to be able to queue 7 transactions at a time
    devcfg.flags                         = SPI_DEVICE_HALFDUPLEX;
    ret                                  = spi_bus_initialize(spihost, &buscfg, SPI_DMA_CH_AUTO);
    ESP_ERROR_CHECK(ret);
    ret = spi_bus_add_device(spihost, &devcfg, &spi);
    ESP_ERROR_CHECK(ret);

    gpio_config_t gpio_conf = {};
    gpio_conf.intr_type     = GPIO_INTR_DISABLE;
    gpio_conf.mode          = GPIO_MODE_OUTPUT;
    gpio_conf.pin_bit_mask  = (0x1ULL << rst_) | (0x1ULL << dc_) | (0x1ULL << cs_);
    gpio_conf.pull_down_en  = GPIO_PULLDOWN_DISABLE;
    gpio_conf.pull_up_en    = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&gpio_conf));

    gpio_conf.intr_type    = GPIO_INTR_DISABLE;
    gpio_conf.mode         = GPIO_MODE_INPUT;
    gpio_conf.pin_bit_mask = (0x1ULL << busy_);
    gpio_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_conf.pull_up_en   = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&gpio_conf));

    Set_ResetIOLevel(1);
}

ePaperPort::~ePaperPort() {
}

void ePaperPort::Set_ResetIOLevel(uint8_t level) {
    gpio_set_level((gpio_num_t) rst_, level ? 1 : 0);
}

void ePaperPort::Set_CSIOLevel(uint8_t level) {
    gpio_set_level((gpio_num_t) cs_, level ? 1 : 0);
}

void ePaperPort::Set_DCIOLevel(uint8_t level) {
    gpio_set_level((gpio_num_t) dc_, level ? 1 : 0);
}

uint8_t ePaperPort::Get_BusyIOLevel() {
    return gpio_get_level((gpio_num_t) busy_);
}

void ePaperPort::EPD_Reset(void) {
    Set_ResetIOLevel(1);
    vTaskDelay(pdMS_TO_TICKS(50));
    Set_ResetIOLevel(0);
    vTaskDelay(pdMS_TO_TICKS(20));
    Set_ResetIOLevel(1);
    vTaskDelay(pdMS_TO_TICKS(50));
}

void ePaperPort::EPD_LoopBusy(void) {
    while (1) {
        if (Get_BusyIOLevel()) {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void ePaperPort::SPI_Write(uint8_t data) {
    esp_err_t         ret;
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length    = 8;
    t.tx_buffer = &data;
    ret         = spi_device_polling_transmit(spi, &t);
    assert(ret == ESP_OK);
}

void ePaperPort::EPD_SendCommand(uint8_t Reg) {
    Set_DCIOLevel(0);
    Set_CSIOLevel(0);
    SPI_Write(Reg);
    Set_CSIOLevel(1);
}

void ePaperPort::EPD_SendData(uint8_t Data) {
    Set_DCIOLevel(1);
    Set_CSIOLevel(0);
    SPI_Write(Data);
    Set_CSIOLevel(1);
}

void ePaperPort::EPD_Sendbuffera(uint8_t *Data, int len) {
    Set_DCIOLevel(1);
    Set_CSIOLevel(0);
    esp_err_t         ret;
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    int      len_scl = len / 5000;
    int      len_dcl = len % 5000;
    uint8_t *ptr     = Data;
    while (len_scl) {
        t.length    = 8 * 5000;
        t.tx_buffer = ptr;
        ret         = spi_device_polling_transmit(spi, &t);
        assert(ret == ESP_OK);
        len_scl--;
        ptr += 5000;
    }
    t.length    = 8 * len_dcl;
    t.tx_buffer = ptr;
    ret         = spi_device_polling_transmit(spi, &t);
    assert(ret == ESP_OK);
    Set_CSIOLevel(1);
}

void ePaperPort::EPD_TurnOnDisplay(void) {

    EPD_SendCommand(0x04); // POWER_ON
    EPD_LoopBusy();

    // Second setting
    EPD_SendCommand(0x06);
    EPD_SendData(0x6F);
    EPD_SendData(0x1F);
    EPD_SendData(0x17);
    EPD_SendData(0x49);

    EPD_SendCommand(0x12); // DISPLAY_REFRESH
    EPD_SendData(0x00);
    EPD_LoopBusy();

    EPD_SendCommand(0x02); // POWER_OFF
    EPD_SendData(0X00);
    EPD_LoopBusy();
}

void ePaperPort::Set_Rotation(uint8_t rot) {
    Rotation = rot;
}

void ePaperPort::Set_Mirror(uint8_t mirr_x,uint8_t mirr_y) {
    mirrx = mirr_x;
    mirry = mirr_y;
}

void ePaperPort::EPD_Init() {
    if(isEPDInit) {
        ESP_LOGW(TAG, "EPD has already been initialized.");
        return;
    }
    EPD_Reset();
    EPD_LoopBusy();
    vTaskDelay(pdMS_TO_TICKS(50));

    EPD_SendCommand(0xAA);
    EPD_SendData(0x49);
    EPD_SendData(0x55);
    EPD_SendData(0x20);
    EPD_SendData(0x08);
    EPD_SendData(0x09);
    EPD_SendData(0x18);

    EPD_SendCommand(0x01);
    EPD_SendData(0x3F);

    EPD_SendCommand(0x00);
    EPD_SendData(0x5F);
    EPD_SendData(0x69);

    EPD_SendCommand(0x03);
    EPD_SendData(0x00);
    EPD_SendData(0x54);
    EPD_SendData(0x00);
    EPD_SendData(0x44);

    EPD_SendCommand(0x05);
    EPD_SendData(0x40);
    EPD_SendData(0x1F);
    EPD_SendData(0x1F);
    EPD_SendData(0x2C);

    EPD_SendCommand(0x06);
    EPD_SendData(0x6F);
    EPD_SendData(0x1F);
    EPD_SendData(0x17);
    EPD_SendData(0x49);

    EPD_SendCommand(0x08);
    EPD_SendData(0x6F);
    EPD_SendData(0x1F);
    EPD_SendData(0x1F);
    EPD_SendData(0x22);

    EPD_SendCommand(0x30);
    EPD_SendData(0x03);

    EPD_SendCommand(0x50);
    EPD_SendData(0x3F);

    EPD_SendCommand(0x60);
    EPD_SendData(0x02);
    EPD_SendData(0x00);

    EPD_SendCommand(0x61);
    EPD_SendData(0x03);
    EPD_SendData(0x20);
    EPD_SendData(0x01);
    EPD_SendData(0xE0);

    EPD_SendCommand(0x84);
    EPD_SendData(0x01);

    EPD_SendCommand(0xE3);
    EPD_SendData(0x2F);

    EPD_SendCommand(0x04);
    EPD_LoopBusy();
    EPD_DispClear(ColorWhite);
    isEPDInit = true;
}

void ePaperPort::EPD_DispClear(uint8_t color) {
    uint8_t *buffer = DispBuffer;
    for (int j = 0; j < DisplayLen; j++) {
        buffer[j] = (color << 4) | color;
    }
}

void ePaperPort::EPD_Display() {
    EPD_PixelRotate();
    EPD_SendCommand(0x10);
    EPD_Sendbuffera(RotationBuffer, DisplayLen);
    EPD_TurnOnDisplay();
}

void ePaperPort::EPD_SrcDisplayCopy(uint8_t *buffer,uint32_t len,uint32_t addlen) {
    if((addlen + len) > DisplayLen) {
        ESP_LOGE(TAG,"Data exceeds the buffer area.");
        return;
    }
    for(uint32_t i = 0; i < len; i++) {
        RotationBuffer[addlen + i] = buffer[i];
    }
    ESP_LOGW(TAG,"buffer: %d",addlen + len);
}

uint8_t* ePaperPort::EPD_GetIMGBuffer() {
    return DispBuffer;
}

void ePaperPort::EPD_SetPixel(uint16_t x, uint16_t y, uint16_t color) {
    if(x >= 800 || y >= 480) {
        ESP_LOGE("Pixel","Beyond the limit: (%d,%d)",x,y);
        return;
    }
    uint32_t index = (y << 8) + (y << 7) + (y << 4) + (x >> 1);
    uint8_t px = DispBuffer[index];
  
    uint8_t xor_mask = (x & 1) ? 0xF0 : 0x0F;
    uint8_t shift    = (x & 1) ? 0     : 4;

    DispBuffer[index] = (px & xor_mask) | (color << shift);
}

uint8_t* ePaperPort::EPD_ParseBMPImage(const char *path) {
    FILE *fp;
    if ((fp = fopen(path, "rb")) == NULL) {
        ESP_LOGE(TAG, "Cann't open the file!");
        return NULL;
    }
    BMPFILEHEADER bmpFileHeader;
    BMPINFOHEADER bmpInfoHeader;

    fseek(fp, 0, SEEK_SET);
    fread(&bmpFileHeader, sizeof(BMPFILEHEADER), 1, fp);  
    fread(&bmpInfoHeader, sizeof(BMPINFOHEADER), 1, fp);

    ESP_LOGW(TAG, "(WIDTH:HEIGHT) = (%ld:%ld)", bmpInfoHeader.biWidth, bmpInfoHeader.biHeight);
    src_width  = bmpInfoHeader.biWidth;
    src_height = bmpInfoHeader.biHeight;
    int readbyte = bmpInfoHeader.biBitCount;
    if (readbyte != 24) {
        ESP_LOGE(TAG, "Bmp image is not 24 bitmap!");
        fclose(fp);
        return NULL;
    }

    fseek(fp, bmpFileHeader.bOffset, SEEK_SET);
    int rowBytes = src_width * 3;
    memset(BmpSrcBuffer, 0xFF, src_width * src_height * 3);  // 填充白色，防止残缺文件残留垃圾数据
    for (int y = src_height - 1; y >= 0; y--) {
        uint8_t *rowPtr = BmpSrcBuffer + y * rowBytes;
        fread(rowPtr, 1, rowBytes, fp);
    }
    fclose(fp);
    if(src_width == 480)
    {Rotation = 3;src_width = 800;src_height = 480;}
    else 
    {Rotation = 2;}
    return BmpSrcBuffer;
}

uint8_t ePaperPort::EPD_ColorToePaperColor(uint8_t b,uint8_t g,uint8_t r) {
    if(b == 0xff && g == 0xff && r == 0xff) {
        return ColorWhite;
    }
    if(b == 0x0 && g == 0x0 && r == 0x0) {
        return ColorBlack;
    }
    if(b == 0x0 && g == 0x0 && r == 0xff) {
        return ColorRed;
    }
    if(b == 0xff && g == 0x0 && r == 0x0) {
        return ColorBlue;
    }
    if(b == 0x0 && g == 0xff && r == 0x0) {
        return ColorGreen;
    }
    if(b == 0x0 && g == 0xff && r == 0xff) {
        return ColorYellow;
    }
    return ColorWhite;
}

bool ePaperPort::EPD_SDcardBmpShakingColor(const char *path,uint16_t x_start, uint16_t y_start) {
    uint8_t r,g,b;
    uint8_t *buffer = EPD_ParseBMPImage(path);
    if(NULL == buffer) {
        return false;   // 文件缺失/损坏/非 24bit BMP：交由调用方决定是否显示提示页
    }
    uint8_t* scapeBuffer = (uint8_t*)buffer;
    for(int y = 0; y < src_height; y++) {
        for(int x = 0; x < src_width; x++) {
            int idx = (y * src_width + x) * 3;
            b = scapeBuffer[idx + 0];
            g = scapeBuffer[idx + 1];
            r = scapeBuffer[idx + 2];
            uint8_t color = EPD_ColorToePaperColor(b, g, r);
            EPD_SetPixel(x_start + x, y_start + y, color);
        }
    }
    return true;
}

void ePaperPort::EPD_SDcardIMGShakingColor(const char *path,uint16_t x_start, uint16_t y_start) {
    uint8_t *decimgbuff = NULL;
    int img_len = 0;
    uint8_t *floyd_buffer = NULL;
    int s_width;
    int s_height;
    if(strstr(path, ".jpg") || strstr(path, ".JPG")) {
        if(dither_.ImgDecode_TFOneJPGPicture(path,&decimgbuff,&img_len,&s_width,&s_height) == ESP_OK) {
            floyd_buffer = (uint8_t *) malloc(width_ * height_ * 3);       // Store the data after applying the RGB888 jitter algorithm
            assert(floyd_buffer);
            ESP_LOGW(TAG,"jpgdecode:(%d,%d)",s_width,s_height);
            dither_.ImgDecode_DitherRgb888(decimgbuff, floyd_buffer,s_width,s_height);     //The RGB888 data has undergone the jittering algorithm.
            dither_.ImgDecode_JPGBufferFree(decimgbuff); 
            if (dither_.ImgDecode_EncodingBmpToSdcard(img_to_bmpName, floyd_buffer, s_width,s_height) == ESP_OK) {
                free(floyd_buffer);
                floyd_buffer = NULL;
                EPD_SDcardBmpShakingColor(img_to_bmpName,0,0);
            } else {
                ESP_LOGE(TAG, "bmp to sdcard fill");
                free(floyd_buffer);
                floyd_buffer = NULL;
            }
        } else {
            ESP_LOGE(TAG, "jpg dec fill");
            if (decimgbuff != NULL) {
                dither_.ImgDecode_JPGBufferFree(decimgbuff);
            }
        }
    } else if(strstr(path, ".png") || strstr(path, ".PNG")) {
        if(dither_.ImgDecode_TFOnePNGPicture(path,&decimgbuff,&s_width,&s_height) == ESP_OK) {
            floyd_buffer = (uint8_t *) malloc(width_ * height_ * 3);       // Store the data after applying the RGB888 jitter algorithm
            assert(floyd_buffer);
            ESP_LOGW(TAG,"pngdecode:(%d,%d)",s_width,s_height);
            dither_.ImgDecode_DitherRgb888(decimgbuff, floyd_buffer,s_width,s_height);     //The RGB888 data has undergone the jittering algorithm.
            dither_.ImgDecode_PNGBufferFree(decimgbuff); 
            if (dither_.ImgDecode_EncodingBmpToSdcard(img_to_bmpName, floyd_buffer, s_width,s_height) == ESP_OK) {
                free(floyd_buffer);
                floyd_buffer = NULL;
                EPD_SDcardBmpShakingColor(img_to_bmpName,0,0);
            } else {
                ESP_LOGE(TAG, "bmp to sdcard fill");
                free(floyd_buffer);
                floyd_buffer = NULL;
            }
        } else {
            ESP_LOGE(TAG, "PNG dec fill");
            if (decimgbuff != NULL) {
                dither_.ImgDecode_PNGBufferFree(decimgbuff);
            }
        }
    } else if(strstr(path, ".bmp") || strstr(path, ".BMP")) {
        if(dither_.ImgDecodebmp_TFOneBMPPicture(path,&decimgbuff,&s_width,&s_height) == ESP_OK) {
            floyd_buffer = (uint8_t *) malloc(width_ * height_ * 3);       // Store the data after applying the RGB888 jitter algorithm
            assert(floyd_buffer);
            ESP_LOGW(TAG,"bmpdecode:(%d,%d)",s_width,s_height);
            dither_.ImgDecode_DitherRgb888(decimgbuff, floyd_buffer,s_width,s_height);     //The RGB888 data has undergone the jittering algorithm.
            dither_.ImgDecode_BMPBufferFree(decimgbuff); 
            if (dither_.ImgDecode_EncodingBmpToSdcard(img_to_bmpName, floyd_buffer, s_width,s_height) == ESP_OK) {
                free(floyd_buffer);
                floyd_buffer = NULL;
                EPD_SDcardBmpShakingColor(img_to_bmpName,0,0);
            } else {
                ESP_LOGE(TAG, "bmp to sdcard fill");
                free(floyd_buffer);
                floyd_buffer = NULL;
            }
        } else {
            ESP_LOGE(TAG, "BMP dec fill");
            if (decimgbuff != NULL) {
                dither_.ImgDecode_BMPBufferFree(decimgbuff);
            }
        }
    }
}

void ePaperPort::EPD_SDcardScaleIMGShakingColor(const char *path,uint16_t x_start, uint16_t y_start) {
    uint8_t *decimgbuff = NULL;
    int img_len = 0;
    uint8_t *scale_buffer = NULL;
    uint8_t *floyd_buffer = NULL;
    int s_width;
    int s_height;
    if(strstr(path, ".jpg") || strstr(path, ".JPG")) {
        if(dither_.ImgDecode_TFOneJPGPicture(path,&decimgbuff,&img_len,&s_width,&s_height) == ESP_OK) {
            ESP_LOGW(TAG,"jpgdecode:(%d,%d)",s_width,s_height);
            if((s_width > scale_MaxWidth_) || (s_height > scale_MaxHeight_)) {
                dither_.ImgDecode_JPGBufferFree(decimgbuff); 
                return;
            }
            if((480 == s_width && 800 == s_height) || (800 == s_width && 480 == s_height)) {
                floyd_buffer = (uint8_t *) malloc(width_ * height_ * 3);       // Store the data after applying the RGB888 jitter algorithm
                assert(floyd_buffer);
                dither_.ImgDecode_DitherRgb888(decimgbuff, floyd_buffer,s_width,s_height);     //The RGB888 data has undergone the jittering algorithm.
                dither_.ImgDecode_JPGBufferFree(decimgbuff); 
                if (dither_.ImgDecode_EncodingBmpToSdcard(img_to_bmpName, floyd_buffer, s_width,s_height) == ESP_OK) {
                    free(floyd_buffer);
                    floyd_buffer = NULL;
                    EPD_SDcardBmpShakingColor(img_to_bmpName,0,0);
                } else {
                    ESP_LOGE(TAG, "bmp to sdcard fill");
                    free(floyd_buffer);
                    floyd_buffer = NULL;
                }
            } else { /*拉伸缩放*/
                scale_buffer = (uint8_t *) malloc(width_ * height_ * 3);       // Store the data after applying the RGB888 jitter algorithm
                assert(scale_buffer);
                if(s_width > s_height) {
                    dither_.ImgDecode_ScaleRgb888Nearest(decimgbuff,s_width,s_height,scale_buffer,width_,height_);
                    s_width = width_;
                    s_height = height_;
                } else {
                    dither_.ImgDecode_ScaleRgb888Nearest(decimgbuff,s_width,s_height,scale_buffer,height_,width_);
                    s_width = height_;
                    s_height = width_;
                }
                dither_.ImgDecode_JPGBufferFree(decimgbuff);
                floyd_buffer = (uint8_t *) malloc(width_ * height_ * 3);       // Store the data after applying the RGB888 jitter algorithm
                assert(floyd_buffer);
                dither_.ImgDecode_DitherRgb888(scale_buffer, floyd_buffer,s_width,s_height);     //The RGB888 data has undergone the jittering algorithm.
                free(scale_buffer);
                scale_buffer = NULL;
                if (dither_.ImgDecode_EncodingBmpToSdcard(img_to_bmpName, floyd_buffer, s_width,s_height) == ESP_OK) {
                    free(floyd_buffer);
                    floyd_buffer = NULL;
                    EPD_SDcardBmpShakingColor(img_to_bmpName,0,0);
                } else {
                    ESP_LOGE(TAG, "bmp to sdcard fill");
                    free(floyd_buffer);
                    floyd_buffer = NULL;
                }
            }
        } else { /*解码失败*/
            ESP_LOGE(TAG, "jpg dec fill");
            if (decimgbuff != NULL) {
                dither_.ImgDecode_JPGBufferFree(decimgbuff);
            }
        }
    } else if(strstr(path, ".png") || strstr(path, ".PNG")) {
        if(dither_.ImgDecode_TFOnePNGPicture(path,&decimgbuff,&s_width,&s_height) == ESP_OK) {
            if((s_width > scale_MaxWidth_) || (s_height > scale_MaxHeight_)) {
                dither_.ImgDecode_PNGBufferFree(decimgbuff);
                return;
            }
            if((480 == s_width && 800 == s_height) || (800 == s_width && 480 == s_height)) { /*正常抖动显示*/
                floyd_buffer = (uint8_t *) malloc(width_ * height_ * 3);       // Store the data after applying the RGB888 jitter algorithm
                assert(floyd_buffer);
                dither_.ImgDecode_DitherRgb888(decimgbuff, floyd_buffer,s_width,s_height);     //The RGB888 data has undergone the jittering algorithm.
                dither_.ImgDecode_JPGBufferFree(decimgbuff); 
                if (dither_.ImgDecode_EncodingBmpToSdcard(img_to_bmpName, floyd_buffer, s_width,s_height) == ESP_OK) {
                    free(floyd_buffer);
                    floyd_buffer = NULL;
                    EPD_SDcardBmpShakingColor(img_to_bmpName,0,0);
                } else {
                    ESP_LOGE(TAG, "bmp to sdcard fill");
                    free(floyd_buffer);
                    floyd_buffer = NULL;
                }
            } else {   /*拉伸缩放*/
                scale_buffer = (uint8_t *) malloc(width_ * height_ * 3);       // Store the data after applying the RGB888 jitter algorithm
                assert(scale_buffer);
                if(s_width > s_height) {
                    dither_.ImgDecode_ScaleRgb888Nearest(decimgbuff,s_width,s_height,scale_buffer,width_,height_);
                    s_width = width_;
                    s_height = height_;
                } else {
                    dither_.ImgDecode_ScaleRgb888Nearest(decimgbuff,s_width,s_height,scale_buffer,height_,width_);
                    s_width = height_;
                    s_height = width_;
                }
                dither_.ImgDecode_PNGBufferFree(decimgbuff);
                floyd_buffer = (uint8_t *) malloc(width_ * height_ * 3);       // Store the data after applying the RGB888 jitter algorithm
                assert(floyd_buffer);
                dither_.ImgDecode_DitherRgb888(scale_buffer, floyd_buffer,s_width,s_height);     //The RGB888 data has undergone the jittering algorithm.
                free(scale_buffer);
                scale_buffer = NULL;
                if (dither_.ImgDecode_EncodingBmpToSdcard(img_to_bmpName, floyd_buffer, s_width,s_height) == ESP_OK) {
                    free(floyd_buffer);
                    floyd_buffer = NULL;
                    EPD_SDcardBmpShakingColor(img_to_bmpName,0,0);
                } else {
                    ESP_LOGE(TAG, "bmp to sdcard fill");
                    free(floyd_buffer);
                    floyd_buffer = NULL;
                }
            }
        } else {      /*解码失败*/
            ESP_LOGE(TAG, "PNG dec fill");
            if (decimgbuff != NULL) {
                dither_.ImgDecode_PNGBufferFree(decimgbuff);
            }
        }
    } else if(strstr(path, ".bmp") || strstr(path, ".BMP")) {
        if(dither_.ImgDecodebmp_TFOneBMPPicture(path,&decimgbuff,&s_width,&s_height) == ESP_OK) {
            if((s_width > scale_MaxWidth_) || (s_height > scale_MaxHeight_)) {
                dither_.ImgDecode_BMPBufferFree(decimgbuff);
                return;
            }
            if((480 == s_width && 800 == s_height) || (800 == s_width && 480 == s_height)) { /*正常抖动显示*/
                floyd_buffer = (uint8_t *) malloc(width_ * height_ * 3);       // Store the data after applying the RGB888 jitter algorithm
                assert(floyd_buffer);
                dither_.ImgDecode_DitherRgb888(decimgbuff, floyd_buffer,s_width,s_height);     //The RGB888 data has undergone the jittering algorithm.
                dither_.ImgDecode_JPGBufferFree(decimgbuff); 
                if (dither_.ImgDecode_EncodingBmpToSdcard(img_to_bmpName, floyd_buffer, s_width,s_height) == ESP_OK) {
                    free(floyd_buffer);
                    floyd_buffer = NULL;
                    EPD_SDcardBmpShakingColor(img_to_bmpName,0,0);
                } else {
                    ESP_LOGE(TAG, "bmp to sdcard fill");
                    free(floyd_buffer);
                    floyd_buffer = NULL;
                }
            } else {   /*拉伸缩放*/
                scale_buffer = (uint8_t *) malloc(width_ * height_ * 3);       // Store the data after applying the RGB888 jitter algorithm
                assert(scale_buffer);
                if(s_width > s_height) {
                    dither_.ImgDecode_ScaleRgb888Nearest(decimgbuff,s_width,s_height,scale_buffer,width_,height_);
                    s_width = width_;
                    s_height = height_;
                } else {
                    dither_.ImgDecode_ScaleRgb888Nearest(decimgbuff,s_width,s_height,scale_buffer,height_,width_);
                    s_width = height_;
                    s_height = width_;
                }
                dither_.ImgDecode_BMPBufferFree(decimgbuff);
                floyd_buffer = (uint8_t *) malloc(width_ * height_ * 3);       // Store the data after applying the RGB888 jitter algorithm
                assert(floyd_buffer);
                dither_.ImgDecode_DitherRgb888(scale_buffer, floyd_buffer,s_width,s_height);     //The RGB888 data has undergone the jittering algorithm.
                free(scale_buffer);
                scale_buffer = NULL;
                if (dither_.ImgDecode_EncodingBmpToSdcard(img_to_bmpName, floyd_buffer, s_width,s_height) == ESP_OK) {
                    free(floyd_buffer);
                    floyd_buffer = NULL;
                    EPD_SDcardBmpShakingColor(img_to_bmpName,0,0);
                } else {
                    ESP_LOGE(TAG, "bmp to sdcard fill");
                    free(floyd_buffer);
                    floyd_buffer = NULL;
                }
            }
        } else {
            ESP_LOGE(TAG, "BMP dec fill");
            if (decimgbuff != NULL) {
                dither_.ImgDecode_BMPBufferFree(decimgbuff);
            }
        }
    }
}

void ePaperPort::EPD_DrawStringCN(uint16_t Xstart, uint16_t Ystart, const char * pString, cFONT* font,uint16_t Color_Foreground, uint16_t Color_Background) {
    const char* p_text = pString;
    int x = Xstart, y = Ystart;
    int i, j,Num;
    uint8_t FONT_BACKGROUND = 0xff;
    /* Send the string character by character on EPD */
    while (*p_text != 0) {
        if(*p_text <= 0xE0) {  //ASCII < 126
            for(Num = 0; Num < font->size; Num++) {
                if(*p_text== font->table[Num].index[0]) {
                    const char* ptr = &font->table[Num].matrix[0];
                    for (j = 0; j < font->Height; j++) {
                        for (i = 0; i < font->Width; i++) {
                            if (FONT_BACKGROUND == Color_Background) { //this process is to speed up the scan
                                if (*ptr & (0x80 >> (i % 8))) {
                                    EPD_SetPixel(x + i, y + j, Color_Foreground);
                                }
                            } else {
                                if (*ptr & (0x80 >> (i % 8))) {
                                    EPD_SetPixel(x + i, y + j, Color_Foreground);
                                } else {
                                    EPD_SetPixel(x + i, y + j, Color_Background);
                                }
                            }
                            if (i % 8 == 7) {
                                ptr++;
                            }
                        }
                        if (font->Width % 8 != 0) {
                            ptr++;
                        }
                    }
                    break;
                }
            }
            /* Point on the next character */
            p_text += 1;
            /* Decrement the column position by 16 */
            x += font->ASCII_Width;
        } else {        //Chinese
            for(Num = 0; Num < font->size; Num++) {
                if((*p_text== font->table[Num].index[0]) && (*(p_text+1) == font->table[Num].index[1])  && (*(p_text+2) == font->table[Num].index[2])) {
                    const char* ptr = &font->table[Num].matrix[0];

                    for (j = 0; j < font->Height; j++) {
                        for (i = 0; i < font->Width; i++) {
                            if (FONT_BACKGROUND == Color_Background) { //this process is to speed up the scan
                                if (*ptr & (0x80 >> (i % 8))) {
                                    EPD_SetPixel(x + i, y + j, Color_Foreground);
                                }
                            } else {
                                if (*ptr & (0x80 >> (i % 8))) {
                                    EPD_SetPixel(x + i, y + j, Color_Foreground);
                                } else {
                                    EPD_SetPixel(x + i, y + j, Color_Background);
                                }
                            }
                            if (i % 8 == 7) {
                                ptr++;
                            }
                        }
                        if (font->Width % 8 != 0) {
                            ptr++;
                        }
                    }
                    break;
                }
            }
            /* Point on the next character */
            p_text += 3;
            /* Decrement the column position by 16 */
            x += font->Width;
        }
    }
}

uint8_t ePaperPort::EPD_GetPixel4(const uint8_t* buf, int width, int x, int y) {
    int index = y * (width >> 1) + (x >> 1);
    uint8_t byte = buf[index];
    return (x & 1) ? (byte & 0x0F) : (byte >> 4);
}

void ePaperPort::EPD_SetPixel4(uint8_t* buf, int width, int x, int y, uint8_t px) {
    int index = y * (width >> 1) + (x >> 1);
    uint8_t old = buf[index];
    if (x & 1)
        buf[index] = (old & 0xF0) | (px & 0x0F);
    else
        buf[index] = (old & 0x0F) | (px << 4);
}

void ePaperPort::EPD_PixelRotate() {
    if(Rotation == 3) {
        EPD_Rotate90CCW_Fast(DispBuffer,RotationBuffer,480,800);
    } else if(Rotation == 1) {
        EPD_Rotate90CW_Fast(DispBuffer,RotationBuffer,480,800);
    } else if(Rotation == 2) {
        EPD_Rotate180_Fast(DispBuffer,RotationBuffer,800,480);
    } else {
        memcpy(RotationBuffer, DispBuffer, DisplayLen);
    }
}

void ePaperPort::EPD_Rotate180_Fast(const uint8_t* src, uint8_t* dst, int width, int height)
{
    const int bytesPerRow = width >> 1;
    const int totalRows   = height;    
    for (int y = 0; y < totalRows; y++) {
        const uint8_t* srcRow = src + y * bytesPerRow;
        uint8_t* dstRow = dst + (totalRows - 1 - y) * bytesPerRow;
        for (int x = 0; x < bytesPerRow; x++) {
            uint8_t b = srcRow[x];
            b = (b << 4) | (b >> 4);
            dstRow[bytesPerRow - 1 - x] = b;
        }
    }
}

void ePaperPort::EPD_Rotate90CCW_Fast(const uint8_t* src, uint8_t* dst, int width, int height)
{
    const int srcBytesPerRow = width >> 1;
    for (int y = 0; y < height; y++) {
        const uint8_t* srcRow = src + y * srcBytesPerRow;
        for (int x = 0; x < width; x += 2) {

            uint8_t b = srcRow[x >> 1];
            uint8_t p0 = b >> 4;
            uint8_t p1 = b & 0x0F;
            int ny0 = width - 1 - x;
            int nx0 = y;
            int ny1 = width - 2 - x;
            int nx1 = y;
            EPD_SetPixel4(dst, height, nx0, ny0, p0);
            EPD_SetPixel4(dst, height, nx1, ny1, p1);
        }
    }
}

void ePaperPort::EPD_Rotate90CW_Fast(const uint8_t* src, uint8_t* dst, int width, int height)
{
    const int srcBytesPerRow = width >> 1;
    for (int y = 0; y < height; y++) {
        const uint8_t* srcRow = src + y * srcBytesPerRow;
        for (int x = 0; x < width; x += 2) {

            uint8_t b = srcRow[x >> 1];
            uint8_t p0 = b >> 4;  
            uint8_t p1 = b & 0x0F;
            int ny0 = x;
            int nx0 = height - 1 - y;
            int ny1 = x + 1;
            int nx1 = height - 1 - y;
            EPD_SetPixel4(dst, height, nx0, ny0, p0);
            EPD_SetPixel4(dst, height, nx1, ny1, p1);
        }
    }
}

void ePaperPort::EPD_DrawChar(uint16_t Xpoint, uint16_t Ypoint, const char Acsii_Char,sFONT* Font, uint16_t Color_Foreground, uint16_t Color_Background) {
    uint16_t Page, Column;

    if (Xpoint > width_ || Ypoint > height_) {
        ESP_LOGE(TAG,"Paint_DrawChar Input exceeds the normal display range");
        return;
    }

    uint32_t Char_Offset = (Acsii_Char - ' ') * Font->Height * (Font->Width / 8 + (Font->Width % 8 ? 1 : 0));
    const unsigned char *ptr = &Font->table[Char_Offset];

    for (Page = 0; Page < Font->Height; Page ++ ) {
        for (Column = 0; Column < Font->Width; Column ++ ) {

            //To determine whether the font background color and screen background color is consistent
            if (0XFF == Color_Background) { //this process is to speed up the scan
                if (*ptr & (0x80 >> (Column % 8)))
                    EPD_SetPixel(Xpoint + Column, Ypoint + Page, Color_Foreground);
                    // Paint_DrawPoint(Xpoint + Column, Ypoint + Page, Color_Foreground, DOT_PIXEL_DFT, DOT_STYLE_DFT);
            } else {
                if (*ptr & (0x80 >> (Column % 8))) {
                    EPD_SetPixel(Xpoint + Column, Ypoint + Page, Color_Foreground);
                    // Paint_DrawPoint(Xpoint + Column, Ypoint + Page, Color_Foreground, DOT_PIXEL_DFT, DOT_STYLE_DFT);
                } else {
                    EPD_SetPixel(Xpoint + Column, Ypoint + Page, Color_Background);
                    // Paint_DrawPoint(Xpoint + Column, Ypoint + Page, Color_Background, DOT_PIXEL_DFT, DOT_STYLE_DFT);
                }
            }
            //One pixel is 8 bits
            if (Column % 8 == 7)
                ptr++;
        }// Write a line
        if (Font->Width % 8 != 0)
            ptr++;
    }// Write all
}

void ePaperPort::EPD_DrawStringEN(uint16_t Xstart, uint16_t Ystart, const char * pString,sFONT* Font, uint16_t Color_Foreground, uint16_t Color_Background) {
    uint16_t Xpoint = Xstart;
    uint16_t Ypoint = Ystart;

    if (Xstart > width_ || Ystart > height_) {
        ESP_LOGE(TAG,"Paint_DrawString_EN Input exceeds the normal display range");
        return;
    }

    while (* pString != '\0') {
        //if X direction filled , reposition to(Xstart,Ypoint),Ypoint is Y direction plus the Height of the character
        if ((Xpoint + Font->Width ) > width_ ) {
            Xpoint = Xstart;
            Ypoint += Font->Height;
        }

        // If the Y direction is full, reposition to(Xstart, Ystart)
        if ((Ypoint  + Font->Height ) > height_ ) {
            Xpoint = Xstart;
            Ypoint = Ystart;
        }
        EPD_DrawChar(Xpoint, Ypoint, * pString, Font, Color_Background, Color_Foreground);

        //The next character of the address
        pString ++;

        //The next word of the abscissa increases the font of the broadband
        Xpoint += Font->Width;
    }
}
