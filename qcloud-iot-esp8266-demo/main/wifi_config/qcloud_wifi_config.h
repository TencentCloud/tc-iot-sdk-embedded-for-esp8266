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

#ifndef __QCLOUD_WIFI_CONFIG_H__
#define __QCLOUD_WIFI_CONFIG_H__

typedef enum {
    WIFI_CONFIG_SUCCESS  = 0, /* WiFi config and MQTT connect success */
    WIFI_CONFIG_GOING_ON = 1, /* WiFi config and MQTT connect is going on */
    WIFI_CONFIG_FAILED   = 2, /* WiFi config and MQTT connect failed */
} eWiFiConfigState;

/**
 * @brief Start softAP WiFi config and device binding process
 *
 * @param ssid: WiFi SSID for the device softAP
 * @param psw:  WiFi password for the device softAP
 * @param ch:   WiFi channel for the device softAP, 0 for auto select
 *
 * @return 0 when success, or err code for failure
 */
int start_softAP(const char *ssid, const char *psw, uint8_t ch);

/**
 * @brief Stop softAP WiFi config and device binding process
 */
void stop_softAP(void);

/**
 * @brief Start SmartConfig WiFi config and device binding process
 *
 * @return 0 when success, or err code for failure
 */
int start_smartconfig(void);

/**
 * @brief Stop SmartConfig WiFi config and device binding process
 */
void stop_smartconfig(void);

/**
 * @brief Check if current WiFi config process is successful or not
 *
 * @return true when success, or false for failure
 */
bool is_wifi_config_successful(void);

/**
 * @brief Check current WiFi config process state
 *
 * @return eWiFiConfigState
 */
int query_wifi_config_state(void);

/**
 * @brief Start a softAP(fixed SSID/PSW) and UDP server to upload log to Wechat mini program
 *
 * @return 0 when success, or err code for failure
 */
int start_log_softAP(void);

#endif  //__QCLOUD_WIFI_CONFIG_H__
