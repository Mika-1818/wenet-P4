#ifndef WENET_CAMERA_HAL_H
#define WENET_CAMERA_HAL_H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t width;
    uint16_t height;
    uint8_t  jpeg_quality; // 0-100, encoder-defined scale
} wenet_camera_config_t;

esp_err_t wenet_camera_init(const wenet_camera_config_t *cfg);

// Captures one frame and JPEG-encodes it into a PSRAM buffer.
// On success, *out_buf is allocated (MALLOC_CAP_SPIRAM) and *out_len is set.
// Caller owns out_buf and must free() it.
esp_err_t wenet_camera_capture_jpeg(uint8_t **out_buf, size_t *out_len);

esp_err_t wenet_camera_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // WENET_CAMERA_HAL_H
