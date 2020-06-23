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
#include <limits.h>

#include "utils_getopt.h"
#include "qcloud_iot_export.h"
#include "qcloud_iot_demo.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define YEILD_THREAD_STACK_SIZE     4096
#define SUBDEV1_THREAD_STACK_SIZE   6144

#define MAX_SIZE_OF_TOPIC (128)
#define MAX_SIZE_OF_DATA (128)

//#define SUB_DEV_USE_DATA_TEMPLATE_LIGHT
#ifdef SUB_DEV_USE_DATA_TEMPLATE_LIGHT
#define LIGHT_SUB_DEV_PRODUCT_ID    "65HDW9BOM1"
#define LIGHT_SUB_DEV_NAME          "test_light01"
extern void* sub_dev1_thread(void *ptr, char *product_id, char *device_name);
extern bool g_subdev1_thread_running;
#endif


static int sg_sub_packet_id = -1;
static bool sg_gw_yield_thread_running = true;

static GatewayDeviceInfo sg_GWdevInfo;

#ifdef AUTH_MODE_CERT
static char sg_cert_file[PATH_MAX + 1];      // full path of device cert file
static char sg_key_file[PATH_MAX + 1];       // full path of device key file
#endif


void _event_handler(void *client, void *context, MQTTEventMsg *msg)
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

static void _message_handler(void *client, MQTTMessage *message, void *user_data)
{
    if (message == NULL) {
        return;
    }

    Log_i("Receive Message With topicName:%.*s, payload:%.*s",
          (int) message->topic_len, message->ptopic, (int) message->payload_len, (char *) message->payload);
}

static int _setup_connect_init_params(GatewayInitParam* init_params)
{
    int rc;

    rc = HAL_GetGwDevInfo((void *)&sg_GWdevInfo);
    if (QCLOUD_RET_SUCCESS != rc) {
        Log_e("Get gateway dev info err,rc:%d", rc);
        return rc;
    }

    init_params->init_param.product_id = sg_GWdevInfo.gw_info.product_id;
    init_params->init_param.device_name = sg_GWdevInfo.gw_info.device_name;

#ifdef AUTH_MODE_CERT
    char certs_dir[PATH_MAX + 1] = "certs";
    char current_path[PATH_MAX + 1];
    char *cwd = getcwd(current_path, sizeof(current_path));
    if (cwd == NULL) {
        Log_e("getcwd return NULL");
        return QCLOUD_ERR_FAILURE;
    }
    sprintf(sg_cert_file, "%s/%s/%s", current_path, certs_dir, sg_GWdevInfo.gw_info.dev_cert_file_name);
    sprintf(sg_key_file, "%s/%s/%s", current_path, certs_dir, sg_GWdevInfo.gw_info.dev_key_file_name);

    init_params->init_param.cert_file = sg_cert_file;
    init_params->init_param.key_file = sg_key_file;
#else
    init_params->init_param.device_secret = sg_GWdevInfo.gw_info.device_secret;
#endif

    init_params->init_param.command_timeout = QCLOUD_IOT_MQTT_COMMAND_TIMEOUT;
    init_params->init_param.auto_connect_enable = 1;
    init_params->init_param.event_handle.h_fp = _event_handler;
    init_params->init_param.keep_alive_interval_ms = QCLOUD_IOT_MQTT_KEEP_ALIVE_INTERNAL;

    return QCLOUD_RET_SUCCESS;
}

/**
 * yield thread runner
 */
static void my_gateway_yield_thread(void *ptr)
{

    int rc = QCLOUD_RET_SUCCESS;
    void *pClient = ptr;

    Log_d("gateway yield thread start ...");
    while (sg_gw_yield_thread_running) {
        rc = IOT_Gateway_Yield(pClient, 500);
        if (rc == QCLOUD_ERR_MQTT_ATTEMPTING_RECONNECT) {
            continue;
        } else if (rc != QCLOUD_RET_SUCCESS && rc != QCLOUD_RET_MQTT_RECONNECTED) {
            Log_e("Something goes error: %d", rc);
            //break;
        }
        HAL_SleepMs(500);
    }

    vTaskDelete(NULL);
    HAL_SleepMs(200);
    return NULL;
}

/**
 * sub dev thread runner
 */

#ifdef SUB_DEV_USE_DATA_TEMPLATE_LIGHT  //show subdev with data template. 
static void sub_dev_thread(void *ptr)
{
    sub_dev1_thread(ptr, LIGHT_SUB_DEV_PRODUCT_ID, LIGHT_SUB_DEV_NAME);
}
#endif


/*Gateway should enable multithread*/
int qcloud_iot_explorer_demo(eDemoType eType)
{
    int rc = QCLOUD_ERR_FAILURE;
    int errCount = 0;
    int i;
    int size;
    void* client = NULL;
    GatewayDeviceInfo *gw = &sg_GWdevInfo;
    GatewayParam param = DEFAULT_GATEWAY_PARAMS;
    DeviceInfo *subDevInfo;

    IOT_Log_Set_Level(eLOG_DEBUG);

    if (eDEMO_GATEWAY != eType) {
        Log_e("Demo config (%d) illegal, please check", eType);
        return QCLOUD_ERR_FAILURE;
    }

    GatewayInitParam init_params = DEFAULT_GATEWAY_INIT_PARAMS;
    rc = _setup_connect_init_params(&init_params);
    if (rc != QCLOUD_RET_SUCCESS) {
        Log_e("init params err,rc=%d", rc);
        return rc;
    }

    client = IOT_Gateway_Construct(&init_params);
    if (client == NULL) {
        Log_e("client constructed failed.");
        return QCLOUD_ERR_FAILURE;
    }

    //mqtt_yeild can be called by only one thread when multi-thread run, so do not use sync operation below
    ThreadParams gw_thread_params      = {0};
    gw_thread_params.thread_func       = my_gateway_yield_thread;
    gw_thread_params.thread_name       = "gateway_yield_thread";
    gw_thread_params.user_arg          = client;
    gw_thread_params.stack_size        = 6144;
    gw_thread_params.priority          = 3;
    rc = HAL_ThreadCreate(&gw_thread_params);
    if (rc != QCLOUD_RET_SUCCESS) {
        Log_e("create gw yield thread fail: %d", rc);
        goto exit;
    }

    HAL_SleepMs(1000); /*wait yield_thread start running*/

    //set GateWay device info
    param.product_id =  gw->gw_info.product_id;
    param.device_name = gw->gw_info.device_name;

    // make sub-device online
    for (i = 0; i < gw->sub_dev_num; i++) {
        subDevInfo = &gw->sub_dev_info[i];
        param.subdev_product_id = subDevInfo->product_id;
        param.subdev_device_name = subDevInfo->device_name;

        rc = IOT_Gateway_Subdev_Online(client, &param);
        if (rc != QCLOUD_RET_SUCCESS) {
            Log_e("subDev Pid:%s devName:%s online fail.", subDevInfo->product_id, subDevInfo->device_name);
            errCount++;
        } else {
            Log_d("subDev Pid:%s devName:%s online success.", subDevInfo->product_id, subDevInfo->device_name);
        }
    }

    if (errCount > 0) {
        Log_e("%d of %d sub devices online fail", errCount, gw->sub_dev_num);
    }

    //subscribe sub-device data_template down stream topic for example
    char topic_filter[MAX_SIZE_OF_TOPIC + 1];
    SubscribeParams sub_param = DEFAULT_SUB_PARAMS;
    for (i = 0; i < gw->sub_dev_num; i++) {
        subDevInfo = &gw->sub_dev_info[i];


#ifdef SUB_DEV_USE_DATA_TEMPLATE_LIGHT  //subdev with data template example.  
        if ((0 == strcmp(subDevInfo->product_id, LIGHT_SUB_DEV_PRODUCT_ID))
            && (0 == strcmp(subDevInfo->device_name, LIGHT_SUB_DEV_NAME))) {
            g_subdev1_thread_running = true;

            ThreadParams thread_params      = {0};
            thread_params.thread_func       = sub_dev_thread;
            thread_params.thread_name       = "sub_dev_thread";
            thread_params.user_arg          = client;
            thread_params.stack_size        = 6144;
            thread_params.priority          = 4;
            rc = HAL_ThreadCreate(&thread_params);
            if (rc != QCLOUD_RET_SUCCESS) {
                Log_e("create sub_dev light thread fail");
                g_subdev1_thread_running = false;
                goto exit;
            } else {
                Log_e("create sub_dev light thread success");
            }
            continue;
        }
#endif

        memset(topic_filter, 0, MAX_SIZE_OF_TOPIC + 1);
        size = HAL_Snprintf(topic_filter, MAX_SIZE_OF_TOPIC, "$thing/down/property/%s/%s", subDevInfo->product_id, subDevInfo->device_name);

        if (size < 0 || size > MAX_SIZE_OF_TOPIC) {
            Log_e("buf size < topic length!");
            rc = QCLOUD_ERR_FAILURE;
            goto exit;
        }

        sub_param.on_message_handler = _message_handler;
        rc = IOT_Gateway_Subscribe(client, topic_filter, &sub_param);
        if (rc < 0) {
            Log_e("IOT_Gateway_Subscribe fail.");
            return rc;
        }
    }

    HAL_SleepMs(2000); /*wait subcribe ack*/

//  publish to sub-device data_template up stream topic for example
    PublishParams pub_param = DEFAULT_PUB_PARAMS;
    pub_param.qos = QOS0;

    pub_param.payload = "{\"method\":\"report\",\"clientToken\":\"123\",\"params\":{}}";
    pub_param.payload_len = strlen(pub_param.payload);
    for (i = 0; i < gw->sub_dev_num; i++) {
        subDevInfo = &gw->sub_dev_info[i];

#ifdef SUB_DEV_USE_DATA_TEMPLATE_LIGHT
        if ((0 == strcmp(subDevInfo->product_id, LIGHT_SUB_DEV_PRODUCT_ID))
            && (0 == strcmp(subDevInfo->device_name, LIGHT_SUB_DEV_NAME))) {
            continue;
        }
#endif
        memset(topic_filter, 0, MAX_SIZE_OF_TOPIC + 1);
        size = HAL_Snprintf(topic_filter, MAX_SIZE_OF_TOPIC, "$thing/up/property/%s/%s", subDevInfo->product_id, subDevInfo->device_name);
        if (size < 0 || size > MAX_SIZE_OF_TOPIC) {
            Log_e("buf size < topic length!");
            return QCLOUD_ERR_FAILURE;
        }

        rc = IOT_Gateway_Publish(client, topic_filter, &pub_param);
        if (rc < 0) {
            Log_e("IOT_Gateway_Publish fail.");
        }
    }

#ifdef SUB_DEV_USE_DATA_TEMPLATE_LIGHT
    int subdev_run_cnt = 0;
    while (g_subdev1_thread_running && subdev_run_cnt++ < 20) {
        Log_d("subdev thread is running");
        HAL_SleepMs(2000);
    };
#else
    HAL_SleepMs(10000); /*wait reply over*/
#endif

exit:

    //set GateWay device info
    param.product_id =  gw->gw_info.product_id;
    param.device_name = gw->gw_info.device_name;
    // make sub-device offline
    errCount = 0;
    for (i = 0; i < gw->sub_dev_num; i++) {
        subDevInfo = &gw->sub_dev_info[i];
        param.subdev_product_id = subDevInfo->product_id;
        param.subdev_device_name = subDevInfo->device_name;

        rc = IOT_Gateway_Subdev_Offline(client, &param);
        if (rc != QCLOUD_RET_SUCCESS) {
            Log_e("subDev Pid:%s devName:%s offline fail.", subDevInfo->product_id, subDevInfo->device_name);
            errCount++;
        } else {
            Log_d("subDev Pid:%s devName:%s offline success.", subDevInfo->product_id, subDevInfo->device_name);
        }
    }

    if (errCount > 0) {
        Log_e("%d of %d sub devices offline fail", errCount, gw->sub_dev_num);
    }

    //stop running thread    
    sg_gw_yield_thread_running = false;
    HAL_SleepMs(2000); /*make sure no thread use client before destroy*/

#ifdef SUB_DEV_USE_DATA_TEMPLATE_LIGHT
    g_subdev1_thread_running = false;
    HAL_SleepMs(2000);
#endif
    rc = IOT_Gateway_Destroy(client);

    return rc;
}
