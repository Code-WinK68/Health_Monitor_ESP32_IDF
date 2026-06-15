#include "tasks_pipeline.h"
#include "app_config.h"
#include "hardware_drivers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"

static const char *PIPE_TAG = "PIPELINE";

// =====================================================================
// IIR Notch Filter 50Hz tại fs = 400Hz
// Công thức: y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2]
//                         - a1*y[n-1] - a2*y[n-2]
//
// FIX: Bản gốc thiếu hệ số b1 và a1 → filter không chặn đúng 50Hz
// Hệ số tính bằng bilinear transform, Q=10, f0=50Hz, fs=400Hz:
// =====================================================================
static const float NOTCH_B0 =  0.9686f;
static const float NOTCH_B1 = -1.6180f;   // -2*cos(2π*50/400)
static const float NOTCH_B2 =  0.9686f;
static const float NOTCH_A1 = -1.6180f;
static const float NOTCH_A2 =  0.9372f;

static inline int32_t iir_notch_filter(int32_t sample_in)
{
    static float x1 = 0.0f, x2 = 0.0f;
    static float y1 = 0.0f, y2 = 0.0f;

    float x0 = (float)sample_in;
    float y0 = (NOTCH_B0 * x0)
             + (NOTCH_B1 * x1)
             + (NOTCH_B2 * x2)
             - (NOTCH_A1 * y1)
             - (NOTCH_A2 * y2);

    x2 = x1; x1 = x0;
    y2 = y1; y1 = y0;

    return (int32_t)y0;
}

// =====================================================================
// task_collector — Core 0, Priority 5
//
// Thiết kế:
//   - Chạy theo nhịp I2S: i2s_channel_read() block đến khi đủ 40 mẫu PCG
//     → tương đương 40/16000 = 2.5ms = đúng 1 chu kỳ ECG 400Hz
//   - Sau khi có đủ 40 mẫu PCG, ĐỌC NGAY leads-off + ADC ECG tại chỗ
//     → ECG và PCG được lấy mẫu tại cùng một thời điểm, không qua biến global
//
// FIX so với bản gốc:
//   1. Bỏ task_lo_poll riêng biệt — tránh race condition qua biến volatile
//   2. Bỏ portENTER_CRITICAL quanh adc_oneshot_read() — hàm này có thể block
//   3. Timestamp lấy TRƯỚC khi đọc ADC để sát với thời điểm PCG xong
//   4. Log cảnh báo khi Queue full thay vì drop thầm lặng
// =====================================================================
void task_collector(void *pvParameters)
{
    ESP_LOGI(PIPE_TAG, "=== Task_Collector (Core 0, Pri 5) bat dau ===");

    i2s_chan_handle_t         i2s_rx  = get_i2s_rx_handle();
    adc_oneshot_unit_handle_t adc_hdl = get_adc_handle();

    raw_record_t record;
    int32_t      i2s_buf[RATIO_PCG_ECG];  // 40 × int32_t = 160 byte, stack OK
    size_t       bytes_read = 0;

    // Đếm drop để log định kỳ (không log từng lần — tốn UART bandwidth)
    uint32_t drop_count = 0;
    uint32_t record_count = 0;
    static uint32_t ok_count = 0;
    while (1) {
        // Bước 1: Chờ đủ 40 mẫu PCG từ I2S DMA (block ~2.5ms)
        esp_err_t ret = i2s_channel_read(i2s_rx, i2s_buf, sizeof(i2s_buf),
                                          &bytes_read, pdMS_TO_TICKS(100));

        if (ret != ESP_OK || bytes_read != sizeof(i2s_buf)) {
            ESP_LOGW(PIPE_TAG, "I2S read loi: ret=%s bytes=%d/%d",
                     esp_err_to_name(ret), bytes_read, sizeof(i2s_buf));
            continue;
        }

        // Bước 2: Timestamp ngay sau khi PCG frame hoàn chỉnh
        record.timestamp_us = esp_timer_get_time();

        // Bước 3: Đọc leads-off và ADC ECG tại cùng thời điểm
        // KHÔNG dùng portENTER_CRITICAL ở đây vì adc_oneshot_read có thể block
        int lo_plus  = gpio_get_level(AD8232_LO_PLUS_PIN);
        int lo_minus = gpio_get_level(AD8232_LO_MINUS_PIN);

        if (lo_plus == 1 || lo_minus == 1) {
            // Điện cực bong — ECG không hợp lệ
            record.leads_off_flag = 1;
            record.ecg_raw_val    = 0;
        } else {
            record.leads_off_flag = 0;
            int raw_adc = 0;
            if (adc_oneshot_read(adc_hdl, ECG_ADC_CHANNEL, &raw_adc) == ESP_OK) {
              //  record.ecg_raw_val = iir_notch_filter(raw_adc);
                record.ecg_raw_val = raw_adc;
            } else {
                record.ecg_raw_val = 0;
                ESP_LOGW(PIPE_TAG, "ADC read loi");
            }
        }

        // Bước 4: Xử lý 40 mẫu PCG — lấy 24-bit MSB từ 32-bit I2S frame
        for (int i = 0; i < RATIO_PCG_ECG; i++) {
            record.pcg_raw_mono[i] = i2s_buf[i] >> 8;
        }

        /*
        // Bước 5: Đưa record vào Queue — timeout=0 để không block collector
        record_count++;
        if (xQueueSend(xQueue_RawData, &record, 0) != pdPASS) {
            drop_count++;
            // Log mỗi 100 lần drop để không flood UART
            if (drop_count % 100 == 0) {
                ESP_LOGW(PIPE_TAG, "Queue full — da drop %lu records / %lu total",
                         drop_count, record_count);
            }
        }*/

        // Hiển thị ECG và PCG trên Serial Plotter
        int32_t pcg_avg = 0;

        for (int i = 0; i < RATIO_PCG_ECG; i++) {
            pcg_avg += record.pcg_raw_mono[i];
        }

        pcg_avg /= RATIO_PCG_ECG;

        // Serial Plotter sẽ vẽ 2 đường
        printf("%ld %ld\n",
            (long)record.ecg_raw_val,
            (long)pcg_avg);
    }
}

// =====================================================================
// task_csv_parser — Core 1, Priority 3
//
// Nhận raw_record_t từ Queue → format thành CSV → gửi batch qua
// xQueue_CSVBatch để task_sd_flash lo việc printf/ghi SD.
//
// FIX so với bản gốc:
//   1. Double buffer dùng đúng cách: parser ghi vào buffer A,
//      khi đầy → gửi A sang task_sd_flash, swap sang buffer B
//      → parser không bao giờ bị block bởi UART
//   2. Bỏ printf() trực tiếp trong parser — tránh block task
//   3. Kiểm tra overflow buffer trước mỗi snprintf
// =====================================================================
void task_csv_parser(void *pvParameters)
{
    ESP_LOGI(PIPE_TAG, "=== Task_CSV_Parser (Core 1, Pri 3) bat dau ===");

    raw_record_t record;

    // Hai buffer thay phiên nhau — parser luôn có buffer sạch để ghi
    char *buffer_A = (char *)malloc(CSV_BUFFER_SIZE);
    char *buffer_B = (char *)malloc(CSV_BUFFER_SIZE);

    if (!buffer_A || !buffer_B) {
        ESP_LOGE(PIPE_TAG, "FATAL: Khong du RAM cho CSV buffer!");
        free(buffer_A);
        free(buffer_B);
        vTaskDelete(NULL);
        return;
    }

    char    *active_buf = buffer_A;
    uint32_t active_len = 0;
    uint32_t row_count  = 0;
    bool     header_sent = false;

    while (1) {
        if (xQueueReceive(xQueue_RawData, &record, portMAX_DELAY) != pdPASS) {
            continue;
        }

        // Gửi header CSV một lần duy nhất qua task_sd_flash
        if (!header_sent) {
            // Header nhỏ — dùng stack buffer tạm, gửi riêng
            char *hdr = (char *)malloc(512);
            if (hdr) {
                int hlen = snprintf(hdr, 512,
                    "\n===== DU LIEU ECG + PCG (ESP32-WROOM-32) =====\n"
                    "Timestamp_us,ECG_Filtered,Leads_Off");
                for (int i = 1; i <= RATIO_PCG_ECG; i++) {
                    hlen += snprintf(hdr + hlen, 512 - hlen, ",PCG_%d", i);
                }
                hlen += snprintf(hdr + hlen, 512 - hlen, "\n");
                csv_batch_t hdr_batch = { .buffer_ptr = hdr, .valid_bytes = hlen, .needs_free = 1 };
                // Gửi header, nếu Queue đầy thì free luôn để không leak
                if (xQueueSend(xQueue_CSVBatch, &hdr_batch, pdMS_TO_TICKS(200)) != pdPASS) {
                    ESP_LOGW(PIPE_TAG, "CSVBatch Queue day — bo qua header");
                    free(hdr);
                }
            }
            header_sent = true;
        }

        // Ước tính bytes cần cho 1 row:
        // timestamp(20) + ecg(6) + lo(1) + 40×pcg(10 mỗi) + dấu phẩy + \n ≈ 430 byte
        // CSV_BUFFER_SIZE = 100 × 250 = 25000 byte — đủ ~58 rows/buffer
        // BATCH_ROWS_COUNT = 100 nhưng thực tế buffer đầy trước → dùng cả 2 điều kiện
        uint32_t remaining = CSV_BUFFER_SIZE - active_len;
        if (remaining < 500) {
            // Buffer sắp đầy — flush ngay dù chưa đủ BATCH_ROWS_COUNT
            csv_batch_t batch = { .buffer_ptr = active_buf, .valid_bytes = active_len, .needs_free = 0 };
            if (xQueueSend(xQueue_CSVBatch, &batch, pdMS_TO_TICKS(500)) != pdPASS) {
                ESP_LOGW(PIPE_TAG, "CSVBatch Queue day — ghi de buffer");
            }
            // Swap sang buffer kia
            active_buf = (active_buf == buffer_A) ? buffer_B : buffer_A;
            active_len = 0;
            row_count  = 0;
            remaining  = CSV_BUFFER_SIZE;
        }

        // Format 1 row CSV
        int len = snprintf(active_buf + active_len, remaining,
                           "%llu,%ld,%d",
                           (unsigned long long)record.timestamp_us,
                           (long)record.ecg_raw_val,
                           record.leads_off_flag);
        if (len > 0) active_len += len;

        for (int i = 0; i < RATIO_PCG_ECG; i++) {
            remaining = CSV_BUFFER_SIZE - active_len;
            len = snprintf(active_buf + active_len, remaining,
                           ",%ld", (long)record.pcg_raw_mono[i]);
            if (len > 0) active_len += len;
        }

        remaining = CSV_BUFFER_SIZE - active_len;
        len = snprintf(active_buf + active_len, remaining, "\n");
        if (len > 0) active_len += len;

        row_count++;

        // Flush khi đủ batch
        if (row_count >= BATCH_ROWS_COUNT) {
            csv_batch_t batch = { .buffer_ptr = active_buf, .valid_bytes = active_len, .needs_free = 0 };
            if (xQueueSend(xQueue_CSVBatch, &batch, pdMS_TO_TICKS(500)) != pdPASS) {
                ESP_LOGW(PIPE_TAG, "CSVBatch Queue day — mat 1 batch %lu rows", row_count);
            }
            active_buf = (active_buf == buffer_A) ? buffer_B : buffer_A;
            active_len = 0;
            row_count  = 0;
        }
    }
}

// =====================================================================
// task_sd_flash — Core 1, Priority 2
//
// Nhận csv_batch_t từ xQueue_CSVBatch → printf ra UART
// Sau này thay printf bằng f_write() khi bật SD Card.
//
// FIX so với bản gốc:
//   1. Tách việc output ra task riêng — parser không bao giờ block vì UART
//   2. free(batch.buffer_ptr) sau khi dùng xong — tránh memory leak
//   3. Sẵn sàng cho SD Card: chỉ cần thay printf bằng f_write()
// =====================================================================
void task_sd_flash(void *pvParameters)
{
    ESP_LOGI(PIPE_TAG, "=== Task_SD_Flash (Core 1, Pri 2) bat dau ===");
    ESP_LOGI(PIPE_TAG, "Che do hien tai: UART output (chua co SD Card)");

    csv_batch_t batch;

    while (1) {
        // Chờ nhận batch từ csv_parser — timeout 1 giây
        if (xQueueReceive(xQueue_CSVBatch, &batch, pdMS_TO_TICKS(1000)) == pdPASS) {
            if (batch.buffer_ptr && batch.valid_bytes > 0) {

#ifdef SD_MOUNT_POINT
                // --- CHẾ ĐỘ SD CARD (bật khi define SD_MOUNT_POINT) ---
                // if (xSemaphoreTake(xSD_Mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
                //     FILE *f = fopen(SD_MOUNT_POINT "/ecg_pcg.csv", "a");
                //     if (f) {
                //         fwrite(batch.buffer_ptr, 1, batch.valid_bytes, f);
                //         fclose(f);
                //     }
                //     xSemaphoreGive(xSD_Mutex);
                // }
#else
                // --- CHẾ ĐỘ UART ---
                // fwrite nhanh hơn printf vì không parse format string
                fwrite(batch.buffer_ptr, 1, batch.valid_bytes, stdout);
                fflush(stdout);
#endif
                // GHI CHÚ MEMORY:
                // - needs_free=0: buffer_A/B tái sử dụng → KHÔNG free
                // - needs_free=1: header malloc riêng → PHẢI free
                if (batch.needs_free) {
                    free(batch.buffer_ptr);
                }
            }
        }
    }
}