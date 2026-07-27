#include <stdio.h>
#include <string.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include "imgdecode_app.h"
#include "test_decoder.h"

#define CLAMP(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))

static const uint8_t PALETTE[6][3] = {
    {0, 0, 0},       // Black
    {255, 255, 255}, // White
    {255, 0, 0},     // Red
    {0, 255, 0},     // Green
    {0, 0, 255},     // Blue
    {255, 255, 0}    // Yellow
};

void ImgDecodeDither::png_read_callback(png_structp png_ptr, png_bytep data, png_size_t length) {
    FILE *fp = (FILE *)png_get_io_ptr(png_ptr);
    fread(data, 1, length, fp);
}

ImgDecodeDither::ImgDecodeDither() {

}

ImgDecodeDither::~ImgDecodeDither() {

}

esp_err_t ImgDecodeDither::ImgDecode_OneJPGPicture(uint8_t *inbuffer, int inlen, uint8_t **outbuffer, int *outlen) {
    if (inbuffer == NULL) {
        ESP_LOGE(TAG, "jpeg_decode fill inbuffer is NULL");
        return ESP_FAIL;
    }
    if (esp_jpeg_decode_one_picture(inbuffer, inlen, outbuffer, outlen,NULL,NULL) == JPEG_ERR_OK) {
        return ESP_OK;
    }
    return ESP_FAIL;
}

esp_err_t ImgDecodeDither::ImgDecode_TFOneJPGPicture(const char *path,uint8_t **outbuffer, int *outlen, int *s_width, int *s_height) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file: %s", path);
        return ESP_FAIL;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    if (file_size <= 0) {
        ESP_LOGE(TAG, "Invalid file size");
        fclose(f);
        return ESP_FAIL;
    }
    fseek(f, 0, SEEK_SET);
    uint8_t *buffer = (uint8_t *)malloc(480 * 800 * 3);
    assert(buffer);
    size_t bytes_read = fread(buffer, 1, file_size, f);
    fclose(f);
    if(bytes_read > 0) {
        if (esp_jpeg_decode_one_picture(buffer, bytes_read, outbuffer, outlen,s_width,s_height) == JPEG_ERR_OK) {
            free(buffer);
            buffer = NULL;
            return ESP_OK;
        } else {
            ESP_LOGE(TAG,"JPG Decode fill");
        }
    }
    if(buffer) {
        free(buffer);
        buffer = NULL;
    }
    return ESP_FAIL;
}

esp_err_t ImgDecodeDither::ImgDecode_TFOnePNGPicture(const char *png_path, uint8_t **out_rgb888,int *out_width, int *out_height) {
    FILE *fp = NULL;
    png_structp png_ptr = NULL;
    png_infop info_ptr = NULL;
    png_byte bit_depth = 0;
    png_byte color_type = 0;
    const int block_rows = 128;
    int total_rows = 0;
    int remaining_rows = 0;
    int current_y = 0;
    png_size_t row_bytes = 0;
    png_bytep *row_pointers = NULL;
    *out_rgb888 = NULL;
    *out_width = 0;
    *out_height = 0;

    fp = fopen(png_path, "rb");
    if (!fp) {
        ESP_LOGE(TAG, "Unable to open PNG file:%s", png_path);
        return ESP_FAIL;
    }

    uint8_t png_header[8];
    fread(png_header, 1, 8, fp);
    if (!png_check_sig(png_header, 8)) {
        ESP_LOGE(TAG, "Not a valid PNG file:%s", png_path);
        goto clean_up;
    }

    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) {
        ESP_LOGE(TAG, "Failed to create png_struct");
        goto clean_up;
    }

    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        ESP_LOGE(TAG, "Failed to create png_info");
        goto clean_up;
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        ESP_LOGE(TAG, "Error occurred during PNG decoding process");
        goto clean_up; 
    }

    png_set_read_fn(png_ptr, fp, png_read_callback);
    png_set_sig_bytes(png_ptr, 8); 

    png_read_info(png_ptr, info_ptr);
    *out_width = png_get_image_width(png_ptr, info_ptr);
    *out_height = png_get_image_height(png_ptr, info_ptr);
    bit_depth = png_get_bit_depth(png_ptr, info_ptr);
    color_type = png_get_color_type(png_ptr, info_ptr);
    ESP_LOGI(TAG, "PNG information: %dx%d, bit depth: %d, color type: %d",*out_width, *out_height, bit_depth, color_type);

    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
        png_set_expand_gray_1_2_4_to_8(png_ptr);
    }
    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)) {
        png_set_tRNS_to_alpha(png_ptr);
    }
    if (bit_depth == 16) {
        png_set_strip_16(png_ptr);
    }
    if (color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY) {
        png_set_add_alpha(png_ptr, 0xFF, PNG_FILLER_AFTER);
    }
    png_read_update_info(png_ptr, info_ptr);

    *out_rgb888 = (uint8_t *)heap_caps_malloc(
        *out_width * *out_height * 3, MALLOC_CAP_SPIRAM);
    if (!*out_rgb888) {
        ESP_LOGE(TAG, "Failed to allocate RGB888 cache");
        goto clean_up;
    }

    total_rows = *out_height;
    remaining_rows = total_rows;
    current_y = 0;
    row_bytes = png_get_rowbytes(png_ptr, info_ptr);

    row_pointers = (png_bytep *)heap_caps_malloc(block_rows * sizeof(png_bytep), MALLOC_CAP_SPIRAM);
    if (!row_pointers) {
        ESP_LOGE(TAG, "Allocation of pointer memory failed");
        goto clean_up;
    }

    for (int i = 0; i < block_rows; i++) {
        row_pointers[i] = (png_bytep)heap_caps_malloc(row_bytes, MALLOC_CAP_SPIRAM);
        if (!row_pointers[i]) {
            ESP_LOGE(TAG, "Failed to allocate memory for row data (line %d)", i);
            for (int j = 0; j < i; j++) {
                heap_caps_free(row_pointers[j]);
            }
            heap_caps_free(row_pointers);
            row_pointers = NULL;
            goto clean_up;
        }
    }

    while (remaining_rows > 0) {
        int decode_rows = (remaining_rows >= block_rows) ? block_rows : remaining_rows;
        png_read_rows(png_ptr, row_pointers, NULL, decode_rows);

        for (int y = 0; y < decode_rows; y++) {
            png_bytep rgba_row = row_pointers[y];
            uint8_t *rgb888_row = *out_rgb888 + (current_y + y) * *out_width * 3;
            for (int x = 0; x < *out_width; x++) {
                uint8_t R = rgba_row[x*4 + 0];
                uint8_t G = rgba_row[x*4 + 1];
                uint8_t B = rgba_row[x*4 + 2];
                rgb888_row[x*3 + 0] = R;
                rgb888_row[x*3 + 1] = G;
                rgb888_row[x*3 + 2] = B;
            }
        }

        remaining_rows -= decode_rows;
        current_y += decode_rows;
    }

    if (row_pointers != NULL) {
        for (int i = 0; i < block_rows; i++) {
            heap_caps_free(row_pointers[i]);
        }
        heap_caps_free(row_pointers);
        row_pointers = NULL;
    }

    png_read_end(png_ptr, NULL);
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    fclose(fp);

    ESP_LOGI(TAG, "PNG decoding has been completed to RGB888! Cache size: %dKB, peak PSRAM usage ≈ %dKB",
             (*out_width * *out_height * 3) / 1024,
             ((*out_width * *out_height * 3) + (block_rows * *out_width * 4)) / 1024);
    return ESP_OK;

clean_up:
    if (row_pointers != NULL) {
        for (int i = 0; i < block_rows; i++) {
            if (row_pointers[i] != NULL) {
                heap_caps_free(row_pointers[i]);
            }
        }
        heap_caps_free(row_pointers);
        row_pointers = NULL;
    }

    if (*out_rgb888 != NULL) {
        heap_caps_free(*out_rgb888);
        *out_rgb888 = NULL;
    }

    if (png_ptr != NULL) {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    }

    if (fp != NULL) {
        fclose(fp);
    }

    ESP_LOGE(TAG, "PNG decoding failed. All caches have been cleared.");
    return ESP_FAIL;
}

esp_err_t ImgDecodeDither::ImgDecodebmp_TFOneBMPPicture(const char *bmp_path, uint8_t **out_rgb888, int *out_width, int *out_height) {
    FILE *fp = fopen(bmp_path, "rb");
    if (!fp) {
        ESP_LOGE(TAG, "Cannot open BMP file: %s", bmp_path);
        return ESP_FAIL;
    }

    BITMAPFILEHEADER file_header;
    fread(&file_header, sizeof(BITMAPFILEHEADER), 1, fp);
    
    if (file_header.bfType != 0x4D42) {
        ESP_LOGE(TAG, "Not a valid BMP file! The current bfType = 0x%04X (should be 0x4D42)", file_header.bfType);
        fclose(fp);
        return ESP_FAIL;
    }

    BITMAPINFOHEADER info_header;
    fread(&info_header, sizeof(BITMAPINFOHEADER), 1, fp);
    
    if (info_header.biBitCount != 24 || info_header.biCompression != 0) {
        ESP_LOGE(TAG, "Only 24-bit uncompressed BMP is supported! Current bit depth: %d, Compression method: %d",
                 info_header.biBitCount, info_header.biCompression);
        fclose(fp);
        return ESP_FAIL;
    }

    *out_width = info_header.biWidth;
    *out_height = abs(info_header.biHeight);
    bool is_row_reverse = (info_header.biHeight > 0); 
    ESP_LOGI(TAG, "BMP information: %dx%d, row reversed: %s", *out_width, *out_height, is_row_reverse ? "Y" : "N");

    int bmp_row_bytes = ((*out_width) * 3 + 3) & ~3; 
    int rgb888_row_bytes = *out_width * 3;

    *out_rgb888 = (uint8_t *)heap_caps_malloc(
                    *out_width * *out_height * 3, MALLOC_CAP_SPIRAM);
    if (!*out_rgb888) {
        ESP_LOGE(TAG, "Failed to allocate RGB888 cache (PSRAM needs to be enabled)");
        fclose(fp);
        return ESP_FAIL;
    }

    fseek(fp, file_header.bfOffBits, SEEK_SET);

    uint8_t *bmp_row_buf = (uint8_t *)malloc(bmp_row_bytes); 
    if (!bmp_row_buf) {
        ESP_LOGE(TAG, "Failed to allocate BMP row cache");
        heap_caps_free(*out_rgb888);
        fclose(fp);
        return ESP_FAIL;
    }

    for (int y = 0; y < *out_height; y++) {
        fread(bmp_row_buf, bmp_row_bytes, 1, fp);

        int rgb888_y = is_row_reverse ? (*out_height - 1 - y) : y;
        uint8_t *rgb888_row = *out_rgb888 + rgb888_y * rgb888_row_bytes;

        for (int x = 0; x < *out_width; x++) {
            uint8_t B = bmp_row_buf[x*3 + 0]; // BMP的B通道
            uint8_t G = bmp_row_buf[x*3 + 1]; // BMP的G通道
            uint8_t R = bmp_row_buf[x*3 + 2]; // BMP的R通道

            rgb888_row[x*3 + 0] = R;
            rgb888_row[x*3 + 1] = G;
            rgb888_row[x*3 + 2] = B;
        }
    }

    free(bmp_row_buf);
    fclose(fp);

    ESP_LOGI(TAG, "BMP decoding completed to RGB888! Cache size: %dKB", (*out_width * *out_height * 3) / 1024);
    return ESP_OK;
}

void ImgDecodeDither::ImgDecode_JPGBufferFree(uint8_t *buffer) {
    if (buffer != NULL) {
        jpeg_free_align(buffer);
        buffer = NULL;
    }
}

void ImgDecodeDither::ImgDecode_PNGBufferFree(uint8_t *buffer) {
    if (buffer != NULL) {
        heap_caps_free(buffer);
        buffer = NULL;
    }
}

void  ImgDecodeDither::ImgDecode_BMPBufferFree(uint8_t *buffer) {
    if (buffer != NULL) {
        heap_caps_free(buffer);
        buffer = NULL;
    }
}

void ImgDecodeDither::ImgDecode_DitherRgb888(uint8_t *in_img, uint8_t *out_img, int w, int h) {
    uint8_t *work = (uint8_t *) malloc(w * h * 3);
    assert(work);
    if (!work)
        return;
    for (int i = 0; i < w * h * 3; i++)
        work[i] = in_img[i];

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int     idx = (y * w + x) * 3;
            uint8_t r   = work[idx + 0];
            uint8_t g   = work[idx + 1];
            uint8_t b   = work[idx + 2];

            // Find the nearest color
            int     ci = ImgDecode_NearestColor(r, g, b);
            uint8_t rr = PALETTE[ci][0];
            uint8_t gg = PALETTE[ci][1];
            uint8_t bb = PALETTE[ci][2];

            // Output result
            out_img[idx + 0] = rr;
            out_img[idx + 1] = gg;
            out_img[idx + 2] = bb;

            // Error
            int err_r = (int) r - rr;
            int err_g = (int) g - gg;
            int err_b = (int) b - bb;

            // Floyd–Steinberg diffusion
            //     *   7
            // 3   5   1
            if (x + 1 < w) {
                int n       = idx + 3;
                work[n + 0] = CLAMP(work[n + 0] + (err_r * 7) / 16, 0, 255);
                work[n + 1] = CLAMP(work[n + 1] + (err_g * 7) / 16, 0, 255);
                work[n + 2] = CLAMP(work[n + 2] + (err_b * 7) / 16, 0, 255);
            }
            if (y + 1 < h) {
                if (x > 0) {
                    int n       = ((y + 1) * w + (x - 1)) * 3;
                    work[n + 0] = CLAMP(work[n + 0] + (err_r * 3) / 16, 0, 255);
                    work[n + 1] = CLAMP(work[n + 1] + (err_g * 3) / 16, 0, 255);
                    work[n + 2] = CLAMP(work[n + 2] + (err_b * 3) / 16, 0, 255);
                }
                int n       = ((y + 1) * w + x) * 3;
                work[n + 0] = CLAMP(work[n + 0] + (err_r * 5) / 16, 0, 255);
                work[n + 1] = CLAMP(work[n + 1] + (err_g * 5) / 16, 0, 255);
                work[n + 2] = CLAMP(work[n + 2] + (err_b * 5) / 16, 0, 255);

                if (x + 1 < w) {
                    int n2       = ((y + 1) * w + (x + 1)) * 3;
                    work[n2 + 0] = CLAMP(work[n2 + 0] + (err_r * 1) / 16, 0, 255);
                    work[n2 + 1] = CLAMP(work[n2 + 1] + (err_g * 1) / 16, 0, 255);
                    work[n2 + 2] = CLAMP(work[n2 + 2] + (err_b * 1) / 16, 0, 255);
                }
            }
        }
    }

    free(work);
}

esp_err_t ImgDecodeDither::ImgDecode_EncodingBmpToSdcard(const char *filename, const uint8_t *inRgb, int width, int height) {
    FILE *f = fopen(filename, "wb");
    if (!f) {
        perror("fopen");
        return ESP_FAIL;
    }

    // Each line must be aligned at 4-byte intervals (as required by BMP)
    int row_stride = (width * 3 + 3) & ~3;
    int img_size   = row_stride * height;

    // Construct the file header
    BITMAPFILEHEADER file_header;
    file_header.bfType      = 0x4D42; // 'BM'
    file_header.bfSize      = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + img_size;
    file_header.bfReserved1 = 0;
    file_header.bfReserved2 = 0;
    file_header.bfOffBits   = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

    // Construction of information header
    BITMAPINFOHEADER info_header;
    memset(&info_header, 0, sizeof(info_header));
    info_header.biSize        = sizeof(BITMAPINFOHEADER);
    info_header.biWidth       = width;
    info_header.biHeight      = height; // Positive numbers = Stored in reverse order (from bottom to top)
    info_header.biPlanes      = 1;
    info_header.biBitCount    = 24;
    info_header.biCompression = 0; // BI_RGB
    info_header.biSizeImage   = img_size;

    // Write the file header and information header
    fwrite(&file_header, sizeof(file_header), 1, f);
    fwrite(&info_header, sizeof(info_header), 1, f);

    // Write pixel data (BMP requires BGR order, each row is aligned at 4 bytes, and written in reverse order)
    uint8_t *row_buf = (uint8_t *) malloc(row_stride);
    if (!row_buf) {
        fclose(f);
        return ESP_FAIL;
    }

    for (int y = 0; y < height; y++) {
        int            src_row = height - 1 - y; // 倒序
        const uint8_t *src     = inRgb + src_row * width * 3;

        // 转 RGB888 -> BGR888
        for (int x = 0; x < width; x++) {
            row_buf[x * 3 + 0] = src[x * 3 + 2]; // B
            row_buf[x * 3 + 1] = src[x * 3 + 1]; // G
            row_buf[x * 3 + 2] = src[x * 3 + 0]; // R
        }
        // Fill-aligned bytes
        for (int p = width * 3; p < row_stride; p++) {
            row_buf[p] = 0;
        }
        fwrite(row_buf, 1, row_stride, f);
    }

    free(row_buf);
    fclose(f);
    return ESP_OK;
}

// Find the closest color from the palette (RGB888)
int ImgDecodeDither::ImgDecode_NearestColor(uint8_t r, uint8_t g, uint8_t b) {
    int best      = 0;
    int best_dist = 999999;

    for (int i = 0; i < 6; i++) {
        int dr   = (int) r - PALETTE[i][0];
        int dg   = (int) g - PALETTE[i][1];
        int db   = (int) b - PALETTE[i][2];
        int dist = dr * dr + dg * dg + db * db;
        if (dist < best_dist) {
            best_dist = dist;
            best      = i;
        }
    }
    return best;
}

void ImgDecodeDither::ImgDecode_ScaleRgb888Nearest(const uint8_t *src, int src_w, int src_h, uint8_t *dst, int dst_w, int dst_h) {
    // 定点数缩放比例（×1024，精度1/1024，平衡精度和速度）
    const int32_t scale_x = (src_w * 1024) / dst_w;
    const int32_t scale_y = (src_h * 1024) / dst_h;

    for (int y = 0; y < dst_h; y++) {
        for (int x = 0; x < dst_w; x++) {
            // 目标像素对应原图像的定点数坐标（×1024）
            int32_t fx = x * scale_x;
            int32_t fy = y * scale_y;

            // 取4个相邻像素的整数坐标
            int x1 = fx / 1024;
            int y1 = fy / 1024;
            int x2 = x1 + 1;
            int y2 = y1 + 1;

            // 边界处理
            x2 = (x2 >= src_w) ? (src_w - 1) : x2;
            y2 = (y2 >= src_h) ? (src_h - 1) : y2;

            // 计算权重（0~1024，替代浮点0~1）
            int wx = fx - x1 * 1024; // 权重x = fx - floor(fx)
            int wy = fy - y1 * 1024; // 权重y = fy - floor(fy)
            int wx1 = 1024 - wx;
            int wy1 = 1024 - wy;

            // 计算4个相邻像素的偏移
            int off1 = (y1 * src_w + x1) * 3; // 左上
            int off2 = (y1 * src_w + x2) * 3; // 右上
            int off3 = (y2 * src_w + x1) * 3; // 左下
            int off4 = (y2 * src_w + x2) * 3; // 右下

            // 加权计算R/G/B通道（定点数运算，最后÷1024²=1048576）
            int r = (src[off1] * wx1 * wy1 + src[off2] * wx * wy1 +
                     src[off3] * wx1 * wy + src[off4] * wx * wy) / 1048576;
            int g = (src[off1+1] * wx1 * wy1 + src[off2+1] * wx * wy1 +
                     src[off3+1] * wx1 * wy + src[off4+1] * wx * wy) / 1048576;
            int b = (src[off1+2] * wx1 * wy1 + src[off2+2] * wx * wy1 +
                     src[off3+2] * wx1 * wy + src[off4+2] * wx * wy) / 1048576;

            // 限制取值范围0~255，防止溢出
            r = (r < 0) ? 0 : (r > 255) ? 255 : r;
            g = (g < 0) ? 0 : (g > 255) ? 255 : g;
            b = (b < 0) ? 0 : (b > 255) ? 255 : b;

            // 写入目标像素
            int dst_off = (y * dst_w + x) * 3;
            dst[dst_off] = r;
            dst[dst_off+1] = g;
            dst[dst_off+2] = b;
        }
    }
}
