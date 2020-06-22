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

#include "qcloud_iot_export.h"
#include "qcloud_iot_import.h"
#include "qcloud_iot_demo.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

DeviceInfo sg_devinfo = {0};

static int sg_sub_packet_id = -1;

static void _mqtt_event_handler(void *pclient, void *handle_context, MQTTEventMsg *msg)
{
    MQTTMessage *mqtt_messge = (MQTTMessage *)msg->msg;
    uintptr_t    packet_id   = (uintptr_t)msg->msg;

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
                  mqtt_messge->topic_len, mqtt_messge->ptopic, mqtt_messge->payload_len, mqtt_messge->payload);
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

static void on_message_callback(void *pClient, MQTTMessage *message, void *userData)
{
    if (message == NULL) {
        Log_e("msg null");
        return;
    }

    if (message->topic_len == 0 && message->payload_len == 0) {
        Log_e("length zero");
        return;
    }

    Log_d("recv msg topic: %s", message->ptopic);

    uint32_t msg_topic_len = message->payload_len + 4;
    char *   buf           = (char *)HAL_Malloc(msg_topic_len);
    if (buf == NULL) {
        Log_e("malloc %u bytes failed, topic: %s", msg_topic_len, message->ptopic);
        return;
    }

    memset(buf, 0, msg_topic_len);
    memcpy(buf, message->payload, message->payload_len);

    Log_d("msg payload: %s", buf);

    HAL_Free(buf);
}

static int _setup_connect_init_params(MQTTInitParams *initParams)
{
    int ret = HAL_GetDevInfo(&sg_devinfo);
    if (ret) {
        Log_e("Load device info from flash error");
        return ret;
    }

    initParams->product_id    = sg_devinfo.product_id;
    initParams->device_name   = sg_devinfo.device_name;
    initParams->device_secret = sg_devinfo.device_secret;

    initParams->command_timeout        = 10 * 1000;
    initParams->keep_alive_interval_ms = QCLOUD_IOT_MQTT_KEEP_ALIVE_INTERNAL;

    initParams->auto_connect_enable  = 1;
    initParams->event_handle.h_fp    = _mqtt_event_handler;
    initParams->event_handle.context = NULL;

    return QCLOUD_RET_SUCCESS;
}

static int _publish_msg(void *client, int qos)
{
    char topicName[128] = {0};
    sprintf(topicName, "%s/%s/%s", sg_devinfo.product_id, sg_devinfo.device_name, "data");

    PublishParams pub_params = DEFAULT_PUB_PARAMS;
    pub_params.qos           = qos;

    char topic_content[128] = {0};

    int size = HAL_Snprintf(topic_content, sizeof(topic_content), "{\"action\": \"publish_test\", \"time\": \"%d\"}",
                            HAL_Timer_current_sec());
    if (size < 0 || size > sizeof(topic_content) - 1) {
        Log_e("payload content length not enough! content size:%d  buf size:%d", size, (int)sizeof(topic_content));
        return -3;
    }

    pub_params.payload     = topic_content;
    pub_params.payload_len = strlen(topic_content);

    return IOT_MQTT_Publish(client, topicName, &pub_params);
}

static int _register_subscribe_topics(void *client, char *key_word, int qos)
{
    char topic_name[128] = {0};
    int  size = HAL_Snprintf(topic_name, sizeof(topic_name), "%s/%s/%s", sg_devinfo.product_id, sg_devinfo.device_name,
                            key_word);
    if (size < 0 || size > sizeof(topic_name) - 1) {
        Log_e("topic content length not enough! content size:%d  buf size:%d", size, (int)sizeof(topic_name));
        return QCLOUD_ERR_FAILURE;
    }
    SubscribeParams sub_params    = DEFAULT_SUB_PARAMS;
    sub_params.qos                = qos;
    sub_params.on_message_handler = on_message_callback;
    return IOT_MQTT_Subscribe(client, topic_name, &sub_params);
}

#ifdef MULTITHREAD_ENABLED
#define YEILD_THREAD_STACK_SIZE  2048
#define THREAD_SLEEP_INTERVAL_MS 200
static bool sg_yield_thread_running = true;

static void mqtt_yield_thread(void *ptr)
{
    int   rc      = QCLOUD_RET_SUCCESS;
    void *pClient = ptr;
    Log_d("template yield thread start ...");
    while (sg_yield_thread_running) {
        rc = IOT_MQTT_Yield(pClient, 200);
        if (rc == QCLOUD_ERR_MQTT_ATTEMPTING_RECONNECT) {
            HAL_SleepMs(THREAD_SLEEP_INTERVAL_MS);
            continue;
        } else if (rc != QCLOUD_RET_SUCCESS && rc != QCLOUD_RET_MQTT_RECONNECTED) {
            Log_e("Something goes error: %d", rc);
        }
        HAL_SleepMs(THREAD_SLEEP_INTERVAL_MS);
    }
    return ;
}
#endif

int qcloud_iot_hub_demo(void)
{
    int  rc;
    int  count        = 0;
    int  pub_interval = 2;
    bool loop         = true;
    int  pub_qos = QOS0, sub_qos = QOS0;

    // init connection parameters
    MQTTInitParams init_params = DEFAULT_MQTTINIT_PARAMS;
    rc                         = _setup_connect_init_params(&init_params);
    if (rc != QCLOUD_RET_SUCCESS) {
        return rc;
    }

    // connect to MQTT server
    void *client = IOT_MQTT_Construct(&init_params);
    if (client != NULL) {
        Log_i("Cloud Device Construct Success");
    } else {
        Log_e("Cloud Device Construct Failed");
        return QCLOUD_ERR_FAILURE;
    }

#ifdef SYSTEM_COMM
    long time = 0;

    rc = IOT_Get_SysTime(client, &time);
    if (QCLOUD_RET_SUCCESS == rc) {
        Log_i("system time is %ld", time);
    } else {
        Log_e("get system time failed!");
    }
#endif

    // register subscribe topics here
    rc = _register_subscribe_topics(client, "data", sub_qos);
    if (rc < 0) {
        Log_e("Client Subscribe Topic Failed: %d", rc);
        goto exit;
    }

    rc = _register_subscribe_topics(client, "control", QOS0);
    if (rc < 0) {
        Log_e("Client Subscribe Topic Failed: %d", rc);
        goto exit;
    }

#ifdef MULTITHREAD_ENABLED
    
    ThreadParams thread_params      = {0};
    thread_params.thread_func       = mqtt_yield_thread;
    thread_params.thread_name       = "mqtt_yield_thread";
    thread_params.user_arg          = client;
    thread_params.stack_size        = 4096;
    thread_params.priority          = 1;

    rc = HAL_ThreadCreate(&thread_params);
    if (QCLOUD_RET_SUCCESS != rc) {
        Log_e("create yield thread fail");
        goto exit;
    }
#endif

    rc = enable_ota_task(&sg_devInfo, client, "1.0.0");
    if (rc)
        Log_e("Start OTA task failed!");

    do {
#ifndef MULTITHREAD_ENABLED
        rc = IOT_MQTT_Yield(client, 200);
        if (rc == QCLOUD_ERR_MQTT_ATTEMPTING_RECONNECT) {
            HAL_SleepMs(1000);
            continue;
        } else if (rc != QCLOUD_RET_SUCCESS && rc != QCLOUD_RET_MQTT_RECONNECTED) {
            Log_e("exit with error: %d", rc);
            break;
        }
#endif
        if (is_fw_downloading()) {
            //Log_d("waiting for OTA firmware update...");
            HAL_SleepMs(500);
            continue;
        }

        // wait for sub result
        if (sg_sub_packet_id > 0 && !(count % pub_interval)) {
            count = 0;
            rc    = _publish_msg(client, pub_qos);
            if (rc < 0) {
                Log_e("client publish topic failed :%d.", rc);
            }
        }

        HAL_SleepMs(1000);
        count++;

    } while (loop);

exit:

    disable_ota_task();

#ifndef MULTITHREAD_ENABLED
    sg_yield_thread_running = false;
    HAL_SleepMs(THREAD_SLEEP_INTERVAL_MS);
#endif

    rc = IOT_MQTT_Destroy(&client);
    return rc;
}


