/*
 * Tencent is pleased to support the open source community by making IoT Hub
 available.
 * Copyright (C) 2016 THL A29 Limited, a Tencent company. All rights reserved.

 * Licensed under the MIT License (the "License"); you may not use this file
 except in
 * compliance with the License. You may obtain a copy of the License at
 * http://opensource.org/licenses/MIT

 * Unless required by applicable law or agreed to in writing, software
 distributed under the License is
 * distributed on an "AS IS" basis, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 KIND,
 * either express or implied. See the License for the specific language
 governing permissions and
 * limitations under the License.
 *
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qcloud_iot_export.h"
#include "qcloud_iot_import.h"
#include "utils_getopt.h"

#include "qcloud_iot_demo.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef AUTH_MODE_CERT
/* NULL cert file */
#define QCLOUD_IOT_NULL_CERT_FILENAME "YOUR_DEVICE_CERT_FILE_NAME"
/* NULL key file */
#define QCLOUD_IOT_NULL_KEY_FILENAME "YOUR_DEVICE_PRIVATE_KEY_FILE_NAME"
#else
/* NULL device secret */
#define QCLOUD_IOT_NULL_DEVICE_SECRET "YOUR_IOT_PSK"
#endif

int qcloud_iot_explorer_demo(eDemoType eType)

{
    int        ret;
    DeviceInfo sDevInfo;
    bool       infoNullFlag = false;

    if (eDEMO_DYNREG != eType) {
        Log_e("Demo config (%d) illegal, please check", eType);
        return QCLOUD_ERR_FAILURE;
    }

    memset((char *)&sDevInfo, 0, sizeof(DeviceInfo));
    ret = HAL_GetDevInfo(&sDevInfo);

#ifndef GATEWAY_ENABLED
    sDevInfo.dev_type = eCOMMON_DEV;
#else
    sDevInfo.dev_type = eGW_SUB_DEV;
#endif

#ifdef AUTH_MODE_CERT
    /* just demo the cert/key files are empty */
    if (!strcmp(sDevInfo.dev_cert_file_name, QCLOUD_IOT_NULL_CERT_FILENAME) ||
        !strcmp(sDevInfo.dev_key_file_name, QCLOUD_IOT_NULL_KEY_FILENAME)) {
        Log_d("dev Cert not exist!");
        infoNullFlag = true;
    } else {
        Log_d("dev Cert exist");
    }
#else
    /* just demo the PSK is empty */
    if (!strcmp(sDevInfo.device_secret, QCLOUD_IOT_NULL_DEVICE_SECRET)) {
        Log_d("dev psk not exist!");
        infoNullFlag = true;
    } else {
        Log_d("dev psk exist");
    }
#endif

    /* device cert/key files or PSK is empty, do dynamic register to fetch */
    if (infoNullFlag) {
        if (QCLOUD_RET_SUCCESS == IOT_DynReg_Device(&sDevInfo)) {
            ret = HAL_SetDevInfo(&sDevInfo);
            if (QCLOUD_RET_SUCCESS != ret) {
                Log_e("devices info save fail");
            } else {
#ifdef AUTH_MODE_CERT
                Log_d(
                    "dynamic register success, productID: %s, devName: %s, CertFile: "
                    "%s, KeyFile: %s",
                    sDevInfo.product_id, sDevInfo.device_name, sDevInfo.dev_cert_file_name, sDevInfo.dev_key_file_name);
#else
                Log_d(
                    "dynamic register success,productID: %s, devName: %s, "
                    "device_secret: %s",
                    sDevInfo.product_id, sDevInfo.device_name, sDevInfo.device_secret);
#endif
            }
        } else {
            Log_e("%s dynamic register fail", sDevInfo.device_name);
        }
    }

    return ret;
}
