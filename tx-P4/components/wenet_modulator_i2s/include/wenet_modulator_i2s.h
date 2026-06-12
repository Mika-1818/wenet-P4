#ifndef WENET_MODULATOR_I2S_H
#define WENET_MODULATOR_I2S_H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the I2S peripheral for bitstream generation at 96 kHz.
// The output pin will be connected to the RFM98W DIO2.
esp_err_t wenet_modulator_init(void);

// Submit a single framed bitstream packet (343 bytes) to the DMA ring buffer.
// Blocks if the buffer is full.
esp_err_t wenet_modulator_submit_frame(const uint8_t frame[343]);

// Start continuous bitstream transmission.
esp_err_t wenet_modulator_start(void);

// Stop transmission.
esp_err_t wenet_modulator_stop(void);

#ifdef __cplusplus
}
#endif

#endif // WENET_MODULATOR_I2S_H
