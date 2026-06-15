#include "hardware_drivers.h"
#include "app_config.h"
#include "esp_log.h"
#include "driver/gpio.h"

#ifdef SD_MOUNT_POINT
#include "esp_vfs_fat.h"
#include "driver/spi_master.h"
#include "sdmmc_cmd.h"
#endif

static const char *DRV_TAG = "HW_DRIVERS";

static adc_oneshot_unit_handle_t s_adc1_handle  = NULL;
static i2s_chan_handle_t         s_i2s_rx_handle = NULL;

// Khởi tạo mặc định: leads-off = 1 (chưa kết nối), ECG = 0
volatile uint32_t g_volatile_ecg_val   = 0;
volatile uint8_t  g_volatile_leads_off = 1;  // uint8_t — đủ cho flag 0/1

i2s_chan_handle_t         get_i2s_rx_handle(void) { return s_i2s_rx_handle; }
adc_oneshot_unit_handle_t get_adc_handle(void)    { return s_adc1_handle;   }

// ---------------------------------------------------------------------
esp_err_t init_gpio_leads_off(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask  = (1ULL << AD8232_LO_PLUS_PIN) | (1ULL << AD8232_LO_MINUS_PIN),
        .mode          = GPIO_MODE_INPUT,
        .pull_up_en    = GPIO_PULLUP_DISABLE,
        // FIX: AD8232 đã có pull logic nội bộ trên chân LO
        // Để PULLDOWN sẽ kéo chân xuống GND → khi dây bong không lên HIGH được
        .pull_down_en  = GPIO_PULLDOWN_DISABLE,
        .intr_type     = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io_conf);
    if (err == ESP_OK) {
        ESP_LOGI(DRV_TAG, "GPIO leads-off OK (LO+=%d, LO-=%d)",
                 AD8232_LO_PLUS_PIN, AD8232_LO_MINUS_PIN);
    }
    return err;
}

// ---------------------------------------------------------------------
esp_err_t init_adc_oneshot_ecg(void)
{
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ECG_ADC_UNIT,
        // FIX: ADC_RTC_CLK_SRC_DEFAULT đúng cho oneshot mode
        // ADC_DIGI_CLK_SRC_DEFAULT chỉ dùng cho continuous/DMA mode
        .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
    };
    esp_err_t err = adc_oneshot_new_unit(&init_config, &s_adc1_handle);
    if (err != ESP_OK) {
        ESP_LOGE(DRV_TAG, "adc_oneshot_new_unit that: %s", esp_err_to_name(err));
        return err;
    }

    adc_oneshot_chan_cfg_t chan_config = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten    = ADC_ATTEN_DB_12,   // ~0–3.3V full range
    };
    err = adc_oneshot_config_channel(s_adc1_handle, ECG_ADC_CHANNEL, &chan_config);
    if (err == ESP_OK) {
        ESP_LOGI(DRV_TAG, "ADC1_CH%d (GPIO36) 12-bit OK", ECG_ADC_CHANNEL);
    }
    return err;
}

// ---------------------------------------------------------------------
esp_err_t init_i2s_microphone(void)
{
    // Tạo channel RX
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_MIC_PORT, I2S_ROLE_MASTER);
    esp_err_t err = i2s_new_channel(&chan_cfg, NULL, &s_i2s_rx_handle);
    if (err != ESP_OK) {
        ESP_LOGE(DRV_TAG, "i2s_new_channel that: %s", esp_err_to_name(err));
        return err;
    }

    // ICS-43434 dùng I2S Philips standard, 32-bit frame, lấy 24-bit MSB
    // SLOT_MODE_MONO + L/R pin = GND → ESP32 nhận kênh LEFT
    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(PCG_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,
                                                         I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .bclk = I2S_BCLK_PIN,
            .ws   = I2S_WS_PIN,
            .dout = I2S_GPIO_UNUSED,
            .din  = I2S_DATA_PIN,   // GPIO34 — input-only, đúng cho DATA IN
        },
    };

    err = i2s_channel_init_std_mode(s_i2s_rx_handle, &std_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(DRV_TAG, "i2s_channel_init_std_mode that: %s", esp_err_to_name(err));
        return err;
    }

    err = i2s_channel_enable(s_i2s_rx_handle);
    if (err == ESP_OK) {
        ESP_LOGI(DRV_TAG, "I2S0 Philips Mono 16kHz 32-bit OK (BCLK=%d WS=%d DIN=%d)",
                 I2S_BCLK_PIN, I2S_WS_PIN, I2S_DATA_PIN);
    }
    return err;
}

// ---------------------------------------------------------------------
#ifdef SD_MOUNT_POINT
static sdmmc_card_t *s_sd_card = NULL;

esp_err_t init_sd_card_spi(void)
{
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files              = 3,
        .allocation_unit_size   = 16 * 1024,
    };

    spi_bus_config_t bus_cfg = {
        .mosi_io_num   = SD_MOSI_PIN,
        .miso_io_num   = SD_MISO_PIN,
        .sclk_io_num   = SD_CLK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    esp_err_t err = spi_bus_initialize(SD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) return err;

    sdmmc_host_t host           = SDSPI_HOST_DEFAULT();
    host.slot                   = SD_SPI_HOST;
    sdspi_device_config_t slot  = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs                = SD_CS_PIN;
    slot.host_id                = SD_SPI_HOST;

    err = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot, &mount_config, &s_sd_card);
    if (err == ESP_OK) {
        ESP_LOGI(DRV_TAG, "SD Card mount OK tại %s", SD_MOUNT_POINT);
    }
    return err;
}
#endif // SD_MOUNT_POINT