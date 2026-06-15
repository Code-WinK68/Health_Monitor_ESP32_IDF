#ifndef HARDWARE_DRIVERS_H
#define HARDWARE_DRIVERS_H

#include "app_config.h"         // raw_record_t, macro pin, ADC_UNIT_1...
#include "esp_err.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/i2s_std.h"

#ifdef __cplusplus
extern "C" {
#endif

// Biến volatile trung chuyển ECG giữa task_collector và driver
// KHÔNG dùng trong critical section khi gọi adc_oneshot_read
extern volatile uint32_t g_volatile_ecg_val;
extern volatile uint8_t  g_volatile_leads_off; // Đổi thành uint8_t — chỉ cần 0/1

esp_err_t init_gpio_leads_off(void);
esp_err_t init_adc_oneshot_ecg(void);
esp_err_t init_i2s_microphone(void);

// SD Card — chỉ khai báo khi macro SD_MOUNT_POINT được định nghĩa
#ifdef SD_MOUNT_POINT
esp_err_t init_sd_card_spi(void);
#endif

i2s_chan_handle_t         get_i2s_rx_handle(void);
adc_oneshot_unit_handle_t get_adc_handle(void);

#ifdef __cplusplus
}
#endif

#endif // HARDWARE_DRIVERS_H