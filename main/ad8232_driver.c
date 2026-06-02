/**
 * @file  ad8232_driver.c
 * @brief Driver cho AD8232 ECG module
 *
 * Chức năng:
 *   1. Khởi tạo ADC1 Oneshot để đọc tín hiệu ECG (GPIO1 / ADC1_CH0)
 *   2. Khởi tạo GPIO để đọc LO+ (GPIO2) và LO- (GPIO3)
 *   3. Cung cấp hàm đọc 1 mẫu ECG + cập nhật leads_off_flag
 *
 * Theo sơ đồ flowchart:
 *   ─ Luồng 1: Poll leads-off → kiểm tra LO+/LO- → đọc hoặc đánh dấu INVALID
 *   ─ ISR Bước 2: đọc g_ecg_val và g_leads_off_flag (đã cập nhật sẵn)
 */

#include "ad8232_driver.h"
#include "common.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "AD8232";

/* Handle ADC — dùng nội bộ module này */
static adc_oneshot_unit_handle_t s_adc_handle = NULL;

/* ─── Biến chia sẻ với ISR (khai báo extern trong common.h) ──── */
volatile uint8_t  g_leads_off_flag = 0;
volatile uint16_t g_ecg_val        = 0;

/* ══════════════════════════════════════════════════════════════ */
/*  Khởi tạo                                                      */
/* ══════════════════════════════════════════════════════════════ */

/**
 * @brief Khởi tạo ADC1 Oneshot cho kênh ECG (ADC1_CH0 / GPIO1)
 *
 * Phải gọi TRƯỚC khi khởi tạo ICS-43434 (I2S DMA), vì cả hai
 * dùng chung tài nguyên ADC1 trên ESP32-S3.
 */
esp_err_t ad8232_init(void)
{
    esp_err_t ret;

    /* 1. Tạo unit ADC1 ở chế độ Oneshot */
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id  = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ret = adc_oneshot_new_unit(&unit_cfg, &s_adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Không tạo được ADC unit: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 2. Cấu hình kênh ADC1_CH0 (GPIO1) */
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = ADC_ATTEN_DB_12,   /* 0–3.3V full scale */
        .bitwidth = ADC_BITWIDTH_12,   /* 0–4095            */
    };
    ret = adc_oneshot_config_channel(s_adc_handle, ADC_CHANNEL_0, &chan_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Cấu hình kênh ADC thất bại: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 3. Cấu hình GPIO leads-off: LO+ (GPIO2), LO- (GPIO3) */
    gpio_config_t lo_cfg = {
        .pin_bit_mask = (1ULL << AD8232_LO_PLUS_GPIO) |
                        (1ULL << AD8232_LO_MINUS_GPIO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ret = gpio_config(&lo_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Cấu hình GPIO LO+/LO- thất bại: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "AD8232 khởi tạo OK — ADC1_CH0 (GPIO%d), LO+(GPIO%d), LO-(GPIO%d)",
             AD8232_OUT_GPIO, AD8232_LO_PLUS_GPIO, AD8232_LO_MINUS_GPIO);
    return ESP_OK;
}

/* ══════════════════════════════════════════════════════════════ */
/*  Hàm đọc 1 mẫu ECG (gọi trong ISR — Bước 2 của flowchart)    */
/* ══════════════════════════════════════════════════════════════ */

/**
 * @brief Đọc 1 mẫu ECG dựa vào trạng thái leads-off hiện tại.
 *
 * Hàm này được gọi từ ISR (Bước 2 trong flowchart).
 * g_leads_off_flag và g_ecg_val đã được task poll cập nhật trước đó.
 *
 * @param[out] out_ecg    Giá trị ADC 12-bit, hoặc ECG_INVALID nếu bong điện cực
 * @param[out] out_lo     1 nếu điện cực bong, 0 nếu bình thường
 */
void ad8232_read_sample_from_isr(int16_t *out_ecg, uint8_t *out_lo)
{
    *out_lo  = g_leads_off_flag;
    *out_ecg = (g_leads_off_flag == 0) ? (int16_t)g_ecg_val : (int16_t)ECG_INVALID;
}

/* ══════════════════════════════════════════════════════════════ */
/*  Task poll leads-off + cập nhật ECG (Luồng 1 của flowchart)   */
/* ══════════════════════════════════════════════════════════════ */

/**
 * @brief Task chạy liên tục để poll leads-off và đọc ECG.
 *
 * Theo flowchart Luồng 1:
 *   Poll LO+/LO- → kiểm tra → nếu bong: g_leads_off_flag=1, g_ecg_val=INVALID
 *                            → nếu OK  : g_leads_off_flag=0, g_ecg_val=adc_read
 *
 * Task này chạy mỗi 1ms (nhanh hơn chu kỳ ECG 2.5ms) để cập nhật
 * trạng thái kịp thời trước khi ISR đọc.
 */
void ad8232_poll_task(void *arg)
{
    int raw = 0;
    ESP_LOGI(TAG, "Task poll leads-off bắt đầu");

    while (1) {
        /* Đọc trạng thái 2 chân LO+ và LO- */
        int lo_plus  = gpio_get_level(AD8232_LO_PLUS_GPIO);
        int lo_minus = gpio_get_level(AD8232_LO_MINUS_GPIO);

        if (lo_plus || lo_minus) {
            /* ── Điện cực bong ── */
            g_leads_off_flag = 1;
            g_ecg_val        = ECG_INVALID;
        } else {
            /* ── Điện cực OK → đọc ADC ── */
            esp_err_t err = adc_oneshot_read(s_adc_handle, ADC_CHANNEL_0, &raw);
            if (err == ESP_OK) {
                g_leads_off_flag = 0;
                g_ecg_val        = (uint16_t)raw;
            }
            /* Nếu đọc lỗi: giữ nguyên giá trị cũ */
        }

        /* Poll mỗi 1ms — nhanh hơn chu kỳ ECG 2.5ms */
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/* ══════════════════════════════════════════════════════════════ */
/*  Dọn dẹp                                                       */
/* ══════════════════════════════════════════════════════════════ */

void ad8232_deinit(void)
{
    if (s_adc_handle) {
        adc_oneshot_del_unit(s_adc_handle);
        s_adc_handle = NULL;
    }
    ESP_LOGI(TAG, "AD8232 đã giải phóng");
}
