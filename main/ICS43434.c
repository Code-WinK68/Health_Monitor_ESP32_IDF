
#include "driver/i2s.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define TAG "ICS43434"

// Setup I2S for ICS43434 microphone
void setup_i2s(void) {
    i2s_config_t i2s_config = {
        .mode = I2S_MODE_MASTER | I2S_MODE_RX,
        .sample_rate = 16000,
        .bits_per_sample = 32,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_I2S,
        .intr_alloc_flags = 0,
        .dma_buf_count = 4,
        .dma_buf_len = 1024,
    };
    
    i2s_pin_config_t pin_config = {
        .bck_io_num = 26,
        .ws_io_num = 25,
        .data_in_num = 22,
        .data_out_num = -1
    };
    
    i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &pin_config);
    i2s_start(I2S_NUM_0);
    
    ESP_LOGI(TAG, "I2S + ICS43434 ready!");
}

// Task to read audio samples from I2S and print them
void read_audio(void) {
    int32_t sample = 0;
    size_t bytes_read = 0;
    
    ESP_LOGI(TAG, "Audio.....\n");
    
    while (1) {
        i2s_read(I2S_NUM_0, &sample, sizeof(int32_t), &bytes_read, portMAX_DELAY);
        if (bytes_read > 0) {
            printf("%ld\n", sample);
        }
        vTaskDelay(1 / portTICK_PERIOD_MS);
    }
}


// Main application entry point
void app_main(void) {
    printf("ESP32 + ICS43434\n");
    setup_i2s();
    read_audio();
}