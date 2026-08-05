#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void mqtt_ha_start(void);
void mqtt_ha_reconfigure(void);
bool mqtt_ha_enabled(void);
const char *mqtt_ha_host(void);
int  mqtt_ha_port(void);
const char *mqtt_ha_username(void);
bool mqtt_ha_connected(void);

#ifdef __cplusplus
}
#endif
