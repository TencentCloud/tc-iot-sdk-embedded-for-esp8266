/*
 * Tencent is pleased to support the open source community by making IoT Hub available.
 * Copyright (C) 2016 THL A29 Limited, a Tencent company. All rights reserved.

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
#include <stdbool.h>
#include <string.h>
#include <signal.h>

#include "utils_getopt.h"
#include "qcloud_iot_export.h"
#include "qcloud_iot_import.h"
#include "qcloud_iot_demo.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


static bool sg_running_state = true;

#define MAGIC_HEAD_NUM              0x55aa
typedef struct _sTestData_ {
    uint8_t  m_power_switch;
    uint8_t  m_color;
    uint8_t  m_brightness;
} sTestData;

typedef struct _sRawDataFrame_ {
    uint16_t   magic_head;
    uint8_t    msg_type;
    uint8_t    res_byte;
    uint32_t   clientId;
    sTestData  data;
} sRawDataFrame;

typedef enum {
    eMSG_REPORT  = 0x01,
    eMSG_CONTROL = 0x20,
    eMSG_DEFAULT = 0xFF,
} eMsgType;

static uint32_t sg_client_id = 0;



#ifdef AUTH_MODE_CERT
static char sg_cert_file[PATH_MAX + 1];      // full path of device cert file
static char sg_key_file[PATH_MAX + 1];       // full path of device key file
#endif


static DeviceInfo sg_devInfo;
static int sg_sub_packet_id = -1;


// MQTT event callback
static void _mqtt_event_handler(void *pclient, void *handle_context, MQTTEventMsg *msg)
{
    MQTTMessage* mqtt_messge = (MQTTMessage*)msg->msg;
    uintptr_t packet_id = (uintptr_t)msg->msg;

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

        case MQTT_EVENT_PUBLISH_RECVEIVED:
            Log_i("topic message arrived but without any related handle: topic=%.*s, topic_msg=%.*s",
                  mqtt_messge->topic_len,
                  mqtt_messge->ptopic,
                  mqtt_messge->payload_len,
                  mqtt_messge->payload);
            break;
        case MQTT_EVENT_SUBCRIBE_SUCCESS:
            Log_i("subscribe success, packet-id=%u", (unsigned int)packet_id);
            sg_sub_packet_id = packet_id;
            break;

        case MQTT_EVENT_SUBCRIBE_TIMEOUT:
            Log_i("subscribe wait ack timeout, packet-id=%u", (unsigned int)packet_id);
            sg_sub_packet_id = packet_id;
            break;

        case MQTT_EVENT_SUBCRIBE_NACK:
            Log_i("subscribe nack, packet-id=%u", (unsigned int)packet_id);
            sg_sub_packet_id = packet_id;
            break;

        case MQTT_EVENT_UNSUBCRIBE_SUCCESS:
            Log_i("unsubscribe success, packet-id=%u", (unsigned int)packet_id);
            break;

        case MQTT_EVENT_UNSUBCRIBE_TIMEOUT:
            Log_i("unsubscribe timeout, packet-id=%u", (unsigned int)packet_id);
            break;

        case MQTT_EVENT_UNSUBCRIBE_NACK:
            Log_i("unsubscribe nack, packet-id=%u", (unsigned int)packet_id);
            break;

        case MQTT_EVENT_PUBLISH_SUCCESS:
            Log_i("publish success, packet-id=%u", (unsigned int)packet_id);
            break;

        case MQTT_EVENT_PUBLISH_TIMEOUT:
            Log_i("publish timeout, packet-id=%u", (unsigned int)packet_id);
            break;

        case MQTT_EVENT_PUBLISH_NACK:
            Log_i("publish nack, packet-id=%u", (unsigned int)packet_id);
            break;
        default:
            Log_i("Should NOT arrive here.");
            break;
    }
}


// Setup MQTT construct parameters
static int _setup_connect_init_params(MQTTInitParams* initParams)
{
    int ret;

    ret = HAL_GetDevInfo((void *)&sg_devInfo);
    if (QCLOUD_RET_SUCCESS != ret) {
        return ret;
    }

    initParams->device_name = sg_devInfo.device_name;
    initParams->product_id = sg_devInfo.product_id;

#ifdef AUTH_MODE_CERT
    char certs_dir[PATH_MAX + 1] = "certs";
    char current_path[PATH_MAX + 1];
    char *cwd = getcwd(current_path, sizeof(current_path));

    if (cwd == NULL) {
        Log_e("getcwd return NULL");
        return QCLOUD_ERR_FAILURE;
    }

#ifdef WIN32
    sprintf(sg_cert_file, "%s\\%s\\%s", current_path, certs_dir, sg_devInfo.dev_cert_file_name);
    sprintf(sg_key_file, "%s\\%s\\%s", current_path, certs_dir, sg_devInfo.dev_key_file_name);
#else
    sprintf(sg_cert_file, "%s/%s/%s", current_path, certs_dir, sg_devInfo.dev_cert_file_name);
    sprintf(sg_key_file, "%s/%s/%s", current_path, certs_dir, sg_devInfo.dev_key_file_name);
#endif

    initParams->cert_file = sg_cert_file;
    initParams->key_file = sg_key_file;
#else
    initParams->device_secret = sg_devInfo.device_secret;
#endif

    initParams->command_timeout = QCLOUD_IOT_MQTT_COMMAND_TIMEOUT;
    initParams->keep_alive_interval_ms = QCLOUD_IOT_MQTT_KEEP_ALIVE_INTERNAL;

    initParams->auto_connect_enable = 1;
    initParams->event_handle.h_fp = _mqtt_event_handler;
    initParams->event_handle.context = NULL;

    return QCLOUD_RET_SUCCESS;
}

static void HexDump(uint8_t *pData, uint16_t len)
{
    int i;

    HAL_Printf("\n[");
    for (i = 0; i < len; i++) {
        if (i == (len - 1)) {
            HAL_Printf("%d", pData[i]);
        } else
            HAL_Printf("%d,", pData[i]);
    }
    HAL_Printf("]\n");
}

// publish raw data msg
static int _publish_raw_data_msg(void *client, QoS qos)
{
    sRawDataFrame raw_data;

    memset((char *)&raw_data, 0, sizeof(sRawDataFrame));
    raw_data.magic_head = MAGIC_HEAD_NUM;
    raw_data.msg_type = eMSG_REPORT;
    raw_data.clientId = sg_client_id++;

    srand((unsigned)HAL_GetTimeMs());
    raw_data.data.m_power_switch = 1;
    raw_data.data.m_color = rand() % 3;
    raw_data.data.m_brightness = rand() % 100;

    char topicName[128] = {0};
    sprintf(topicName, "$thing/up/raw/%s/%s", sg_devInfo.product_id, sg_devInfo.device_name);

    PublishParams pub_params = DEFAULT_PUB_PARAMS;
    pub_params.qos = qos;
    pub_params.payload = &raw_data;
    pub_params.payload_len = sizeof(raw_data);

    Log_d("raw_data published dump:");
    HexDump((uint8_t *)&raw_data, sizeof(raw_data));

    return IOT_MQTT_Publish(client, topicName, &pub_params);
}

// callback when MQTT msg arrives
static void on_raw_data_message_callback(void *pClient, MQTTMessage *message, void *userData)
{
    if (message == NULL) {
        return;
    }
    Log_i("Receive Message With topicName:%.*s, payloadlen:%d", (int) message->topic_len, message->ptopic, (int) message->payload_len);

    Log_d("raw_data reveived dump:");
    HexDump((uint8_t *)message->payload, (int)message->payload_len);
}

// subscrib MQTT topic
static int _subscribe_raw_data_topic(void *client, QoS qos)
{
    static char topic_name[128] = {0};
    int size = HAL_Snprintf(topic_name, sizeof(topic_name), "$thing/down/raw/%s/%s", sg_devInfo.product_id, sg_devInfo.device_name);

    if (size < 0 || size > sizeof(topic_name) - 1) {
        Log_e("topic content length not enough! content size:%d  buf size:%d", size, (int)sizeof(topic_name));
        return QCLOUD_ERR_FAILURE;
    }
    SubscribeParams sub_params = DEFAULT_SUB_PARAMS;
    sub_params.qos = qos;
    sub_params.on_message_handler = on_raw_data_message_callback;
    return IOT_MQTT_Subscribe(client, topic_name, &sub_params);
}

int qcloud_iot_explorer_demo(eDemoType eType)
{
    int rc;

    if (eDEMO_RAW_DATA != eType) {
        Log_e("Demo config (%d) illegal, please check", eType);
        return QCLOUD_ERR_FAILURE;
    }

    //init connection
    MQTTInitParams init_params = DEFAULT_MQTTINIT_PARAMS;
    rc = _setup_connect_init_params(&init_params);
    if (rc != QCLOUD_RET_SUCCESS) {
        Log_e("init params error, rc = %d", rc);
        return rc;
    }

    // create MQTT client and connect with server
    void *client = IOT_MQTT_Construct(&init_params);
    if (client != NULL) {
        Log_i("Cloud Device Construct Success");
    } else {
        rc = init_params.err_code;
        Log_e("MQTT Construct failed, rc = %d", rc);
        return QCLOUD_ERR_FAILURE;
    }

    //subscribe normal topics here
    rc = _subscribe_raw_data_topic(client, QOS1);
    if (rc < 0) {
        Log_e("Client Subscribe raw data topic Failed: %d", rc);
        return rc;
    }

    do {

        rc = IOT_MQTT_Yield(client, 3000);
        if (rc == QCLOUD_ERR_MQTT_ATTEMPTING_RECONNECT) {
            HAL_SleepMs(1000);
            continue;
        } else if (rc != QCLOUD_RET_SUCCESS && rc != QCLOUD_RET_MQTT_RECONNECTED) {
            Log_e("exit with error: %d", rc);
            break;
        }

        if (sg_sub_packet_id > 0) {
            rc = _publish_raw_data_msg(client, QOS0);
            if (rc < 0) {
                Log_e("client publish topic failed :%d.", rc);
            }
        }

        HAL_SleepMs(10000);
    } while (sg_running_state);

    rc = IOT_MQTT_Destroy(&client);

    return rc;
}
