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



#include <string.h>
#include <time.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "cJSON.h"

#include "esp_wifi.h"
#include "esp_smartconfig.h"
#include "esp_event_loop.h"
#include "driver/gpio.h"
#include "tcpip_adapter.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"

#include "qcloud_iot_export.h"
#include "qcloud_iot_import.h"
#include "utils_hmac.h"
#include "utils_base64.h"

#include "qcloud_iot_export.h"
#include "board_ops.h"




#define APP_SERVER_PORT             (8266)
#define SOFT_AP_BLINK_TIME          (200)
#define SMART_CONFIG_BLINK_TIME     (500)
#define WIFI_APP_WAIT_COUNT         (600)  /*5 minutes*/

#define SELECT_WAIT_TIME_SECONDS    (3)     /*seconds*/
#define WAIT_CNT_5MIN_SECONDS       (300)

#define COMM_SERVER_TASK_NAME         "comm_server_task"
#define COMM_SERVER_TASK_STACK_BYTES  4096
#define COMM_SERVER_TASK_PRIO         3

#define SMARTCONFIG_TASK_NAME         "smart_config_task"
#define SMARTCONFIG_TASK_STACK_BYTES  5120
#define SMARTCONFIG_TASK_PRIO         2

#define SOFTAP_TASK_NAME              "soft_ap_task"
#define SOFTAP_TASK_STACK_BYTES       5120
#define SOFTAP_TASK_PRIO              2

typedef enum {
    CMD_GET_INFO = 0,           /**< Get device info  */
    CMD_SEND_SSID_PW = 1,       /**< SSID and PW data recived*/
    CMD_REPORT = 2,             /**< dev report information*/
} eTcpCmdType;


/* unified socket type for TCP/UDP */
typedef struct {
    int socket_id;
    struct sockaddr *socket_addr;
    socklen_t addr_len;
} comm_peer_t;


/* control flags */
static bool g_smart_task_run = false;
static bool g_sap_task_run = false;
static bool g_comm_task_run = false;
static bool g_wifi_init_done = false;
static bool g_wifi_sta_connected = false;
static bool g_mqtt_connected = false;
static bool g_signature_sent = false;
static bool g_boarding_error = false;
static bool g_ssid_pwd_recv  = false;


/* FreeRTOS event group to signal when we are connected & ready to make a request */
static EventGroupHandle_t g_wifi_event_group;
static const int CONNECTED_BIT = BIT0;
static const int ESPTOUCH_DONE_BIT = BIT1;
static const int APSTA_DISCONNECTED_BIT = BIT2;


/**********************************************************************************/
/************** WiFi boarding error msg collect and post feature ******************/
/**********************************************************************************/
#define WIFI_ERR_LOG_POST

#ifdef WIFI_ERR_LOG_POST
/* FreeRTOS msg queue */
static void *g_err_log_queue = NULL;

static const char *g_err_log[] = {
    "Everything OK!",               /* SUCCESS_TYPE = 0,*/
    "MQTT connect error!",          /* ERR_MQTT_CONNECT = 1, */
    "APP command error!",           /* ERR_APP_CMD = 2,*/
    "WIFI boarding stop!",          /* ERR_BD_STOP = 3,*/
    "RTOS task error!",             /* ERR_OS_TASK = 4,*/
    "RTOS queue error!",            /* ERR_OS_QUEUE = 5,*/
    "WIFI STA init error!",         /* ERR_WIFI_STA_INIT = 6,*/
    "WIFI AP init error!",          /* ERR_WIFI_AP_INIT = 7,*/
    "WIFI start error!",            /* ERR_WIFI_START = 8,*/
    "WIFI config error!",           /* ERR_WIFI_CONFIG = 9,*/
    "WIFI connect error!",          /* ERR_WIFI_CONNECT = 10,*/
    "WIFI disconnect error!",       /* ERR_WIFI_DISCONNECT = 11,*/
    "WIFI AP STA error!",           /* ERR_WIFI_AP_STA = 12,*/
    "ESP smartconfig start error!", /* ERR_SC_START = 13,*/
    "ESP smartconfig data error!",  /* ERR_SC_DATA = 14, */
    "SRV socket open error!",       /* ERR_SOCKET_OPEN = 15,*/
    "SRV socket bind error!",       /* ERR_SOCKET_BIND = 16,*/
    "SRV socket listen error!",     /* ERR_SOCKET_LISTEN = 17,*/
    "SRV socket fcntl error!",      /* ERR_SOCKET_FCNTL = 18,*/
    "SRV socket accept error!",     /* ERR_SOCKET_ACCEPT = 19,*/
    "SRV socket recv error!",       /* ERR_SOCKET_RECV = 20,*/
    "SRV socket select error!",     /* ERR_SOCKET_SELECT = 21,*/
    "SRV socket send error!",       /* ERR_SOCKET_SEND = 22,*/
};
#endif

typedef enum {
    SUCCESS_TYPE = 0,
    ERR_MQTT_CONNECT = 1,
    ERR_APP_CMD = 2,
    ERR_BD_STOP = 3,
    ERR_OS_TASK = 4,
    ERR_OS_QUEUE = 5,
    ERR_WIFI_STA_INIT = 6,
    ERR_WIFI_AP_INIT = 7,
    ERR_WIFI_START = 8,
    ERR_WIFI_CONFIG = 9,
    ERR_WIFI_CONNECT = 10,
    ERR_WIFI_DISCONNECT = 11,
    ERR_WIFI_AP_STA = 12,
    ERR_SC_START = 13,
    ERR_SC_DATA = 14,
    ERR_SOCKET_OPEN = 15,
    ERR_SOCKET_BIND = 16,
    ERR_SOCKET_LISTEN = 17,
    ERR_SOCKET_FCNTL = 18,
    ERR_SOCKET_ACCEPT = 19,
    ERR_SOCKET_RECV = 20,
    ERR_SOCKET_SELECT = 21,
    ERR_SOCKET_SEND = 22,
} eErrLogType;

typedef enum {
    ERR_SC_APP_STOP = 101,
    ERR_SC_AT_STOP = 102,
    ERR_SC_EXEC_TIMEOUT = 103,
    ERR_SC_INVALID_DATA = 104,
    ERR_APP_CMD_JSON_FORMAT = 201,
    ERR_APP_CMD_TIMESTAMP = 202,
    ERR_APP_CMD_SIGNATURE = 203,
    ERR_APP_CMD_AP_INFO = 204,
    ERR_JSON_PRINT = 205,
} eErrLogSubType;

typedef enum {
    CUR_ERR = 0,        /* current connect error */
    PRE_ERR = 1,        /* previous connect error */
} eErrRecordType;

typedef struct {
    uint8_t    record;
    uint8_t    reserved;
    uint16_t   err_id;          /* error msg Id */
    int32_t    err_sub_id;      /* error msg sub Id */
} err_log_t;

typedef struct {
    uint32_t    magic_header;                           /* VALID_MAGIC_CODE for valid info */
    uint32_t    log_cnt;
    err_log_t   err_log[0];
} save_err_log_t;

#define ERR_LOG_QUEUE_SIZE 16
#define VALID_MAGIC_CODE 0xF00DBEEF
size_t g_errlog_addr  = 0x1FB000;

static int init_error_log_queue(void)
{
#ifdef WIFI_ERR_LOG_POST
    g_err_log_queue = xQueueCreate(ERR_LOG_QUEUE_SIZE, sizeof(err_log_t));
    if (g_err_log_queue == NULL) {
        Log_e("xQueueCreate failed");
        return ERR_OS_QUEUE;
    }
#endif
    return 0;
}

static int push_error_log(uint16_t err_id, int32_t err_sub_id)
{
    Log_e("error happen, err_id: %u err_sub_id: %d");

    g_boarding_error = true;

#ifdef WIFI_ERR_LOG_POST
    if (g_err_log_queue == NULL) {
        Log_e("log queue not initialized and push_error_log called");
        return ERR_OS_QUEUE;
    }

    err_log_t err_log = {.record = CUR_ERR,
                         .err_id = err_id, .err_sub_id = err_sub_id
                        };
    Log_e("Boarding error happen: %s (%u, %d)",
          g_err_log[err_log.err_id], err_log.err_id, err_log.err_sub_id);
    /* unblocking send */
    int ret = xQueueGenericSend(g_err_log_queue, &err_log, 0, queueSEND_TO_BACK);
    if (ret != pdPASS) {
        Log_e("xQueueGenericSend failed: %d", ret);
        return ERR_OS_QUEUE;
    }
#endif
    return 0;
}

static int app_send_error_log(comm_peer_t *peer, uint8_t record, uint16_t err_id, int32_t err_sub_id)
{
#ifdef WIFI_ERR_LOG_POST
    int ret;
    char msg_str[64] = {0};
    char json_str[128] = {0};

    HAL_Snprintf(msg_str, sizeof(msg_str), "%s (%u, %d)",
                 g_err_log[err_id], err_id, err_sub_id);

    cJSON * reply_json = cJSON_CreateObject();
    cJSON_AddNumberToObject(reply_json, "cmdType", CMD_REPORT);
    cJSON_AddStringToObject(reply_json, "deviceReply", record == CUR_ERR ? "Current_Error" : "Previous_Error");
    cJSON_AddStringToObject(reply_json, "log", msg_str);
    if (0 == cJSON_PrintPreallocated(reply_json, json_str, sizeof(json_str), 0)) {
        Log_e("cJSON_PrintPreallocated failed!");
        cJSON_Delete(reply_json);
        return -1;
    }
    /* append msg delimiter */
    strcat(json_str, "\r\n");

    ret = sendto(peer->socket_id, json_str, strlen(json_str), 0, peer->socket_addr, peer->addr_len);
    if (ret < 0) {
        Log_e("send error: %s", strerror(errno));
    } else
        Log_w("send error msg: %s", json_str);

    cJSON_Delete(reply_json);
    return ret;
#else
    return 0;
#endif
}

static int get_and_post_error_log(comm_peer_t *peer)
{
    int err_cnt = 0;
#ifdef WIFI_ERR_LOG_POST
    err_log_t err_msg;
    signed portBASE_TYPE rc;
    do {
        rc = xQueueReceive(g_err_log_queue, &err_msg, 0);
        if ( rc == pdPASS) {
            app_send_error_log(peer, err_msg.record, err_msg.err_id, err_msg.err_sub_id);
            if (err_msg.record == CUR_ERR)
                err_cnt++;
        }
    } while (rc == pdPASS);
#endif
    return err_cnt;
}

#ifdef WIFI_ERR_LOG_POST
#include "spi_flash.h"

static int check_err_log(uint32_t *log_cnt)
{
    esp_err_t  ret;
    uint32_t header, msg_cnt;
    ret = spi_flash_read(g_errlog_addr, (uint32_t *)&header, sizeof(uint32_t));
    if (ESP_OK != ret) {
        Log_e("spi_flash_read error: %d", ret);
        return (int)ret;
    }

    /* not a valid log */
    if (header != VALID_MAGIC_CODE) {
        Log_i("invalid magic code: 0x%x", header);
        return 0;
    }

    ret = spi_flash_read(g_errlog_addr + sizeof(uint32_t), (uint32_t *)&msg_cnt, sizeof(uint32_t));
    if (ESP_OK != ret) {
        Log_e("spi_flash_read error: %d", ret);
        return (int)ret;
    }

    *log_cnt = msg_cnt;
    return 0;
}

static int load_err_log(void *log, size_t log_size)
{
    esp_err_t  ret;
    size_t log_addr = g_errlog_addr + 2 * sizeof(uint32_t);
    ret = spi_flash_read(log_addr, (uint32_t *)log, log_size);
    if (ESP_OK != ret) {
        Log_e("spi_flash_read error: %d", ret);
        return (int)ret;
    }

    return 0;
}

static int save_err_log(void *log, size_t log_size)
{
    esp_err_t  ret;

    ret = spi_flash_erase_sector(g_errlog_addr / SPI_FLASH_SEC_SIZE);
    if (ESP_OK != ret) {
        Log_e("spi_flash_erase error: %d", ret);
        return (int)ret;
    }

    ret = spi_flash_write(g_errlog_addr, (uint32_t *)log, log_size);
    if (ESP_OK != ret) {
        Log_e("spi_flash_write error: %d", ret);
        return (int)ret;
    }

    return 0;

}

static int clear_err_log(void)
{
    esp_err_t ret = spi_flash_erase_sector(g_errlog_addr / SPI_FLASH_SEC_SIZE);
    if (ESP_OK != ret) {
        Log_e("spi_flash_erase error: %d", ret);
        return (int)ret;
    }

    return 0;

}
#endif

static int save_error_log(void)
{
#ifdef WIFI_ERR_LOG_POST
    signed portBASE_TYPE rc;
    uint32_t log_cnt = (uint32_t)uxQueueMessagesWaiting(g_err_log_queue);
    if (log_cnt == 0)
        return 0;

    size_t log_size = 2 * sizeof(uint32_t) + log_cnt * sizeof(err_log_t);
    save_err_log_t *log_src = (save_err_log_t *)HAL_Malloc(log_size);
    if (log_src == NULL) {
        Log_e("malloc failed");
        return -1;
    }

    uint32_t i = 0, j = 0;
    do {
        rc = xQueueReceive(g_err_log_queue, &log_src->err_log[i], 0);
        if ( rc == pdPASS) {
            /* only save current error */
            if (log_src->err_log[i].record == CUR_ERR) {
                Log_w("Save unhandled Current Error %u: %s (%u, %d)", j + 1, g_err_log[log_src->err_log[i].err_id],
                      log_src->err_log[i].err_id, log_src->err_log[i].err_sub_id);
                j++;
            } else {
                Log_w("Discard Previous Error: %s (%u, %d)", g_err_log[log_src->err_log[i].err_id],
                      log_src->err_log[i].err_id, log_src->err_log[i].err_sub_id);
            }
            i++;
        }
    } while (i < log_cnt && rc == pdPASS);

    vQueueDelete(g_err_log_queue);

    log_src->magic_header = VALID_MAGIC_CODE;
    log_src->log_cnt = j;
    log_size = 2 * sizeof(uint32_t) + log_src->log_cnt * sizeof(err_log_t);
    if (save_err_log(log_src, log_size)) {
        Log_e("save error log to flash failed");
        return -1;
    }
#endif
    return 0;
}

static int handle_saved_error_log(void)
{
#ifdef WIFI_ERR_LOG_POST
    uint32_t log_cnt = 0;
    if (check_err_log(&log_cnt)) {
        Log_e("check error log cnt failed");
        return -1;
    }

    if (log_cnt == 0)
        return 0;

    if (log_cnt > ERR_LOG_QUEUE_SIZE) {
        Log_e("invalid log cnt: %u", log_cnt);
        return -1;
    }

    size_t log_size = log_cnt * sizeof(err_log_t);
    err_log_t *log_src = (err_log_t *)HAL_Malloc(log_size);
    if (load_err_log(log_src, log_size)) {
        Log_e("load error log failed");
        return -1;
    }

    clear_err_log();

    uint32_t i = 0;
    do {
        /* change the record type to previous error */
        log_src[i].record = PRE_ERR;
        Log_i("Handle Previous Error: %s (%u, %d)",
              g_err_log[log_src[i].err_id], log_src[i].err_id, log_src[i].err_sub_id);

        /* unblocking send */
        int ret = xQueueGenericSend(g_err_log_queue, &log_src[i], 0, queueSEND_TO_BACK);
        if (ret != pdPASS) {
            Log_e("xQueueGenericSend failed: %d", ret);
            return ERR_OS_QUEUE;
        }
        i++;
    } while (i < log_cnt);
#endif
    return 0;
}
/**********************************************************************************/
/************** WiFi boarding error msg collect and post feature ******************/
/**********************************************************************************/




static void _mqtt_event_handler(void *pclient, void *handle_context, MQTTEventMsg *msg)
{

    switch (msg->event_type) {
        case MQTT_EVENT_UNDEF:
            Log_i("undefined event occur.");
            break;

        case MQTT_EVENT_DISCONNECT:
            Log_i("MQTT disconnect.");
            break;

        case MQTT_EVENT_RECONNECT:
            Log_i("MQTT reconnect.");
            break;

        default:
            Log_i("Should NOT arrive here. Event: %d", msg->event_type);
            break;
    }
}


int do_one_mqtt_connect(void)
{
    DeviceInfo  devinfo;
    int ret = HAL_GetDevInfo(&devinfo);
    if (ret) {
        Log_e("Load device info from flash error");
        return ret;
    }


    //init connection
    MQTTInitParams init_params = DEFAULT_MQTTINIT_PARAMS;
    init_params.device_name = devinfo.device_name;
    init_params.product_id = devinfo.product_id;
    init_params.device_secret = devinfo.device_secret;
    init_params.event_handle.h_fp = _mqtt_event_handler;
    init_params.event_handle.context = NULL;

    void *client = IOT_MQTT_Construct(&init_params);
    if (client != NULL) {
        Log_i("Cloud Device Construct Success");
    } else {
        Log_e("Cloud Device Construct Failed");
        ret = IOT_MQTT_GetErrCode();
        return ret;
    }

    g_mqtt_connected = true;
    IOT_MQTT_Yield(client, 500);

    if (client)
        IOT_MQTT_Destroy(&client);

    return 0;
}

//============================ ESP wifi functions begin ===========================//

static system_event_cb_t g_cb_bck = NULL;

static esp_err_t _wifi_event_handler(void* ctx, system_event_t* event)
{
    if (event == NULL) {
        return ESP_OK;
    }

    switch (event->event_id) {
        case SYSTEM_EVENT_STA_START:
            Log_i("SYSTEM_EVENT_STA_START");
            break;

        case SYSTEM_EVENT_STA_CONNECTED:
            Log_i("SYSTEM_EVENT_STA_CONNECTED to AP %s at channel %u",
                  (char *)event->event_info.connected.ssid, event->event_info.connected.channel);
            break;

        case SYSTEM_EVENT_STA_GOT_IP:
            Log_i("STA Got IPv4[%s]", ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));

            xEventGroupSetBits(g_wifi_event_group, CONNECTED_BIT);
            g_wifi_sta_connected = true;
            break;

        case SYSTEM_EVENT_STA_DISCONNECTED:
            Log_e("SYSTEM_EVENT_STA_DISCONNECTED from AP %s reason: %u",
                  (char *)event->event_info.disconnected.ssid, event->event_info.disconnected.reason);
            xEventGroupClearBits(g_wifi_event_group, CONNECTED_BIT);
            g_wifi_sta_connected = false;
            if (g_ssid_pwd_recv)
                push_error_log(ERR_WIFI_CONNECT, event->event_info.disconnected.reason);
            break;

        case SYSTEM_EVENT_AP_START: {
            uint8_t channel = 0;
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
            Log_i("SYSTEM_EVENT_AP_STACONNECTED, mac:" MACSTR ", aid:%d", \
                  MAC2STR(staconnected->mac), staconnected->aid);
            break;
        }

        case SYSTEM_EVENT_AP_STADISCONNECTED: {
            xEventGroupSetBits(g_wifi_event_group, APSTA_DISCONNECTED_BIT);
            system_event_ap_stadisconnected_t *stadisconnected = &event->event_info.sta_disconnected;
            Log_i("SYSTEM_EVENT_AP_STADISCONNECTED, mac:" MACSTR ", aid:%d", \
                  MAC2STR(stadisconnected->mac), stadisconnected->aid);
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

static int wifi_ap_init(const char *ssid, const char *psw, uint8_t ch)
{
    esp_err_t rc;
    if (!g_wifi_init_done) {
        tcpip_adapter_init();
        g_wifi_event_group = xEventGroupCreate();
        if (g_wifi_event_group == NULL) {
            Log_e("xEventGroupCreate failed!");
            return ESP_ERR_NO_MEM;
        }

        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        rc = esp_wifi_init(&cfg);
        if (rc != ESP_OK) {
            Log_e("esp_wifi_init failed: %d", rc);
            return rc;
        }
        g_wifi_init_done = true;
    }

    xEventGroupClearBits(g_wifi_event_group, CONNECTED_BIT | ESPTOUCH_DONE_BIT | APSTA_DISCONNECTED_BIT);

    //should disconnect first, could be failed if not connected
    rc = esp_wifi_disconnect();
    if ( ESP_OK != rc ) {
        Log_e("esp_wifi_disconnect failed: %d", rc);
        //return rc;
    }

    rc = esp_wifi_stop();
    if (rc != ESP_OK) {
        Log_e("esp_wifi_stop failed: %d", rc);
        //return rc;
    }

    if (esp_event_loop_init(_wifi_event_handler, NULL) && g_cb_bck == NULL) {
        Log_w("replace esp wifi event handler");
        g_cb_bck = esp_event_loop_set_cb(_wifi_event_handler, NULL);
    }

    rc = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (rc != ESP_OK) {
        Log_e("esp_wifi_set_storage failed: %d", rc);
        return rc;
    }

    wifi_config_t wifi_config = {0};
    strcpy((char *)wifi_config.ap.ssid, ssid);
    strcpy((char *)wifi_config.ap.password, psw);
    wifi_config.ap.ssid_len = strlen(ssid);
    wifi_config.ap.max_connection = 3;
    wifi_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    wifi_config.ap.channel = (uint8_t)ch;

    rc = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (rc != ESP_OK) {
        Log_e("esp_wifi_set_mode failed: %d", rc);
        return rc;
    }

    rc = esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config);
    if (rc != ESP_OK) {
        Log_e("esp_wifi_set_config failed: %d", rc);
        return rc;
    }

    return ESP_OK;

}

static int wifi_sta_init(void)
{
    esp_err_t rc;
    if (!g_wifi_init_done) {
        tcpip_adapter_init();
        g_wifi_event_group = xEventGroupCreate();
        if (g_wifi_event_group == NULL) {
            Log_e("xEventGroupCreate failed!");
            return ESP_ERR_NO_MEM;
        }

        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        rc = esp_wifi_init(&cfg);
        if (rc != ESP_OK) {
            Log_e("esp_wifi_init failed: %d", rc);
            return rc;
        }
        g_wifi_init_done = true;
    }

    xEventGroupClearBits(g_wifi_event_group, CONNECTED_BIT | ESPTOUCH_DONE_BIT | APSTA_DISCONNECTED_BIT);

    rc = esp_wifi_stop();
    if (rc != ESP_OK) {
        Log_e("esp_wifi_stop failed: %d", rc);
        return rc;
    }

    if (esp_event_loop_init(_wifi_event_handler, NULL) && g_cb_bck == NULL) {
        Log_w("replace esp wifi event handler");
        g_cb_bck = esp_event_loop_set_cb(_wifi_event_handler, NULL);
    }

    rc = esp_wifi_set_storage(WIFI_STORAGE_FLASH);
    if (rc != ESP_OK) {
        Log_e("esp_wifi_set_storage failed: %d", rc);
        return rc;
    }

    rc = esp_wifi_set_mode(WIFI_MODE_STA);
    if (rc != ESP_OK) {
        Log_e("esp_wifi_set_mode failed: %d", rc);
        return rc;
    }

    return ESP_OK;
}

static int wifi_ap_sta_connect(wifi_config_t *config)
{
    esp_err_t rc = ESP_OK;

    rc = esp_wifi_set_storage(WIFI_STORAGE_FLASH);
    if (rc != ESP_OK) {
        Log_e("esp_wifi_set_storage failed: %d", rc);
        return rc;
    }

    rc = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (rc != ESP_OK) {
        Log_e("esp_wifi_set_mode failed: %d", rc);
        return rc;
    }

    rc = esp_wifi_set_config(ESP_IF_WIFI_STA, config);
    if (rc != ESP_OK) {
        Log_e("esp_wifi_set_config failed: %d", rc);
        return rc;
    }

    rc = esp_wifi_connect();
    if ( ESP_OK != rc ) {
        Log_e("esp_wifi_connect failed: %d", rc);
        return rc;
    }

    return 0;

}

//============================ Qcloud app TCP/UDP functions begin ===========================//
static int app_reply_msg(comm_peer_t *peer, char *msg_str)
{
    int ret;
    char json_str[128] = {0};
    cJSON * reply_json = cJSON_CreateObject();
    cJSON_AddNumberToObject(reply_json, "cmdType", CMD_REPORT);
    cJSON_AddStringToObject(reply_json, "deviceReply", msg_str);

    if (0 == cJSON_PrintPreallocated(reply_json, json_str, sizeof(json_str), 0)) {
        Log_e("cJSON_PrintPreallocated failed!");
        cJSON_Delete(reply_json);
        return -1;
    }
    /* append msg delimiter */
    strcat(json_str, "\r\n");
    ret = sendto(peer->socket_id, json_str, strlen(json_str), 0, peer->socket_addr, peer->addr_len);
    if (ret < 0) {
        Log_e("send error: %s", strerror(errno));
    } else
        Log_d("send reply msg: %s", json_str);

    cJSON_Delete(reply_json);

    return ret;
}

static int calc_device_sign(char* sign_out, int timestamp, char *conn_id, char *product_id, char *device_name, char *device_secret)
{
#define CONTENT_SIZE 128
#define DECODE_PSK_LENGTH 48
    unsigned char psk_str[DECODE_PSK_LENGTH] = {0};
    char content[CONTENT_SIZE + 1] = {0};
    int rc;
    size_t len = 0;

    HAL_Snprintf(content, sizeof(content), "DeviceName=%s&DeviceTimestamp=%d&ProductId=%s&ConnId=%s",
                 device_name, timestamp, product_id, conn_id);

    rc = qcloud_iot_utils_base64decode(psk_str, sizeof( psk_str ), &len, (unsigned char *)device_secret, strlen(device_secret) );
    if (rc != QCLOUD_RET_SUCCESS) {
        Log_e("Device secret decode err, secret:%s", device_secret);
        return -1;
    }

    utils_hmac_sha1(content, strlen(content), sign_out, (char *)psk_str, len);

    Log_i(">>>content: %s signature: %s", content, sign_out);

    return 0;
}

#define MAX_CONN_ID_LEN 6
static void _get_next_conn_id(char *conn_id)
{
    int i;
    srand((unsigned)HAL_GetTimeMs());
    for (i = 0; i < MAX_CONN_ID_LEN - 1; i++) {
        int flag = rand() % 3;
        switch (flag) {
            case 0:
                conn_id[i] = (rand() % 26) + 'a';
                break;
            case 1:
                conn_id[i] = (rand() % 26) + 'A';
                break;
            case 2:
                conn_id[i] = (rand() % 10) + '0';
                break;
        }
    }

    conn_id[MAX_CONN_ID_LEN - 1] = '\0';
}

static int app_reply_signature(comm_peer_t *peer, double time_value)
{
#define SIGNATURE_SIZE 40

    int ret;
    char conn_id[MAX_CONN_ID_LEN] = {"happy"};

    //if (!get_module_info()->use_fixed_connid)
    _get_next_conn_id(conn_id);

    DeviceInfo  devinfo;
    ret = HAL_GetDevInfo(&devinfo);
    if (ret) {
        Log_e("load dev info failed: %d", ret);
        app_send_error_log(peer, CUR_ERR, ERR_APP_CMD, ret);
        return -1;
    }

    char signature[SIGNATURE_SIZE + 1] = {0};
    ret = calc_device_sign(signature, (int)time_value, conn_id,
                           devinfo.product_id, devinfo.device_name, devinfo.device_secret);
    if (ret) {
        Log_e("calc signature failed: %d", ret);
        app_send_error_log(peer, CUR_ERR, ERR_APP_CMD, ERR_APP_CMD_SIGNATURE);
        return -1;
    }
    strcpy(devinfo.device_secret, "null");

    cJSON * reply_json = cJSON_CreateObject();
    cJSON_AddNumberToObject(reply_json, "cmdType", CMD_REPORT);
    cJSON_AddStringToObject(reply_json, "productId", devinfo.product_id);
    cJSON_AddStringToObject(reply_json, "deviceName", devinfo.device_name);
    cJSON_AddStringToObject(reply_json, "connId", conn_id);
    cJSON_AddStringToObject(reply_json, "signature", signature);
    cJSON_AddNumberToObject(reply_json, "timestamp", time_value);
    cJSON_AddStringToObject(reply_json, "wifiState", g_wifi_sta_connected ? "connected" : "disconnected");
    cJSON_AddStringToObject(reply_json, "mqttState", g_mqtt_connected ? "connected" : "unconnected");

    char json_str[256] = {0};
    if (0 == cJSON_PrintPreallocated(reply_json, json_str, sizeof(json_str), 0)) {
        Log_e("cJSON_PrintPreallocated failed!");
        cJSON_Delete(reply_json);
        app_send_error_log(peer, CUR_ERR, ERR_APP_CMD, ERR_JSON_PRINT);
        return -1;
    }
    /* append msg delimiter */
    strcat(json_str, "\r\n");
    cJSON_Delete(reply_json);

    int udp_resend_cnt = 5;
udp_resend:
    ret = sendto(peer->socket_id, json_str, strlen(json_str), 0, peer->socket_addr, peer->addr_len);
    if (ret < 0) {
        Log_e("send error: %s", strerror(errno));
        push_error_log(ERR_SOCKET_SEND, errno);
        return -1;
    }
    /* UDP packet could be lost, send it again */
    /* NOT necessary for TCP */
    if (peer->socket_addr != NULL && udp_resend_cnt--) {
        HAL_SleepMs(1000);
        goto udp_resend;
    }

    HAL_Printf("Report signature: %s", json_str);
    g_signature_sent = true;
    return ret;
}


/* handle cmd data from app
 * return value:
 * -1: something wrong
 * 0:  NO error but need to wait for next cmd
 * 1: Everything OK and we've finished the job
 */
static int app_handle_recv_data(comm_peer_t *peer, char *pdata, int len)
{
    int ret;
    cJSON *root = cJSON_Parse(pdata);
    if (!root) {
        Log_e("Parsing JSON Error: [%s]", cJSON_GetErrorPtr());
        app_send_error_log(peer, CUR_ERR, ERR_APP_CMD, ERR_APP_CMD_JSON_FORMAT);
        return -1;
    }

    cJSON *cmd_json = cJSON_GetObjectItem(root, "cmdType");
    if (cmd_json == NULL) {
        Log_e("Invalid cmd JSON: %s", pdata);
        cJSON_Delete(root);
        app_send_error_log(peer, CUR_ERR, ERR_APP_CMD, ERR_APP_CMD_JSON_FORMAT);
        return -1;
    }

    int cmd = cmd_json->valueint;
    switch (cmd) {
        case CMD_GET_INFO: {
            cJSON *timestamp_json = cJSON_GetObjectItem(root, "timestamp");
            if (timestamp_json == NULL) {
                Log_e("invlaid timestamp!");
                cJSON_Delete(root);
                app_send_error_log(peer, CUR_ERR, ERR_APP_CMD, ERR_APP_CMD_TIMESTAMP);
                return -1;
            }
            double time_value = timestamp_json->valuedouble;
            cJSON_Delete(root);

            if (g_smart_task_run || g_sap_task_run) {
                /* wait for WiFi/MQTT connect event */
                uint32_t mqtt_cnt = 5;
                while ((!g_mqtt_connected) && (mqtt_cnt--)) {
                    HAL_SleepMs(1000);
                }
            }

            ret = app_reply_signature(peer, time_value);
            if (ret < 0) {
                Log_e("reply signature failed: %d", ret);
                return ret;
            }

            /* 0:  NO error but need to wait for next cmd
                 * 1: Everything OK and we've finished the job */
            if (g_mqtt_connected)
                return 1;
            else
                return 0;
        }
        break;

        case CMD_SEND_SSID_PW: {
            cJSON *ssid_json = cJSON_GetObjectItem(root, "ssid");
            cJSON *psw_json = cJSON_GetObjectItem(root, "password");

            if (ssid_json && psw_json) {
                wifi_config_t router_wifi_config = {0};
                memset(&router_wifi_config, 0, sizeof(router_wifi_config));
                strcpy((char *)router_wifi_config.sta.ssid, ssid_json->valuestring);
                strcpy((char *)router_wifi_config.sta.password, psw_json->valuestring);
                cJSON_Delete(root);
                app_reply_msg(peer, "dataRecived");

                if (!g_ssid_pwd_recv) {
                    Log_i("APSTA to connect SSID:%s PASSWORD:%s", router_wifi_config.sta.ssid, router_wifi_config.sta.password);

                    ret = wifi_ap_sta_connect(&router_wifi_config);
                    if (ret) {
                        Log_e("wifi_ap_sta_connect failed: %d", ret);
                        app_send_error_log(peer, CUR_ERR, ERR_WIFI_AP_STA, ret);
                        return -1;
                    }
                    g_ssid_pwd_recv = true;
                }

                /* 0:  NO error but need to wait for next cmd
                      * 1: Everything OK and we've finished the job */
                if (g_signature_sent)
                    return 1;
                else
                    return 0;
            } else {
                cJSON_Delete(root);
                Log_e("invlaid ssid or password!");
                app_send_error_log(peer, CUR_ERR, ERR_APP_CMD, ERR_APP_CMD_AP_INFO);
                return -1;
            }
        }
        break;

        default: {
            cJSON_Delete(root);
            Log_e("Unknown cmd: %d", cmd);
            app_send_error_log(peer, CUR_ERR, ERR_APP_CMD, ERR_APP_CMD_JSON_FORMAT);
        }
        break;
    }

    return -1;
}

static int tcp_client_data_loop(comm_peer_t *peer, char *client_addr)
{
    fd_set sets;
    uint32_t read_cnt = WAIT_CNT_5MIN_SECONDS / SELECT_WAIT_TIME_SECONDS;
    while (g_comm_task_run && read_cnt--) {
        char rx_buffer[256] = {0};
        FD_ZERO(&sets);
        FD_SET(peer->socket_id, &sets);
        struct timeval timeout;
        timeout.tv_sec = SELECT_WAIT_TIME_SECONDS;
        timeout.tv_usec = 0;

        get_and_post_error_log(peer);

        int ret = select(peer->socket_id + 1, &sets, NULL, NULL, &timeout);
        if (ret > 0) {
            int len = recv(peer->socket_id, rx_buffer, sizeof(rx_buffer) - 1, 0);
            // Error occured during receiving
            if (len < 0) {
                Log_e("recv failed: errno %d", errno);
                app_send_error_log(peer, CUR_ERR, ERR_SOCKET_RECV, errno);
                return -1;
            }
            // Connection closed
            else if (len == 0) {
                Log_w("Connection closed is by peer: %s", client_addr);
                return 0;
            }
            // Data received
            else {
                rx_buffer[len] = 0; // Null-terminate whatever we received and treat like a string
                Log_i("Received %d bytes msg: %s", len, rx_buffer);

                ret = app_handle_recv_data(peer, rx_buffer, len);
                if (ret == 1) {
                    Log_w("Finish app cmd handling.");

                    return 1;
                } else if (ret < 0) {
                    Log_e("Error occured during handle rx msg: %d", ret);
                }
                ret = get_and_post_error_log(peer);
                if (ret)
                    return -1;

                continue;
            }
        } else if (0 == ret) {
            Log_d("wait for read...");
            ret = get_and_post_error_log(peer);
            if (ret)
                return -1;
            continue;
        } else {
            Log_e("select-recv error: %d", errno);
            app_send_error_log(peer, CUR_ERR, ERR_SOCKET_SELECT, errno);
            return -1;
        }
    }

    if (read_cnt == 0) {
        Log_e("client loop timeout!");
        return -1;
    }

    return 0;
}

static int tcp_init_server_socket(int *socket_ret)
{
    int ret, server_socket = -1;
    struct sockaddr_in server_addr;

    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(APP_SERVER_PORT);

    server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (server_socket < 0) {
        Log_e("Unable to create socket: errno %d", errno);
        push_error_log(ERR_SOCKET_OPEN, errno);
        *socket_ret = -1;
        return -1;
    }

    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        Log_e("Set socket option failed: errno %d", errno);
        push_error_log(ERR_SOCKET_OPEN, errno);
        *socket_ret = -1;
        return -1;
    }

    *socket_ret = server_socket;
    ret = bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (ret != 0) {
        Log_e("Socket unable to bind: errno %d", errno);
        push_error_log(ERR_SOCKET_BIND, errno);
        return -1;
    }

    ret = listen(server_socket, 1);
    if (ret != 0) {
        Log_e("Error occured during listen: errno %d", errno);
        push_error_log(ERR_SOCKET_LISTEN, errno);
        return -1;
    }

    int flags = fcntl(server_socket, F_GETFL, 0);
    ret = fcntl(server_socket, F_SETFL, flags | O_NONBLOCK);
    if (ret == -1) {
        Log_e("fcntl tcp socket failed: errno %d", errno);
        push_error_log(ERR_SOCKET_FCNTL, errno);
        return -1;
    }

    return 0;
}

static void tcp_server_task(void *pvParameters)
{
    int ret, server_socket = -1;
    char addr_str[128] = {0};
    uint32_t server_count = WIFI_APP_WAIT_COUNT + 60; /* stay longer than 5 minutes to handle error log */

    ret = tcp_init_server_socket(&server_socket);
    if (ret < 0) {
        Log_e("Init server socket failed: %d", ret);
        goto end_of_task;
    }
    Log_i("TCP server socket listening...");

    while (g_comm_task_run && --server_count) {
        struct sockaddr_in sourceAddr;
        uint addrLen = sizeof(sourceAddr);
        int client_socket = accept(server_socket, (struct sockaddr *)&sourceAddr, &addrLen);
        if (client_socket == -1) {
            if (errno != EWOULDBLOCK) {
                Log_e("Unable to accept connection: errno %d", errno);
                push_error_log(ERR_SOCKET_ACCEPT, errno);
            }

            HAL_SleepMs(500);
            if (g_boarding_error && !g_wifi_sta_connected)
                break;
            else
                continue;
        }

        inet_ntoa_r(((struct sockaddr_in *)&sourceAddr)->sin_addr.s_addr, addr_str, sizeof(addr_str) - 1);
        Log_i("Socket accepted from client: %s", addr_str);
        comm_peer_t peer_client = {
            .socket_id = client_socket,
            .socket_addr = NULL,
            .addr_len = 0,
        };

        ret = tcp_client_data_loop(&peer_client, addr_str);

        if (client_socket != -1) {
            get_and_post_error_log(&peer_client);
            HAL_SleepMs(1000);
            Log_w("Shutting down client socket");
            shutdown(client_socket, 0);
            close(client_socket);
        }

        if (g_sap_task_run) {
            Log_i("Switch from APSTA to STA mode");
            if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) {
                Log_e("esp_wifi_set_mode STA failed");
            }
        }

        break;
    }

end_of_task:
    if (server_socket != -1) {
        Log_w("Shutting down server socket");
        shutdown(server_socket, 0);
        close(server_socket);
    }

    if (g_sap_task_run || g_smart_task_run) {
        /* stop the other task */
        g_smart_task_run = false;
        g_sap_task_run = false;
        Log_i("boarding tasks stop");
    }

    if (g_mqtt_connected)
        set_wifi_led_state(WIFI_LED_ON);
    else
        set_wifi_led_state(WIFI_LED_OFF);

    save_error_log();

    Log_i("tcp server task quit");
    vTaskDelete(NULL);
}


static void udp_server_task(void *pvParameters)
{
    int ret, server_socket = -1;
    char addr_str[128] = {0};
    uint32_t server_count = WAIT_CNT_5MIN_SECONDS / SELECT_WAIT_TIME_SECONDS + 5; /* stay longer than 5 minutes to handle error log */

    struct sockaddr_in server_addr;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(APP_SERVER_PORT);
    inet_ntoa_r(server_addr.sin_addr, addr_str, sizeof(addr_str) - 1);

    server_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (server_socket < 0) {
        Log_e("Unable to create socket: errno %d", errno);
        push_error_log(ERR_SOCKET_OPEN, errno);
        goto end_of_task;
    }

    ret = bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (ret < 0) {
        Log_e("Socket unable to bind: errno %d", errno);
        /*TODO: probably another udp task is still running */
        push_error_log(ERR_SOCKET_BIND, errno);
        goto end_of_task;
    }

    Log_i("UDP server socket listening...");
    fd_set sets;
    comm_peer_t peer_client = {
        .socket_id = server_socket,
        .socket_addr = NULL,
        .addr_len = 0,
    };

    while (g_comm_task_run && --server_count) {

        FD_ZERO(&sets);
        FD_SET(server_socket, &sets);
        struct timeval timeout;
        timeout.tv_sec = SELECT_WAIT_TIME_SECONDS;
        timeout.tv_usec = 0;

        int ret = select(server_socket + 1, &sets, NULL, NULL, &timeout);
        if (ret > 0) {

            struct sockaddr_in sourceAddr;
            uint addrLen = sizeof(sourceAddr);
            char rx_buffer[256] = {0};

            int len = recvfrom(server_socket, rx_buffer, sizeof(rx_buffer) - 1, MSG_DONTWAIT, (struct sockaddr *)&sourceAddr, &addrLen);

            // Error occured during receiving
            if (len < 0) {
                Log_e("recvfrom failed: errno %d", errno);
                // for UDP, only send data after receiving client's data(then we know the address)
                if (peer_client.socket_addr == NULL) {
                    push_error_log(ERR_SOCKET_RECV, errno);
                    continue;
                } else {
                    app_send_error_log(&peer_client, CUR_ERR, ERR_SOCKET_RECV, errno);
                    break;
                }
            }
            // Connection closed
            else if (len == 0) {
                Log_w("Connection is closed by peer");
                continue;
            }
            // Data received
            else {
                // Get the sender's ip address as string
                inet_ntoa_r(((struct sockaddr_in *)&sourceAddr)->sin_addr.s_addr, addr_str, sizeof(addr_str) - 1);
                rx_buffer[len] = 0;
                Log_i("Received %d bytes from <%s:%u> msg: %s", len, addr_str, sourceAddr.sin_port, rx_buffer);

                peer_client.socket_addr = (struct sockaddr *)&sourceAddr;
                peer_client.addr_len = sizeof(sourceAddr);

                // send error log here, otherwise no chance
                get_and_post_error_log(&peer_client);

                ret = app_handle_recv_data(&peer_client, rx_buffer, len);
                if (ret == 1) {
                    Log_w("Finish app cmd handling.");
                    break;
                } else if (ret < 0) {
                    Log_e("Error occured during handle rx msg: %d", ret);
                }
                ret = get_and_post_error_log(&peer_client);
                if (ret)
                    break;

                continue;
            }
        } else if (0 == ret) {
            Log_d("wait for read...");
            if (peer_client.socket_addr != NULL) {
                ret = get_and_post_error_log(&peer_client);
                if (ret)
                    break;
            }
            continue;
        } else {
            Log_e("select-recv error: %d", errno);
            if (peer_client.socket_addr == NULL) {
                push_error_log(ERR_SOCKET_SELECT, errno);
                continue;
            } else {
                app_send_error_log(&peer_client, CUR_ERR, ERR_SOCKET_SELECT, errno);
                break;
            }
        }
    }

end_of_task:
    if (server_socket != -1) {
        Log_w("Shutting down UDP server socket");
        shutdown(server_socket, 0);
        close(server_socket);
    }

    if (g_sap_task_run) {
        Log_i("Switch from APSTA to STA mode");
        if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) {
            Log_e("esp_wifi_set_mode STA failed");
        }
    }

    if (g_sap_task_run || g_smart_task_run) {
        /* stop the other task */
        g_smart_task_run = false;
        g_sap_task_run = false;
        g_comm_task_run = false;
        Log_i("boarding tasks stop");
    }

    if (g_mqtt_connected)
        set_wifi_led_state(WIFI_LED_ON);
    else
        set_wifi_led_state(WIFI_LED_OFF);

    save_error_log();

    Log_i("UDP server task quit");
    vTaskDelete(NULL);
}



//============================ Qcloud WiFi app functions end ===========================//


//============================ WiFi softAP/smartconfig begin ===========================//

void softAP_task(void *pvParameters)
{
    uint8_t blueValue = 0;
    uint32_t server_count = (WIFI_APP_WAIT_COUNT * 500) / SOFT_AP_BLINK_TIME;
    while (g_sap_task_run && (--server_count)) {
        EventBits_t uxBits;

        uxBits = xEventGroupWaitBits(g_wifi_event_group, CONNECTED_BIT,
                                     true, false, SOFT_AP_BLINK_TIME / portTICK_RATE_MS);
        if (uxBits & CONNECTED_BIT) {
            Log_d("WiFi Connected to ap");

            int ret = do_one_mqtt_connect();
            if (ret) {
                push_error_log(ERR_MQTT_CONNECT, ret);
            } else {
                Log_i("WIFI_CONNECT_SUCCESS\n");
                set_wifi_led_state(WIFI_LED_ON);
            }

            //setup_sntp();
            break;
        } else {
            blueValue = (~blueValue) & 0x01;
            set_wifi_led_state(blueValue);
        }
    }

    if (g_signature_sent) {
        Log_i("Switch from APSTA to STA mode");
        if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) {
            Log_e("esp_wifi_set_mode STA failed");
        }
    }

    if (g_cb_bck) {
        esp_event_loop_set_cb(g_cb_bck, NULL);
        g_cb_bck = NULL;
    }

    if (server_count == 0) {
        Log_w("softap timeout");
        push_error_log(ERR_BD_STOP, ERR_SC_EXEC_TIMEOUT);
    }

    Log_i("softAP task quit");
    vTaskDelete(NULL);
}


int start_softAP(const char *ssid, const char *psw, uint8_t ch)
{
    int ret = init_error_log_queue();
    if (ret) {
        Log_e("init log queue failed: %d", ret);
        return ret;
    }

    handle_saved_error_log();

    Log_i("enter softAP mode");

    if (g_comm_task_run) {
        // last cmd not finished, waiting
        g_smart_task_run = false;
        g_sap_task_run = false;
        g_comm_task_run = false;

        HAL_SleepMs(SELECT_WAIT_TIME_SECONDS * 1000 + 100);
    }

    g_smart_task_run = false;
    g_sap_task_run = false;
    g_comm_task_run = false;
    g_wifi_sta_connected = false;
    g_mqtt_connected = false;
    g_signature_sent = false;
    g_boarding_error = false;
    g_ssid_pwd_recv = false;

    ret = wifi_ap_init(ssid, psw, ch);
    if ( ESP_OK != ret ) {
        Log_e("wifi_ap_init failed: %d", ret);
        push_error_log(ERR_WIFI_AP_INIT, ret);
        goto err_exit;
    }

    ret = esp_wifi_start();
    if ( ESP_OK != ret ) {
        Log_e("esp_wifi_start failed: %d", ret);
        push_error_log(ERR_WIFI_START, ret);
        goto err_exit;
    }

    ret = xTaskCreate(udp_server_task, COMM_SERVER_TASK_NAME, COMM_SERVER_TASK_STACK_BYTES, NULL, COMM_SERVER_TASK_PRIO, NULL);
    if (ret != pdPASS) {
        Log_e("create tcp_server_task failed: %d", ret);
        push_error_log(ERR_OS_TASK, ret);
        goto err_exit;
    }
    g_comm_task_run = true;

    ret = xTaskCreate(softAP_task, SOFTAP_TASK_NAME, SOFTAP_TASK_STACK_BYTES, NULL, SOFTAP_TASK_PRIO, NULL);
    if (ret != pdPASS)  {
        Log_e("create softAP_task failed: %d", ret);
        push_error_log(ERR_OS_TASK, ret);
        g_comm_task_run = false;
        goto err_exit;
    }
    g_sap_task_run = true;

    return 0;

err_exit:

    save_error_log();

    g_smart_task_run = false;
    g_sap_task_run = false;
    g_comm_task_run = false;

    return QCLOUD_ERR_FAILURE;
}

void stop_softAP(void)
{
    Log_w("Stop softAP");

    if (!g_mqtt_connected && !g_signature_sent)
        push_error_log(ERR_BD_STOP, ERR_SC_AT_STOP);

    g_smart_task_run = false;
    g_sap_task_run = false;
    g_comm_task_run = false;

    esp_wifi_set_mode(WIFI_MODE_STA);
    set_wifi_led_state(WIFI_LED_OFF);
}

static void _smartconfig_event_cb(smartconfig_status_t status, void *pdata)
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
                Log_i( "SC_STATUS_LINK SSID:%s PSD: %s", wifi_config->sta.ssid, wifi_config->sta.password);

                int ret = esp_wifi_disconnect();
                if ( ESP_OK != ret ) {
                    Log_e("esp_wifi_disconnect failed: %d", ret);
                    push_error_log(ERR_WIFI_DISCONNECT, ret);
                }

                ret = esp_wifi_set_config(ESP_IF_WIFI_STA, wifi_config);
                if ( ESP_OK != ret ) {
                    Log_e("esp_wifi_set_config failed: %d", ret);
                    push_error_log(ERR_WIFI_CONFIG, ret);
                }

                ret = esp_wifi_connect();
                if ( ESP_OK != ret ) {
                    Log_e("esp_wifi_connect failed: %d", ret);
                    push_error_log(ERR_WIFI_CONNECT, ret);
                }
            } else {
                Log_e("invalid smart config link data");
                push_error_log(ERR_SC_DATA, ERR_SC_INVALID_DATA);
            }
            break;
        case SC_STATUS_LINK_OVER:
            Log_w( "SC_STATUS_LINK_OVER");
            if (pdata != NULL) {
                uint8_t phone_ip[4] = { 0 };
                memcpy(phone_ip, (uint8_t* )pdata, 4);
                Log_i( "Phone ip: %d.%d.%d.%d\n", phone_ip[0], phone_ip[1], phone_ip[2], phone_ip[3]);
            }

            xEventGroupSetBits(g_wifi_event_group, ESPTOUCH_DONE_BIT);
            break;
        default:
            break;
    }
}


void smartconfig_task(void * parm)
{
    uint32_t server_count = WIFI_APP_WAIT_COUNT;
    uint8_t blueValue = 0;
    uint32_t BlinkTime = SMART_CONFIG_BLINK_TIME;
    EventBits_t uxBits;

    int ret = esp_smartconfig_set_type(SC_TYPE_ESPTOUCH);
    if ( ESP_OK != ret ) {
        Log_e("esp_smartconfig_set_type failed: %d", ret);
        push_error_log(ERR_SC_START, ret);
        return ;
    }

    ret = esp_smartconfig_start(_smartconfig_event_cb);
    if ( ESP_OK != ret ) {
        Log_e("esp_smartconfig_start failed: %d", ret);
        push_error_log(ERR_SC_START, ret);
        return ;
    }

    while (g_smart_task_run && (--server_count)) {
        uxBits = xEventGroupWaitBits(g_wifi_event_group, CONNECTED_BIT | ESPTOUCH_DONE_BIT, true, false, BlinkTime / portTICK_RATE_MS);

        if (uxBits & CONNECTED_BIT) {
            Log_d("WiFi Connected to AP");

            ret = do_one_mqtt_connect();
            if (ret) {
                push_error_log(ERR_MQTT_CONNECT, ret);
            } else {
                Log_i("+TCSTARTSMART:WIFI_CONNECT_SUCCESS\n");
                set_wifi_led_state(WIFI_LED_ON);
            }

            //setup_sntp();
            break;
        }

        if (uxBits & ESPTOUCH_DONE_BIT) {
            /* recv smartconfig stop event before connected */
            Log_w("smartconfig over");
            push_error_log(ERR_BD_STOP, ERR_SC_APP_STOP);
            esp_smartconfig_stop();
            break;
        }

        blueValue = (~blueValue) & 0x01;
        set_wifi_led_state(blueValue);

    }

    esp_smartconfig_stop();
    xEventGroupClearBits(g_wifi_event_group, CONNECTED_BIT | ESPTOUCH_DONE_BIT);

    if (g_cb_bck) {
        esp_event_loop_set_cb(g_cb_bck, NULL);
        g_cb_bck = NULL;
    }

    if (server_count == 0) {
        Log_w("smartconfig timeout");
        push_error_log(ERR_BD_STOP, ERR_SC_EXEC_TIMEOUT);
    }

    Log_i("smartconfig task quit");
    vTaskDelete(NULL);
}

int start_smartconfig(void)
{
    int ret = init_error_log_queue();
    if (ret) {
        Log_e("init log queue failed: %d", ret);
        return ret;
    }

    handle_saved_error_log();

    Log_d("Enter smartconfig");

    g_smart_task_run = false;
    g_sap_task_run = false;
    g_comm_task_run = false;
    g_wifi_sta_connected = false;
    g_mqtt_connected = false;
    g_signature_sent = false;
    g_boarding_error = false;
    g_ssid_pwd_recv = false;

    ret = wifi_sta_init();
    if ( ESP_OK != ret ) {
        Log_e("wifi_sta_init failed: %d", ret);
        push_error_log(ERR_WIFI_STA_INIT, ret);
        goto err_exit;
    }

    ret = esp_wifi_start();
    if ( ESP_OK != ret ) {
        Log_e("esp_wifi_start failed: %d", ret);
        push_error_log(ERR_WIFI_START, ret);
        goto err_exit;
    }

    ret = xTaskCreate(tcp_server_task, COMM_SERVER_TASK_NAME, COMM_SERVER_TASK_STACK_BYTES, NULL, COMM_SERVER_TASK_PRIO, NULL);
    if (ret != pdPASS) {
        Log_e("create tcp_server_task failed: %d", ret);
        push_error_log(ERR_OS_TASK, ret);
        goto err_exit;
    }
    g_comm_task_run = true;

    ret = xTaskCreate(smartconfig_task, SMARTCONFIG_TASK_NAME, SMARTCONFIG_TASK_STACK_BYTES, NULL, SMARTCONFIG_TASK_PRIO, NULL);
    if (ret != pdPASS)  {
        Log_e("create smartconfig_task failed: %d", ret);
        push_error_log(ERR_OS_TASK, ret);
        g_comm_task_run = false;
        goto err_exit;
    }
    g_smart_task_run = true;

    return 0;

err_exit:

    save_error_log();

    g_smart_task_run = false;
    g_sap_task_run = false;
    g_comm_task_run = false;

    return QCLOUD_ERR_FAILURE;

}

void stop_smartconfig(void)
{
    Log_w("Stop smartconfig");

    if (!g_mqtt_connected && !g_signature_sent)
        push_error_log(ERR_BD_STOP, ERR_SC_AT_STOP);

    g_smart_task_run = false;
    g_sap_task_run = false;
    g_comm_task_run = false;

    esp_smartconfig_stop();
    set_wifi_led_state(WIFI_LED_OFF);
}

bool is_wifi_boarding_successful(void)
{
    return (g_wifi_sta_connected && g_mqtt_connected);
}

//============================ WiFi connect/softAP/smartconfig end ===========================//



