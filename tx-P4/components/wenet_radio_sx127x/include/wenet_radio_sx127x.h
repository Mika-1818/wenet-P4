#ifndef WENET_RADIO_SX127X_H
#define WENET_RADIO_SX127X_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the SPI peripheral and ping the RFM98W device (check RegVersion).
esp_err_t wenet_radio_init(void);

// Configure the RFM98W for FSK transmission at the given frequency and power.
// freq_hz: e.g. 443500000 for 443.500 MHz
// power_dbm: 2 to 17 dBm
esp_err_t wenet_radio_configure(uint32_t freq_hz, uint8_t power_dbm);

// Enable or disable continuous FSK transmission mode (DIO2 as data input).
esp_err_t wenet_radio_set_tx_continuous(bool enable);

// Read the version register from the radio (expect 0x12 for SX1276).
uint8_t wenet_radio_read_version(void);

#ifdef __cplusplus
}
#endif

#endif // WENET_RADIO_SX127X_H
