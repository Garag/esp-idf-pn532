#ifndef PN532_DRIVER_I2C_H
#define PN532_DRIVER_I2C_H

#include "driver/i2c_master.h"
#include "driver/gpio.h"

#include "pn532_driver.h"

#ifdef __cplusplus
extern "C"
{
#endif

    esp_err_t pn532_new_driver_i2c(gpio_num_t sda,
                                   gpio_num_t scl,
                                   gpio_num_t reset,
                                   gpio_num_t irq,
                                   i2c_port_num_t i2c_port_number,
                                   pn532_io_handle_t io_handle);
    
    /**
     * @brief A flexible I2C driver that can utilize existing I2C bus.
     * 
     * @param io_handle Handle to device.
     * @param gpio_config Configuration of GPIO pins that is common for all drivers.
     * @param bus_handle Handle to I2C bus.
     * @return esp_err_t 
     */
    esp_err_t pn532_new_driver_i2c_ext(
        pn532_io_handle_t io_handle,
        pn532_gpio_conf_t gpio_config,
        i2c_master_bus_handle_t bus_handle
    );

#ifdef __cplusplus
}
#endif

#endif // PN532_DRIVER_I2C_H
