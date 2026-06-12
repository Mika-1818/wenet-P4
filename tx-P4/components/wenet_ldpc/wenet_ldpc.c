#include "wenet_ldpc.h"
#include <string.h>

#define Nibits 2064
#define Npbits 516
#define Nwt     12

static const unsigned short hrows[] = { 
#include "Hrow2064.txt"
};

void wenet_ldpc_encode(const uint8_t info[258], uint8_t parity[65]) {
    uint8_t ibits[Nibits];
    uint8_t pbits[Npbits];

    // Unpack bits from bytes (MSB first)
    for (int i = 0; i < 258; i++) {
        for (int b = 0; b < 8; b++) {
            ibits[i * 8 + b] = (info[i] >> (7 - b)) & 1;
        }
    }

    unsigned int p, i_idx, tmp, par, prev = 0;
    
    // LDPC Encode
    for (p = 0; p < Npbits; p++) {
        par = 0; 
        for (i_idx = 0; i_idx < Nwt; i_idx++) {
            par = par + ibits[hrows[p * Nwt + i_idx] - 1];
        }
        tmp = par + prev;
        tmp &= 1; // retain LSB
        prev = tmp; 
        pbits[p] = tmp; 
    }

    // Pack bits back to bytes (MSB first)
    memset(parity, 0, 65);
    for (p = 0; p < Npbits; p++) {
        if (pbits[p]) {
            parity[p / 8] |= (1 << (7 - (p % 8)));
        }
    }
}
