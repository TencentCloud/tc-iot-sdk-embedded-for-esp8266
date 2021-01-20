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

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"

#include "qcloud_iot_export.h"
#include "qcloud_iot_import.h"
#include "utils_hmac.h"
#include "utils_base64.h"

#include "qcloud_wifi_config.h"
#include "wifi_config_internal.h"
#include "board_ops.h"
#include "esp_qcloud_storage.h"

/* task control flags and counters */
static bool sg_mqtt_task_run = false;
static bool sg_comm_task_run = false;
static bool sg_log_task_run  = false;

static int sg_mqtt_task_cnt = 0;
static int sg_comm_task_cnt = 0;
static int sg_log_task_cnt  = 0;

/* wifi config state flag */
static bool sg_wifi_config_success = false;
static bool sg_token_received      = false;

#define WAIT_BIND   "wait_bind"
static bool sg_wait_app_bind_device_state = false;
//============================ MQTT communication functions begin ===========================//

#define MAX_TOKEN_LENGTH 32
static char sg_token_str[MAX_TOKEN_LENGTH + 1] = {0};
static char sg_region_str[MAX_SIZE_OF_PRODUCT_REGION + 1] = {"ap-guangzhou"};
static DeviceInfo sg_device_info = {0};

typedef struct TokenHandleData {
    bool sub_ready;
    bool send_ready;
    int  reply_code;
} TokenHandleData;


// user can customize device storage function here
static int _load_device_info(DeviceInfo *dev_info)
{
    return HAL_GetDevInfo(dev_info);
}

static void _mqtt_event_handler(void *pclient, void *handle_context, MQTTEventMsg *msg)
{
    MQTTMessage *    mqtt_messge = (MQTTMessage *)msg->msg;
    uintptr_t        packet_id   = (uintptr_t)msg->msg;
    TokenHandleData *app_data    = (TokenHandleData *)handle_context;

    switch (msg->event_type) {
        case MQTT_EVENT_DISCONNECT:
            Log_w("MQTT disconnect");
            PUSH_LOG("MQTT disconnect");
            break;

        case MQTT_EVENT_RECONNECT:
            Log_i("MQTT reconnected");
            PUSH_LOG("MQTT reconnected");
            break;

        case MQTT_EVENT_PUBLISH_RECVEIVED:
            Log_i("unhandled msg arrived: topic=%s", mqtt_messge->ptopic);
            break;

        case MQTT_EVENT_SUBCRIBE_SUCCESS:
            Log_d("mqtt topic subscribe success");
            app_data->sub_ready = true;
            break;

        case MQTT_EVENT_SUBCRIBE_TIMEOUT:
            Log_i("mqtt topic subscribe timeout");
            app_data->sub_ready = false;
            break;

        case MQTT_EVENT_SUBCRIBE_NACK:
            Log_i("mqtt topic subscribe NACK");
            app_data->sub_ready = false;
            break;

        case MQTT_EVENT_PUBLISH_SUCCESS:
            Log_i("publish success, packet-id=%u", (unsigned int)packet_id);
            app_data->send_ready = true;
            break;

        case MQTT_EVENT_PUBLISH_TIMEOUT:
            Log_i("publish timeout, packet-id=%u", (unsigned int)packet_id);
            app_data->send_ready = false;
            break;

        case MQTT_EVENT_PUBLISH_NACK:
            Log_i("publish nack, packet-id=%u", (unsigned int)packet_id);
            app_data->send_ready = false;
            break;

        default:
            Log_i("unknown event type: %d", msg->event_type);
            break;
    }
}

// callback when MQTT msg arrives
static void _on_message_callback(void *pClient, MQTTMessage *message, void *userData)
{
    if (message == NULL) {
        Log_e("msg null");
        return;
    }

    if (message->topic_len == 0 && message->payload_len == 0) {
        Log_e("length zero");
        return;
    }

    Log_i("recv msg topic: %s", message->ptopic);

    uint32_t msg_topic_len = message->payload_len + 4;
    char *   buf           = (char *)HAL_Malloc(msg_topic_len);
    if (buf == NULL) {
        Log_e("malloc %u bytes failed", msg_topic_len);
        return;
    }

    memset(buf, 0, msg_topic_len);
    memcpy(buf, message->payload, message->payload_len);
    Log_i("msg payload: %s", buf);

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        Log_e("Parsing JSON Error");
        push_error_log(ERR_TOKEN_REPLY, ERR_APP_CMD_JSON_FORMAT);
    } else {
        cJSON *method_json = cJSON_GetObjectItem(root, "method");
        cJSON *code_json = cJSON_GetObjectItem(root, "code");
        if (NULL != method_json) {
            if (0 == strcmp("bind_device", method_json->valuestring)) {
                Log_d("method [bind_device] msg down");
                sg_wait_app_bind_device_state = false;
            } else if (0 == strcmp("bind_device_query_reply", method_json->valuestring)) {                
                if (NULL != code_json) {
                    Log_d("method [bind_device_query_reply] bind code = %d", code_json->valueint);
                    TokenHandleData *app_data = (TokenHandleData *)userData;
                    app_data->reply_code      = code_json->valueint;
                }
            } else if (0 == strcmp("app_bind_token_reply", method_json->valuestring)) {                
                if (code_json) {
                    Log_d("method [app_bind_token_reply] bind code = %d", code_json->valueint);
                    TokenHandleData *app_data = (TokenHandleData *)userData;
                    app_data->reply_code      = code_json->valueint;
                    Log_d("token reply code = %d", code_json->valueint);

                    if (app_data->reply_code) {
                        Log_e("token reply error: %d", code_json->valueint);
                        PUSH_LOG("token reply error: %d", code_json->valueint);
                        push_error_log(ERR_TOKEN_REPLY, app_data->reply_code);
                    }
                } else {
                    Log_e("Parsing reply code Error");
                    push_error_log(ERR_TOKEN_REPLY, ERR_APP_CMD_JSON_FORMAT);
                }
            }
        }

        cJSON_Delete(root);
    }

    HAL_Free(buf);
}

// subscrib MQTT topic
static int _subscribe_topic_wait_result(void *client, DeviceInfo *dev_info, TokenHandleData *app_data)
{
    char topic_name[128] = {0};
    // int size = HAL_Snprintf(topic_name, sizeof(topic_name), "%s/%s/data", dev_info->product_id,
    // dev_info->device_name);
    int size = HAL_Snprintf(topic_name, sizeof(topic_name), "$thing/down/service/%s/%s", dev_info->product_id,
                            dev_info->device_name);
    if (size < 0 || size > sizeof(topic_name) - 1) {
        Log_e("topic content length not enough! content size:%d  buf size:%d", size, (int)sizeof(topic_name));
        return QCLOUD_ERR_FAILURE;
    }
    SubscribeParams sub_params    = DEFAULT_SUB_PARAMS;
    sub_params.qos                = QOS0;
    sub_params.on_message_handler = _on_message_callback;
    sub_params.user_data          = (void *)app_data;
    int rc                        = IOT_MQTT_Subscribe(client, topic_name, &sub_params);
    if (rc < 0) {
        Log_e("MQTT subscribe failed: %d", rc);
        return rc;
    }

    int wait_cnt = 2;
    while (!app_data->sub_ready && (wait_cnt-- > 0)) {
        // wait for subscription result
        rc = IOT_MQTT_Yield(client, 1000);
        if (rc) {
            Log_e("MQTT error: %d", rc);
            return rc;
        }
    }

    if (wait_cnt > 0) {
        return QCLOUD_RET_SUCCESS;
    } else {
        Log_w("wait for subscribe result timeout!");
        return QCLOUD_ERR_FAILURE;
    }
}

// publish MQTT msg
static int _publish_token_msg(void *client, DeviceInfo *dev_info, char *token_str)
{
    char topic_name[128] = {0};
    // int size = HAL_Snprintf(topic_name, sizeof(topic_name), "%s/%s/data", dev_info->product_id,
    // dev_info->device_name);
    int size = HAL_Snprintf(topic_name, sizeof(topic_name), "$thing/up/service/%s/%s", dev_info->product_id,
                            dev_info->device_name);
    if (size < 0 || size > sizeof(topic_name) - 1) {
        Log_e("topic content length not enough! content size:%d  buf size:%d", size, (int)sizeof(topic_name));
        return QCLOUD_ERR_FAILURE;
    }

    PublishParams pub_params = DEFAULT_PUB_PARAMS;
    pub_params.qos           = QOS1;

    char topic_content[256] = {0};
    size                    = HAL_Snprintf(topic_content, sizeof(topic_content),
                        "{\"method\":\"app_bind_token\",\"clientToken\":\"%s-%u\",\"params\": {\"token\":\"%s\"}}",
                        dev_info->device_name, HAL_GetTimeMs(), token_str);
    if (size < 0 || size > sizeof(topic_content) - 1) {
        Log_e("payload content length not enough! content size:%d  buf size:%d", size, (int)sizeof(topic_content));
        return QCLOUD_ERR_MALLOC;
    }

    pub_params.payload     = topic_content;
    pub_params.payload_len = strlen(topic_content);

    return IOT_MQTT_Publish(client, topic_name, &pub_params);
}

// send token data via mqtt publishing
static int _send_token_wait_reply(void *client, DeviceInfo *dev_info, TokenHandleData *app_data)
{
    int ret      = 0;
    int wait_cnt = 20;

    // for smartconfig, we need to wait for the token data from app
    while (!sg_token_received && (wait_cnt-- > 0)) {
        IOT_MQTT_Yield(client, 1000);
        Log_i("wait for token data...");
    }

    if (!sg_token_received) {
        Log_e("Wait for token data timeout");
        PUSH_LOG("Wait for token data timeout");
        return ERR_TOKEN_RECV;
    }

    // compare region of user and region of product
    if (strncmp(dev_info->product_region, sg_region_str, MAX_SIZE_OF_PRODUCT_REGION)) {
        Log_e("Region fault: %s != %s", dev_info->product_region, sg_region_str);
        PUSH_LOG("Region fault: %s != %s", dev_info->product_region, sg_region_str);
        return ERR_REGION_FAULT;
    }

    wait_cnt = 3;
publish_token:
    ret = _publish_token_msg(client, dev_info, sg_token_str);
    if (ret < 0 && (wait_cnt-- > 0)) {
        Log_e("Client publish token failed: %d", ret);
        if (is_wifi_sta_connected() && IOT_MQTT_IsConnected(client)) {
            IOT_MQTT_Yield(client, 500);
        } else {
            Log_e("Wifi or MQTT lost connection! Wait and retry!");
            IOT_MQTT_Yield(client, 2000);
        }
        goto publish_token;
    }

    if (ret < 0) {
        Log_e("pub token failed: %d", ret);
        PUSH_LOG("pub token failed: %d", ret);
        return ret;
    }

    wait_cnt = 5;
    while (!app_data->send_ready && (wait_cnt-- > 0)) {
        IOT_MQTT_Yield(client, 1000);
        Log_i("wait for token sending result...");
    }

    ret = 0;
    if (!app_data->send_ready) {
        Log_e("pub token timeout");
        PUSH_LOG("pub token timeout");
        ret = QCLOUD_ERR_MQTT_UNKNOWN;
    }

    return ret;
}

// get the device info or do device dynamic register
static int _get_reg_dev_info(DeviceInfo *dev_info)
{
    if (strlen(dev_info->product_id) == 0 || 
            strlen(dev_info->device_name) == 0) {
        Log_e("invalid product_id or device_name");
        PUSH_LOG("invalid product_id or device_name");
        return -1;    
    }

    // just demo condition of dynamic register device
    // device_secret == "YOUR_IOT_PSK" and product_secret != "YOUR_PRODUCT_SECRET"
    // user can customize different policy here
    if (!strncmp(dev_info->device_secret, "YOUR_IOT_PSK", MAX_SIZE_OF_DEVICE_SECRET) &&
        strncmp(dev_info->product_secret, "YOUR_PRODUCT_SECRET", MAX_SIZE_OF_PRODUCT_SECRET)) {

        int ret = IOT_DynReg_Device(dev_info);
        if (ret) {
            Log_e("dynamic register device failed: %d", ret);
            PUSH_LOG("dynamic register device failed: %d", ret);
            return ret;
        }

        // save the dev info
        ret = HAL_SetDevInfo(dev_info);
        if (ret) {
            Log_e("HAL_SetDevInfo failed: %d", ret);
            return ret;
        }

        // delay a while
        HAL_SleepMs(500);
    }

    return QCLOUD_RET_SUCCESS;
}

// setup mqtt connection and return client handle
static void *_setup_mqtt_connect(DeviceInfo *dev_info, TokenHandleData *app_data)
{
    MQTTInitParams init_params       = DEFAULT_MQTTINIT_PARAMS;
    init_params.device_name          = dev_info->device_name;
    init_params.product_id           = dev_info->product_id;
    init_params.device_secret        = dev_info->device_secret;
    init_params.region               = dev_info->product_region;
    init_params.event_handle.h_fp    = _mqtt_event_handler;
    init_params.event_handle.context = (void *)app_data;

    void *client = IOT_MQTT_Construct(&init_params);
    if (client != NULL) {
        Log_i("Cloud Device Construct Success");
        return client;
    }

    // construct failed, give it another try
    if (is_wifi_sta_connected()) {
        Log_e("Cloud Device Construct Failed: %d! Try one more time!", init_params.err_code);
        HAL_SleepMs(1000);
    } else {
        Log_e("Wifi lost connection! Wait and retry!");
        HAL_SleepMs(2000);
    }

    client = IOT_MQTT_Construct(&init_params);
    if (client != NULL) {
        Log_i("Cloud Device Construct Success");
        return client;
    }

    Log_e("Device %s/%s connect failed: %d", 
        dev_info->product_id, dev_info->device_name, init_params.err_code);
    PUSH_LOG("Device %s/%s connect failed: %d", 
        dev_info->product_id, dev_info->device_name, init_params.err_code);
    push_error_log(ERR_MQTT_CONNECT, init_params.err_code);
    return NULL;
}

#if WIFI_CONFIG_WAIT_APP_BIND_STATE
// wait cloud platform publish bind_device method msg
static int _wait_platform_bind_device_msg(void *mqtt_client)
{
    int retry_cnt = 120;
    int ret = QCLOUD_RET_SUCCESS;

    if (NULL == mqtt_client) {
        Log_e("wait method [bind_deive] failed, mqtt_client is NULL");
        return QCLOUD_ERR_FAILURE;
    }
    
    // wait 2mins
    retry_cnt = 120;
    ret = QCLOUD_ERR_MQTT_REQUEST_TIMEOUT;
    do {
        // bind_device method recved
        if (false == sg_wait_app_bind_device_state) {
            if (ESP_OK != esp_qcloud_storage_erase(WAIT_BIND)) {
               Log_e("erase wait_app_bind_device false failed");
            }
            ret = QCLOUD_RET_SUCCESS;
            break;
        }
        IOT_MQTT_Yield(mqtt_client, 1000);
        Log_d("wait for bind device method :%d, task_run:%d, try:%d, ret:%d", sg_wait_app_bind_device_state,
              sg_mqtt_task_run, retry_cnt, ret);
        
        
    } while (ret && retry_cnt-- && sg_mqtt_task_run);

    if (retry_cnt == 0) {
        Log_e("wait method [bind_deive] timeout");
    }

    return ret;
}

// query app bind device result; device publish bind_device_query msg
static int _query_app_bind_device_result(void *client, DeviceInfo *dev_info, TokenHandleData *app_data)
{    
    int ret = QCLOUD_RET_SUCCESS;
    int wait_cnt = 5;
    char topic_name[128] = {0};

    // publish bind_device_query msg
    int size = HAL_Snprintf(topic_name, sizeof(topic_name), "$thing/up/service/%s/%s", dev_info->product_id,
                            dev_info->device_name);
    if (size < 0 || size > sizeof(topic_name) - 1) {
        Log_e("topic content length not enough! content size:%d  buf size:%d", size, (int)sizeof(topic_name));
        return QCLOUD_ERR_FAILURE;
    }

    PublishParams pub_params = DEFAULT_PUB_PARAMS;
    pub_params.qos           = QOS0;

    char topic_content[256] = {0};
    size                    = HAL_Snprintf(topic_content, sizeof(topic_content),
                        "{\"method\":\"bind_device_query\",\"clientToken\":\"%s-%u\",\"timestamp\": %u}",
                        dev_info->device_name, HAL_GetTimeMs(), HAL_GetTimeMs());
    if (size < 0 || size > sizeof(topic_content) - 1) {
        Log_e("payload content length not enough! content size:%d  buf size:%d", size, (int)sizeof(topic_content));
        return QCLOUD_ERR_MALLOC;
    }

    pub_params.payload     = topic_content;
    pub_params.payload_len = strlen(topic_content);

publish_query_msg:
    app_data->reply_code = -1;
    app_data->send_ready = false;
    
    ret = IOT_MQTT_Publish(client, topic_name, &pub_params);
    
    if (ret < 0 && (wait_cnt-- > 0)) {
        Log_e("Client publish query bind device failed: %d", ret);
        if (is_wifi_sta_connected() && IOT_MQTT_IsConnected(client)) {
            IOT_MQTT_Yield(client, 500);
        } else {
            Log_e("Wifi or MQTT lost connection! Wait and retry!");
            IOT_MQTT_Yield(client, 2000);
        }
        goto publish_query_msg;
    }
    
    if (ret < 0) {
        Log_e("pub query bind device failed: %d", ret);
        return ret;
    }

    wait_cnt = 5;
    do {
        IOT_MQTT_Yield(client, 1000);
        Log_i("wait for query bind device result %d, %d,%d...", app_data->reply_code, app_data->send_ready);
    } while ((-1 == app_data->reply_code) && (wait_cnt-- > 0));

    if (wait_cnt != 0) {
        if (ESP_OK != esp_qcloud_storage_erase(WAIT_BIND)) {
            Log_e("erase wait_wechat_applet_bind_device false failed");
        }
    }
    
    if (0 == app_data->reply_code) {
        Log_e("query bind device binded");
        ret = LAST_APP_BIND_STATE_SUCCESS;
        set_wifi_led_state(LED_ON);
    } else if (404 == app_data->reply_code) {
        Log_e("query bind device not bind");
        ret = LAST_APP_BIND_STATE_FAILED;
    }
    
    return ret;
}

// query app bind device result
WIFI_CONFIG_LAST_APP_BIND_STATE mqtt_query_app_bind_result(void)
{
    //get the device info or do device dynamic register
    DeviceInfo  dev_info;
    bool wait_app_bind_device_state = true;
    int ret = QCLOUD_RET_SUCCESS;
    void *client = NULL;

    if (ESP_OK != esp_qcloud_storage_get(WAIT_BIND,
                                         &wait_app_bind_device_state,
                                         sizeof(wait_app_bind_device_state))) {
        Log_i("get storage failed, no wait_bind key");
        return LAST_APP_BIND_STATE_NOBIND;
    } else {
        sg_wait_app_bind_device_state = wait_app_bind_device_state;
        if (false == sg_wait_app_bind_device_state) {
            Log_i("get storage success not wait app bind state");
            return LAST_APP_BIND_STATE_NOBIND;
        }
    }

    ret = _load_device_info(&dev_info);
    if (ret) {
        Log_e("Get device info failed: %d", ret);
        return LAST_APP_BIND_STATE_ERROR;
    }
    //token handle data
    TokenHandleData app_data;
    app_data.sub_ready = false;
    app_data.send_ready = false;
    app_data.reply_code = -1;

    //mqtt connection
    client = _setup_mqtt_connect(&dev_info, &app_data);
    if (client == NULL) {
        ret = IOT_MQTT_GetErrCode();
        Log_e("Cloud Device Construct Failed: %d", ret);
        push_error_log(ERR_MQTT_CONNECT, ret);
        return LAST_APP_BIND_STATE_ERROR;
    }

    //subscribe token reply topic
    ret = _subscribe_topic_wait_result(client, &dev_info, &app_data);
    if (ret < 0) {
        Log_w("Client Subscribe Topic Failed: %d", ret);
        ret = LAST_APP_BIND_STATE_ERROR;
    } else if ((ret = _query_app_bind_device_result(client, &dev_info, &app_data)) < 0) {
        Log_w("query app bind failed: %d", ret);
        ret = LAST_APP_BIND_STATE_FAILED;
    }

    IOT_MQTT_Destroy(&client);
    if (0 == ret) {
        HAL_SleepMs(5000);
    }
        
    return ret;
}
#endif
int mqtt_send_token(void)
{
    // get the device info or do device dynamic register
    DeviceInfo *dev_info = &sg_device_info;
    int              ret = _get_reg_dev_info(dev_info);
    if (ret) {
        Log_e("Get device info failed: %d", ret);
        PUSH_LOG("Get device info failed: %d", ret);
        push_error_log(ERR_DEVICE_INFO, ret);
        return ret;
    }

    // token handle data
    TokenHandleData app_data;
    app_data.sub_ready  = false;
    app_data.send_ready = false;
    app_data.reply_code = 404;

    // mqtt connection
    void *client = _setup_mqtt_connect(dev_info, &app_data);
    if (client == NULL) {
        return QCLOUD_ERR_MQTT_NO_CONN;
    } else {
        Log_i("Device %s/%s connect success", dev_info->product_id, dev_info->device_name);
        PUSH_LOG("Device %s/%s connect success", dev_info->product_id, dev_info->device_name);
    }

    // subscribe token reply topic
    ret = _subscribe_topic_wait_result(client, dev_info, &app_data);
    if (ret < 0) {
        Log_w("Subscribe topic failed: %d", ret);
        PUSH_LOG("Subscribe topic failed: %d", ret);
    }
#if WIFI_CONFIG_WAIT_APP_BIND_STATE
    sg_wait_app_bind_device_state = true;
    /* store wait wechat applet bind success flag to flash */
    if (ESP_OK != esp_qcloud_storage_set(WAIT_BIND,
                                         &sg_wait_app_bind_device_state,
                                         sizeof(sg_wait_app_bind_device_state))) {
        Log_e("set wait_app_bind_device true failed");
    }
#endif

    // publish token msg and wait for reply
    int retry_cnt = 2;
    do {
        ret = _send_token_wait_reply(client, dev_info, &app_data);

        IOT_MQTT_Yield(client, 1000);
    } while (ret && ret != ERR_REGION_FAULT && retry_cnt-- && sg_mqtt_task_run);

    if (ret) {
        switch (ret) {
            case ERR_TOKEN_RECV:
            case ERR_REGION_FAULT:
                push_error_log(ret, QCLOUD_ERR_INVAL);
                break;
            default:
                push_error_log(ERR_TOKEN_SEND, ret);
                break;
        }
    }
#if WIFI_CONFIG_WAIT_APP_BIND_STATE
    if ((QCLOUD_RET_SUCCESS == ret) && (0 == app_data.reply_code)) {
        ret = _wait_platform_bind_device_msg(client);
    } else if ((QCLOUD_RET_SUCCESS == ret) && (0 != app_data.reply_code)) {
        Log_e("app bind device code:%d", app_data.reply_code);
        ret = QCLOUD_ERR_FAILURE;
    }
#endif
    IOT_MQTT_Destroy(&client);

    // sleep 5 seconds to avoid frequent MQTT connection
    if (ret == 0)
        HAL_SleepMs(5000);

    return ret;
}
//============================ MQTT communication functions end ===========================//

//============================ Qcloud app TCP/UDP functions begin ===========================//

/* reply data to app/mini program
 * return -1, error and can not proceed
 * return 1,  error but can proceed
 * return 0,  everything all right
 */
static int app_reply_dev_info(comm_peer_t *peer)
{
    int ret;
    cJSON *reply_json = cJSON_CreateObject();
    cJSON_AddNumberToObject(reply_json, "cmdType", (int)CMD_DEVICE_REPLY);
    cJSON_AddStringToObject(reply_json, "productId", sg_device_info.product_id);
    cJSON_AddStringToObject(reply_json, "deviceName", sg_device_info.device_name);
    cJSON_AddStringToObject(reply_json, "productRegion", sg_device_info.product_region);
    cJSON_AddStringToObject(reply_json, "protoVersion", WIFI_CONFIG_PROTO_VERSION);

    char json_str[256] = {0};
    if (0 == cJSON_PrintPreallocated(reply_json, json_str, sizeof(json_str), 0)) {
        cJSON_Delete(reply_json);
        Log_e("cJSON_PrintPreallocated failed!");
        PUSH_LOG("cJSON_PrintPreallocated failed!");
        push_error_log(ERR_APP_CMD, ERR_JSON_PRINT);
        return 1;
    }
    /* append msg delimiter */
    strcat(json_str, "\r\n");
    cJSON_Delete(reply_json);

    int udp_resend_cnt = 3;
udp_resend:
    ret = sendto(peer->socket_id, json_str, strlen(json_str), 0, peer->socket_addr, peer->addr_len);
    if (ret < 0) {
        Log_e("send error: %s", strerror(errno));
        PUSH_LOG("send error: %s", strerror(errno));
        push_error_log(ERR_SOCKET_SEND, errno);
        return 1;
    }
    /* UDP packet could be lost, send it again */
    /* NOT necessary for TCP */
    if (peer->socket_addr != NULL && --udp_resend_cnt) {
        HAL_SleepMs(1000);
        goto udp_resend;
    }

    HAL_Printf("Report dev info: %s", json_str);
    return 0;
}

static int app_handle_recv_data(comm_peer_t *peer, char *pdata, int len)
{
    int    ret;
    cJSON *root = cJSON_Parse(pdata);
    if (!root) {
        Log_e("JSON Err: %s", cJSON_GetErrorPtr());
        PUSH_LOG("JSON Err: %s", cJSON_GetErrorPtr());
        app_send_error_log(peer, CUR_ERR, ERR_APP_CMD, ERR_APP_CMD_JSON_FORMAT);
        return -1;
    }

    cJSON *cmd_json = cJSON_GetObjectItem(root, "cmdType");
    if (cmd_json == NULL) {
        Log_e("Invalid cmd JSON: %s", pdata);
        PUSH_LOG("Invalid cmd JSON: %s", pdata);
        cJSON_Delete(root);
        app_send_error_log(peer, CUR_ERR, ERR_APP_CMD, ERR_APP_CMD_JSON_FORMAT);
        return -1;
    }

    cJSON *region_json = cJSON_GetObjectItem(root, "region");
    if (region_json) {
        memset(sg_region_str, 0, sizeof(sg_region_str));
        strncpy(sg_region_str, region_json->valuestring, MAX_SIZE_OF_PRODUCT_REGION);
    }

    int cmd = cmd_json->valueint;
    switch (cmd) {
        /* Token only for smartconfig  */
        case CMD_TOKEN_ONLY: {
            cJSON *token_json = cJSON_GetObjectItem(root, "token");

            if (token_json) {
                memset(sg_token_str, 0, sizeof(sg_token_str));
                strncpy(sg_token_str, token_json->valuestring, MAX_TOKEN_LENGTH);
                sg_token_received = true;
                cJSON_Delete(root);

                app_reply_dev_info(peer);

                /* 1: Everything OK and we've finished the job */
                return 1;
            } else {
                cJSON_Delete(root);
                Log_e("invlaid token: %s", pdata);
                PUSH_LOG("invlaid token: %s", pdata);
                app_send_error_log(peer, CUR_ERR, ERR_APP_CMD, ERR_APP_CMD_AP_INFO);
                return -1;
            }
        } break;

        /* SSID/PW/TOKEN for softAP */
        case CMD_SSID_PW_TOKEN: {
            cJSON *ssid_json  = cJSON_GetObjectItem(root, "ssid");
            cJSON *psw_json   = cJSON_GetObjectItem(root, "password");
            cJSON *token_json = cJSON_GetObjectItem(root, "token");

            if (ssid_json && psw_json && token_json) {
                memset(sg_token_str, 0, sizeof(sg_token_str));
                strncpy(sg_token_str, token_json->valuestring, MAX_TOKEN_LENGTH);
                sg_token_received = true;

                app_reply_dev_info(peer);
                // sleep a while before changing to STA mode
                HAL_SleepMs(1000);

                Log_i("STA to connect SSID:%s PASSWORD:%s", ssid_json->valuestring, psw_json->valuestring);
                PUSH_LOG("SSID:%s|PSW:%s|TOKEN:%s", ssid_json->valuestring, psw_json->valuestring,
                         token_json->valuestring);
                ret = wifi_sta_connect(ssid_json->valuestring, psw_json->valuestring);
                if (ret) {
                    Log_e("wifi_sta_connect failed: %d", ret);
                    PUSH_LOG("wifi_sta_connect failed: %d", ret);
                    push_error_log(ERR_WIFI_AP_STA, ret);
                    cJSON_Delete(root);
                    return -1;
                }
                cJSON_Delete(root);

                /* return 1 as device alreay switch to STA mode and unable to recv cmd anymore
                 * 1: Everything OK and we've finished the job */
                return 1;
            } else {
                cJSON_Delete(root);
                Log_e("invlaid ssid/password/token!");
                PUSH_LOG("invlaid ssid/password/token!");
                app_send_error_log(peer, CUR_ERR, ERR_APP_CMD, ERR_APP_CMD_AP_INFO);
                return -1;
            }
        } break;

        case CMD_LOG_QUERY:
            ret = app_send_dev_log(peer);
            Log_i("app_send_dev_log ret: %d", ret);
            return 1;

        default: {
            cJSON_Delete(root);
            Log_e("invalid cmd: %s", pdata);
            PUSH_LOG("invalid cmd: %s", pdata);
            app_send_error_log(peer, CUR_ERR, ERR_APP_CMD, ERR_APP_CMD_JSON_FORMAT);
        } break;
    }

    return -1;
}

static void udp_server_task(void *pvParameters)
{
    int  ret, server_socket = -1;
    char addr_str[128] = {0};
    sg_comm_task_cnt++;

    /* stay longer than 5 minutes to handle error log */
    uint32_t server_count = WAIT_CNT_5MIN_SECONDS / SELECT_WAIT_TIME_SECONDS + 5;

    struct sockaddr_in server_addr;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(APP_SERVER_PORT);
    inet_ntoa_r(server_addr.sin_addr, addr_str, sizeof(addr_str) - 1);

    server_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (server_socket < 0) {
        Log_e("socket failed: errno %d", errno);
        PUSH_LOG("socket failed: errno %d", errno);
        push_error_log(ERR_SOCKET_OPEN, errno);
        goto end_of_task;
    }

    ret = bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (ret < 0) {
        Log_e("bind failed: errno %d", errno);
        PUSH_LOG("bind failed: errno %d", errno);
        push_error_log(ERR_SOCKET_BIND, errno);
        goto end_of_task;
    }

    Log_i("UDP server socket listening...");
    fd_set      sets;
    comm_peer_t peer_client = {
        .socket_id   = server_socket,
        .socket_addr = NULL,
        .addr_len    = 0,
    };

    int select_err_cnt = 0;
    int recv_err_cnt   = 0;
    while (sg_comm_task_run && --server_count) {
        FD_ZERO(&sets);
        FD_SET(server_socket, &sets);
        struct timeval timeout;
        timeout.tv_sec  = SELECT_WAIT_TIME_SECONDS;
        timeout.tv_usec = 0;

        int ret = select(server_socket + 1, &sets, NULL, NULL, &timeout);
        if (ret > 0) {
            select_err_cnt = 0;
            struct sockaddr_in source_addr;
            uint               addrLen        = sizeof(source_addr);
            char               rx_buffer[256] = {0};

            int len = recvfrom(server_socket, rx_buffer, sizeof(rx_buffer) - 1, MSG_DONTWAIT,
                               (struct sockaddr *)&source_addr, &addrLen);

            // Error occured during receiving
            if (len < 0) {
                recv_err_cnt++;
                Log_w("recvfrom error: %d, cnt: %d", errno, recv_err_cnt);
                if (recv_err_cnt > 3) {
                    Log_e("recvfrom error: %d, cnt: %d", errno, recv_err_cnt);
                    PUSH_LOG("recvfrom error: %d, cnt: %d", errno, recv_err_cnt);
                    push_error_log(ERR_SOCKET_RECV, errno);
                    break;
                }
                continue;
            }
            // Connection closed
            else if (len == 0) {
                recv_err_cnt = 0;
                Log_w("Connection is closed by peer");
                PUSH_LOG("Connection is closed by peer");
                continue;
            }
            // Data received
            else {
                recv_err_cnt = 0;
                // Get the sender's ip address as string
                inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr.s_addr, addr_str, sizeof(addr_str) - 1);
                rx_buffer[len] = 0;
                Log_i("Received %d bytes from <%s:%u> msg: %s", len, addr_str, source_addr.sin_port, rx_buffer);
                PUSH_LOG("%s", rx_buffer);
                peer_client.socket_addr = (struct sockaddr *)&source_addr;
                peer_client.addr_len    = sizeof(source_addr);

                // send error log here, otherwise no chance for previous error
                get_and_post_error_log(&peer_client);

                ret = app_handle_recv_data(&peer_client, rx_buffer, len);
                if (ret == 1) {
                    Log_w("Finish app cmd handle");
                    PUSH_LOG("Finish app cmd handle");
                }
                break;

                // only one round of data recv/send
                //get_and_post_error_log(&peer_client);
                //continue;
            }
        } else if (0 == ret) {
            select_err_cnt = 0;
            Log_d("wait for read...");
            if (peer_client.socket_addr != NULL) {
                get_and_post_error_log(&peer_client);
            }
            continue;
        } else {
            select_err_cnt++;
            Log_w("select-recv error: %d, cnt: %d", errno, select_err_cnt);
            if (select_err_cnt > 3) {
                Log_e("select-recv error: %d, cnt: %d", errno, select_err_cnt);
                PUSH_LOG("select-recv error: %d, cnt: %d", errno, select_err_cnt);
                push_error_log(ERR_SOCKET_SELECT, errno);
                break;
            }
            HAL_SleepMs(500);
        }
    }

end_of_task:
    if (server_socket != -1) {
        Log_w("Shutting down UDP server socket");
        shutdown(server_socket, 0);
        close(server_socket);
    }

    sg_comm_task_run = false;
    Log_i("UDP server task quit");
    sg_comm_task_cnt--;
    vTaskDelete(NULL);
}

static void softAP_mqtt_task(void *pvParameters)
{
    uint8_t  led_state    = 0;
    uint32_t server_count = WIFI_CONFIG_WAIT_TIME_MS / SOFT_AP_BLINK_TIME_MS;
    sg_mqtt_task_cnt++;

    while (sg_mqtt_task_run && (--server_count)) {
        int state = wifi_wait_event(SOFT_AP_BLINK_TIME_MS);
        if (state == EVENT_WIFI_CONNECTED) {
            Log_d("WiFi Connected to ap");
            set_wifi_led_state(LED_ON);

            int ret = mqtt_send_token();
            if (ret) {
                Log_e("SoftAP: WIFI_MQTT_CONNECT_FAILED: %d", ret);
                PUSH_LOG("SoftAP: WIFI_MQTT_CONNECT_FAILED: %d", ret);
                set_wifi_led_state(LED_OFF);
            } else {
                Log_i("SoftAP: WIFI_MQTT_CONNECT_SUCCESS");
                PUSH_LOG("SoftAP: WIFI_MQTT_CONNECT_SUCCESS");
                set_wifi_led_state(LED_ON);
                sg_wifi_config_success = true;
            }

            break;
        } else if (state == EVENT_WIFI_DISCONNECTED) {
            // reduce the wait time as it meet disconnect
            server_count = WIFI_CONFIG_HALF_TIME_MS / SOFT_AP_BLINK_TIME_MS;
            Log_i("disconnect event! wait count change to %d", server_count);

        } else {
            led_state = (~led_state) & 0x01;
            set_wifi_led_state(led_state);
        }

        // check comm server task state
        if (!sg_comm_task_run && !sg_token_received) {
            Log_e("comm server task error!");
            PUSH_LOG("comm server task error");
            set_wifi_led_state(LED_OFF);
            break;
        }
    }

    if (server_count == 0 && !sg_wifi_config_success) {
        Log_w("SoftAP: TIMEOUT");
        PUSH_LOG("SoftAP: TIMEOUT");
        push_error_log(ERR_BD_STOP, ERR_SC_EXEC_TIMEOUT);
        set_wifi_led_state(LED_OFF);
    }

    wifi_stop_softap();
    save_error_log();

    sg_comm_task_run = false;
    sg_mqtt_task_run = false;
    Log_i("softAP mqtt task quit");
    sg_mqtt_task_cnt--;
    vTaskDelete(NULL);
}

int start_softAP(const char *ssid, const char *psw, uint8_t ch)
{
    sg_log_task_run = false;
    sg_mqtt_task_run = false;
    sg_comm_task_run = false;

    while (sg_comm_task_cnt > 0 || sg_mqtt_task_cnt > 0 || sg_log_task_cnt > 0) {
        Log_w("Wait for last config finish: %d %d %d", sg_comm_task_cnt, sg_mqtt_task_cnt, sg_log_task_cnt);
        sg_log_task_run = false;
        sg_mqtt_task_run = false;
        sg_comm_task_run = false;
        HAL_SleepMs(1000);
    }

    init_dev_log_queue();
    init_error_log_queue();
    Log_i("enter softAP mode");
    PUSH_LOG("start softAP: %s", ssid);

    sg_wifi_config_success = false;
    sg_token_received      = false;
    memset(sg_token_str, 0, sizeof(sg_token_str));

    int ret = _load_device_info(&sg_device_info);
    if (ret) {
        Log_e("load device info failed: %d", ret);
        PUSH_LOG("load device info failed: %d", ret);
        push_error_log(ERR_DEVICE_INFO, ret);
        ret = ERR_DEVICE_INFO;
        goto err_exit;
    }

    ret = wifi_ap_init(ssid, psw, ch);
    if (ret) {
        Log_e("wifi_ap_init failed: %d", ret);
        PUSH_LOG("wifi_ap_init failed: %d", ret);
        push_error_log(ERR_WIFI_AP_INIT, ret);
        ret = ERR_WIFI_AP_INIT;
        goto err_exit;
    }

    ret = wifi_start_running();
    if (ret) {
        Log_e("wifi_start_running failed: %d", ret);
        PUSH_LOG("wifi_start_running failed: %d", ret);
        push_error_log(ERR_WIFI_START, ret);
        ret = ERR_WIFI_START;
        goto err_exit;
    }

    sg_comm_task_run = true;
    ret = xTaskCreate(udp_server_task, COMM_SERVER_TASK_NAME, COMM_SERVER_TASK_STACK_BYTES, NULL, COMM_SERVER_TASK_PRIO,
                      NULL);
    if (ret != pdPASS) {
        Log_e("create comm_server_task failed: %d", ret);
        PUSH_LOG("create comm_server_task failed: %d", ret);
        push_error_log(ERR_OS_TASK, ret);
        ret = ERR_OS_TASK;
        goto err_exit;
    }

    sg_mqtt_task_run = true;
    ret = xTaskCreate(softAP_mqtt_task, SOFTAP_TASK_NAME, SOFTAP_TASK_STACK_BYTES, NULL, SOFTAP_TASK_PRIO, NULL);
    if (ret != pdPASS) {
        Log_e("create softAP_mqtt_task failed: %d", ret);
        PUSH_LOG("create softAP_mqtt_task failed: %d", ret);
        push_error_log(ERR_OS_TASK, ret);
        ret = ERR_OS_TASK;
        goto err_exit;
    }

    return 0;

err_exit:

    wifi_stop_softap();
    save_error_log();

    sg_mqtt_task_run = false;
    sg_comm_task_run = false;

    return ret;
}

void stop_softAP(void)
{
    Log_w("Stop softAP");

    if (!sg_wifi_config_success) {
        push_error_log(ERR_BD_STOP, ERR_SC_AT_STOP);
        PUSH_LOG("softAP stopped by user!");
    }

    sg_mqtt_task_run = false;
    sg_comm_task_run = false;
    sg_log_task_run  = false;

    set_wifi_led_state(LED_OFF);
    wifi_stop_softap();
}

static void smartconfig_mqtt_task(void *parm)
{
    uint32_t wait_count = WIFI_CONFIG_WAIT_TIME_MS / SMART_CONFIG_BLINK_TIME_MS;
    uint8_t  led_state  = 0;
    int      ret;
    sg_mqtt_task_cnt++;

    while (sg_mqtt_task_run && (--wait_count)) {
        int state = wifi_wait_event(SMART_CONFIG_BLINK_TIME_MS);

        if (state == EVENT_WIFI_CONNECTED) {
            Log_d("WiFi Connected to AP");
            set_wifi_led_state(LED_ON);

            ret = mqtt_send_token();
            if (ret) {
                Log_e("SmartConfig: WIFI_MQTT_CONNECT_FAILED: %d", ret);
                PUSH_LOG("SmartConfig: WIFI_MQTT_CONNECT_FAILED: %d", ret);
                set_wifi_led_state(LED_OFF);
            } else {
                Log_i("SmartConfig: WIFI_MQTT_CONNECT_SUCCESS");
                PUSH_LOG("SmartConfig: WIFI_MQTT_CONNECT_SUCCESS");
                set_wifi_led_state(LED_ON);
                sg_wifi_config_success = true;
            }

            break;
        } else if (state == EVENT_SMARTCONFIG_STOP) {
            /* recv smartconfig stop event before connected */
            Log_w("smartconfig over");
            PUSH_LOG("smartconfig over");
            push_error_log(ERR_BD_STOP, ERR_SC_APP_STOP);
            set_wifi_led_state(LED_OFF);
            break;
        } else if (state == EVENT_WIFI_DISCONNECTED) {
            // reduce the wait time as it meet disconnect
            wait_count = WIFI_CONFIG_HALF_TIME_MS / SMART_CONFIG_BLINK_TIME_MS;
            Log_i("disconnect event! wait count change to %d", wait_count);

        } else {
            led_state = (~led_state) & 0x01;
            set_wifi_led_state(led_state);
        }

        // check comm server task state
        if (!sg_comm_task_run && !sg_token_received) {
            Log_e("comm server task error!");
            PUSH_LOG("comm server task error");
            set_wifi_led_state(LED_OFF);
            break;
        }
    }

    if (wait_count == 0 && !sg_wifi_config_success) {
        Log_w("SmartConfig: TIMEOUT");
        PUSH_LOG("SmartConfig: TIMEOUT");
        push_error_log(ERR_BD_STOP, ERR_SC_EXEC_TIMEOUT);
        set_wifi_led_state(LED_OFF);
    }

    wifi_stop_smartconfig();
    save_error_log();

    sg_comm_task_run = false;
    sg_mqtt_task_run = false;
    Log_i("smartconfig task quit");
    sg_mqtt_task_cnt--;
    vTaskDelete(NULL);
}

int start_smartconfig(void)
{
    sg_log_task_run = false;
    sg_mqtt_task_run = false;
    sg_comm_task_run = false;

    while (sg_comm_task_cnt > 0 || sg_mqtt_task_cnt > 0 || sg_log_task_cnt > 0) {
        Log_w("Wait for last config finish: %d %d %d", sg_comm_task_cnt, sg_mqtt_task_cnt, sg_log_task_cnt);
        sg_log_task_run = false;
        sg_mqtt_task_run = false;
        sg_comm_task_run = false;
        HAL_SleepMs(1000);
    }

    init_dev_log_queue();
    init_error_log_queue();
    Log_d("Enter smartconfig");
    PUSH_LOG("Enter smartconfig");

    sg_wifi_config_success = false;
    sg_token_received      = false;
    memset(sg_token_str, 0, sizeof(sg_token_str));

    int ret = _load_device_info(&sg_device_info);
    if (ret) {
        Log_e("load device info failed: %d", ret);
        PUSH_LOG("load device info failed: %d", ret);
        push_error_log(ERR_DEVICE_INFO, ret);
        ret = ERR_DEVICE_INFO;
        goto err_exit;
    }

    ret = wifi_sta_init();
    if (ret) {
        Log_e("wifi_sta_init failed: %d", ret);
        PUSH_LOG("wifi_sta_init failed: %d", ret);
        push_error_log(ERR_WIFI_STA_INIT, ret);
        ret = ERR_WIFI_STA_INIT;
        goto err_exit;
    }

    ret = wifi_start_running();
    if (ret) {
        Log_e("wifi_start_running failed: %d", ret);
        PUSH_LOG("wifi_start_running failed: %d", ret);
        push_error_log(ERR_WIFI_START, ret);
        ret = ERR_WIFI_START;
        goto err_exit;
    }

    ret = wifi_start_smartconfig();
    if (ret) {
        Log_e("start smartconfig failed: %d", ret);
        PUSH_LOG("start smartconfig failed: %d", ret);
        push_error_log(ERR_SC_START, ret);
        ret = ERR_SC_START;
        goto err_exit;
    }

    sg_comm_task_run = true;
    ret = xTaskCreate(udp_server_task, COMM_SERVER_TASK_NAME, COMM_SERVER_TASK_STACK_BYTES, NULL, COMM_SERVER_TASK_PRIO,
                      NULL);
    if (ret != pdPASS) {
        Log_e("create udp_server_task failed: %d", ret);
        PUSH_LOG("create udp_server_task failed: %d", ret);
        push_error_log(ERR_OS_TASK, ret);
        ret = ERR_OS_TASK;
        goto err_exit;
    }

    sg_mqtt_task_run = true;
    ret              = xTaskCreate(smartconfig_mqtt_task, SMARTCONFIG_TASK_NAME, SMARTCONFIG_TASK_STACK_BYTES, NULL,
                      SMARTCONFIG_TASK_PRIO, NULL);
    if (ret != pdPASS) {
        Log_e("create smartconfig_mqtt_task failed: %d", ret);
        PUSH_LOG("create smartconfig_mqtt_task failed: %d", ret);
        push_error_log(ERR_OS_TASK, ret);
        ret = ERR_OS_TASK;
        goto err_exit;
    }

    return 0;

err_exit:

    save_error_log();
    sg_mqtt_task_run = false;
    sg_comm_task_run = false;

    return ret;
}

void stop_smartconfig(void)
{
    Log_w("Stop smartconfig");

    if (!sg_wifi_config_success) {
        push_error_log(ERR_BD_STOP, ERR_SC_AT_STOP);
        PUSH_LOG("softAP stopped by user!");
    }

    sg_mqtt_task_run = false;
    sg_comm_task_run = false;
    sg_log_task_run  = false;

    wifi_stop_smartconfig();
    set_wifi_led_state(LED_OFF);
}

bool is_wifi_config_successful(void)
{
    return (sg_wifi_config_success);
}

int query_wifi_config_state(void)
{
    if (sg_wifi_config_success)
        return WIFI_CONFIG_SUCCESS;
    else if (sg_mqtt_task_run)
        return WIFI_CONFIG_GOING_ON;
    else
        return WIFI_CONFIG_FAILED;
}

#ifdef WIFI_LOG_UPLOAD
static void log_server_task(void *pvParameters)
{
    int  ret, server_socket = -1;
    char addr_str[128] = {0};
    sg_log_task_cnt++;

    /* stay 6 minutes to handle log */
    uint32_t server_count = 360 / SELECT_WAIT_TIME_SECONDS;

    struct sockaddr_in server_addr;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(LOG_SERVER_PORT);
    inet_ntoa_r(server_addr.sin_addr, addr_str, sizeof(addr_str) - 1);

    server_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (server_socket < 0) {
        Log_e("socket failed: errno %d", errno);
        goto end_of_task;
    }

    ret = bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (ret < 0) {
        Log_e("bind failed: errno %d", errno);
        goto end_of_task;
    }

    Log_i("LOG server socket listening...");
    fd_set      sets;
    comm_peer_t peer_client = {
        .socket_id   = server_socket,
        .socket_addr = NULL,
        .addr_len    = 0,
    };

    int select_err_cnt = 0;
    int recv_err_cnt   = 0;
    while (sg_log_task_run && --server_count) {
        FD_ZERO(&sets);
        FD_SET(server_socket, &sets);
        struct timeval timeout;
        timeout.tv_sec  = SELECT_WAIT_TIME_SECONDS;
        timeout.tv_usec = 0;

        int ret = select(server_socket + 1, &sets, NULL, NULL, &timeout);
        if (ret > 0) {
            select_err_cnt = 0;
            struct sockaddr_in source_addr;
            uint               addrLen        = sizeof(source_addr);
            char               rx_buffer[256] = {0};

            int len = recvfrom(server_socket, rx_buffer, sizeof(rx_buffer) - 1, MSG_DONTWAIT,
                               (struct sockaddr *)&source_addr, &addrLen);

            // Error occured during receiving
            if (len < 0) {
                recv_err_cnt++;
                Log_w("recvfrom error: %d, cnt: %d", errno, recv_err_cnt);
                if (recv_err_cnt > 3) {
                    Log_e("recvfrom error: %d, cnt: %d", errno, recv_err_cnt);
                    break;
                }
                continue;
            }
            // Connection closed
            else if (len == 0) {
                recv_err_cnt = 0;
                Log_w("Connection is closed by peer");
                continue;
            }
            // Data received
            else {
                recv_err_cnt = 0;
                // Get the sender's ip address as string
                inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr.s_addr, addr_str, sizeof(addr_str) - 1);
                rx_buffer[len] = 0;
                Log_i("Received %d bytes from <%s:%u> msg: %s", len, addr_str, source_addr.sin_port, rx_buffer);

                peer_client.socket_addr = (struct sockaddr *)&source_addr;
                peer_client.addr_len    = sizeof(source_addr);

                if (strncmp(rx_buffer, "{\"cmdType\":3}", 12) == 0) {
                    ret = app_send_dev_log(&peer_client);
                    Log_i("app_send_dev_log ret: %d", ret);
                    break;
                }

                continue;
            }
        } else if (0 == ret) {
            select_err_cnt = 0;
            Log_d("wait for read...");
            continue;
        } else {
            select_err_cnt++;
            Log_w("select-recv error: %d, cnt: %d", errno, select_err_cnt);
            if (select_err_cnt > 3) {
                Log_e("select-recv error: %d, cnt: %d", errno, select_err_cnt);
                break;
            }
            HAL_SleepMs(500);
        }
    }

end_of_task:
    if (server_socket != -1) {
        Log_w("Shutting down LOG server socket");
        shutdown(server_socket, 0);
        close(server_socket);
    }

    // don't destory if mqtt task run
    if (!sg_mqtt_task_run) {
        wifi_stop_softap();
        delete_dev_log_queue();
    }

    sg_log_task_run = false;
    Log_i("LOG server task quit");

#ifdef CONFIG_FREERTOS_USE_TRACE_FACILITY
    TaskStatus_t task_status;
    vTaskGetInfo(NULL, &task_status, pdTRUE, eRunning);
    Log_i(">>>>> task %s stack left: %u, free heap: %u", task_status.pcTaskName, task_status.usStackHighWaterMark,
          esp_get_free_heap_size());
#endif

    sg_log_task_cnt--;
    vTaskDelete(NULL);
}
#endif

int start_log_softAP(void)
{
#ifdef WIFI_LOG_UPLOAD
    Log_i("enter log softAP mode");

    sg_log_task_run = false;

    int ret = wifi_ap_init("ESP-LOG-QUERY", "86013388", 0);
    if (ret) {
        Log_e("wifi_ap_init failed: %d", ret);
        goto err_exit;
    }

    ret = wifi_start_running();
    if (ret) {
        Log_e("wifi_start_running failed: %d", ret);
        goto err_exit;
    }

    sg_log_task_run = true;
    ret = xTaskCreate(log_server_task, "log_server_task", COMM_SERVER_TASK_STACK_BYTES, NULL, COMM_SERVER_TASK_PRIO,
                      NULL);
    if (ret != pdPASS) {
        Log_e("create log_server_task failed: %d", ret);
        goto err_exit;
    }

    return 0;

err_exit:

    wifi_stop_softap();
    delete_dev_log_queue();

    sg_log_task_run = false;
#endif
    return QCLOUD_ERR_FAILURE;
}
//============================ WiFi connect/softAP/smartconfig end ===========================//

