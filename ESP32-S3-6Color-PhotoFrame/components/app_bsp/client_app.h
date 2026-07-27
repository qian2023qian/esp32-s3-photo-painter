#ifndef CLIENT_BSP_H
#define CLIENT_BSP_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 获取天气数据
 * @return 返回天气数据的JSON字符串，使用后需要调释放内存
 */
const char *get_weather_data(void);

#ifdef __cplusplus
}
#endif

#endif