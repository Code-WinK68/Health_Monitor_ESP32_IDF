#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_timer.h"
#include "esp_log.h"

#define LO_PLUS_PIN     GPIO_NUM_3   
#define LO_MINUS_PIN    GPIO_NUM_2   
#define ECG_ADC_CH      ADC_CHANNEL_0 

// Tần số 400Hz -> Chu kỳ lấy mẫu = 1000000 µs / 400 = 2500 µs
#define SAMPLE_PERIOD_US 2500 

static adc_oneshot_unit_handle_t adc_handle;

// =====================================================
// HÀM NGẮT TIMER (CHẠY ĐÚNG MỖI 2500 µs - 400Hz)
// =====================================================
static void timer_callback(void *arg)
{
    int raw_ecg = 0;
    int lo_plus = gpio_get_level(LO_PLUS_PIN);
    int lo_minus = gpio_get_level(LO_MINUS_PIN);

    // Đọc giá trị ADC thô (Dải giá trị 0 -> 4095)
    adc_oneshot_read(adc_handle, ECG_ADC_CH, &raw_ecg);

    // Kiểm tra trạng thái hở mạch của các điện cực (Lead-off)
    if (lo_plus == 1 || lo_minus == 1) {
        printf("2048\n"); // Giữ mức cố định ở giữa dải đo khi hở mạch
    } else {
        // ĐÃ LOẠI BỎ TẤT CẢ BỘ LỌC: Xuất trực tiếp giá trị ADC thô ra cổng Serial
        printf("%d\n", raw_ecg);
    }
    fflush(stdout);
}

// =====================================================
// KHỞI TẠO PHẦN CỨNG
// =====================================================
static void init_hardware(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LO_PLUS_PIN) | (1ULL << LO_MINUS_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    adc_oneshot_unit_init_cfg_t unit_cfg = { .unit_id = ADC_UNIT_1 };
    adc_oneshot_new_unit(&unit_cfg, &adc_handle);

    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten    = ADC_ATTEN_DB_12, // Dải đo điện áp tối đa khoảng 0 -> 3.3V
    };
    adc_oneshot_config_channel(adc_handle, ECG_ADC_CH, &chan_cfg);
}

// =====================================================
// HÀM CHÍNH
// =====================================================
void app_main(void)
{
    init_hardware();

    // Cấu hình Hardware Timer chạy định kỳ
    const esp_timer_create_args_t timer_args = {
        .callback = &timer_callback,
        .name = "ecg_400hz_timer"
    };
    esp_timer_handle_t ecg_timer;
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &ecg_timer));
    
    // Kích hoạt timer chạy lặp lại mỗi 2500 µs (400Hz)
    ESP_ERROR_CHECK(esp_timer_start_periodic(ecg_timer, SAMPLE_PERIOD_US));

    printf("--- Dang lay mau ECG thong qua tín hieu ADC thô (400Hz) ---\n");

    // Vòng lặp chính trống rỗng, ép ngủ hoàn toàn để các luồng khác chạy, không lo lỗi Watchdog
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
