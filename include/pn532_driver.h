#ifndef PN532_DRIVER_H
#define PN532_DRIVER_H

#include "esp_err.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C"
{
#endif
struct pn532_io_t;
typedef struct pn532_io_t pn532_io_t;
typedef struct pn532_io_t * pn532_io_handle_t;

#define PN532_PREAMBLE                      (0x00)
#define PN532_STARTCODE1                    (0x00)
#define PN532_STARTCODE2                    (0xFF)
#define PN532_POSTAMBLE                     (0x00)

#define PN532_HOST_TO_PN532                 (0xD4)
#define PN532_PN532TOHOST                   (0xD5)

#define PN532_WRITE_TIMEOUT                 100  // in ms
#define PN532_READ_TIMEOUT                  100  // in ms
#define PN532_READY_WAIT_TIMEOUT            1000 // in ms

static const uint8_t ACK_FRAME[]  = { 0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00 };
static const uint8_t NACK_FRAME[] = { 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00 };

struct pn532_io_t {
    esp_err_t (*pn532_init_io)(pn532_io_handle_t io_handle);
    void (*pn532_release_io)(pn532_io_handle_t io_handle);
    void (*pn532_release_driver)(pn532_io_handle_t io_handle);
    esp_err_t (*pn532_read)(pn532_io_handle_t io_handle, uint8_t *read_buffer, size_t read_size, int xfer_timeout_ms);
    esp_err_t (*pn532_write)(pn532_io_handle_t io_handle, const uint8_t *write_buffer, size_t write_size, int xfer_timeout_ms);
    esp_err_t (*pn532_init_extra)(pn532_io_handle_t io_handle);
    esp_err_t (*pn532_is_ready)(pn532_io_handle_t io_handle);

    gpio_num_t reset;
    gpio_num_t irq;

    bool isSAMConfigDone;
#ifdef CONFIG_ENABLE_IRQ_ISR
    QueueHandle_t IRQQueue;
#endif

    void * driver_data;
};

/**
 * Initialize PN532 RFID module
 * @param io_handle PN532 io handle
 * @return ESP_OK if successful
 */
esp_err_t pn532_init(pn532_io_handle_t io_handle);

/**
 * Release io resources for PN532 module
 * @param io_handle PN532 io handle
 */
void pn532_release(pn532_io_handle_t io_handle);

/**
 * Release all driver resources
 * @param io_handle PN532 io handle
 */
void pn532_delete_driver(pn532_io_handle_t io_handle);

/**
 * Reset PN532 RFID module
 * @param io_handle PN532 io handle
 */
void pn532_reset(pn532_io_handle_t io_handle);

/**
 * Write command to PN532, automatically inserting the preamble
 * and required frame details (checksum, len, ...)
 * @param io_handle PN532 io handle
 * @param cmd pointer to command buffer
 * @param cmdlen length of command
 * @return ESP_OK if successful
 */
esp_err_t pn532_write_command(pn532_io_handle_t io_handle, const uint8_t *cmd, uint8_t cmdlen, int timeout);

/**
 * Read data from PN532.
 * @param io_handle PN532 io handle
 * @param buffer buffer to store data
 * @param length  number of bytes to read
 * @param timeout timeout in milli seconds. if 0 wait forever.
 * @return ESP_OK if successful
 */
esp_err_t pn532_read_data(pn532_io_handle_t io_handle, uint8_t *buffer, uint8_t length, int32_t timeout);

/**
 * Wait until PN532 is ready
 * @param io_handle PN532 io handle
 * @param timeout timeout in milliseconds. If 0, wait for ever
 * @return ESP_OK if ready or ESP_ERR_TIMEOUT for timeout
 */
esp_err_t pn532_wait_ready(pn532_io_handle_t io_handle, int32_t timeout);

/**
 * Configure the SAM (Secure Access Module). Use normal mode.
 * @param io_handle PN532 io handle
 * @return ESP_OK if successful
 */
esp_err_t pn532_SAM_config(pn532_io_handle_t io_handle);

/**
 * Send a command to PN532 and try to receive an ACK.
 * @param io_handle PN532 io handle
 * @param cmd data to send
 * @param cmd_length length in bytes
 * @param timeout timeout to wait for ACK
 * @return ESP_OK if ACK received
 */
esp_err_t pn532_send_command_wait_ack(pn532_io_handle_t io_handle, const uint8_t *cmd, uint8_t cmd_length, int32_t timeout);

/**
 *
 * @param io_handle PN532 io handle
 * @return ESP_OK if successful
 */
esp_err_t pn532_read_ack(pn532_io_handle_t io_handle);

#ifdef __cplusplus
}
#endif

#endif //PN532_DRIVER_H
