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

#ifndef __WIFI_CONFIG_INTERNAL_H__
#define __WIFI_CONFIG_INTERNAL_H__

#include "lwip/sockets.h"

#define SOFTAP_BOARDING_VERSION "2.0"

#define WIFI_ERR_LOG_POST
#define WIFI_LOG_UPLOAD

#define APP_SERVER_PORT            (8266)
#define LOG_SERVER_PORT            (9876)
#define SOFT_AP_BLINK_TIME_MS      (300)
#define SMART_CONFIG_BLINK_TIME_MS (600)
#define WIFI_CONFIG_WAIT_TIME_MS   (300 * 1000) /*300 seconds*/
#define WIFI_CONFIG_HALF_TIME_MS   (50 * 1000)  /*50 seconds*/

#define SELECT_WAIT_TIME_SECONDS (3)   /*seconds*/
#define WAIT_CNT_5MIN_SECONDS    (300) /*5 minutes*/

#define COMM_SERVER_TASK_NAME        "comm_server_task"
#define COMM_SERVER_TASK_STACK_BYTES 4096
#define COMM_SERVER_TASK_PRIO        1

#define SMARTCONFIG_TASK_NAME        "smartconfig_mqtt_task"
#define SMARTCONFIG_TASK_STACK_BYTES 5120
#define SMARTCONFIG_TASK_PRIO        2

#define SOFTAP_TASK_NAME        "softAP_mqtt_task"
#define SOFTAP_TASK_STACK_BYTES 5120
#define SOFTAP_TASK_PRIO        2

typedef enum {
    CMD_TOKEN_ONLY    = 0, /* Token only for smartconfig  */
    CMD_SSID_PW_TOKEN = 1, /* SSID/PW/TOKEN for softAP */
    CMD_DEVICE_REPLY  = 2, /* device reply */
    CMD_LOG_QUERY     = 3, /* app query log */
} eWiFiConfigCmd;

typedef enum {
    SUCCESS_TYPE        = 0,
    ERR_MQTT_CONNECT    = 1,
    ERR_APP_CMD         = 2,
    ERR_BD_STOP         = 3,
    ERR_OS_TASK         = 4,
    ERR_OS_QUEUE        = 5,
    ERR_WIFI_STA_INIT   = 6,
    ERR_WIFI_AP_INIT    = 7,
    ERR_WIFI_START      = 8,
    ERR_WIFI_CONFIG     = 9,
    ERR_WIFI_CONNECT    = 10,
    ERR_WIFI_DISCONNECT = 11,
    ERR_WIFI_AP_STA     = 12,
    ERR_SC_START        = 13,
    ERR_SC_DATA         = 14,
    ERR_SOCKET_OPEN     = 15,
    ERR_SOCKET_BIND     = 16,
    ERR_SOCKET_LISTEN   = 17,
    ERR_SOCKET_FCNTL    = 18,
    ERR_SOCKET_ACCEPT   = 19,
    ERR_SOCKET_RECV     = 20,
    ERR_SOCKET_SELECT   = 21,
    ERR_SOCKET_SEND     = 22,
    ERR_TOKEN_SEND      = 23,
    ERR_TOKEN_REPLY     = 24,
} eErrLogType;

typedef enum {
    ERR_SC_APP_STOP         = 101,
    ERR_SC_AT_STOP          = 102,
    ERR_SC_EXEC_TIMEOUT     = 103,
    ERR_SC_INVALID_DATA     = 104,
    ERR_APP_CMD_JSON_FORMAT = 201,
    ERR_APP_CMD_TIMESTAMP   = 202,
    ERR_APP_CMD_SIGNATURE   = 203,
    ERR_APP_CMD_AP_INFO     = 204,
    ERR_JSON_PRINT          = 205,
    ERR_COMM_TASK_ERR       = 206,
} eErrLogSubType;

typedef enum {
    CUR_ERR = 0, /* current connect error */
    PRE_ERR = 1, /* previous connect error */
} eErrRecordType;

typedef enum {
    EVENT_WAIT_TIMEOUT     = 0, /* Nothing happen but just waiting timeout */
    EVENT_WIFI_CONNECTED   = 1, /* Station has connected to target AP */
    EVENT_WIFI_DISCONNECTED = 2, /* Station has been disconnected from target AP */
    EVENT_SMARTCONFIG_STOP  = 3, /* Smartconfig stop event before connected */
} eWiFiConfigEvent;

/* unified socket type for TCP/UDP */
typedef struct {
    int              socket_id;
    struct sockaddr *socket_addr;
    socklen_t        addr_len;
} comm_peer_t;

//============================ Platform WiFi functions ===========================//
int wifi_ap_init(const char *ssid, const char *psw, uint8_t ch);
int wifi_sta_connect(const char *ssid, const char *psw);
int wifi_ap_sta_connect(const char *ssid, const char *psw);
int wifi_stop_softap(void);

int wifi_sta_init(void);
int wifi_start_smartconfig(void);
int wifi_stop_smartconfig(void);

int  wifi_wait_event(unsigned int timeout_ms);
int  wifi_start_running(void);
bool is_wifi_sta_connected(void);
//============================ Platform WiFi functions ===========================//

//============================ WiFi config error handling ===========================//
int  init_error_log_queue(void);
int  push_error_log(uint16_t err_id, int32_t err_sub_id);
bool is_config_error_happen(void);
int  app_send_error_log(comm_peer_t *peer, uint8_t record, uint16_t err_id, int32_t err_sub_id);
int  get_and_post_error_log(comm_peer_t *peer);
int  save_error_log(void);
//============================ WiFi config error handling ===========================//

//============================ WiFi running log upload ===========================//
int  init_dev_log_queue(void);
void delete_dev_log_queue(void);
int  push_dev_log(const char *func, const int line, const char *fmt, ...);
int  app_send_dev_log(comm_peer_t *peer);

#ifdef WIFI_LOG_UPLOAD
#define PUSH_LOG(fmt, ...) push_dev_log(__FUNCTION__, __LINE__, fmt, ##__VA_ARGS__)
#else
#define PUSH_LOG(...)
#endif
//============================ WiFi running log upload ===========================//

#endif  //__WIFI_CONFIG_INTERNAL_H__
