#ifndef PN532_DRIVER_HSU_H
#define PN532_DRIVER_HSU_H

#include "driver/uart.h"
#include "driver/gpio.h"

#include "pn532_driver.h"

#ifdef __cplusplus
extern "C"
{
#endif

esp_err_t pn532_new_driver_hsu(gpio_num_t uart_rx,
                               gpio_num_t uart_tx,
                               gpio_num_t reset,
                               gpio_num_t irq,
                               uart_port_t uart_port,
                               int32_t baudrate,
                               pn532_io_handle_t io_handle);

#ifdef __cplusplus
}
#endif

#endif // PN532_DRIVER_HSU_H
