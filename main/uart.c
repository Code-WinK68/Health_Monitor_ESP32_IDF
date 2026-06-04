/* @file  uart.c
   @brief source file for uart functions.
   @author Shyam Jha (Avinashee Tech)
*/

#include "uart.h"

const int RX_BUF_SIZE = 2048;

/**  
 * @brief  init function for uart peripheral
 * @param  None
 * @retval None.
*/
void uart_init(void) {
    // uart configuration
    const uart_config_t uart_config = {
        .baud_rate = 921600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    
    uart_driver_install(UART_NUM_1, RX_BUF_SIZE, 0, 0, NULL, 0);  //install uart driver
    uart_param_config(UART_NUM_1, &uart_config); //configure
    uart_set_pin(UART_NUM_1, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);  //attach pins
}

/**  
 * @brief  function to send uart data
 * @param  logName - serial display name for monitoring
 * @param  data -  data buffer address 
 * @param  len - data length to send
 * @retval number of bytes transmitted
*/
int uart_sendData(const char* logName, void* data, size_t len)
{
    const int txBytes = uart_write_bytes(UART_NUM_1, data, len);
    ESP_LOGI(logName, "Wrote %d bytes", txBytes);
    return txBytes;
}
