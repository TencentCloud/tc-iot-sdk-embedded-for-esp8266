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


#ifndef __QCLOUD_WIFI_BOARDING_H__
#define __QCLOUD_WIFI_BOARDING_H__


int start_softAP(const char *ssid, const char *psw, uint8_t ch);

void stop_softAP(void);

int start_smartconfig(void);

void stop_smartconfig(void);

bool is_wifi_boarding_successful(void);


#endif //__QCLOUD_WIFI_BOARDING_H__
