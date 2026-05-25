#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"11111111111111111111111111111
#include "driver/gpio.h"
#include <portmacro.h>


//#define LED_PIN GPIO_NUM_48


void app_main(void)
{
    // Thiết lập thông số
    gpio_config_t Gpio_config= {};
        Gpio_config.pin_bit_mask = (1ULL << 48);          
        Gpio_config.mode         = GPIO_MODE_OUTPUT;                 
        Gpio_config.pull_up_en   = GPIO_PULLDOWN_DISABLE;           
        Gpio_config.pull_down_en = GPIO_PULLDOWN_DISABLE;         
        Gpio_config.intr_type    = GPIO_INTR_DISABLE;             

    gpio_config(&Gpio_config);

    while(1){
        gpio_set_level(48, 0);   // LED sang
        vTaskDelay(2000/ portTICK_PERIOD_MS);

        gpio_set_level(48, 1);   // LED tat
        vTaskDelay(2000/ portTICK_PERIOD_MS);
    }
}
