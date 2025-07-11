#include <string.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "pn532_driver.h"

static const char TAG[] = "pn532_driver";

//#define CONFIG_PN532DEBUG

#ifndef CONFIG_ENABLE_IRQ_ISR
static bool pn532_is_ready();
#endif

#define PN532_COMMAND_BUFFER_LEN 64

#ifdef CONFIG_ENABLE_IRQ_ISR
/**
 *  @brief  ISR for PN532 to indicate data is available.
 *  @param  arg PN532 io handle
 */
static void IRAM_ATTR pn532_irq_handler(void *arg) {
    pn532_io_handle_t io_handle = (pn532_io_handle_t)arg;
    xQueueSendToBackFromISR(io_handle->IRQQueue, &io_handle->irq, NULL);
}
#endif

esp_err_t pn532_init(pn532_io_handle_t io_handle)
{
    gpio_config_t io_conf;

    if (io_handle == NULL)
        return ESP_ERR_INVALID_ARG;

    if (io_handle->reset != GPIO_NUM_NC) {
        // configure Reset pin
        io_conf.intr_type = GPIO_INTR_DISABLE;
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pin_bit_mask = ((1ULL) << io_handle->reset);
        io_conf.pull_down_en = 0;
        io_conf.pull_up_en = 1;
        if (gpio_config(&io_conf) != ESP_OK)
            return ESP_FAIL;
    }

#ifdef CONFIG_ENABLE_IRQ_ISR
    if (io_handle->irq != GPIO_NUM_NC) {
        if (io_handle->IRQQueue != NULL)
            vQueueDelete(io_handle->IRQQueue);
        // create a queue to handle gpio event from isr
        io_handle->IRQQueue = xQueueCreate(1, sizeof(gpio_num_t));

        // install IRQ handler
        gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
        gpio_isr_handler_add(io_handle->irq, pn532_irq_handler, (void *) io_handle);
        ESP_LOGD(TAG, "pn532_init(): using IRQ line with ISR.");
    }
#else
    ESP_LOGD(TAG, "pn532_init(): using IRQ line in polling mode.");
#endif

    if (io_handle->irq != GPIO_NUM_NC) {
        // configure IRQ pin
#ifdef CONFIG_ENABLE_IRQ_ISR
        io_conf.intr_type = GPIO_INTR_NEGEDGE;
#else
        io_conf.intr_type = GPIO_INTR_DISABLE;
#endif
        io_conf.mode = GPIO_MODE_INPUT;
        io_conf.pin_bit_mask = ((1ULL) << io_handle->irq);
        io_conf.pull_down_en = 0;
        io_conf.pull_up_en = 1;
        if (gpio_config(&io_conf) != ESP_OK)
            return ESP_FAIL;
    }

    // Reset the PN532
    if (io_handle->reset != GPIO_NUM_NC) {
        ESP_LOGD(TAG, "reset PN532 ...");
        pn532_reset(io_handle);
        ESP_LOGD(TAG, "reset done");
    }

    ESP_LOGD(TAG, "pn532_init(): call pn532_init_io() ...");
    io_handle->isSAMConfigDone = false;
    esp_err_t err = io_handle->pn532_init_io(io_handle);
    if (err != ESP_OK)
        return err;


#ifdef CONFIG_ENABLE_IRQ_ISR
    // clear queue after successful initialization
    if (io_handle->irq >= 0)
        xQueueReset(io_handle->IRQQueue);
#endif

    ESP_LOGD(TAG, "pn532_init(): call pn532_SAM_config() ...");
    err = pn532_SAM_config(io_handle);
    if (err != ESP_OK)
        return err;

    io_handle->isSAMConfigDone = true;

    ESP_LOGD(TAG, "pn532_init(): call pn532_init_extra() ...");
    if (io_handle->pn532_init_extra != NULL) {
        err = io_handle->pn532_init_extra(io_handle);
        if (err != ESP_OK)
            return err;
    }

    ESP_LOGD(TAG, "init ok");
    return err;
}

void pn532_release(pn532_io_handle_t io_handle)
{
    if (io_handle == NULL)
        return;

    ESP_LOGD(TAG, "call pn532_release_io() ...");
    if (io_handle->pn532_release_io != NULL) {
        io_handle->pn532_release_io(io_handle);
    }

#ifdef CONFIG_ENABLE_IRQ_ISR
    if (io_handle->irq != GPIO_NUM_NC) {
        ESP_LOGD(TAG, "remove irq handler");
        gpio_isr_handler_remove(io_handle->irq);
        gpio_uninstall_isr_service();

        if (io_handle->IRQQueue != NULL) {
            ESP_LOGD(TAG, "delete IRQ queue");
            vQueueDelete(io_handle->IRQQueue);
            io_handle->IRQQueue = NULL;
        }
    }
#endif

    if (io_handle->reset != GPIO_NUM_NC) {
        ESP_LOGD(TAG, "release reset pin");
        gpio_reset_pin(io_handle->reset);
    }

    if (io_handle->irq != GPIO_NUM_NC) {
        ESP_LOGD(TAG, "release irq pin");
        gpio_reset_pin(io_handle->irq);
    }
}

void pn532_delete_driver(pn532_io_handle_t io_handle)
{
    if (io_handle == NULL)
        return;

    if (io_handle->pn532_release_driver != NULL)
        io_handle->pn532_release_driver(io_handle);
}

void pn532_reset(pn532_io_handle_t io_handle)
{
    if (io_handle->reset == GPIO_NUM_NC)
        return;

    gpio_set_level(io_handle->reset, 0);
    vTaskDelay(400 / portTICK_PERIOD_MS);
    gpio_set_level(io_handle->reset, 1);
    vTaskDelay(10 / portTICK_PERIOD_MS);

    io_handle->isSAMConfigDone = false;
}

esp_err_t pn532_write_command(pn532_io_handle_t io_handle, const uint8_t *cmd, uint8_t cmdlen, int timeout)
{
    static uint8_t command[256];

    uint8_t checksum = PN532_HOST_TO_PN532;

    int idx = 0;
    command[idx++] = PN532_STARTCODE1;
    command[idx++] = PN532_STARTCODE2;
    command[idx++] = (cmdlen + 1);
    command[idx++] = 0x100 - (cmdlen + 1);
    command[idx++] = PN532_HOST_TO_PN532;

    uint8_t i = 0;
    for (i = 0; i < cmdlen; i++) {
        command[idx++] = cmd[i];
        checksum += cmd[i];
    }
    command[idx++] = ~checksum + 1;

#ifdef CONFIG_PN532DEBUG
    ESP_LOGD(TAG, "%s Sending :", __func__);
    esp_log_buffer_hex(TAG, command, idx);
#endif

//    vTaskDelay(pdMS_TO_TICKS(100));

//    ESP_LOGI(TAG, "going to send command ...");
    esp_err_t result = io_handle->pn532_write(io_handle, command, idx, timeout);

    if (result != ESP_OK) {
        char *resultText = NULL;
        switch (result) {
            case ESP_ERR_INVALID_ARG:
                resultText = "parameter error";
                break;
            case ESP_FAIL:
                resultText = "send command failed";
                break;
            case ESP_ERR_INVALID_STATE:
                resultText = "invalid state";
                break;
            case ESP_ERR_TIMEOUT:
                resultText = "timeout occurred";
                break;
            default:
                resultText = "unknown error";

        }
        ESP_LOGW(TAG, "%s write failed: %s!", __func__, resultText);
    }
    return result;
}

esp_err_t pn532_read_data(pn532_io_handle_t io_handle, uint8_t *buffer, uint8_t length, int32_t timeout)
{
    static uint8_t local_buffer[256];

    bzero(local_buffer, sizeof(local_buffer));

    if (timeout == 0) {
        timeout = -1;
    }

    esp_err_t res = io_handle->pn532_read(io_handle, local_buffer, length, timeout);
    if (res != ESP_OK) {
        return res;
    }

#ifdef CONFIG_PN532DEBUG
    ESP_LOGD(TAG, "Reading: ");
    esp_log_buffer_hex(TAG, local_buffer, length);
#endif

    // copy data without preamble
    memcpy(buffer, local_buffer, length);
    return ESP_OK;
}

#ifndef CONFIG_ENABLE_IRQ_ISR
/**
 * Check if data is ready
 * @param io_handle PN532 io handle
 * @return true, if ready
 */
bool pn532_is_ready(pn532_io_handle_t io_handle)
{
    // check if status is ready by IRQ line being pulled low.
    uint8_t x = gpio_get_level(io_handle->irq);
#ifdef CONFIG_IRQDEBUG
    ESP_LOGI(TAG, "IRQ: %d", x);
#endif
    return (x == 0);
}
#endif
static esp_err_t pn532_poll_ready(pn532_io_handle_t io_handle, int32_t timeout)
{
    TickType_t start_ticks = xTaskGetTickCount();
    TickType_t timeout_ticks = (timeout > 0) ? pdMS_TO_TICKS(timeout) : portMAX_DELAY;
    TickType_t elapsed_ticks = 0;

    esp_rom_delay_us(1000);

    bool is_ready = false;
    while (!is_ready && elapsed_ticks <= timeout_ticks)
    {
        is_ready = ESP_OK == io_handle->pn532_is_ready(io_handle);
        if (!is_ready) {
            vTaskDelay(pdMS_TO_TICKS(10));
            elapsed_ticks = xTaskGetTickCount() - start_ticks;
        }
    }

    return is_ready ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t pn532_wait_ready(pn532_io_handle_t io_handle, int32_t timeout)
{
    if (io_handle->irq == GPIO_NUM_NC) {
        esp_err_t err = ESP_OK;
        if (io_handle->pn532_is_ready != NULL) {
            err = pn532_poll_ready(io_handle, timeout);
        }
        return err;
    }

#ifdef CONFIG_ENABLE_IRQ_ISR
    uint32_t io_num = 0;
    TickType_t delay = 0;

    if (timeout == 0)
        delay = portMAX_DELAY;
    else
        delay = timeout / portTICK_PERIOD_MS;

    BaseType_t res = xQueueReceive(io_handle->IRQQueue, &io_num, delay);

    if (res) {
        return ESP_OK;
    }

    return ESP_FAIL;
#else
    uint16_t timer = 0;
    while (!pn532_is_ready(io_handle))
    {
        if (timeout != 0)
        {
            timer += 10;
            if (timer > timeout)
            {
#ifdef CONFIG_PN532DEBUG
                ESP_LOGE (TAG, "Wait ready TIMEOUT after %d ms!", (int)timeout);
#endif
                return ESP_ERR_TIMEOUT;
            }
        }
        vTaskDelay (10 / portTICK_PERIOD_MS);
    }
    return ESP_OK;
#endif
}

esp_err_t pn532_SAM_config(pn532_io_handle_t io_handle)
{
    esp_err_t result;
    uint8_t response_buffer[16];

    if (io_handle == NULL || io_handle->driver_data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    static const uint8_t sam_config_frame[] = { 0x14, 0x01, 0x00, 0x01 }; // normal mode and use IRQ pin

    result = pn532_send_command_wait_ack(io_handle, sam_config_frame, sizeof(sam_config_frame), 1000);
    if (ESP_OK != result)
        return result;

#ifdef CONFIG_PN532DEBUG
    ESP_LOGD(TAG, "pn532_SAM_config(): Waiting for IRQ/ready");
#endif
    result = pn532_wait_ready(io_handle, 100);
    if (ESP_OK != result) {
#ifdef CONFIG_PN532DEBUG
        ESP_LOGD(TAG, "pn532_SAM_config(): Timeout occurred");
#endif
        return result;
    }

    // read data packet
    result = pn532_read_data(io_handle, response_buffer, 10, PN532_READ_TIMEOUT);
    if (ESP_OK != result)
        return result;

    if (response_buffer[6] != 0x15) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t pn532_send_command_wait_ack(pn532_io_handle_t io_handle, const uint8_t *cmd, uint8_t cmd_length, int32_t timeout)
{
    esp_err_t result;

    if (io_handle == NULL || io_handle->driver_data == NULL || cmd == NULL || cmd_length == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // write the command
    result = pn532_write_command(io_handle, cmd, cmd_length, timeout);
    if (result != ESP_OK) {
        return result;
    }

#ifdef CONFIG_PN532DEBUG
    ESP_LOGD(TAG, "pn532_send_command_wait_ack(): Waiting for PN532 IRQ/ready");
#endif
    result = pn532_wait_ready(io_handle, timeout);
    if (result != ESP_OK) {
#ifdef CONFIG_PN532DEBUG
        if (result == ESP_ERR_TIMEOUT)
            ESP_LOGE(TAG, "pn532_send_command_wait_ack(): Timeout occurred!");
#endif
        ESP_LOGD(TAG, "pn532_wait_ready() failed with 0x%X", result);
        return result;
    }

    // read acknowledgement

    result = pn532_read_ack(io_handle);
    if (result != ESP_OK) {
#ifdef CONFIG_PN532DEBUG
        ESP_LOGD(TAG, "pn532_send_command_wait_ack(): No ACK frame received!");
#endif
    }

    return result;
}

esp_err_t pn532_read_ack(pn532_io_handle_t io_handle) {
    uint8_t ack_buffer[6];
    esp_err_t result;

    if (io_handle == NULL || io_handle->driver_data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    result = pn532_read_data(io_handle, ack_buffer, sizeof(ACK_FRAME), PN532_READ_TIMEOUT);
    if (result != ESP_OK)
        return result;

    if (0 != memcmp(ack_buffer, ACK_FRAME, sizeof(ACK_FRAME))) {
        return ESP_FAIL;
    }

    return ESP_OK;
}



esp_err_t pn532_disable_irq(pn532_io_handle_t io_handle) {

    if (io_handle == NULL || io_handle->driver_data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
#ifdef CONFIG_ENABLE_IRQ_ISR
    ESP_LOGD(TAG, "Disabling PN532 IRQ handler.");
    return gpio_isr_handler_remove(io_handle->irq);
#else
    return ESP_OK; // No ISR to disable in polling mode
#endif
}

esp_err_t pn532_gpio_conf_check_validity(pn532_gpio_conf_t config) {
  esp_err_t ret = ESP_OK;

  if (config.reset == GPIO_NUM_NC) {
    ret = ESP_ERR_INVALID_ARG;
  } 
  else if (config.irq == GPIO_NUM_NC) {
    ret = ESP_ERR_INVALID_ARG;
  }

  return ret;
}

esp_err_t pn532_enable_irq(pn532_io_handle_t io_handle) {

    if (io_handle == NULL || io_handle->driver_data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
#ifdef CONFIG_ENABLE_IRQ_ISR
    ESP_LOGD(TAG, "Re-enabling PN532 IRQ handler.");
    // The ISR service should already be installed from pn532_init()
    return gpio_isr_handler_add(io_handle->irq, pn532_irq_handler, (void *) io_handle);
#else
    return ESP_OK; // No ISR to enable in polling mode
#endif
}
