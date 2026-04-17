#include <string.h>
#include "pn532_driver.h"
#include "pn532_driver_i2c.h"
#include "esp_log.h"

static const char TAG[] = "pn532_driver_i2c";

#define PN532_I2C_RAW_ADDRESS               (0x24)

typedef struct {
    gpio_num_t sda;
    gpio_num_t scl;
    i2c_port_num_t i2c_port_number;
    i2c_master_bus_handle_t i2c_bus_handle;
    i2c_master_dev_handle_t i2c_dev_handle;
    bool bus_created;
    uint8_t frame_buffer[256];
} pn532_i2c_driver_config;

static esp_err_t pn532_init_io(pn532_io_handle_t io_handle);
static void pn532_release_driver(pn532_io_handle_t io_handle);
static void pn532_release_io(pn532_io_handle_t io_handle);
static esp_err_t pn532_read(pn532_io_handle_t io_handle, uint8_t *read_buffer, size_t read_size, int xfer_timeout_ms);
static esp_err_t pn532_write(pn532_io_handle_t io_handle, const uint8_t *write_buffer, size_t write_size, int xfer_timeout_ms);
static esp_err_t pn532_is_ready(pn532_io_handle_t io_handle);

static esp_err_t pn532_init_io_ext(pn532_io_handle_t io_handle);
static void pn532_release_io_ext(pn532_io_handle_t io_handle);
static void pn532_empty_callback(pn532_io_handle_t io_handle);

esp_err_t pn532_new_driver_i2c(gpio_num_t sda,
                               gpio_num_t scl,
                               gpio_num_t reset,
                               gpio_num_t irq,
                               i2c_port_num_t i2c_port_number,
                               pn532_io_handle_t io_handle)
{
    if (io_handle == NULL)
        return ESP_ERR_INVALID_ARG;

    if (i2c_port_number < 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    pn532_i2c_driver_config *dev_config = heap_caps_calloc(1, sizeof(pn532_i2c_driver_config), MALLOC_CAP_DEFAULT);
    if (dev_config == NULL) {
        return ESP_ERR_NO_MEM;
    }

    io_handle->reset = reset;
    io_handle->irq = irq;

    dev_config->i2c_port_number = i2c_port_number;
    dev_config->scl = scl;
    dev_config->sda = sda;
    dev_config->bus_created = false;
    io_handle->driver_data = dev_config;

    io_handle->pn532_init_io = pn532_init_io;
    io_handle->pn532_release_io = pn532_release_io;
    io_handle->pn532_release_driver = pn532_release_driver;
    io_handle->pn532_read = pn532_read;
    io_handle->pn532_write = pn532_write;
    io_handle->pn532_init_extra = NULL;
    io_handle->pn532_is_ready = pn532_is_ready;

#ifdef CONFIG_ENABLE_IRQ_ISR
    io_handle->IRQQueue = NULL;
#endif

    return ESP_OK;
}



esp_err_t pn532_new_driver_i2c_ext(pn532_io_handle_t io_handle,
                                   pn532_gpio_conf_t gpio_config,
                                   i2c_master_bus_handle_t bus_handle) {

    if (io_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (bus_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (pn532_gpio_conf_check_validity(gpio_config) != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }

    io_handle->reset = gpio_config.reset;
    io_handle->irq = gpio_config.irq;

    pn532_i2c_driver_config *dev_config = heap_caps_calloc(1, sizeof(pn532_i2c_driver_config), MALLOC_CAP_DEFAULT);
    if (dev_config == NULL) {
        return ESP_ERR_NO_MEM;
    }

    // no need to initialize the bus since user already provided the bus
    dev_config->i2c_port_number = I2C_NUM_MAX;
    dev_config->scl = GPIO_NUM_NC;
    dev_config->sda = GPIO_NUM_NC;
    dev_config->bus_created = true;
    dev_config->i2c_bus_handle = bus_handle;

    io_handle->driver_data = dev_config;

    // these callbacks work the same
    io_handle->pn532_read = pn532_read;
    io_handle->pn532_write = pn532_write;
    io_handle->pn532_init_extra = NULL;
    io_handle->pn532_is_ready = pn532_is_ready;

    // since bus is provided by user its his responsibilty to manage it
    io_handle->pn532_release_driver = pn532_empty_callback;

    // basically the same function as previous version, but it only adds new device to bus
    io_handle->pn532_init_io = pn532_init_io_ext;
    
    // basically the same function as previous version, but it only removes new device from bus
    io_handle->pn532_release_io = pn532_release_io_ext;


#ifdef CONFIG_ENABLE_IRQ_ISR
    io_handle->IRQQueue = NULL;
#endif
    
    return ESP_OK;
}

void pn532_release_driver(pn532_io_handle_t io_handle)
{
    if (io_handle == NULL || io_handle->driver_data == NULL)
        return;

    pn532_release_io(io_handle);

    free(io_handle->driver_data);
    io_handle->driver_data = NULL;
}

esp_err_t pn532_init_io(pn532_io_handle_t io_handle)
{
    if (io_handle == NULL || io_handle->driver_data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    pn532_i2c_driver_config *driver_config = (pn532_i2c_driver_config *)io_handle->driver_data;

    if (driver_config->i2c_bus_handle != NULL && driver_config->bus_created) {
        pn532_release_io(io_handle);
    }

    driver_config->bus_created = false;
    if (driver_config->scl != GPIO_NUM_NC && driver_config->sda != GPIO_NUM_NC) {
        // create new master bus
        i2c_master_bus_config_t conf = {
                //Open the I2C Bus
                .clk_source = I2C_CLK_SRC_DEFAULT,
                .i2c_port = driver_config->i2c_port_number,
                .sda_io_num = driver_config->sda,
                .scl_io_num = driver_config->scl,
                .glitch_ignore_cnt = 7,
                .flags.enable_internal_pullup = true,
        };
        if (i2c_new_master_bus(&conf, &driver_config->i2c_bus_handle) != ESP_OK) {
            ESP_LOGE(TAG, "i2c_new_master_bus() failed");
            return ESP_FAIL;
        }
        driver_config->bus_created = true;
    }
    else {
        // try to get bus handle
        if (i2c_master_get_bus_handle(driver_config->i2c_port_number, &driver_config->i2c_bus_handle) != ESP_OK) {
            ESP_LOGE(TAG, "i2c_master_get_bus_handle() failed");
            return ESP_FAIL;
        }
    }

    i2c_device_config_t dev_cfg;
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = PN532_I2C_RAW_ADDRESS; // 7-bit address without RW flag
    dev_cfg.scl_speed_hz = 100000;
    dev_cfg.scl_wait_us = 200000;

    if (i2c_master_bus_add_device(driver_config->i2c_bus_handle, &dev_cfg, &driver_config->i2c_dev_handle) != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_bus_add_device() failed");
        return ESP_FAIL;
    }

    return ESP_OK;
}

void pn532_release_io(pn532_io_handle_t io_handle)
{
    if (io_handle == NULL || io_handle->driver_data == NULL) {
        return;
    }

    pn532_i2c_driver_config *driver_config = (pn532_i2c_driver_config *)io_handle->driver_data;

    if (driver_config->i2c_dev_handle != NULL) {
        ESP_LOGD(TAG, "remove i2c device ...");
        i2c_master_bus_rm_device(driver_config->i2c_dev_handle);
        driver_config->i2c_dev_handle = NULL;
    }

    if (driver_config->i2c_bus_handle != NULL) {
        if (driver_config->bus_created) {
            ESP_LOGD(TAG, "delete i2c bus ...");
            i2c_del_master_bus(driver_config->i2c_bus_handle);
            driver_config->bus_created = false;
        }
        driver_config->i2c_bus_handle = NULL;
    }
}

esp_err_t pn532_is_ready(pn532_io_handle_t io_handle)
{
    uint8_t status;
    if (io_handle == NULL || io_handle->driver_data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    pn532_i2c_driver_config *driver_config = (pn532_i2c_driver_config *)io_handle->driver_data;
    esp_err_t result = i2c_master_receive(driver_config->i2c_dev_handle, &status, 1, 10);

    if (result != ESP_OK)
        return result;

    return (status == 0x01) ? ESP_OK : ESP_FAIL;
}

esp_err_t pn532_read(pn532_io_handle_t io_handle, uint8_t *read_buffer, size_t read_size, int xfer_timeout_ms)
{
    static uint8_t rx_buffer[256];


    TickType_t start_ticks = xTaskGetTickCount();
    TickType_t timeout_ticks = (xfer_timeout_ms > 0) ? pdMS_TO_TICKS(xfer_timeout_ms) : portMAX_DELAY;
    TickType_t elapsed_ticks = 0;

    int read_timeout = (xfer_timeout_ms > 0) ? xfer_timeout_ms : 100;

    if (io_handle == NULL || io_handle->driver_data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    pn532_i2c_driver_config *driver_config = (pn532_i2c_driver_config *)io_handle->driver_data;

    esp_err_t result = ESP_FAIL;
    bool is_ready = false;
    while (!is_ready && elapsed_ticks < timeout_ticks) {
        result = i2c_master_receive(driver_config->i2c_dev_handle, rx_buffer, read_size + 1, read_timeout);
        if (result == ESP_OK && rx_buffer[0] == 0x01) {
            is_ready = true;
        }
        elapsed_ticks = xTaskGetTickCount() - start_ticks;
    }

    if (result != ESP_OK)
        return result;

    // check status byte if PN532 is ready
    if (rx_buffer[0] != 0x01) {
        // PN532 not ready
        return ESP_ERR_TIMEOUT;
    }

    // skip status byte and copy only response data
    memcpy(read_buffer, rx_buffer + 1, read_size);
    return result;
}

esp_err_t pn532_write(pn532_io_handle_t io_handle, const uint8_t *write_buffer, size_t write_size, int xfer_timeout_ms)
{
    if (io_handle == NULL || io_handle->driver_data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (write_size > 254) {
        return ESP_ERR_INVALID_SIZE;
    }

    pn532_i2c_driver_config *driver_config = (pn532_i2c_driver_config *)io_handle->driver_data;

    driver_config->frame_buffer[0] = 0;
    memcpy(driver_config->frame_buffer + 1, write_buffer, write_size);
    driver_config->frame_buffer[write_size + 1] = 0;

    return i2c_master_transmit(driver_config->i2c_dev_handle, driver_config->frame_buffer, write_size + 2, xfer_timeout_ms);
}

esp_err_t pn532_init_io_ext(pn532_io_handle_t io_handle) { 
    
    if (io_handle == NULL || io_handle->driver_data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    pn532_i2c_driver_config *driver_config = (pn532_i2c_driver_config *)io_handle->driver_data;

    i2c_device_config_t dev_cfg;
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = PN532_I2C_RAW_ADDRESS; // 7-bit address without RW flag
    dev_cfg.scl_speed_hz = 100000;
    dev_cfg.scl_wait_us = 200000;

    if (i2c_master_bus_add_device(driver_config->i2c_bus_handle, &dev_cfg, &driver_config->i2c_dev_handle) != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_bus_add_device() failed");
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

void pn532_release_io_ext(pn532_io_handle_t io_handle) {
    if (io_handle == NULL || io_handle->driver_data == NULL) {
        return;
    }

    pn532_i2c_driver_config *driver_config = (pn532_i2c_driver_config *)io_handle->driver_data;

    if (driver_config->i2c_dev_handle != NULL) {
        ESP_LOGD(TAG, "remove i2c device ...");
        i2c_master_bus_rm_device(driver_config->i2c_dev_handle);
        driver_config->i2c_dev_handle = NULL;
    }
}

void pn532_empty_callback(pn532_io_handle_t io_handle) {
  return;
}
