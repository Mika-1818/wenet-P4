#include "wenet_radio_sx127x.h"
#include <string.h>
#include "esp_log.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "wenet_radio";

// Change these pins according to the actual board layout
#define PIN_NUM_MISO 2
#define PIN_NUM_MOSI 3
#define PIN_NUM_CLK  4
#define PIN_NUM_CS   5

static spi_device_handle_t spi_handle;

static esp_err_t radio_write_reg(uint8_t reg, uint8_t val) {
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = 8;
    t.cmd = reg | 0x80; // Write bit is 0x80 for SX127x
    t.tx_buffer = &val;
    return spi_device_transmit(spi_handle, &t);
}

static uint8_t radio_read_reg(uint8_t reg) {
    spi_transaction_t t;
    uint8_t val;
    memset(&t, 0, sizeof(t));
    t.length = 8;
    t.cmd = reg & 0x7F; // Read bit is 0
    t.rx_buffer = &val;
    spi_device_transmit(spi_handle, &t);
    return val;
}

esp_err_t wenet_radio_init(void) {
    ESP_LOGI(TAG, "Initializing SPI for SX127x");

    spi_bus_config_t buscfg = {
        .miso_io_num = PIN_NUM_MISO,
        .mosi_io_num = PIN_NUM_MOSI,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 32
    };

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 1000000, // 1 MHz conservative start
        .mode = 0,                 // SPI mode 0
        .spics_io_num = PIN_NUM_CS,
        .queue_size = 1,
        .command_bits = 8          // Send command (register address)
    };

    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) return ret;

    ret = spi_bus_add_device(SPI2_HOST, &devcfg, &spi_handle);
    if (ret != ESP_OK) return ret;

    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t version = radio_read_reg(0x42);
    ESP_LOGI(TAG, "Radio RegVersion: 0x%02X", version);
    if (version == 0x00 || version == 0xFF) {
        ESP_LOGE(TAG, "Failed to communicate with radio");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t wenet_radio_configure(uint32_t freq_hz, uint8_t power_dbm) {
    ESP_LOGI(TAG, "Configuring radio: %lu Hz, %d dBm", freq_hz, power_dbm);

    // Sleep Mode (FSK)
    radio_write_reg(0x01, 0x00);
    vTaskDelay(pdMS_TO_TICKS(10));

    // Continuous Transmit Mode
    radio_write_reg(0x31, 0x00);

    // Calculate Frf
    // Frf = freq_hz / (32000000 / 2^19) = (freq_hz * 524288) / 32000000
    uint64_t frf_val = ((uint64_t)freq_hz << 19) / 32000000;
    radio_write_reg(0x06, (frf_val >> 16) & 0xFF);
    radio_write_reg(0x07, (frf_val >> 8) & 0xFF);
    radio_write_reg(0x08, frf_val & 0xFF);

    // Set Deviation (~70 kHz). radio_wrappers.py uses deviation = 71797 for 115200 baud
    // deviation / 61.03 = 1176 = 0x0498
    radio_write_reg(0x04, 0x04);
    radio_write_reg(0x05, 0x98);

    // TX Power Mapping from radio_wrappers.py
    uint8_t paconfig = 0x80;
    if (power_dbm >= 2 && power_dbm <= 17) {
        paconfig = 0x80 | (power_dbm - 2);
    }
    radio_write_reg(0x09, paconfig);

    return ESP_OK;
}

esp_err_t wenet_radio_set_tx_continuous(bool enable) {
    if (enable) {
        ESP_LOGI(TAG, "Entering TX Continuous Mode");
        // FSTX mode
        radio_write_reg(0x01, 0x02);
        vTaskDelay(pdMS_TO_TICKS(10));
        // TX mode
        radio_write_reg(0x01, 0x03);
    } else {
        ESP_LOGI(TAG, "Entering Sleep Mode");
        // Sleep mode
        radio_write_reg(0x01, 0x00);
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    return ESP_OK;
}

uint8_t wenet_radio_read_version(void) {
    return radio_read_reg(0x42);
}
