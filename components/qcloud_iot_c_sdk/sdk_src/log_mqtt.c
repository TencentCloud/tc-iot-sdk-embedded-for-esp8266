/*
 * Tencent is pleased to support the open source community by making IoT Hub available.
 * Copyright (C) 2018-2020 THL A29 Limited, a Tencent company. All rights reserved.

 * Licensed under the MIT License (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://opensource.org/licenses/MIT

 * Unless required by applicable law or agreed to in writing, software distributed under the License is
 * distributed on an "AS IS" basis, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "config.h"
#ifdef LOG_UPLOAD

#include <string.h>

#include "lite-utils.h"
#include "log_upload.h"
#include "mqtt_client.h"
#include "qcloud_iot_device.h"

typedef struct _log_mqtt_state {
    bool topic_sub_ok;
    bool result_recv_ok;
    int  log_level;
} LogMQTTState;

static LogMQTTState sg_log_state = {.topic_sub_ok = false, .result_recv_ok = false, .log_level = eLOG_ERROR};

static bool _get_json_log_level(char *json, int32_t *res)
{
    char *v = LITE_json_value_of("log_level", json);
    if (v == NULL) {
        Log_e("Invalid log level from JSON: %s", STRING_PTR_PRINT_SANITY_CHECK(json));
        return false;
    }
    if (LITE_get_int32(res, v) != QCLOUD_RET_SUCCESS) {
        Log_e("Invalid log level from JSON: %s", json);
        HAL_Free(v);
        return false;
    }
    HAL_Free(v);
    return true;
}

static void _log_level_sub_cb(void *pClient, MQTTMessage *message, void *pUserData)
{
#define LOG_JSON_LENGTH 128
    char    json_buf[LOG_JSON_LENGTH] = {0};
    int32_t json_buf_len              = 0;
    if (message == NULL) {
        return;
    }
    LogMQTTState *state = (LogMQTTState *)pUserData;

    json_buf_len = Min(LOG_JSON_LENGTH - 1, message->payload_len);
    memcpy(json_buf, message->payload, json_buf_len);
    json_buf[json_buf_len] = '\0';  // json_parse relies on a string

    Log_d("Recv Msg Topic:%s, payload:%s", STRING_PTR_PRINT_SANITY_CHECK(message->ptopic), json_buf);
    int log_level;
    if (!_get_json_log_level(json_buf, &log_level)) {
        return;
    }

    switch (log_level) {
        case eLOG_DISABLE:
            Log_w("Upload log level change to: %d", log_level);
            clear_upload_buffer();
            set_log_upload_in_comm_err(true);
            IOT_Log_Set_Upload_Level(eLOG_ERROR);
            break;
        case eLOG_ERROR:
        case eLOG_WARN:
        case eLOG_INFO:
        case eLOG_DEBUG:
            if (log_level < IOT_Log_Get_Upload_Level())
                clear_upload_buffer();
            IOT_Log_Set_Upload_Level((LOG_LEVEL)log_level);
            Log_w("Upload log level change to: %d", log_level);
            set_log_upload_in_comm_err(false);
            break;
        default:
            Log_e("Invalid log level: %d", log_level);
            break;
    }

    state->log_level      = log_level;
    state->result_recv_ok = true;
}

static void _log_mqtt_sub_event_handler(void *pclient, MQTTEventType event_type, void *pUserData)
{
    LogMQTTState *state = (LogMQTTState *)pUserData;

    switch (event_type) {
        case MQTT_EVENT_SUBCRIBE_SUCCESS:
            Log_d("mqtt log topic subscribe success");
            state->topic_sub_ok = true;
            break;

        case MQTT_EVENT_SUBCRIBE_TIMEOUT:
            Log_i("mqtt log topic subscribe timeout");
            state->topic_sub_ok = false;
            break;

        case MQTT_EVENT_SUBCRIBE_NACK:
            Log_i("mqtt log topic subscribe NACK");
            state->topic_sub_ok = false;
            break;
        case MQTT_EVENT_UNSUBSCRIBE:
            Log_i("mqtt log topic has been unsubscribed");
            state->topic_sub_ok = false;
            ;
            break;
        case MQTT_EVENT_CLIENT_DESTROY:
            Log_i("mqtt client has been destroyed");
            state->topic_sub_ok = false;
            ;
            break;
        default:
            return;
    }
}

static int _iot_log_level_get_publish(void *pClient, DeviceInfo *dev_info)
{
    POINTER_SANITY_CHECK(pClient, QCLOUD_ERR_INVAL);
    static unsigned int sg_client_token = 1;

    Qcloud_IoT_Client *mqtt_client          = (Qcloud_IoT_Client *)pClient;
    char               topic_name[128]      = {0};
    char               payload_content[128] = {0};

    HAL_Snprintf(topic_name, sizeof(topic_name), "$log/operation/%s/%s", dev_info->product_id, dev_info->device_name);
    HAL_Snprintf(payload_content, sizeof(payload_content),
                 "{\"type\": \"get_log_level\", "
                 "\"clientToken\": \"%s-%u\"}",
                 dev_info->product_id, sg_client_token++);

    PublishParams pub_params = DEFAULT_PUB_PARAMS;
    pub_params.qos           = QOS0;
    pub_params.payload       = payload_content;
    pub_params.payload_len   = strlen(payload_content);

    return IOT_MQTT_Publish(mqtt_client, topic_name, &pub_params);
}

static int _log_topic_subscribe(void *client, DeviceInfo *dev_info)
{
    /* subscribe the log topic: "$log/operation/result/${productId}/${deviceName}" */
    char topic_name[128] = {0};
    int  size = HAL_Snprintf(topic_name, sizeof(topic_name), "$log/operation/result/%s/%s", dev_info->product_id,
                            dev_info->device_name);
    if (size < 0 || size > sizeof(topic_name) - 1) {
        Log_e("topic content buf not enough! content size:%d buf size:%d", size, (int)sizeof(topic_name));
        return QCLOUD_ERR_FAILURE;
    }

    SubscribeParams sub_params      = DEFAULT_SUB_PARAMS;
    sub_params.on_message_handler   = _log_level_sub_cb;
    sub_params.on_sub_event_handler = _log_mqtt_sub_event_handler;
    sub_params.user_data            = (void *)&sg_log_state;
    sub_params.qos                  = QOS0;

    return IOT_MQTT_Subscribe(client, topic_name, &sub_params);
}

int qcloud_get_log_level(int *log_level)
{
#define SUB_RETRY_TIMES 3
#define SYNC_TIMES      20

    int ret    = 0;
    int cntSub = 0;
    int cntRev = 0;

    Qcloud_IoT_Client *mqtt_client = (Qcloud_IoT_Client *)get_log_mqtt_client();
    DeviceInfo *       dev_info    = (DeviceInfo *)get_log_dev_info();
    POINTER_SANITY_CHECK(mqtt_client, QCLOUD_ERR_INVAL);
    LogMQTTState *pLogState = &sg_log_state;

    // subscribe log topic: $log/operation/get/${productid}/${devicename}
    // skip this if the subscription is done and valid
    if (!pLogState->topic_sub_ok) {
        for (cntSub = 0; cntSub < SUB_RETRY_TIMES; cntSub++) {
            ret = _log_topic_subscribe(mqtt_client, dev_info);
            if (ret < 0) {
                Log_w("qcloud_log_topic_subscribe failed: %d, cnt: %d", ret, cntSub);
                continue;
            }

            /* wait for sub ack */
            ret = IOT_MQTT_Yield(mqtt_client, 100);
            if (pLogState->topic_sub_ok) {
                break;
            }
        }
    }

    // return failure if subscribe failed
    if (!pLogState->topic_sub_ok) {
        Log_e("Subscribe log topic failed!");
        return QCLOUD_ERR_FAILURE;
    }

    pLogState->result_recv_ok = false;
    // publish msg to get log level
    ret = _iot_log_level_get_publish(mqtt_client, dev_info);
    if (ret < 0) {
        Log_e("client publish log topic failed :%d", ret);
        return ret;
    }

    do {
        ret = IOT_MQTT_Yield(mqtt_client, 100);
        cntRev++;
    } while (!ret && !pLogState->result_recv_ok && cntRev < SYNC_TIMES);

    *log_level = pLogState->log_level;
    if (pLogState->result_recv_ok)
        ret = QCLOUD_RET_SUCCESS;
    else
        ret = QCLOUD_ERR_FAILURE;

    return ret;
#undef SUB_RETRY_TIMES
}

#endif

#ifdef __cplusplus
}
#endif
