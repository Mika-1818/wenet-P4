#include "wenet_modulator_i2s.h"
#include <string.h>
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "wenet_modulator";

// Hardware-specific pin - connected to RFM98W DIO2
#define PIN_NUM_DIO2 10

static i2s_chan_handle_t tx_chan;
static QueueHandle_t frame_queue;
static TaskHandle_t modulator_task_handle;

// One frame is 343 bytes. We queue multiple frames.
#define FRAME_SIZE 343
#define QUEUE_LENGTH 8

extern void wenet_generate_idle_payload(uint8_t payload[256]);
extern size_t wenet_build_frame(const uint8_t payload[256], uint8_t out_frame[343]);

static void modulator_task(void *arg) {
    uint8_t frame[FRAME_SIZE];
    uint8_t idle_frame[FRAME_SIZE];
    uint8_t idle_payload[256];
    
    // Pre-build the idle frame to pump when starved
    wenet_generate_idle_payload(idle_payload);
    wenet_build_frame(idle_payload, idle_frame);

    size_t bytes_written = 0;
    while (1) {
        // Wait for a frame to be available, or timeout quickly to pump idle frame
        if (xQueueReceive(frame_queue, frame, pdMS_TO_TICKS(10)) == pdTRUE) {
            i2s_channel_write(tx_chan, frame, FRAME_SIZE, &bytes_written, portMAX_DELAY);
        } else {
            // Starved! Pump idle frame to keep DIO2 modulated correctly
            i2s_channel_write(tx_chan, idle_frame, FRAME_SIZE, &bytes_written, portMAX_DELAY);
        }
    }
}

esp_err_t wenet_modulator_init(void) {
    ESP_LOGI(TAG, "Initializing I2S Modulator at 96kHz");

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    esp_err_t ret = i2s_new_channel(&chan_cfg, &tx_chan, NULL);
    if (ret != ESP_OK) return ret;

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(96000), // 96 kHz bit rate
        .slot_cfg = I2S_STD_PCM_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_8BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_GPIO_UNUSED,
            .ws   = I2S_GPIO_UNUSED,
            .dout = PIN_NUM_DIO2,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    ret = i2s_channel_init_std_mode(tx_chan, &std_cfg);
    if (ret != ESP_OK) return ret;

    frame_queue = xQueueCreate(QUEUE_LENGTH, FRAME_SIZE);
    if (!frame_queue) return ESP_ERR_NO_MEM;

    xTaskCreatePinnedToCore(modulator_task, "radio_tx_task", 4096, NULL, configMAX_PRIORITIES - 1, &modulator_task_handle, 1);

    return ESP_OK;
}

esp_err_t wenet_modulator_submit_frame(const uint8_t frame[343]) {
    if (xQueueSend(frame_queue, frame, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Modulator queue full, frame dropped!");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t wenet_modulator_start(void) {
    ESP_LOGI(TAG, "Starting I2S Modulator");
    return i2s_channel_enable(tx_chan);
}

esp_err_t wenet_modulator_stop(void) {
    ESP_LOGI(TAG, "Stopping I2S Modulator");
    return i2s_channel_disable(tx_chan);
}
