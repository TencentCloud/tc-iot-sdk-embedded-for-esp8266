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
#include "qcloud_iot_export.h"
#include "qcloud_iot_import.h"
#include "utils_getopt.h"
#include "lite-utils.h"
#include "data_config.c"

#include "board_ops.h"
#include "qcloud_iot_demo.h"
#include "qcloud_iot_ota_esp.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "time.h"

#ifdef AUTH_MODE_CERT
static char sg_cert_file[PATH_MAX + 1];  // full path of device cert file
static char sg_key_file[PATH_MAX + 1];   // full path of device key file
#endif

static DeviceInfo    sg_devInfo;
static MQTTEventType sg_subscribe_event_result = MQTT_EVENT_UNDEF;
static bool          sg_control_msg_arrived    = false;
static char          sg_data_report_buffer[2048];
static size_t        sg_data_report_buffersize = sizeof(sg_data_report_buffer) / sizeof(sg_data_report_buffer[0]);

#ifdef EVENT_POST_ENABLED

#include "events_config.c"
static void update_events_timestamp(sEvent *pEvents, int count)
{
    int i;

    for (i = 0; i < count; i++) {
        if (NULL == (&pEvents[i])) {
            Log_e("null event pointer");
            return;
        }
#ifdef EVENT_TIMESTAMP_USED
        pEvents[i].timestamp = time(NULL);  // should be UTC and accurate
#else
        pEvents[i].timestamp = 0;
#endif
    }
}

static void event_post_cb(void *pClient, MQTTMessage *msg)
{
    Log_d("Reply:%.*s", msg->payload_len, msg->payload);
    //    IOT_Event_clearFlag(pClient, FLAG_EVENT0 | FLAG_EVENT1 | FLAG_EVENT2);
}

// event check and post
static void eventPostCheck(void *client)
{
    int      i;
    int      rc;
    uint32_t eflag;
    uint8_t  event_count;
    sEvent * pEventList[EVENT_COUNTS];

    eflag = IOT_Event_getFlag(client);
    if ((EVENT_COUNTS > 0) && (eflag > 0)) {
        event_count = 0;
        for (i = 0; i < EVENT_COUNTS; i++) {
            if ((eflag & (1 << i)) & ALL_EVENTS_MASK) {
                pEventList[event_count++] = &(g_events[i]);
                update_events_timestamp(&g_events[i], 1);
                IOT_Event_clearFlag(client, (1 << i) & ALL_EVENTS_MASK);
            }
        }

        rc = IOT_Post_Event(client, sg_data_report_buffer, sg_data_report_buffersize, event_count, pEventList,
                            event_post_cb);
        if (rc < 0) {
            Log_e("events post failed: %d", rc);
        }
    }
}

#endif

#ifdef ACTION_ENABLED
#include "action_config.c"

// action : regist action and set the action handle callback, add your aciton logic here
static void OnActionCallback(void *pClient, const char *pClientToken, DeviceAction *pAction)
{
    int        i;
    sReplyPara replyPara;

    // do something base on input, just print as an sample
    DeviceProperty *pActionInput = pAction->pInput;
    for (i = 0; i < pAction->input_num; i++) {
        if (JSTRING == pActionInput[i].type) {
            Log_d("Input:[%s], data:[%s]", pActionInput[i].key, pActionInput[i].data);
            HAL_Free(pActionInput[i].data);
        } else {
            if (JINT32 == pActionInput[i].type) {
                Log_d("Input:[%s], data:[%d]", pActionInput[i].key, *((int *)pActionInput[i].data));
            } else if (JFLOAT == pActionInput[i].type) {
                Log_d("Input:[%s], data:[%f]", pActionInput[i].key, *((float *)pActionInput[i].data));
            } else if (JUINT32 == pActionInput[i].type) {
                Log_d("Input:[%s], data:[%u]", pActionInput[i].key, *((uint32_t *)pActionInput[i].data));
            }
        }
    }

    // construct output
    memset((char *)&replyPara, 0, sizeof(sReplyPara));
    replyPara.code       = eDEAL_SUCCESS;
    replyPara.timeout_ms = QCLOUD_IOT_MQTT_COMMAND_TIMEOUT;
    strcpy(replyPara.status_msg, "action execute success!");  // add the message about the action resault

    DeviceProperty *pActionOutnput = pAction->pOutput;
    (void)pActionOutnput;  // elimate warning
    // TO DO: add your aciont logic here and set output properties which will be reported by action_reply

    IOT_Action_Reply(pClient, pClientToken, sg_data_report_buffer, sg_data_report_buffersize, pAction, &replyPara);
}

static int _register_data_template_action(void *pTemplate_client)
{
    int i, rc;

    for (i = 0; i < TOTAL_ACTION_COUNTS; i++) {
        rc = IOT_Template_Register_Action(pTemplate_client, &g_actions[i], OnActionCallback);
        if (rc != QCLOUD_RET_SUCCESS) {
            rc = IOT_Template_Destroy(pTemplate_client);
            Log_e("register device data template action failed, err: %d", rc);
            return rc;
        } else {
            Log_i("data template action=%s registered.", g_actions[i].pActionId);
        }
    }

    return QCLOUD_RET_SUCCESS;
}
#endif

static void event_handler(void *pclient, void *handle_context, MQTTEventMsg *msg)
{
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

        case MQTT_EVENT_SUBCRIBE_SUCCESS:
            sg_subscribe_event_result = msg->event_type;
            Log_i("subscribe success, packet-id=%u", packet_id);
            break;

        case MQTT_EVENT_SUBCRIBE_TIMEOUT:
            sg_subscribe_event_result = msg->event_type;
            Log_i("subscribe wait ack timeout, packet-id=%u", packet_id);
            break;

        case MQTT_EVENT_SUBCRIBE_NACK:
            sg_subscribe_event_result = msg->event_type;
            Log_i("subscribe nack, packet-id=%u", packet_id);
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

/*add user init code, like sensor init*/
static void _usr_init(void)
{
    Log_d("add your init code here");
}

// Setup MQTT construct parameters
static int _setup_connect_init_params(TemplateInitParams *initParams)
{
    int ret;

    ret = HAL_GetDevInfo((void *)&sg_devInfo);
    if (QCLOUD_RET_SUCCESS != ret) {
        return ret;
    }

    initParams->device_name = sg_devInfo.device_name;
    initParams->product_id  = sg_devInfo.product_id;

#ifdef AUTH_MODE_CERT
    /* TLS with certs*/
    char  certs_dir[PATH_MAX + 1] = "certs";
    char  current_path[PATH_MAX + 1];
    char *cwd = getcwd(current_path, sizeof(current_path));
    if (cwd == NULL) {
        Log_e("getcwd return NULL");
        return QCLOUD_ERR_FAILURE;
    }
    sprintf(sg_cert_file, "%s/%s/%s", current_path, certs_dir, sg_devInfo.dev_cert_file_name);
    sprintf(sg_key_file, "%s/%s/%s", current_path, certs_dir, sg_devInfo.dev_key_file_name);

    initParams->cert_file = sg_cert_file;
    initParams->key_file  = sg_key_file;
#else
    initParams->device_secret = sg_devInfo.device_secret;
#endif

    initParams->command_timeout        = QCLOUD_IOT_MQTT_COMMAND_TIMEOUT;
    initParams->keep_alive_interval_ms = QCLOUD_IOT_MQTT_KEEP_ALIVE_INTERNAL;
    initParams->auto_connect_enable    = 1;
    initParams->event_handle.h_fp      = event_handler;
    initParams->usr_control_handle     = NULL;

    return QCLOUD_RET_SUCCESS;
}

static void OnControlMsgCallback(void *pClient, const char *pJsonValueBuffer, uint32_t valueLength,
                                 DeviceProperty *pProperty)
{
    int i = 0;

    for (i = 0; i < TOTAL_PROPERTY_COUNT; i++) {
        /* handle self defined string/json here. Other properties are dealed in _handle_delta()*/
        if (strcmp(sg_DataTemplate[i].data_property.key, pProperty->key) == 0) {
            sg_DataTemplate[i].state = eCHANGED;
            Log_i("Property=%s changed", pProperty->key);
            sg_control_msg_arrived = true;
            return;
        }
    }

    Log_e("Property=%s changed no match", pProperty->key);
}

static void OnReportReplyCallback(void *pClient, Method method, ReplyAck replyAck, const char *pJsonDocument,
                                  void *pUserdata)
{
    Log_i("recv report reply response, reply ack: %d", replyAck);
}

// register data template properties
static int _register_data_template_property(void *pTemplate_client)
{
    int i, rc;

    for (i = 0; i < TOTAL_PROPERTY_COUNT; i++) {
        rc = IOT_Template_Register_Property(pTemplate_client, &sg_DataTemplate[i].data_property, OnControlMsgCallback);
        if (rc != QCLOUD_RET_SUCCESS) {
            rc = IOT_Template_Destroy(pTemplate_client);
            Log_e("register device data template property failed, err: %d", rc);
            return rc;
        } else {
            Log_i("data template property=%s registered.", sg_DataTemplate[i].data_property.key);
        }
    }

    return QCLOUD_RET_SUCCESS;
}

// when control msg received, data_template's properties has been parsed in pData you should add your logic how to use
// pData
void deal_down_stream_user_logic(void *client, ProductDataDefine *pData)
{
    Log_d("someting about your own product logic wait to be done");

#ifdef EVENT_POST_ENABLED
    // IOT_Event_setFlag(client, FLAG_EVENT0);  //set the events flag when the evnts your defined occured, see
    // events_config.c
#endif
}

/*get local property data, like sensor data*/
static void _refresh_local_property(void)
{
    // add your local property refresh logic
}

/*find propery need report*/
static int find_wait_report_property(DeviceProperty *pReportDataList[])
{
    int i, j;

    for (i = 0, j = 0; i < TOTAL_PROPERTY_COUNT; i++) {
        if (eCHANGED == sg_DataTemplate[i].state) {
            pReportDataList[j++]     = &(sg_DataTemplate[i].data_property);
            sg_DataTemplate[i].state = eNOCHANGE;
        }
    }

    return j;
}

// demo for up-stream
// add changed properties to pReportDataList, then the changed properties would be reported
// you should add your own logic for how to get the changed properties
int deal_up_stream_user_logic(DeviceProperty *pReportDataList[], int *pCount)
{
    // refresh local property
    _refresh_local_property();

    /*find propery need report*/
    *pCount = find_wait_report_property(pReportDataList);

    return (*pCount > 0) ? QCLOUD_RET_SUCCESS : QCLOUD_ERR_FAILURE;
}

/*You should get the real info for your device, here just for example*/
static int _get_sys_info(void *handle, char *pJsonDoc, size_t sizeOfBuffer)
{
    /*platform info has at least one of module_hardinfo/module_softinfo/fw_ver property*/
    DeviceProperty plat_info[] = {
        {.key = "module_hardinfo", .type = TYPE_TEMPLATE_STRING, .data = "ESP8266"},
        {.key = "module_softinfo", .type = TYPE_TEMPLATE_STRING, .data = "V1.0"},
        {.key = "fw_ver", .type = TYPE_TEMPLATE_STRING, .data = QCLOUD_IOT_DEVICE_SDK_VERSION},
        {.key = "imei", .type = TYPE_TEMPLATE_STRING, .data = "11-22-33-44"},
        {.key = "lat", .type = TYPE_TEMPLATE_STRING, .data = "22.546015"},
        {.key = "lon", .type = TYPE_TEMPLATE_STRING, .data = "113.941125"},
        {.key = NULL, .data = NULL}  // end
    };

    /*self define info*/
    DeviceProperty self_info[] = {
        {.key = "append_info", .type = TYPE_TEMPLATE_STRING, .data = "your self define info"},
        {.key = NULL, .data = NULL}  // end
    };

    return IOT_Template_JSON_ConstructSysInfo(handle, pJsonDoc, sizeOfBuffer, plat_info, self_info);
}

static int parse_arguments(int argc, char **argv)
{
    int c;
    while ((c = utils_getopt(argc, argv, "c:l:")) != EOF) switch (c) {
            case 'c':
                if (HAL_SetDevInfoFile(utils_optarg))
                    return -1;
                break;

            default:
                HAL_Printf(
                    "usage: %s [options]\n"
                    "  [-c <config file for DeviceInfo>] \n",
                    argv[0]);
                return -1;
        }
    return 0;
}

int qcloud_iot_explorer_demo(eDemoType eType)
{
    int             rc;
    sReplyPara      replyPara;
    DeviceProperty *pReportDataList[TOTAL_PROPERTY_COUNT];
    int             ReportCont;

    if (eDEMO_DATA_TEMPLATE != eType) {
        Log_e("Demo config (%d) illegal, please check", eType);
        return QCLOUD_ERR_FAILURE;
    }
    // init log level
    IOT_Log_Set_Level(eLOG_DEBUG);

    // init connection
    TemplateInitParams init_params = DEFAULT_TEMPLATE_INIT_PARAMS;
    rc                             = _setup_connect_init_params(&init_params);
    if (rc != QCLOUD_RET_SUCCESS) {
        Log_e("init params err,rc=%d", rc);
        return rc;
    }

    void *client = IOT_Template_Construct(&init_params, NULL);
    if (client != NULL) {
        Log_i("Cloud Device Construct Success");
    } else {
        Log_e("Cloud Device Construct Failed");
        return QCLOUD_ERR_FAILURE;
    }

#ifdef MULTITHREAD_ENABLED
    if (QCLOUD_RET_SUCCESS != IOT_Template_Start_Yield_Thread(client)) {
        Log_e("start template yield thread fail");
        goto exit;
    }
#endif

    // user init
    _usr_init();

    // init data template
    _init_data_template();

    // register data template propertys here
    rc = _register_data_template_property(client);
    if (rc == QCLOUD_RET_SUCCESS) {
        Log_i("Register data template propertys Success");
    } else {
        Log_e("Register data template propertys Failed: %d", rc);
        goto exit;
    }

    // register data template actions here
#ifdef ACTION_ENABLED
    rc = _register_data_template_action(client);
    if (rc == QCLOUD_RET_SUCCESS) {
        Log_i("Register data template actions Success");
    } else {
        Log_e("Register data template actions Failed: %d", rc);
        goto exit;
    }
#endif

    // report device info, then you can manager your product by these info, like position
    rc = _get_sys_info(client, sg_data_report_buffer, sg_data_report_buffersize);
    if (QCLOUD_RET_SUCCESS == rc) {
        rc = IOT_Template_Report_SysInfo_Sync(client, sg_data_report_buffer, sg_data_report_buffersize,
                                              QCLOUD_IOT_MQTT_COMMAND_TIMEOUT);
        if (rc != QCLOUD_RET_SUCCESS) {
            Log_e("Report system info fail, err: %d", rc);
        }
    } else {
        Log_e("Get system info fail, err: %d", rc);
    }

    // get the property changed during offline
    rc = IOT_Template_GetStatus_sync(client, QCLOUD_IOT_MQTT_COMMAND_TIMEOUT);
    if (rc != QCLOUD_RET_SUCCESS) {
        Log_e("Get data status fail, err: %d", rc);
    } else {
        Log_d("Get data status success");
    }

    while (IOT_Template_IsConnected(client) || rc == QCLOUD_ERR_MQTT_ATTEMPTING_RECONNECT ||
           rc == QCLOUD_RET_MQTT_RECONNECTED || QCLOUD_RET_SUCCESS == rc) {
        rc = IOT_Template_Yield(client, 200);
        if (rc == QCLOUD_ERR_MQTT_ATTEMPTING_RECONNECT) {
            HAL_SleepMs(1000);
            continue;
        } else if (rc != QCLOUD_RET_SUCCESS) {
            Log_e("Exit loop caused of errCode: %d", rc);
        }

        /* handle control msg from server */
        if (sg_control_msg_arrived) {
            deal_down_stream_user_logic(client, &sg_ProductData);
            /* control msg should reply, otherwise server treat device didn't receive and retain the msg which would be
             * get by  get status*/
            memset((char *)&replyPara, 0, sizeof(sReplyPara));
            replyPara.code          = eDEAL_SUCCESS;
            replyPara.timeout_ms    = QCLOUD_IOT_MQTT_COMMAND_TIMEOUT;
            replyPara.status_msg[0] = '\0';  // add extra info to replyPara.status_msg when error occured

            rc = IOT_Template_ControlReply(client, sg_data_report_buffer, sg_data_report_buffersize, &replyPara);
            if (rc == QCLOUD_RET_SUCCESS) {
                Log_d("Contol msg reply success");
                sg_control_msg_arrived = false;
            } else {
                Log_e("Contol msg reply failed, err: %d", rc);
            }
        } else {
            Log_d("No control msg received...");
        }

        /*report msg to server*/
        /*report the lastest properties's status*/
        if (QCLOUD_RET_SUCCESS == deal_up_stream_user_logic(pReportDataList, &ReportCont)) {
            rc = IOT_Template_JSON_ConstructReportArray(client, sg_data_report_buffer, sg_data_report_buffersize,
                                                        ReportCont, pReportDataList);
            if (rc == QCLOUD_RET_SUCCESS) {
                rc = IOT_Template_Report(client, sg_data_report_buffer, sg_data_report_buffersize,
                                         OnReportReplyCallback, NULL, QCLOUD_IOT_MQTT_COMMAND_TIMEOUT);
                if (rc == QCLOUD_RET_SUCCESS) {
                    Log_i("data template reporte success");
                } else {
                    Log_e("data template reporte failed, err: %d", rc);
                }
            } else {
                Log_e("construct reporte data failed, err: %d", rc);
            }

        } else {
            // Log_d("no data need to be reported or someting goes wrong");
        }
#ifdef EVENT_POST_ENABLED
        eventPostCheck(client);
#endif
        HAL_SleepMs(3000);
    }

exit:

#ifdef MULTITHREAD_ENABLED
    IOT_Template_Stop_Yield_Thread(client);
#endif
    rc = IOT_Template_Destroy(client);

    return rc;
}
