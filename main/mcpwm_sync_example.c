/* MCPWM sync example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

/*
 * This example will show you how to use capture function to read HC-SR04 sonar sensor.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_rom_gpio.h"
#include "soc/mcpwm_periph.h"
#include "hal/gpio_hal.h"
#include "esp_log.h"
#include "esp_check.h"
#include "soc/rtc.h"
#include "driver/mcpwm.h"
#include "driver/ledc.h"
#include "hal/ledc_types.h"


const static char *TAG = "sync_example";

#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_DUTY_RES           LEDC_TIMER_10_BIT // Set duty resolution to 13 bits

static void init_ledc(int channel, int gpio, int duty) 
{


    // Prepare and then apply the LEDC PWM timer configuration
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .timer_num        = LEDC_TIMER,
        .duty_resolution  = LEDC_DUTY_RES,
        .freq_hz          = 50000,  // Set output frequency at 5 kHz
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // Prepare and then apply the LEDC PWM channel configuration
    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_MODE,
        .channel        = channel,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = gpio,
        .duty           = duty, // Set duty to 0%
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, channel, duty));
    // Update duty to apply the new value
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, channel));
}

void app_main(void)
{
    ESP_LOGI(TAG, "MCPWM sync example");

    init_ledc(LEDC_CHANNEL_0, 13, 128);
    init_ledc(LEDC_CHANNEL_1, 14, 256);
    init_ledc(LEDC_CHANNEL_2, 15, 512);
    init_ledc(LEDC_CHANNEL_3, 16, 768);
    init_ledc(LEDC_CHANNEL_4, 17, 200);
    init_ledc(LEDC_CHANNEL_5, 18, 300);
    init_ledc(LEDC_CHANNEL_6, 19, 768);
    init_ledc(LEDC_CHANNEL_7, 21, 512);

    mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM0A, 1);
    mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM0B, 2);
    mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM1A, 3);
    mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM1B, 4);
    mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM2A, 5);
    mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM2B, 6);
    mcpwm_gpio_init(MCPWM_UNIT_1, MCPWM0A, 7);
    mcpwm_gpio_init(MCPWM_UNIT_1, MCPWM0B, 8);
    mcpwm_gpio_init(MCPWM_UNIT_1, MCPWM1A, 9);
    mcpwm_gpio_init(MCPWM_UNIT_1, MCPWM1B, 10);
    mcpwm_gpio_init(MCPWM_UNIT_1, MCPWM2A, 11);
    mcpwm_gpio_init(MCPWM_UNIT_1, MCPWM2B, 12);

    mcpwm_config_t pwm_config = {
        .frequency = 50000, // 
        .cmpr_a = 0,     // duty cycle of PWMxA = 0
        .cmpr_b = 0,     // duty cycle of PWMxb = 0
        .counter_mode = MCPWM_UP_COUNTER,
        .duty_mode = MCPWM_DUTY_MODE_0,
    };
    mcpwm_init(MCPWM_UNIT_0, MCPWM_TIMER_0, &pwm_config);
    mcpwm_init(MCPWM_UNIT_0, MCPWM_TIMER_1, &pwm_config);
    mcpwm_init(MCPWM_UNIT_0, MCPWM_TIMER_2, &pwm_config);
    mcpwm_init(MCPWM_UNIT_1, MCPWM_TIMER_0, &pwm_config);
    mcpwm_init(MCPWM_UNIT_1, MCPWM_TIMER_1, &pwm_config);
    mcpwm_init(MCPWM_UNIT_1, MCPWM_TIMER_2, &pwm_config);

    while (1)
    {
        mcpwm_set_duty(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A, 10);
        mcpwm_set_duty(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_B, 12);
        mcpwm_set_duty(MCPWM_UNIT_0, MCPWM_TIMER_1, MCPWM_OPR_A, 14);
        mcpwm_set_duty(MCPWM_UNIT_0, MCPWM_TIMER_1, MCPWM_OPR_B, 16);
        mcpwm_set_duty(MCPWM_UNIT_0, MCPWM_TIMER_2, MCPWM_OPR_A, 18);
        mcpwm_set_duty(MCPWM_UNIT_0, MCPWM_TIMER_2, MCPWM_OPR_B, 20);
        
        mcpwm_set_duty(MCPWM_UNIT_1, MCPWM_TIMER_0, MCPWM_OPR_A, 50);
        mcpwm_set_duty(MCPWM_UNIT_1, MCPWM_TIMER_0, MCPWM_OPR_B, 55);
        mcpwm_set_duty(MCPWM_UNIT_1, MCPWM_TIMER_1, MCPWM_OPR_A, 60);
        mcpwm_set_duty(MCPWM_UNIT_1, MCPWM_TIMER_1, MCPWM_OPR_B, 65);
        mcpwm_set_duty(MCPWM_UNIT_1, MCPWM_TIMER_2, MCPWM_OPR_A, 70);
        mcpwm_set_duty(MCPWM_UNIT_1, MCPWM_TIMER_2, MCPWM_OPR_B, 75);
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
}
