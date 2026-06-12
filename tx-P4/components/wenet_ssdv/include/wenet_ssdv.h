#ifndef WENET_SSDV_H
#define WENET_SSDV_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*wenet_ssdv_packet_cb_t)(const uint8_t *packet, void *ctx);

// Initialize SSDV encoding parameters
void wenet_ssdv_encode_begin(const char *callsign, uint8_t image_id, uint8_t quality);

// Encode a complete JPEG buffer into SSDV packets.
// packet_cb is called for each 256-byte SSDV packet generated.
void wenet_ssdv_encode_jpeg(const uint8_t *jpeg_buf, size_t jpeg_len, wenet_ssdv_packet_cb_t packet_cb, void *ctx);

#ifdef __cplusplus
}
#endif

#endif // WENET_SSDV_H
