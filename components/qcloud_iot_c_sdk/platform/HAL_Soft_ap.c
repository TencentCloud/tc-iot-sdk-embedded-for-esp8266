/*
 * Copyright (c) 2020 Tencent Cloud. All rights reserved.

 * Licensed under the MIT License (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://opensource.org/licenses/MIT

 * Unless required by applicable law or agreed to in writing, software distributed under the License is
 * distributed on an "AS IS" basis, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "qcloud_iot_export.h"
#include "utils_timer.h"

#include "qcloud_wifi_config.h"
#include "qcloud_wifi_config_internal.h"

#if WIFI_PROV_SOFT_AP_ENABLE

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_err.h"
#include "esp_smartconfig.h"
#include "esp_event_loop.h"
#include "tcpip_adapter.h"

static bool    sg_soft_ap_start = false;
static uint8_t sg_soft_ap_done  = WIFI_CONFIG_ING;

extern int esp_wifi_pre_init(void);

void set_soft_ap_config_result(eWiFiConfigState result)
{
    sg_soft_ap_done = result;
}

static eWiFiConfigState get_soft_ap_config_result(void)
{
    return sg_soft_ap_done;
}

static void _soft_ap_config_task(void *para)
{
    uint32_t                time_count           = WIFI_CONFIG_WAIT_TIME_MS / WIFI_CONFIG_BLINK_TIME_MS;
    WifiConfigEventCallBack wifi_config_event_cb = para;

    while ((true == sg_soft_ap_start) && (--time_count)) {
        if (WIFI_CONFIG_SUCCESS == get_soft_ap_config_result() && (true == HAL_Wifi_IsConnected())) {
            wifi_config_event_cb(EVENT_WIFI_CONFIG_SUCCESS, NULL);
            break;
        } else if (WIFI_CONFIG_FAIL == get_soft_ap_config_result()) {
            wifi_config_event_cb(EVENT_WIFI_CONFIG_FAILED, NULL);
            break;
        }

        HAL_SleepMs(WIFI_CONFIG_BLINK_TIME_MS);

        if (time_count % 10 == 0) {
            Log_d("soft_ap_task running!");
        }
    }
    if (0 == time_count) {
        wifi_config_event_cb(EVENT_WIFI_CONFIG_TIMEOUT, NULL);
    }

    return;
}

static int _soft_ap_task_start(void *params)
{
    static ThreadParams thread_params;
    thread_params.thread_func = _soft_ap_config_task;
    thread_params.thread_name = "_soft_ap_config_task";
    thread_params.stack_size  = 8192;
    thread_params.priority    = 2;
    thread_params.user_arg    = params;
    int ret                   = HAL_ThreadCreate(&thread_params);
    if (QCLOUD_RET_SUCCESS != ret) {
        Log_e("HAL_ThreadCreate(_soft_ap_task_start) failed!");
        return -1;
    }

    return 0;
}
#endif

int start_device_softAP(const char *ssid, const char *psw, uint8_t ch)
{
#if WIFI_PROV_SOFT_AP_ENABLE
    esp_err_t     rc;
    wifi_config_t wifi_config = {0};
    esp_wifi_pre_init();
    strcpy((char *)wifi_config.ap.ssid, ssid);
    wifi_config.ap.ssid_len       = strlen(ssid);
    wifi_config.ap.max_connection = 3;
    wifi_config.ap.channel        = (uint8_t)ch;
    if (psw) {
        strcpy((char *)wifi_config.ap.password, psw);
        wifi_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    } else
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;

    rc = esp_wifi_set_mode(WIFI_MODE_AP);
    if (rc != ESP_OK) {
        Log_e("esp_wifi_set_mode failed: %d", rc);
        return rc;
    }

    rc = esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config);
    if (rc != ESP_OK) {
        Log_e("esp_wifi_set_config failed: %d", rc);
        return rc;
    }
    rc = esp_wifi_start();
    if (rc != ESP_OK) {
        Log_e("esp_wifi_start failed: %d", rc);
        return rc;
    } else {
        Log_i("soft AP init success . ssid : %s, psw : %s, ch : %d", ssid, psw, ch);
    }
#endif
    return QCLOUD_RET_SUCCESS;
}

int stop_device_softAP(void)
{
    return QCLOUD_RET_SUCCESS;
}

/**
 * @brief Start softAP config
 *
 * @param params See eSimpleConfigParams
 *
 * @return 0 when success, or err code for failure
 */
int HAL_SoftApProvision_Start(void *params, WifiConfigEventCallBack event_cb)
{
    int ret = QCLOUD_RET_SUCCESS;
#if WIFI_PROV_SOFT_AP_ENABLE

    WiFiConfigParams *apConfig = (WiFiConfigParams *)params;
    (void)apConfig;

    set_soft_ap_config_result(WIFI_CONFIG_ING);

    ret = start_device_softAP(apConfig->ssid, apConfig->psw, apConfig->ch);

    if (QCLOUD_RET_SUCCESS == ret) {
        Log_d("start softAP success!");
        ret = _soft_ap_task_start(event_cb);
    }

    if (QCLOUD_RET_SUCCESS != ret) {
        HAL_SoftApProvision_Stop();
        return ret;
    }

    sg_soft_ap_start = true;
#endif
    return ret;
}

/**
 * @brief Stop WiFi config and device binding process
 */
int HAL_SoftApProvision_Stop(void)
{
    int ret = QCLOUD_RET_SUCCESS;
#if WIFI_PROV_SOFT_AP_ENABLE

    set_soft_ap_config_result(WIFI_CONFIG_FAIL);

    if (true == sg_soft_ap_start) {
        Log_i("HAL_SoftApProvision_Stop");

        sg_soft_ap_start = false;

        ret = stop_device_softAP();
    }
#endif
    return ret;
}
