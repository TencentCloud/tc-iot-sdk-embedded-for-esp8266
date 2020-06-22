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

#ifndef _QCLOUD_IOT_OTA_ESP_H_
#define _QCLOUD_IOT_OTA_ESP_H_

#include "qcloud_iot_export.h"

/**
 * @brief Start OTA background task to do firmware update
 *
 * @param dev_info:     Qcloud IoT device info
 * @param mqtt_client:  Qcloud IoT MQTT client handle
 * @param version:      Local firmware version
 *
 * @return 0 when success, or err code for failure
 */
int enable_ota_task(DeviceInfo *dev_info, void *mqtt_client, char *version);

/**
 * @brief Stop OTA background task
 *
 * @return 0 when success, or err code for failure
 */
int disable_ota_task(void);

/**
 * @brief Check if OTA task is in downloading state or not
 *
 * @return true when downloading, or false for not downloading
 */
bool is_fw_downloading(void);

#endif  // _QCLOUD_IOT_OTA_ESP_H_

