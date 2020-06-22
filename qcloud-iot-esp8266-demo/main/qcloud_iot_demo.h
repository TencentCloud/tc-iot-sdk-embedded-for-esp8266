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

#ifndef QCLOUD_IOT_DEMO_H_
#define QCLOUD_IOT_DEMO_H_
#if defined(__cplusplus)
extern "C" {
#endif

typedef enum {
    eDEMO_SMART_LIGHT = 0,
    eDEMO_GATEWAY     = 1,
    eDEMO_RAW_DATA    = 2,
    eDEMO_MQTT        = 3,
    eDEMO_DEFAULT
} eDemoType;


int qcloud_iot_explorer_demo(eDemoType eType);

int qcloud_iot_hub_demo(void);




#if defined(__cplusplus)
}
#endif
#endif  /* QCLOUD_IOT_DEMO_H_ */

