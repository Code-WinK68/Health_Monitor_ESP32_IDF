/* @file  i2s.c
   @brief source file for ICS43434 microphone based i2s functions.
   @author Shyam Jha (Avinashee Tech)
*/

#include "i2s.h"


/* Setting I2S configurations */
i2s_std_config_t std_cfg = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
    .slot_cfg = {
        .data_bit_width = I2S_DATA_BIT_WIDTH_32BIT,
        .slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT,
        .slot_mode = I2S_SLOT_MODE_MONO,           // mono
        .slot_mask = I2S_STD_SLOT_LEFT,            // capture left channel
        .ws_width = I2S_SLOT_BIT_WIDTH_32BIT,
        .ws_pol = false,                           // WS = 0 → left channel
        .bit_shift = true,                         // Philips mode
        .msb_right = false,                        // MSB first
    },
    .gpio_cfg = {
        .mclk = I2S_GPIO_UNUSED,
        .bclk = I2S_MIC_SERIAL_CLOCK,
        .ws = I2S_MIC_LEFT_RIGHT_CLOCK,
        .dout = I2S_GPIO_UNUSED,
        .din = I2S_MIC_SERIAL_DATA,
        .invert_flags = {
            .mclk_inv = false,
            .bclk_inv = false,
            .ws_inv = false,
        },
    },
};

i2s_chan_handle_t rx_handle;        // I2S rx channel handler

/**  
 * @brief  init function for i2s peripheral
 * @param  None
 * @retval None.
 * @note   used to create i2s receive channel.
*/
void i2s_setup(void){
  i2s_chan_config_t chan_cfg = {
    .id = I2S_NUM_0, 
    .role = I2S_ROLE_MASTER,
    .dma_desc_num = 8,
    .dma_frame_num = 120,
  };

  /* Allocate a new RX channel and get the handle of this channel */
  i2s_new_channel(&chan_cfg, NULL, &rx_handle);

  /* Initialize the channel */
  i2s_channel_init_std_mode(rx_handle, &std_cfg);

  /* Before reading data, start the RX channel first */
  i2s_channel_enable(rx_handle);
}

/**  
 * @brief  i2s sample read function
 * @param  dest - pointer of receiving application buffer 
 * @param  bytes_read - variable to store number of bytes read 
 * @retval None.
*/
void i2s_readsamples(void *dest, size_t *bytes_read){
    i2s_channel_read(rx_handle, dest, sizeof(int32_t)*SAMPLE_BUFFER_SIZE, bytes_read, portMAX_DELAY);
}
