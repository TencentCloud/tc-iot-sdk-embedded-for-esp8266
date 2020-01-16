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


#ifndef __BOARD_OPS_H__
#define __BOARD_OPS_H__

#include "esp_err.h"


/* ESP8266-Launcher board */

#define     GPIO_WIFI_LED   (12)

#define     GPIO_RELAY_LED  (15)


#define     GPIO_SET        (1)
#define     GPIO_CLEAR      (0)

#define     WIFI_LED_ON          GPIO_CLEAR
#define     WIFI_LED_OFF         GPIO_SET

#define     RELAY_LED_ON          GPIO_SET
#define     RELAY_LED_OFF         GPIO_CLEAR

void board_init(void);

esp_err_t set_wifi_led_state(uint32_t state);
esp_err_t set_relay_led_state(uint32_t state);


#endif //__BOARD_OPS_H__

