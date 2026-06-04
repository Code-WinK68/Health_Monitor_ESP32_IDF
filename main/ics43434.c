/* @file  ESP32_ICS43434.c
   @brief this code involves ESP32 and ICS43434 mems microphone 
          i2s implementation with serial uart transmission.
   @author Shyam Jha (Avinashee Tech)
*/

#include <stdio.h>
#include "i2s.h"
#include "uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

//macro to enable polling time calculation - watch video for clarity
#define GET_POLLING_TIME 0

//variables
int32_t r_buf[SAMPLE_BUFFER_SIZE];    //application user buffer
size_t bytes_read = 0;

/**
 * @brief  main application
 * @param  None
 * @retval None
 */
void app_main(void)
{
    //local variables
    static const char *TX_TASK_TAG = "TX";
    int64_t last_us = 0;
    int64_t max_poll_us = 0;

    // Init I2S
    i2s_setup();

    // Init UART 1
    uart_init(); 

    while(1){

#if GET_POLLING_TIME
        int64_t now_us = esp_timer_get_time();
        if (last_us != 0) {
            int64_t gap = now_us - last_us;
            if (gap > max_poll_us) {
                max_poll_us = gap;
                printf("polling time: %lld us\n", max_poll_us);
            }
        }

        last_us = now_us;
#endif
        i2s_readsamples(r_buf,&bytes_read);    //read i2s samples  
        int samples_read = bytes_read / sizeof(int32_t);  //calculate samples read
        uart_sendData(TX_TASK_TAG, (uint8_t*)r_buf, samples_read*sizeof(int32_t)); //transmit samples read over UART

    }
}
