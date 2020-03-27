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


#include "driver/gpio.h"
#include "esp8266/pin_mux_register.h"

#include "board_ops.h"


uint32_t g_wifi_led_gpio = GPIO_WIFI_LED;
uint32_t g_relay_led_gpio = GPIO_RELAY_LED;


esp_err_t set_wifi_led_state(uint32_t state)
{
    return gpio_set_level(g_wifi_led_gpio, state);
}

esp_err_t set_relay_led_state(uint32_t state)
{
    return gpio_set_level(g_relay_led_gpio, state);
}


void board_init(void)
{
    gpio_config_t ioconfig;

    ioconfig.pin_bit_mask = (1ULL << g_wifi_led_gpio);
    ioconfig.intr_type = GPIO_INTR_DISABLE;
    ioconfig.mode = GPIO_MODE_OUTPUT;
    ioconfig.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&ioconfig);

    ioconfig.pin_bit_mask = (1ULL << g_relay_led_gpio);
    ioconfig.intr_type = GPIO_INTR_DISABLE;
    ioconfig.mode = GPIO_MODE_OUTPUT;
    ioconfig.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&ioconfig);

    set_wifi_led_state(WIFI_LED_OFF);
    set_relay_led_state(RELAY_LED_OFF);
}


