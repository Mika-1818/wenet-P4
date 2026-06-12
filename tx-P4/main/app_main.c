#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "wenet_camera_hal.h"
#include "wenet_radio_sx127x.h"
#include "wenet_modulator_i2s.h"
#include "wenet_ssdv.h"
#include "wenet_framing.h"

static const char *TAG = "wenet_main";

static void ssdv_packet_callback(const uint8_t *packet, void *ctx) {
    uint8_t out_frame[343];
    wenet_build_frame(packet, out_frame);
    wenet_modulator_submit_frame(out_frame);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Wenet ESP32-P4 Port Started");

    // 1. Initialize Radio (SPI + SX127x)
    if (wenet_radio_init() == ESP_OK) {
        wenet_radio_configure(443500000, 17); // 443.500 MHz, 17 dBm
        wenet_radio_set_tx_continuous(true);
    }

    // 2. Initialize Modulator (I2S -> DIO2)
    wenet_modulator_init();
    wenet_modulator_start();

    // 3. Initialize Camera
    wenet_camera_config_t cam_cfg = {
        .width = 1280,
        .height = 720,
        .jpeg_quality = 10
    };
    wenet_camera_init(&cam_cfg);

    // Continuous capture loop
    while (1) {
        uint8_t *jpeg_buf = NULL;
        size_t jpeg_len = 0;

        if (wenet_camera_capture_jpeg(&jpeg_buf, &jpeg_len) == ESP_OK) {
            ESP_LOGI(TAG, "Captured JPEG, length: %d bytes. Encoding SSDV...", jpeg_len);
            
            wenet_ssdv_encode_begin("VK5QI", 1, 4);
            wenet_ssdv_encode_jpeg(jpeg_buf, jpeg_len, ssdv_packet_callback, NULL);
            
            free(jpeg_buf);
        }

        vTaskDelay(pdMS_TO_TICKS(5000)); // Delay between images
    }
}
