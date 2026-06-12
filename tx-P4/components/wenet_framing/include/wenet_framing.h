#ifndef WENET_FRAMING_H
#define WENET_FRAMING_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Calculate CRC16-CCITT (false) for the given buffer.
// Polynomial: 0x1021, Initial value: 0xFFFF, No reflection, Final XOR: 0x0000.
uint16_t wenet_crc16_ccitt_false(const uint8_t *buf, size_t len);

// Scramble the buffer using the XOR scrambler PN sequence in-place.
void wenet_scramble(uint8_t *buf, size_t len);

// Build a full frame.
// payload must be exactly 256 bytes.
// out_frame must have room for 343 bytes: 16 (preamble) + 4 (UW) + 256 (payload) + 2 (CRC) + 65 (LDPC parity).
// This function constructs the frame, calls the LDPC encoder (stubbed until M2), and applies scrambling.
size_t wenet_build_frame(const uint8_t payload[256], uint8_t out_frame[343]);

// Generate an idle packet payload (0x56 packet type + 255x0x56).
void wenet_generate_idle_payload(uint8_t payload[256]);

#ifdef __cplusplus
}
#endif

#endif // WENET_FRAMING_H
