/*
 * Tencent is pleased to support the open source community by making IoT Hub
 available.
 * Copyright (C) 2018-2020 THL A29 Limited, a Tencent company. All rights
 reserved.

 * Licensed under the MIT License (the "License"); you may not use this file
 except in
 * compliance with the License. You may obtain a copy of the License at
 * http://opensource.org/licenses/MIT

 * Unless required by applicable law or agreed to in writing, software
 distributed under the License is
 * distributed on an "AS IS" basis, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 KIND,
 * either express or implied. See the License for the specific language
 governing permissions and
 * limitations under the License.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qcloud_iot_export.h"
#include "qcloud_iot_import.h"

#include "qcloud_wifi_config.h"
#include "qcloud_wifi_config_internal.h"
#include "utils_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_smartconfig.h"
#include "esp_event_loop.h"
#include "tcpip_adapter.h"

extern publish_token_info_t sg_publish_token_info;

/* FreeRTOS event group to signal when we are connected & ready to make a request */
static bool sg_wifi_sta_connected = false;
// WiFi硬件是否初始化标志
static bool sg_wifi_init_done = false;

static system_event_cb_t g_cb_bck = NULL;

static char sg_ssid[MAX_SSID_LEN]      = {0};
static char sg_password[MAX_PSK_LEN]   = {0};
static char sg_ipv4_addr[MAX_IPV4_LEN] = {0};

static esp_err_t esp_wifi_event_handler(void *ctx, system_event_t *event)
{
    if (event == NULL) {
        return ESP_OK;
    }

    switch (event->event_id) {
        case SYSTEM_EVENT_STA_START:
            Log_i("SYSTEM_EVENT_STA_START");
            break;

        case SYSTEM_EVENT_STA_CONNECTED:
            Log_i("SYSTEM_EVENT_STA_CONNECTED to AP %s at channel %u", (char *)event->event_info.connected.ssid,
                  event->event_info.connected.channel);
            break;

        case SYSTEM_EVENT_STA_GOT_IP:
            Log_i("STA Got IP[%s]", ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
            strncpy(sg_ipv4_addr, ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip), MAX_IPV4_LEN);
            sg_wifi_sta_connected = true;
            break;

        case SYSTEM_EVENT_STA_DISCONNECTED:
            Log_e("SYSTEM_EVENT_STA_DISCONNECTED from AP %s reason: %u", (char *)event->event_info.disconnected.ssid,
                  event->event_info.disconnected.reason);
            sg_wifi_sta_connected = false;

            break;

        case SYSTEM_EVENT_AP_START: {
            uint8_t            channel = 0;
            wifi_second_chan_t second;
            esp_wifi_get_channel(&channel, &second);
            Log_i("SYSTEM_EVENT_AP_START at channel %u", channel);
            break;
        }

        case SYSTEM_EVENT_AP_STOP:
            Log_i("SYSTEM_EVENT_AP_STOP");
            break;

        case SYSTEM_EVENT_AP_STACONNECTED: {
            system_event_ap_staconnected_t *staconnected = &event->event_info.sta_connected;
            Log_i("SYSTEM_EVENT_AP_STACONNECTED, mac:" MACSTR ", aid:%d", MAC2STR(staconnected->mac),
                  staconnected->aid);
            break;
        }

        case SYSTEM_EVENT_AP_STADISCONNECTED: {
            system_event_ap_stadisconnected_t *stadisconnected = &event->event_info.sta_disconnected;
            Log_i("SYSTEM_EVENT_AP_STADISCONNECTED, mac:" MACSTR ", aid:%d", MAC2STR(stadisconnected->mac),
                  stadisconnected->aid);
            break;
        }

        case SYSTEM_EVENT_AP_STAIPASSIGNED:
            Log_i("SYSTEM_EVENT_AP_STAIPASSIGNED");
            break;

        default:
            Log_i("unknown event id: %d", event->event_id);
            break;
    }

    return ESP_OK;
}
void esp_smartconfig_event_cb(smartconfig_status_t status, void *pdata)
{
    switch (status) {
        case SC_STATUS_WAIT:
            Log_i("SC_STATUS_WAIT");
            break;
        case SC_STATUS_FIND_CHANNEL:
            Log_i("SC_STATUS_FINDING_CHANNEL");
            break;
        case SC_STATUS_GETTING_SSID_PSWD:
            Log_i("SC_STATUS_GETTING_SSID_PSWD");
            break;
        case SC_STATUS_LINK:
            if (pdata) {
                wifi_config_t *wifi_config = pdata;
                Log_i("SC_STATUS_LINK SSID:%s PSW:%s", wifi_config->sta.ssid, wifi_config->sta.password);
                strncpy(sg_ssid, (char *)wifi_config->sta.ssid, MAX_SSID_LEN);
                strncpy(sg_password, (char *)wifi_config->sta.password, MAX_PSK_LEN);
                HAL_Wifi_StaConnect((const char *)wifi_config->sta.ssid, (const char *)wifi_config->sta.password,
                                    wifi_config->sta.channel);

            } else {
                Log_e("invalid smart config link data");
            }
            break;
        case SC_STATUS_LINK_OVER:
            Log_w("SC_STATUS_LINK_OVER");
            if (pdata != NULL) {
                uint8_t phone_ip[4] = {0};
                memcpy(phone_ip, (uint8_t *)pdata, 4);
                Log_i("Phone ip: %d.%d.%d.%d\n", phone_ip[0], phone_ip[1], phone_ip[2], phone_ip[3]);
            }
            sg_wifi_sta_connected = true;
            break;
        default:
            break;
    }
}

int esp_wifi_pre_init(void)
{
    int rc = QCLOUD_RET_SUCCESS;
    if (!sg_wifi_init_done) {
        tcpip_adapter_init();

        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        rc                     = esp_wifi_init(&cfg);
        if (rc != ESP_OK) {
            Log_e("esp_wifi_init failed: %d", rc);
            return rc;
        }
        sg_wifi_init_done = true;
    }
    sg_wifi_sta_connected = false;

    // should disconnect first, could be failed if not connected
    rc = esp_wifi_disconnect();
    if (ESP_OK != rc) {
        Log_w("esp_wifi_disconnect failed: %d", rc);
    }

    rc = esp_wifi_stop();
    if (rc != ESP_OK) {
        Log_w("esp_wifi_stop failed: %d", rc);
    }

    if (esp_event_loop_init(esp_wifi_event_handler, NULL) && g_cb_bck == NULL) {
        Log_w("replace esp wifi event handler");
        g_cb_bck = esp_event_loop_set_cb(esp_wifi_event_handler, NULL);
    }
    return rc;
}

int HAL_Wifi_StaConnect(const char *ssid, const char *psw, uint8_t channel)
{
    Timer timer;

    Log_i("STA to connect SSID:%s PASSWORD:%s CHANNEL:%d", ssid, psw, channel);
    // TO-DO
    wifi_config_t router_wifi_config = {0};
    memset(&router_wifi_config, 0, sizeof(router_wifi_config));
    strncpy((char *)router_wifi_config.sta.ssid, ssid, 32);
    strncpy((char *)router_wifi_config.sta.password, psw, 64);

    esp_err_t rc          = ESP_OK;
    sg_wifi_sta_connected = false;
    rc                    = esp_wifi_set_mode(WIFI_MODE_STA);
    if (rc != ESP_OK) {
        Log_e("esp_wifi_set_mode failed: %d", rc);
        return rc;
    }

    rc = esp_wifi_set_config(ESP_IF_WIFI_STA, &router_wifi_config);
    if (rc != ESP_OK) {
        Log_e("esp_wifi_set_config failed: %d", rc);
        return rc;
    }

    rc = esp_wifi_connect();
    if (ESP_OK != rc) {
        Log_e("esp_wifi_connect failed: %d", rc);
        return rc;
    }
    // wait wifi sta connect
    // countdown(&timer, 20);
    // while ((false == sg_wifi_sta_connected) && !expired(&timer)) {
    //     HAL_SleepMs(100);
    // }
    // if (sg_wifi_sta_connected) {
    //     sg_publish_token_info.pairTime.wifiConnected = HAL_GetTimeMs();
    //     return QCLOUD_RET_SUCCESS;
    // }
    // return QCLOUD_ERR_GET_TIMEOUT;
    return QCLOUD_RET_SUCCESS;
}

bool HAL_Wifi_IsConnected(void)
{
    // TO-DO, Get IP is true
    return sg_wifi_sta_connected;
}

int HAL_Wifi_read_err_log(uint32_t offset, void *log, size_t log_size)
{
    Log_i("HAL_Wifi_read_err_log");

    return QCLOUD_RET_SUCCESS;
}

int HAL_Wifi_write_err_log(uint32_t offset, void *log, size_t log_size)
{
    Log_i("HAL_Wifi_write_err_log");

    return QCLOUD_RET_SUCCESS;
}

int HAL_Wifi_clear_err_log(void)
{
    Log_i("HAL_Wifi_clear_err_log");

    return QCLOUD_RET_SUCCESS;
}
int HAL_Wifi_GetLocalIP(char *ipv4_buf, int ipv4_buf_len)
{
    // TO-DO, return QCLOUD_ERR_FAILURE when invalid ip
    // return QCLOUD_RET_SUCCESS when get valid ip
    // copy ipv4 string to ipv4_buf
    strncpy(ipv4_buf, sg_ipv4_addr, ipv4_buf_len);
    return strlen(sg_ipv4_addr);
}

int HAL_Wifi_GetAP_SSID(char *ssid_buf, int ssid_buf_len)
{
    // TO-DO, return connected ap ssid len
    // invalid ssid return QCLOUD_ERR_FAILURE
    // copy ssid string to ssid_buf
    strncpy(ssid_buf, sg_ssid, ssid_buf_len);
    return strlen(sg_ssid);
}

int HAL_Wifi_GetAP_PWD(char *pwd_buf, int pwd_buf_len)
{
    // TO-DO, return connected ap password of ssid len
    // invalid password return QCLOUD_ERR_FAILURE
    // copy password string to pwd_buf
    strncpy(pwd_buf, sg_password, pwd_buf_len);
    return strlen(sg_password);
}