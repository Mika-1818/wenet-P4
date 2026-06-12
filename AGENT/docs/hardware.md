# Hardware Connections (ESP32-P4 to RFM98W)

This document outlines the pin connections required between the ESP32-P4 development board and the HopeRF RFM98W (SX1276) radio module. 

> [!NOTE]
> The camera connections (MIPI-CSI) are omitted here, as they typically depend on the specific FPC ribbon connector and adapter board you are using.

## RFM98W Pinout

| ESP32-P4 Pin | RFM98W Pin | Function | Description |
| :--- | :--- | :--- | :--- |
| **GPIO 2** | `MISO` | SPI MISO | Master In Slave Out for radio register access |
| **GPIO 3** | `MOSI` | SPI MOSI | Master Out Slave In for radio register access |
| **GPIO 4** | `SCK` | SPI SCK | SPI Clock |
| **GPIO 5** | `NSS` | SPI CS | Chip Select (Active Low) |
| **GPIO 10** | `DIO2` | Data In | I2S Data Out (ESP32) -> FSK Data In (Continuous mode) |
| **3.3V** | `3.3V` | Power | Make sure your regulator can supply the TX current spikes! |
| **GND** | `GND` | Ground | Common Ground |

> [!IMPORTANT]
> - Ensure you connect an appropriate 433 MHz antenna to the `ANT` pin of the RFM98W before transmitting.
> - The `DIO2` pin is absolutely critical. In Continuous Transmit mode, the state of this pin directly shifts the carrier frequency up or down by the configured deviation. If it is disconnected, the modulator will not work.
