#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_AP_SSID  "PhotoFrame"
#define WIFI_AP_PASS  "12345678"
#define NVS_NS_WIFI   "wifi"

void wifi_manager_init(void);
bool wifi_manager_connect(void);
bool wifi_manager_is_connected(void);
void wifi_manager_start_ap(void);
void wifi_manager_stop_ap(void);
void wifi_manager_disconnect(void);
void wifi_manager_clear_credentials(void);
void wifi_manager_scan(char (*ssids)[33], int max, int *count);
bool wifi_manager_set_credentials(const char *ssid, const char *pass);
bool wifi_manager_get_credentials(char *ssid, size_t ssid_len, char *pass, size_t pass_len);
bool wifi_manager_apply_credentials(void);
void wifi_manager_reset(void);
void wifi_manager_button_init(void);

#ifdef __cplusplus
}
#endif
