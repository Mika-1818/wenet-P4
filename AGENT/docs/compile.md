# Compiling Wenet for ESP32-P4

This document explains how to build the firmware using ESP-IDF.

## Prerequisites

You must have ESP-IDF (>= v5.4) installed and its tools available in your current terminal session.

If you installed it using the standard installer, you activate the environment by sourcing the script:
```bash
source "/home/mika/.espressif/tools/activate_idf_v6.0.1.sh"
```
*(Note: Use the exact path provided during your ESP-IDF installation).*

## Build Instructions

1. **Navigate to the Project Directory:**
   ```bash
   cd /home/mika/wenet-P4/tx-P4
   ```

2. **Set the Target to ESP32-P4:**
   This tells the build system which SoC you are compiling for. You only need to do this once.
   ```bash
   idf.py set-target esp32p4
   ```

3. **Build the Project:**
   Compile the code into a binary.
   ```bash
   idf.py build
   ```

4. **Flash and Monitor:**
   Connect your ESP32-P4 development board via USB. If your device appears on `/dev/ttyUSB0`, flash the code and immediately open the serial monitor to view the logs:
   ```bash
   idf.py -p /dev/ttyUSB0 flash monitor
   ```
   *(Replace `/dev/ttyUSB0` with the actual port if it differs, e.g., `/dev/ttyACM0`).*

## Exiting the Monitor
To exit the serial monitor at any time, use the keyboard shortcut `Ctrl + ]`.
