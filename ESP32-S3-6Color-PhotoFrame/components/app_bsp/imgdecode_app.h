#pragma once

#include "png.h"

#pragma pack(push, 1) // Ensure that the structure is aligned at 1 byte intervals.

typedef struct {
    uint16_t bfType;      // File type must be "BM"
    uint32_t bfSize;      // File size
    uint16_t bfReserved1; // Retain
    uint16_t bfReserved2; // Retain
    uint32_t bfOffBits;   // Pixel data offset
} BITMAPFILEHEADER;

typedef struct {
    uint32_t biSize;          // Size of this structure
    int32_t  biWidth;         // Image width
    int32_t  biHeight;        // Image height (positive value indicates reverse storage)
    uint16_t biPlanes;        // Must be 1
    uint16_t biBitCount;      // Bit depth per pixel, here it is 24
    uint32_t biCompression;   // Compression method, 0 = BI_RGB
    uint32_t biSizeImage;     // Image data size (can be 0)
    int32_t  biXPelsPerMeter; // Horizontal resolution
    int32_t  biYPelsPerMeter; // Vertical resolution
    uint32_t biClrUsed;       // Number of used color indices
    uint32_t biClrImportant;  // Number of important colors
} BITMAPINFOHEADER;


#pragma pack(pop)

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} RGB888_Pixel;

class ImgDecodeDither
{
private:
    const char *TAG = "ImgDecode";
    
    int ImgDecode_NearestColor(uint8_t r, uint8_t g, uint8_t b);
    static void png_read_callback(png_structp png_ptr, png_bytep data, png_size_t length);
public:
    ImgDecodeDither();
    ~ImgDecodeDither();

    esp_err_t ImgDecode_OneJPGPicture(uint8_t *inbuffer, int inlen, uint8_t **outbuffer, int *outlen);
    esp_err_t ImgDecode_TFOneJPGPicture(const char *path,uint8_t **outbuffer, int *outlen, int *s_width, int *s_height);
    esp_err_t ImgDecode_TFOnePNGPicture(const char *png_path, uint8_t **out_rgb888,int *out_width, int *out_height);
    esp_err_t ImgDecodebmp_TFOneBMPPicture(const char *bmp_path, uint8_t **out_rgb888, int *out_width, int *out_height);
    void ImgDecode_JPGBufferFree(uint8_t *buffer);
    void ImgDecode_PNGBufferFree(uint8_t *buffer);
    void ImgDecode_BMPBufferFree(uint8_t *buffer);
    void ImgDecode_DitherRgb888(uint8_t *in_img, uint8_t *out_img, int w, int h);
    esp_err_t ImgDecode_EncodingBmpToSdcard(const char *filename, const uint8_t *inRgb, int width, int height);
    /*拉伸缩放算法*/
    void ImgDecode_ScaleRgb888Nearest(const uint8_t *src, int src_w, int src_h, uint8_t *dst, int dst_w, int dst_h);
};
