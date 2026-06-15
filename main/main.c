#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "app_config.h"
#include "hardware_drivers.h"
#include "tasks_pipeline.h"

static const char *TAG = "MAIN_APP";

QueueHandle_t     xQueue_RawData  = NULL;
QueueHandle_t     xQueue_CSVBatch = NULL;
SemaphoreHandle_t xSD_Mutex       = NULL;

void app_main(void)
{
    ESP_LOGI(TAG, "=================================================");
    ESP_LOGI(TAG, "KHOI CHAY HE THONG DONG BO TREN ESP32-WROOM-32");
    ESP_LOGI(TAG, "ECG 400Hz (AD8232) + PCG 16kHz (ICS-43434)");
    ESP_LOGI(TAG, "=================================================");

    // FIX: Giảm Queue từ 128 → 44 slot
    // 128 × 173 byte = ~22KB — quá lớn cho ESP32 (~300KB heap)
    // 44 × 173 byte  = ~7.6KB — đủ buffer cho pipeline
    xQueue_RawData  = xQueueCreate(44, sizeof(raw_record_t));
    xQueue_CSVBatch = xQueueCreate(4,  sizeof(csv_batch_t));
    xSD_Mutex       = xSemaphoreCreateMutex(); // Reserved cho SD Card sau này

    if (!xQueue_RawData || !xQueue_CSVBatch || !xSD_Mutex) {
        ESP_LOGE(TAG, "FATAL: Khong du RAM khoi tao FreeRTOS entities!");
        return;
    }
    ESP_LOGI(TAG, "FreeRTOS Queue + Mutex OK");

    // Khởi tạo phần cứng
    if (init_gpio_leads_off()   != ESP_OK) { ESP_LOGE(TAG, "FATAL: Loi LO GPIO");    return; }
    if (init_adc_oneshot_ecg()  != ESP_OK) { ESP_LOGE(TAG, "FATAL: Loi ADC1");       return; }
    if (init_i2s_microphone()   != ESP_OK) { ESP_LOGE(TAG, "FATAL: Loi I2S Mic");    return; }
    ESP_LOGI(TAG, "Phan cung khoi tao thanh cong");

    // Tạo tasks — pin đúng core, priority đúng thứ tự
    //
    // Core 0: task_collector (time-critical — đọc I2S + ADC đồng bộ)
    // Core 1: task_csv_parser + task_sd_flash (I/O bound)
    //
    // FIX priority:
    //   task_collector: 5 (cao nhất — không được bị preempt)
    //   task_csv_parser: 3
    //   task_sd_flash:   2 (thấp nhất — UART/SD chậm)
    //
    // FIX stack task_csv_parser: 8192 (snprintf 100 rows × 250 chars tốn nhiều stack)
    // task_lo_poll đã bỏ — task_collector tự đọc LO + ADC trong cùng thời điểm

    xTaskCreatePinnedToCore(task_collector,  "Task_Collect", 4096, NULL, 5, NULL, 0);
    //xTaskCreatePinnedToCore(task_csv_parser, "Task_Parser",  8192, NULL, 3, NULL, 1);
    //xTaskCreatePinnedToCore(task_sd_flash,   "Task_SD",      4096, NULL, 2, NULL, 1);

    ESP_LOGI(TAG, "--- 3 TASK DA KHOI DONG ---");
    ESP_LOGI(TAG, "Output: UART Serial Monitor");
    ESP_LOGI(TAG, "Format: CSV (Timestamp, ECG, Leads_Off, PCG_1..PCG_40)");
}