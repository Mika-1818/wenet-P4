#include "wenet_ssdv.h"
#include "ssdv.h"

static ssdv_t s_ssdv;

void wenet_ssdv_encode_begin(const char *callsign, uint8_t image_id, uint8_t quality) {
    // 256 is the standard Wenet SSDV payload size
    ssdv_enc_init(&s_ssdv, SSDV_TYPE_NORMAL, (char*)callsign, image_id, quality, 256);
}

void wenet_ssdv_encode_jpeg(const uint8_t *jpeg_buf, size_t jpeg_len, wenet_ssdv_packet_cb_t packet_cb, void *ctx) {
    uint8_t packet_buf[256];
    ssdv_enc_set_buffer(&s_ssdv, packet_buf);

    size_t offset = 0;
    while (1) {
        int c;
        while ((c = ssdv_enc_get_packet(&s_ssdv)) == SSDV_FEED_ME) {
            if (offset >= jpeg_len) {
                // Should not happen unless EOI wasn't reached, but prevent infinite loop
                return;
            }
            
            // Feed up to 128 bytes at a time
            size_t chunk = (jpeg_len - offset) > 128 ? 128 : (jpeg_len - offset);
            ssdv_enc_feed(&s_ssdv, (uint8_t*)&jpeg_buf[offset], chunk);
            offset += chunk;
        }

        if (c == SSDV_EOI) {
            break; // Done encoding
        } else if (c != SSDV_OK) {
            // Error
            break;
        }

        // We have a packet
        packet_cb(packet_buf, ctx);
    }
}
