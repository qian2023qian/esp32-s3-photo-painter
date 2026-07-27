#pragma once

#include <freertos/FreeRTOS.h>
#include "sdcard_bsp.h"
#include "traverse_nvs.h"

extern EventGroupHandle_t ServerPortGroups;


/*Only one of them can be initialized.*/
void ServerPort_NetworkAPInit(void);
uint8_t ServerPort_NetworkSTAInit(wifi_credential_t creden);

void ServerPort_init(CustomSDPort *SDPort);
void ServerPort_SetNetworkSleep(void);

uint8_t Get_NetworkMode(void);
void Mdns_init_config(void);

