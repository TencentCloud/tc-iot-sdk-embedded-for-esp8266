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

#ifndef QCLOUD_IOT_EXPLORER_ESP8266_MAIN_BOARD_OPS_H_
#define QCLOUD_IOT_EXPLORER_ESP8266_MAIN_BOARD_OPS_H_

#include "esp_err.h"

/* ESP8266-NodeMCU board v1.2*/

#define GPIO_LED_R (4)
#define GPIO_LED_G (2)
#define GPIO_LED_B (5)

#define GPIO_BUTTON_1 (12)

#define GPIO_WIFI_LED  GPIO_LED_B
#define GPIO_RELAY_LED GPIO_LED_G

#define LED_ON  (1)
#define LED_OFF (0)

#define PWM_CHANNELS (3)

#define PWM_R_OUT_IO_MUX  PERIPHS_IO_MUX_GPIO4_U
#define PWM_R_OUT_IO_NUM  4
#define PWM_R_OUT_IO_FUNC FUNC_GPIO4

#define PWM_G_OUT_IO_MUX  PERIPHS_IO_MUX_GPIO2_U
#define PWM_G_OUT_IO_NUM  2
#define PWM_G_OUT_IO_FUNC FUNC_GPIO2

#define PWM_B_OUT_IO_MUX  PERIPHS_IO_MUX_GPIO5_U
#define PWM_B_OUT_IO_NUM  5
#define PWM_B_OUT_IO_FUNC FUNC_GPIO5

typedef enum {
    LED_COLOR_RED = 0,
    LED_COLOR_GREEN,
    LED_COLOR_BLUE,
    LED_COLOR_MAX,
} eLED_COLOR;

void board_init(void);
int  get_button_state(int gpio_num);

void led_pwm_init(void);
void led_pwm_set_brightness(uint8_t power_switch, eLED_COLOR color, uint8_t brightness);

esp_err_t set_wifi_led_state(uint32_t state);
esp_err_t set_relay_led_state(uint32_t state);

#endif  // QCLOUD_IOT_EXPLORER_ESP8266_MAIN_BOARD_OPS_H_
