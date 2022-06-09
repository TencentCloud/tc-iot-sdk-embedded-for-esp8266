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
#include "driver/pwm.h"

#include "board_ops.h"

uint32_t g_wifi_led_gpio  = GPIO_WIFI_LED;
uint32_t g_relay_led_gpio = GPIO_RELAY_LED;

#define GPIO_SET   (1)
#define GPIO_CLEAR (0)

#define WIFI_LED_ON  GPIO_CLEAR
#define WIFI_LED_OFF GPIO_SET

#define RELAY_LED_ON  GPIO_SET
#define RELAY_LED_OFF GPIO_CLEAR

esp_err_t set_wifi_led_state(uint32_t state)
{
    if (state == LED_ON)
        return gpio_set_level(g_wifi_led_gpio, GPIO_CLEAR);
    else
        return gpio_set_level(g_wifi_led_gpio, GPIO_SET);
}

esp_err_t set_relay_led_state(uint32_t state)
{
    if (state == LED_ON)
        return gpio_set_level(g_relay_led_gpio, GPIO_SET);
    else
        return gpio_set_level(g_relay_led_gpio, GPIO_CLEAR);
}

static void led_gpio_init(void)
{
    gpio_config_t ioconfig;

    ioconfig.pin_bit_mask = (1ULL << GPIO_LED_R);
    ioconfig.intr_type    = GPIO_INTR_DISABLE;
    ioconfig.mode         = GPIO_MODE_OUTPUT;
    ioconfig.pull_up_en   = GPIO_PULLUP_ENABLE;
    gpio_config(&ioconfig);

    ioconfig.pin_bit_mask = (1ULL << GPIO_LED_G);
    ioconfig.intr_type    = GPIO_INTR_DISABLE;
    ioconfig.mode         = GPIO_MODE_OUTPUT;
    ioconfig.pull_up_en   = GPIO_PULLUP_DISABLE;
    gpio_config(&ioconfig);

    ioconfig.pin_bit_mask = (1ULL << GPIO_LED_B);
    ioconfig.intr_type    = GPIO_INTR_DISABLE;
    ioconfig.mode         = GPIO_MODE_OUTPUT;
    ioconfig.pull_up_en   = GPIO_PULLUP_DISABLE;
    gpio_config(&ioconfig);

    gpio_set_level(GPIO_LED_R, LED_OFF);
    gpio_set_level(GPIO_LED_G, LED_OFF);
    gpio_set_level(GPIO_LED_B, LED_OFF);
}

void led_pwm_init(void)
{
    /* 初始化 PWM，周期, pwm_duty_init占空比, 3通道数, 通道 IO */
    uint32_t io_info[PWM_CHANNELS]       = {PWM_R_OUT_IO_NUM, PWM_G_OUT_IO_NUM, PWM_B_OUT_IO_NUM};
    uint32_t pwm_duty_init[PWM_CHANNELS] = {0, 0, 0};
    // phase table, (phase/180)*depth
    int16_t phase[PWM_CHANNELS] = {0, 0, 0};
    led_gpio_init();
    pwm_init(100, pwm_duty_init, PWM_CHANNELS, io_info);
    pwm_set_phases(phase);
}

void led_pwm_set_brightness(uint8_t power_switch, eLED_COLOR color, uint8_t brightness)
{
    pwm_set_duty(LED_COLOR_RED, 0);
    pwm_set_duty(LED_COLOR_GREEN, 0);
    pwm_set_duty(LED_COLOR_BLUE, 0);
    if (power_switch) {
        pwm_set_duty(color, brightness);
        pwm_start();
    } else {
        pwm_stop(0);  // 关闭PWM通道 并保持为低电平
    }
}

static void button_init(void)
{
    gpio_config_t io_conf;
    // interrupt of rising edge
    io_conf.intr_type = GPIO_INTR_DISABLE;
    // bit mask of the pins, use GPIO4/5 here
    io_conf.pin_bit_mask = (1ULL << GPIO_BUTTON_1);
    // set as input mode
    io_conf.mode = GPIO_MODE_INPUT;
    // enable pull-up mode
    io_conf.pull_up_en = 1;
    gpio_config(&io_conf);
}

int get_button_state(int gpio_num)
{
    return gpio_get_level(gpio_num);
}

void board_init(void)
{
    led_gpio_init();
    button_init();
}
