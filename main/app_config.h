#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "driver/gpio.h"
#include "hal/adc_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

// --- CẤU HÌNH ĐỒNG BỘ TẦN SỐ ---
#define RATIO_PCG_ECG       40
#define BATCH_ROWS_COUNT    100
#define CSV_BUFFER_SIZE     (BATCH_ROWS_COUNT * 250)

// --- CẤU HÌNH ECG (AD8232) --- ESP32-WROOM-32
#define AD8232_LO_PLUS_PIN  GPIO_NUM_4
#define AD8232_LO_MINUS_PIN GPIO_NUM_16
#define ECG_ADC_UNIT        ADC_UNIT_1
#define ECG_ADC_CHANNEL     ADC_CHANNEL_0  // ADC1_CH0 = GPIO36

// --- CẤU HÌNH PCG (ICS-43434) ---
#define I2S_MIC_PORT        I2S_NUM_0
#define I2S_BCLK_PIN        GPIO_NUM_26
#define I2S_WS_PIN          GPIO_NUM_25
#define I2S_DATA_PIN        GPIO_NUM_34   // Input-only GPIO, đúng cho I2S DATA IN
#define PCG_SAMPLE_RATE     16000

// --- SD CARD (SPI) — comment out khi chưa dùng ---
// #define SD_SPI_HOST      SPI2_HOST
// #define SD_MISO_PIN      GPIO_NUM_12
// #define SD_MOSI_PIN      GPIO_NUM_13
// #define SD_CLK_PIN       GPIO_NUM_14
// #define SD_CS_PIN        GPIO_NUM_15
// #define SD_MOUNT_POINT   "/sdcard"

// --- STRUCT DỮ LIỆU ---
typedef struct {
    uint64_t timestamp_us;
    int32_t  ecg_raw_val;
    int32_t  pcg_raw_mono[RATIO_PCG_ECG];
    uint8_t  leads_off_flag;
} raw_record_t;

typedef struct {
    char    *buffer_ptr;
    uint32_t valid_bytes;
    uint8_t  needs_free;   // 1 = task_sd_flash phải free() sau khi dùng (header)
                           // 0 = buffer tái sử dụng (buffer_A/B), không free
} csv_batch_t;

// --- BIẾN TOÀN CỤC FreeRTOS ---
extern QueueHandle_t      xQueue_RawData;
extern QueueHandle_t      xQueue_CSVBatch;
extern SemaphoreHandle_t  xSD_Mutex;

#endif // APP_CONFIG_H