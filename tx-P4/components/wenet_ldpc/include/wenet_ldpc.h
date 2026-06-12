#ifndef WENET_LDPC_H
#define WENET_LDPC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// RA LDPC encoder over 258 info bytes (2064 bits), producing 65 parity bytes (516 bits padded).
// The info block is typically the 256-byte payload plus the 2-byte CRC.
void wenet_ldpc_encode(const uint8_t info[258], uint8_t parity[65]);

#ifdef __cplusplus
}
#endif

#endif // WENET_LDPC_H
