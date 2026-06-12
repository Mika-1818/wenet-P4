# WENET → ESP32-P4 Port: Engineering Specification for an Agentic Coding AI

## 0. Mission Statement

Port Project Horus's **Wenet** high-speed imagery downlink transmitter (currently a Raspberry Pi
application) to an **ESP32-P4** development board, producing a firmware image that is
**on-air bit-compatible with Wenet v2 (I2S mode)**: 96 kbaud 2-FSK, rate-0.8 LDPC FEC, SSDV
image packets, scrambled framing — decodable by the unmodified existing Wenet receive chain
(`fsk_demod` + `drs232_ldpc` + SSDV tools + `wenet.sondehub.org` / WebWenet).

**Camera targets:**
- **Primary (mandatory):** Raspberry Pi Camera Module **V1** — Sony/OmniVision **OV5647** sensor,
  2-lane MIPI-CSI, sensor-internal ISP.
- **Secondary (best-effort / stretch goal):** Raspberry Pi Camera Module **V2** — Sony **IMX219**
  sensor, 2-lane MIPI-CSI, **RAW10 Bayer only, no internal ISP**. This path is explicitly
  feasibility-gated (see Milestone M9) because it depends on ESP32-P4 hardware ISP/demosaic
  driver maturity at the time of implementation.

**Radio:** HopeRF **RFM98W** (SX1276/77/78/79 family), driven via SPI for configuration and via
**I2S → DIO2** for direct-asynchronous FSK modulation, exactly as in `tx/radio_wrappers.py`.

**Navigation:** u-blox GPS module over **UART** (NMEA/UBX), configured for Airborne <1g dynamic
model, polled for position/velocity/time, replicating `tx/ublox.py`.

This document is written **for an autonomous coding agent** (e.g., Claude Code or similar) with
shell, filesystem, and build-tool access. It defines architecture, interfaces, milestones,
validation methods (especially **hardware-independent, host-runnable tests**, since most of the
agent's iteration loop will not have physical hardware attached), and explicit rules for handling
ambiguity.

---

## 1. Definition of Done

The project is complete when **all** of the following hold:

1. **Protocol-layer bit-exactness** (host-testable, no hardware required):
   - CRC16-CCITT (false), the v2 XOR scrambler, packet framing (preamble + UW + payload + CRC +
     LDPC parity), and the LDPC(258→65) RA encoder all produce **byte-identical output** to the
     reference `wenet` repo implementations for a shared set of golden test vectors.
   - The ported SSDV encoder produces **byte-identical SSDV packet streams** to `fsphil/ssdv`'s
     `ssdv -e` for a shared set of JPEG test images.
2. **Camera pipeline (OV5647, mandatory):** the firmware captures frames from an OV5647-based Pi
   Camera V1 via MIPI-CSI, produces baseline JPEG images via the ESP32-P4 hardware JPEG encoder
   (or a documented fallback), at a configurable resolution/quality.
3. **Radio pipeline:** the firmware configures the RFM98W for direct-async FSK at a configurable
   frequency/power, and streams framed/scrambled/LDPC-encoded bits via I2S at 96 kbaud,
   continuously and without underrun-induced glitches.
4. **End-to-end RF validation (hardware-in-the-loop, HIL):** a frame transmitted by the ESP32-P4
   is correctly demodulated and decoded by the **unmodified** upstream `wenet` RX tools
   (`fsk_demod` → `drs232_ldpc` → packet parser / `ssdv -d`), for both telemetry text packets and
   SSDV image packets.
5. **GPS:** the firmware configures a u-blox module for Airborne <1g dynamic model over UART,
   parses position/velocity/time, and emits 0x01 GPS Telemetry packets matching the documented
   wire format.
6. **System integration:** a FreeRTOS task/queue scheduler reproduces `PacketTX.py`'s priority
   behaviour (telemetry/text packets pre-empt SSDV image packets; idle packets fill gaps).
7. **IMX219 (Pi Cam V2) path:** either (a) fully working via the same JPEG/SSDV/radio pipeline,
   with documented image-quality caveats, or (b) explicitly marked **not feasible at this
   ESP-IDF version**, with a written explanation and a re-evaluation note for future IDF
   releases. Either outcome is an acceptable "done" for this stretch goal — silence/ambiguity is
   not.
8. Every component has a `README.md` documenting: what it was ported from, what (if anything)
   was changed and why, and its current test status.

---

## 2. Repository Layout

```
wenet-p4/
├── [wenet]                        # already existing in the wenet project ie. the original repo
├── AGENT_LOG.md                   # running log of decisions/assumptions (see §12)
├── ASSUMPTIONS.md                 # explicit list of unresolved ambiguities + chosen defaults
├── docs/
│   ├── protocol_reference.md      # extracted from wenet wiki (frame/packet formats)
│   ├── camera_notes.md            # OV5647 vs IMX219 findings
│   └── rf_validation.md           # HIL test logs
├── tools/
│   └── host_tests/                # native (x86) build of protocol-layer components + golden vectors
│       ├── CMakeLists.txt
│       ├── vectors/               # golden test vectors (binary files + generator scripts)
│       └── test_*.c
├── main/
│   └── app_main.c
└── components/
    ├── wenet_framing/              # CRC16, scrambler, preamble/UW framing
    ├── wenet_ldpc/                 # ported ldpc_enc.c (RA code, rate 0.8)
    ├── wenet_ssdv/                 # ported fsphil/ssdv encoder
    ├── wenet_camera_hal/           # sensor-agnostic capture/JPEG interface
    │   ├── ov5647/
    │   └── imx219/                 # stretch goal, feature-gated
    ├── wenet_radio_sx127x/         # SPI register driver for RFM98W
    ├── wenet_modulator_i2s/        # I2S DMA bitstream generator
    ├── wenet_gps_ublox/            # UART UBX/NMEA driver + Airborne<1g config
    ├── wenet_telemetry/            # packet builders (0x00/0x01/0x03/idle)
    └── wenet_config/               # NVS-backed runtime configuration
```

The split between `tools/host_tests` (runs on the development machine, no target hardware) and
`components/` (target firmware) is the backbone of the agent's iteration loop: **as much logic as
possible should live in plain, portable C that is unit-tested on the host first**, then wrapped
into ESP-IDF components.

---

## 3. Source Materials the Agent Must Acquire and Study

Before writing code, clone/fetch and read these. For each, the agent should extract the specific
artifact noted and record its location/commit hash in `AGENT_LOG.md`.

| Source | What to extract |
|---|---|
| `github.com/projecthorus/wenet` | `tx/ldpc_enc.c` + matrix header(s) (LDPC encoder); `tx/radio_wrappers.py` (scrambler sequence, SX127x register sequence, framing constants); `tx/ublox.py` (UBX message list, Airborne<1g config, telemetry packet construction); `tx/PacketTX.py` (queue priority logic, packet type constants); `tx/WenetPiCam.py` / `tx_picam_gps.py` (overall control flow); wiki page "Modem & Packet Format Details" (frame/packet byte layouts — also summarized in Appendix A below) |
| `github.com/fsphil/ssdv` | `ssdv.c`/`ssdv.h` encoder path + embedded RS(255,223) encoder |
| `github.com/projecthorus/wenet` Dockerfile / `start_tx.sh` / `start_tx_uart.sh` | Default radio parameters (freq, power, baud) and the I2S device-tree overlay (`tx/i2smaster/i2smaster.dts`) as a reference for *what* clock configuration the Pi used — not literally portable, but useful for cross-checking bit-rate math |
| Espressif `esp-idf` (P4 branch ≥ v5.4) | `esp_cam_sensor` component (OV5647 + IMX219 driver presence/status), `esp_driver_jpeg`, `esp_driver_cam` / CSI driver, `driver/i2s_std.h` / `i2s_tdm.h`, `esp_driver_ppa` |
| Espressif camera examples: `peripherals/camera/mipi_isp_dsi`, `esp32_p4_function_ev_board` camera demo | Reference CSI+ISP+JPEG init sequences for OV5647 |
| `github.com/bytequest-gemini/ESP32-P4-IMX219-PoC` (or successor) | Current state of IMX219 RAW10 capture + (software or hardware) demosaic on P4 — **read this first** before starting M9, it documents exactly which pieces were missing at PoC time |
| Semtech **SX1276/77/78/79 datasheet** (public PDF) | Register map for `RegOpMode`, `RegFrf*`, `RegFdev*`, `RegPaConfig`, `RegPacketConfig2` (continuous vs. packet `DataMode`), `RegDioMapping1/2` (DIO2 function in continuous mode) |
| u-blox **UBX protocol spec** (M8/M9 Interface Description, public PDF) | `UBX-NAV-PVT` (0x01 0x07) and `UBX-NAV-TIMEGPS` (0x01 0x20) payload layouts, `UBX-CFG-NAV5` (0x06 0x24) dynamic model field, UBX checksum algorithm (8-bit Fletcher over class+id+len+payload) |

**Agent rule:** Where this spec and the actual upstream source disagree (e.g., the exact byte
offset of a scrambler table, or whether `ublox.py` uses NAV-PVT vs NAV-TIMEGPS for week/TOW), the
**upstream source is authoritative**. Record the resolved value in `docs/protocol_reference.md`
and cite the file/line it came from.

---

## 4. High-Level Architecture

```
                ┌──────────────────────────────────────────────────────────┐
                │                     ESP32-P4 (FreeRTOS)                    │
                │                                                            │
 MIPI-CSI ──────┤  wenet_camera_hal (OV5647 | IMX219)                        │
 (Pi Cam V1/V2) │        │ JPEG buffer (PSRAM)                               │
                │        ▼                                                  │
                │  Camera Task ──► wenet_ssdv ──► ssdv_queue (FreeRTOS Q)    │
                │                                       │                    │
 UART ──────────┤  wenet_gps_ublox                      │                    │
 (u-blox GPS)   │     │ GPS fix                         │                    │
                │     ▼                                 │                    │
                │  GPS/Telemetry Task ─► telemetry_queue ┤                    │
                │                                       │                    │
                │              TX Scheduler Task ◄──────┘                    │
                │     (priority: telemetry_queue > ssdv_queue > idle)        │
                │              │                                             │
                │              ▼                                             │
                │   wenet_framing (CRC16 + scrambler + preamble/UW)          │
                │              │                                             │
                │              ▼                                             │
                │   wenet_ldpc (rate-0.8 RA encode)                          │
                │              │                                             │
                │              ▼                                             │
 SPI ───────────┤   wenet_radio_sx127x (config: freq, power, FSK direct mode)│
 I2S(DOUT) ─────┤   wenet_modulator_i2s (96 kbaud bitstream, DMA)            │
                └──────┬───────────────────────────────┬────────────────────┘
                        │ SPI (CS/SCK/MOSI/MISO/RESET)   │ I2S DOUT → DIO2
                        ▼                                ▼
                   RFM98W config/control            RFM98W FSK data input
```

### 4.1 Camera abstraction (`wenet_camera_hal`)

To support OV5647 now and IMX219 later (or on different boards), define a single C interface that
the rest of the pipeline depends on, with sensor-specific backends behind a Kconfig choice
(`CONFIG_WENET_CAMERA_OV5647` / `CONFIG_WENET_CAMERA_IMX219`):

```c
// components/wenet_camera_hal/include/wenet_camera_hal.h
typedef struct {
    uint16_t width;
    uint16_t height;
    uint8_t  jpeg_quality;   // 0-100, encoder-defined scale
} wenet_camera_config_t;

esp_err_t wenet_camera_init(const wenet_camera_config_t *cfg);

// Captures one frame and JPEG-encodes it into a PSRAM buffer.
// On success, *out_buf is allocated (MALLOC_CAP_SPIRAM) and *out_len is set.
// Caller owns out_buf and must free() it.
esp_err_t wenet_camera_capture_jpeg(uint8_t **out_buf, size_t *out_len);

esp_err_t wenet_camera_deinit(void);
```

Both backends must satisfy this interface. This means the SSDV/LDPC/framing/radio pipeline,
queues, and the TX scheduler are **entirely sensor-agnostic** and can be developed/tested without
either camera physically present (feed them a JPEG file loaded from flash/SPIFFS for bring-up).

### 4.2 FreeRTOS task/queue model

| Task | Priority | Responsibility |
|---|---|---|
| `radio_tx_task` | Highest, pinned to one core | Continuously feeds I2S DMA; never blocks on camera/GPS |
| `gps_task` | High | UART read + UBX parse @ ~1 Hz; pushes 0x01 packets to `telemetry_queue` |
| `telemetry_task` | Medium | Periodic 0x00 status/text packets, board health, into `telemetry_queue` |
| `camera_task` | Low | Capture → JPEG → SSDV → `ssdv_queue` |
| `tx_scheduler_task` | High | Pops `telemetry_queue` (priority) else `ssdv_queue` else builds idle (0x56) packet; runs framing+LDPC; hands bit-buffer to modulator |

`telemetry_queue` and `ssdv_queue` hold **256-byte raw payloads** (pre-CRC/pre-LDPC). The
`tx_scheduler_task` is the only place framing/LDPC/scrambling happens, keeping those components
single-threaded and simple to test.

---

## 5. Build & Test Infrastructure — Build This *First*

Because hardware-in-the-loop (HIL) testing is the bottleneck resource for an autonomous agent,
**Milestones M1–M3 must be fully host-testable**. Set up `tools/host_tests/` as a small native
CMake project (separate from the ESP-IDF `idf.py` build) that compiles
`wenet_framing`, `wenet_ldpc`, and `wenet_ssdv` source files directly (they must therefore avoid
ESP-IDF-specific headers — isolate any `esp_err_t`/FreeRTOS usage behind thin wrapper files that
are *not* compiled into the host test binary).

### 5.1 Golden vector generation procedure

The agent should generate golden vectors **once**, on the development machine, using the upstream
projects, and check the resulting binary files into `tools/host_tests/vectors/` (small files only
— a handful of representative payloads and 2–3 small JPEGs, not full-resolution images):

1. Clone `projecthorus/wenet` and `fsphil/ssdv`; build their native tools
   (`gcc -O2 tx/ldpc_enc.c -o ldpc_enc`, `make` in `ssdv/`).
2. **LDPC vectors:** for a small set of 258-byte info blocks (all-zeros, all-ones, an
   incrementing-byte pattern, and 2–3 pseudo-random patterns with a fixed seed — document the
   seed), run the reference encoder and save `(info_block, parity_65bytes)` pairs.
3. **Framing/scrambler vectors:** using the scrambler table extracted from
   `tx/radio_wrappers.py`, hand-construct (in a small Python script kept in
   `tools/host_tests/vectors/gen_framing_vectors.py`) the expected scrambled output for the same
   258-byte blocks above, and the full framed bitstream (preamble + UW + scrambled payload).
4. **SSDV vectors:** pick 2–3 small JPEGs (e.g. 320×240, baseline, 4:2:0) representative of what
   the camera pipeline will produce. Run `ssdv -e -n -q <Q> -c TESTCALL -i <id> in.jpg out.bin`
   for a couple of quality settings and save `(in.jpg, Q, callsign, image_id, out.bin)`.
5. **GPS/UBX vectors:** hand-construct `UBX-NAV-PVT` and `UBX-NAV-TIMEGPS` binary frames using the
   public message layouts (with correct UBX checksums computed via the documented Fletcher
   algorithm) for a few known position/time fixes, and the expected `UBX-CFG-NAV5` "set
   dynModel=6 (Airborne <1g)" frame with its checksum. Save as binary files +
   a JSON file with the expected decoded field values.

### 5.2 Test runner

`tools/host_tests/` should build a single test binary (Unity or a minimal hand-rolled
assert-based runner is fine) that, for each vector, calls the ported C function and `memcmp`s (or
field-compares) against the golden output. This binary must run in CI / on every agent iteration
with `cmake --build . && ctest` or equivalent, in seconds.

---

## 6. Milestone Plan

Each milestone lists: objective, key tasks, interface/deliverables, validation method, and whether
hardware-in-the-loop (HIL) is required. **Milestones M1–M3 and M7a (GPS parser) require no
hardware** and should be completed first and most thoroughly, since they are where an agent can
make fully autonomous, verifiable progress.

### M0 — Toolchain & Repo Bootstrap (no HIL)
- Set up ESP-IDF (≥ v5.4, P4 support) per Espressif's "Get Started" guide for `esp32p4` target.
- Create the repo skeleton from §2.
- Stub out every component with empty headers + `idf_component.yml`/`CMakeLists.txt` so
  `idf.py build` succeeds for an empty `app_main`.
- **DoD:** `idf.py build` succeeds targeting `esp32p4`; `cmake` host-test project builds (with
  zero tests yet).

### M1 — Protocol Core: CRC16 + Scrambler + Framing (no HIL)
- Implement `wenet_crc16_ccitt_false(buf, len)`.
- Extract the XOR scrambler PN sequence from `radio_wrappers.py` into a `const uint8_t[]` table
  in `wenet_framing`; implement `wenet_scramble(buf, len)` (XOR with repeating sequence,
  in-place or out-of-place — match upstream's exact starting offset/reset behaviour per packet).
- Implement `wenet_build_frame()`:
  ```c
  // payload must be exactly 256 bytes.
  // out_frame must have room for: 16 (preamble) + 4 (UW) + 256 + 2 (CRC) + 65 (LDPC parity) = 343 bytes
  size_t wenet_build_frame(const uint8_t payload[256], uint8_t out_frame[343]);
  ```
  (LDPC step itself stubbed/zeroed until M2; structure the function so M2 only needs to fill in
  the parity-computation call.)
- Implement idle-packet payload generator: 0x56 packet type + 255×0x56.
- **Validation:** host tests against framing vectors from §5.1 (preamble/UW/CRC/scramble correct;
  parity bytes can be zero-checked separately once M2 lands).
- **DoD:** all M1 host tests pass.

### M2 — LDPC Encoder Port (no HIL)
- Port `tx/ldpc_enc.c` and its matrix data into `wenet_ldpc`. Expect this to be **almost
  copy-paste**; only touch:
  - Replace any file-based test/demo `main()` with a clean `wenet_ldpc_encode(const uint8_t
    info[258], uint8_t parity[65])`.
  - Verify integer types are fixed-width (`stdint.h`) and there's no reliance on host endianness
    beyond what's already explicit in the algorithm (RA encoders are bit/array based, so this is
    usually a non-issue).
- Wire `wenet_build_frame()` to call `wenet_ldpc_encode()`.
- **Validation:** host tests against LDPC vectors from §5.1 — **must be byte-identical**. Also
  re-run the M1 framing tests now that parity is non-zero, comparing the *entire* 343-byte frame
  against an end-to-end golden vector (generate this by piping the M1 framing vector through the
  reference `ldpc_enc` and scrambler).
- **DoD:** LDPC + full-frame host tests pass byte-exact.

### M3 — SSDV Encoder Port (no HIL)
- Port `ssdv.c`/`ssdv.h` (encoder side + embedded RS(255,223)) into `wenet_ssdv`.
- Wrap as: `wenet_ssdv_encode_begin(callsign, image_id, quality)`,
  `wenet_ssdv_encode_jpeg(jpeg_buf, jpeg_len, packet_cb, ctx)` which invokes `packet_cb` once per
  256-byte SSDV packet produced (avoids needing to buffer the whole output if undesired, but for
  simplicity it's fine to also provide a variant that returns a full array of packets).
- **Validation:** host tests feeding the golden JPEGs from §5.1 through the ported encoder and
  comparing the resulting packet stream **byte-for-byte** against `ssdv -e` output for the same
  `(jpeg, quality, callsign, image_id)` tuple.
- **DoD:** SSDV host tests pass byte-exact for all golden JPEGs/qualities.

> **Checkpoint:** At this point the entire "protocol stack" (framing, LDPC, SSDV) is verified
> correct independent of any hardware. Everything after this point involves ESP32-P4 peripherals
> and, eventually, physical RF validation.

### M4 — Camera Pipeline: OV5647 (Pi Camera V1) — HIL required for final validation, but structure for incremental bring-up
- Implement `wenet_camera_hal` OV5647 backend:
  - Initialize CSI + (sensor-internal or SoC) ISP per Espressif's `mipi_isp_dsi` /
    `esp32_p4_function_ev_board` camera example, targeting the OV5647 driver in `esp_cam_sensor`.
  - Enumerate supported sensor output modes via the sensor driver's format-query API; choose a
    mode close to the desired Wenet resolution (e.g. 1920×1080 or smaller — pick based on the
    throughput budget in Appendix C).
  - If the chosen mode doesn't match exactly, use the PPA to crop/scale before JPEG encode.
  - Feed ISP/PPA output (YUV422 or RGB) into `esp_driver_jpeg` hardware encoder → buffer in
    PSRAM.
- **Incremental validation (recommended even before full HIL):**
  1. First get the example camera demo (Espressif's stock example) building and, when hardware
     is available, producing a viewable image (e.g. over DSI to an LCD, or saved to SD/flash and
     pulled off via `idf.py monitor`/USB MSC).
  2. Then integrate into `wenet_camera_hal` and write a small standalone test app that calls
     `wenet_camera_capture_jpeg()` and writes the result to flash/SD; pull the JPEG off-target and
     confirm it (a) opens in a standard image viewer, (b) is **baseline (non-progressive) JPEG
     with 4:2:0 or 4:2:2 subsampling** (verify with `file`/`identify` — SSDV requires this), and
     (c) is in the size range expected for the link budget (Appendix C).
  3. Feed that captured JPEG through the M3 SSDV host test as a sanity check that real camera
     output round-trips through SSDV without errors (not necessarily byte-identical to anything —
     just structurally valid packets).
- **DoD:** `wenet_camera_capture_jpeg()` reliably returns valid baseline-JPEG buffers from a
  physically connected Pi Camera V1 at the configured resolution/quality.
- **HIL:** Yes — requires the dev board + Pi Cam V1 connected.

### M5 — RFM98W SX127x Driver (config path) — partial HIL
- Implement `wenet_radio_sx127x`:
  - SPI master init (mode 0, conservative clock e.g. 1–4 MHz initially).
  - Register read/write primitives; a `wenet_radio_read_version()` that reads `RegVersion`
    (0x42) and checks for the expected SX127x silicon revision — **this is the first HIL smoke
    test** (no RF needed, just confirms SPI wiring/CS/level-shifting is correct).
  - Port the configuration sequence from `radio_wrappers.py`: mode transitions
    (Sleep→Standby→FSTx/Tx), `RegFrf*` frequency calculation
    (`Frf = freq_Hz / FSTEP`, `FSTEP = FXOSC / 2^19`, `FXOSC = 32 MHz`), `RegFdev*` deviation,
    `RegPaConfig`/`RegPaDac` power level (2–17 dBm range per `start_tx.sh`), and the
    `RegPacketConfig2`/`RegDioMapping*` bits that select **continuous mode with DIO2 as the raw
    FSK data input** (cross-reference the SX1276 datasheet's "Continuous Mode" section).
  - Expose `wenet_radio_configure(freq_hz, power_dbm)` and `wenet_radio_set_tx_continuous(bool
    enable)`.
- **Validation:**
  - Host-testable subset: pure math functions (`Hz_to_Frf_regs()`, `dBm_to_paconfig()`) can have
    unit tests with known input/output pairs computed by hand from the datasheet formulas.
  - HIL: `RegVersion` readback; then, with an SDR or spectrum analyzer nearby, confirm a carrier
    appears at the configured frequency when `wenet_radio_set_tx_continuous(true)` is called with
    DIO2 held at a fixed level (should produce an unmodulated-ish tone at `Frf ± Fdev` depending
    on level).
- **DoD:** SPI register access verified; carrier presence/frequency verified with test equipment.
- **HIL:** Required for full validation; SPI-only portion can be smoke-tested with just the
  RFM98W wired up (no GPS/camera needed).

### M6 — I2S Direct-FSK Modulator + End-to-End RF — HIL required
- Implement `wenet_modulator_i2s`:
  - Configure an I2S peripheral as master, generating a serial bit clock at **96 kHz**, with the
    data output pin wired to RFM98W **DIO2**.
  - Build a continuous DMA pipeline: a ring buffer of bit-packed frames (each 343 bytes / 2744
    bits from `wenet_build_frame()`), MSB-first, with **idle frames auto-inserted** whenever the
    TX scheduler has nothing queued, so DIO2 is *never* starved.
  - Expose: `wenet_modulator_init()`, `wenet_modulator_submit_frame(const uint8_t frame[343])`
    (blocks/queues if buffer full), `wenet_modulator_start()`/`stop()`.
  - Compute and document the timing budget (Appendix C): one 343-byte frame = 2744 bits ≈
    28.6 ms at 96 kbaud. Size the DMA ring buffer to hold at least 4–8 frames to absorb scheduling
    jitter from `tx_scheduler_task`.
- **Validation (HIL, staged):**
  1. **Loopback/logic-analyzer check:** capture the I2S data pin with a logic analyzer; confirm a
     steady 96 kHz bit rate, MSB-first byte order, and correct bit pattern for a known test frame
     (compare against the host-computed `wenet_build_frame()` output for the same payload).
  2. **RF decode with stock Wenet RX tools:** with RFM98W configured per M5 and modulator running,
     transmit a repeating known text-message frame (0x00 packet). On a separate machine running
     the **unmodified** `wenet` RX chain (`fsk_demod` against an RTL-SDR or similar receiver, then
     `drs232_ldpc`), confirm the text message is decoded correctly and the CRC/LDPC pass.
  3. **SSDV end-to-end:** feed a captured camera JPEG (M4) through SSDV (M3) → framing/LDPC (M1/M2)
     → modulator, and confirm the RX chain reconstructs a valid JPEG via `ssdv -d`/WebWenet.
- **DoD:** Step 3 above succeeds — **this is the single most important integration checkpoint in
  the whole project.**
- **HIL:** Required (RFM98W + SDR/receiver + a machine running stock `wenet` RX tools).

### M7 — GPS (u-blox, UART)

#### M7a — UBX/NMEA Parser + Airborne<1g Config (no HIL)
- Implement `wenet_gps_ublox` as a UART-agnostic parser core + a thin UART glue layer:
  - UBX frame sync/checksum validation (Fletcher 8-bit over class+id+len+payload; sync bytes
    `0xB5 0x62`).
  - Parse `UBX-NAV-PVT` (0x01/0x07, 92-byte payload): extract `lon`, `lat`, `height`, `gSpeed`,
    velocity-down (for ascent/descent rate), `fixType`, `numSV`, `flags`.
  - Parse `UBX-NAV-TIMEGPS` (0x01/0x20): extract `week`, `iTOW`, `leapS` — **confirm against
    `tx/ublox.py` whether the original implementation sources week/TOW/leapS from this message or
    derives them from NAV-PVT's UTC fields; replicate whichever upstream actually does.**
  - Build the **`UBX-CFG-NAV5`** (0x06/0x24) message that sets `dynModel = 6` (Airborne <1g),
    with correct checksum, and a function to send it + wait for `UBX-ACK-ACK`.
- **Validation:** host tests using the §5.1 GPS vectors — feed synthetic UBX frames (including at
  least one with an intentionally corrupted checksum, which must be rejected) and confirm parsed
  field values match expected JSON; confirm the generated `CFG-NAV5` frame bytes match a
  hand-computed golden frame.
- **DoD:** All GPS parser host tests pass, including checksum-rejection test.

#### M7b — UART Integration + Telemetry Packet Build (HIL for live fix)
- Wire the parser core to an ESP-IDF UART driver (configurable port/baud, default 115200 per
  `start_tx.sh`'s `GPSBAUD`).
- On init, send the Airborne<1g `CFG-NAV5` and confirm ACK (log a warning but continue if no GPS
  is attached — must not block the rest of the system).
- Build the **0x01 GPS Telemetry packet** per Appendix B's field table, at ≥1 Hz, pushing to
  `telemetry_queue`.
- **Validation:** HIL with a real u-blox module — confirm dynamic model is set (read back
  `CFG-NAV5`), confirm NAV-PVT/TIMEGPS messages arrive, confirm the assembled 0x01 packet's
  fields are sane (compare lat/lon against known receiver location).
- **DoD:** Live GPS fix produces correctly-formatted 0x01 packets in `telemetry_queue`.

### M8 — System Integration (HIL for full soak test)
- Implement `wenet_config` (NVS): callsign (≤6 chars), TX frequency, TX power, image
  resolution/quality, inter-image delay, GPS UART port/baud — mirroring the variables at the top
  of `start_tx.sh`.
- Implement `wenet_telemetry`'s 0x00 text-message builder (`wenet_send_text(const char *msg)`,
  ≤252 chars, auto-incrementing message ID per Appendix B).
- Implement `tx_scheduler_task` priority logic (telemetry > ssdv > idle), idle-packet (0x56)
  fallback.
- Wire all tasks together in `app_main`; add a task-watchdog around `radio_tx_task` /
  `tx_scheduler_task`.
- **Validation:**
  - Host-testable: scheduler priority logic can be tested with a mock/fake queue implementation
    on the host (inject items, assert pop order).
  - HIL: multi-hour soak test — continuous image capture/encode/transmit + GPS telemetry
    interleaving, verified via the M6 RX chain, watching for DMA underruns, heap fragmentation
    (log `esp_get_free_heap_size()`/PSRAM stats periodically), and watchdog resets.
- **DoD:** ≥2 hour soak test with no underrun glitches, no resets, telemetry interleaved
  correctly with image data as observed on the RX side.

### M9 — Pi Camera V2 (IMX219) — Feasibility Gate + Best-Effort Implementation
This milestone is **explicitly allowed to conclude "not currently feasible"** — that is a valid
and useful outcome. Do not silently skip it; produce a decision and document it.

**Step 1 — Feasibility check (no HIL needed for this step):**
- Check the `esp_cam_sensor` component (at the ESP-IDF version pinned in M0) for an IMX219
  driver, and check `esp_driver_cam`/ISP documentation for **RAW10 Bayer → RGB/YUV demosaic**
  support on ESP32-P4 (this is the gap identified by the `ESP32-P4-IMX219-PoC` project at the time
  it was written — the agent must check whether this has since been resolved in the pinned IDF
  version).
- Decision matrix:
  - **If hardware ISP demosaic for RAW10 is available and documented:** proceed to Step 2a
    (hardware path).
  - **If not, but a software demosaic + software JPEG encode path is feasible** (e.g. via
    `esp_new_jpeg` software encoder operating on a demosaiced RGB buffer produced by a simple
    bilinear-demosaic routine): proceed to Step 2b (software path), and document the expected
    cost — lower max frame size/rate, higher CPU load, and the need for basic software AWB/gamma
    to avoid the "dark/green-tinted" result noted by the PoC project.
  - **If neither is practical:** stop here, write up findings in `docs/camera_notes.md` under an
    "IMX219: Not Supported (as of IDF <version>)" heading, including what specifically is
    missing, and exit M9 as **complete with a documented negative result**.

**Step 2a/2b — Implementation (HIL required):**
- Implement the `imx219` backend of `wenet_camera_hal` satisfying the **same interface** as
  OV5647 (§4.1) — this is why the abstraction exists. No changes to SSDV/LDPC/framing/radio
  components should be needed.
- Apply the same validation as M4 (baseline JPEG, correct subsampling, size-range check).
- **DoD:** Either `wenet_camera_capture_jpeg()` works end-to-end with IMX219 and produces images
  of acceptable quality for SSDV transmission, or M9 concludes with the documented negative
  result from Step 1.

### M10 — Field Validation & Documentation (HIL)
- Side-by-side comparison with a Pi-based Wenet TX at the same frequency/power: image
  arrival rate, decode success rate, range (if feasible).
- Power consumption measurement of the full board (camera + radio TX active) for battery sizing.
- Finalize all component `README.md`s, `docs/protocol_reference.md`, `ASSUMPTIONS.md`.

---

## 7. Camera Subsystem Notes

### 7.1 OV5647 (Pi Camera V1) — primary path
- 2-lane MIPI-CSI, sensor has **internal ISP** and can output YUV422/RGB directly — this is the
  same configuration used in multiple existing ESP32-P4 community examples, so driver support is
  comparatively mature.
- Native modes typically include something close to 2592×1944 (full), and binned modes such as
  1920×1080 / 1296×972 / 640×480 — the agent should query actual supported modes from the driver
  rather than assume, since exact mode lists vary by driver version.
- Feeds directly into the ESP32-P4 ISP (which may be configured in bypass/pass-through if the
  sensor already produces YUV/RGB) and then the hardware JPEG encoder.

### 7.2 IMX219 (Pi Camera V2) — stretch path
- 2-lane MIPI-CSI, **RAW10 Bayer only** — no internal ISP. The ESP32-P4 must perform demosaic,
  AWB, AE, and gamma correction (normally the ESP32-P4 ISP block's job) before JPEG encoding.
- As of community proof-of-concept work, full hardware-ISP RAW10 demosaic support on P4 was not
  yet generally available, with a documented fallback of RAW10 capture via the `esp_video`/V4L2
  path plus **software** demosaic — at reduced frame rate and without AE/AWB/gamma (resulting in
  dark/green-tinted images without further processing).
- Even a "best-effort" IMX219 path should therefore budget for: (1) checking current driver
  status against the pinned IDF version (could well have improved since), (2) if software
  demosaic is needed, adding a minimal AWB (simple gray-world) and gamma LUT stage so SSDV output
  isn't unusable, and (3) accepting a lower capture cadence than OV5647.

---

## 8. RFM98W / SX127x Driver Notes

- **Frequency register:** `Frf[23:0] = freq_Hz / FSTEP`, where `FSTEP = FXOSC / 2^19` and
  `FXOSC = 32 MHz` ⇒ `FSTEP ≈ 61.035 Hz`. For 443.5 MHz this gives `Frf ≈ 7,267,533` → split
  across `RegFrfMsb/Mid/Lsb`.
- **Deviation register:** `Fdev[13:0] = deviation_Hz / FSTEP`. The deviation value used by the
  original Wenet TX (to achieve the documented ~200 kHz occupied bandwidth at 96 kbaud) should be
  taken directly from `radio_wrappers.py` rather than re-derived, then sanity-checked against the
  occupied-bandwidth figure in the wenet README.
- **Continuous/direct mode:** `RegPacketConfig2`'s `DataMode` bit selects continuous vs.
  packet-engine mode; in continuous mode (with bit synchronizer disabled), **DIO2 functions as
  the raw FSK data input**, and the chip's frequency output tracks DIO2's logic level between
  `Frf - Fdev` and `Frf + Fdev`. Confirm exact bit positions against the SX1276 datasheet and
  cross-check against the register values written in `radio_wrappers.py` — if they disagree,
  trust `radio_wrappers.py` (it's the proven-working configuration) but flag the discrepancy in
  `ASSUMPTIONS.md` for human review.
- **Power:** `RegPaConfig`/`RegPaDac` cover the 2–17 dBm range exposed as `TXPOWER` in
  `start_tx.sh`; replicate the same mapping.
- **Mode sequencing:** SX127x requires going through Standby/FS modes when changing frequency;
  replicate the exact sequence and any settling delays (`vTaskDelay`) from `radio_wrappers.py` —
  these delays are usually empirically tuned and not arbitrary.

---

## 9. I2S Modulator Notes

- Target bit rate: **96,000 bits/sec**, MSB-first, one frame = 343 bytes = 2744 bits ≈ **28.6 ms**.
- Use the ESP32-P4 I2S peripheral's master-clock generation (APLL or equivalent fractional clock
  source) to hit 96 kHz as precisely as possible — clock *accuracy* and especially *continuity*
  (no DMA underrun gaps) matter more than achieving exactly 96.000 kHz, since the RX-side
  `fsk_demod` performs its own timing recovery, but a glitch/gap in the bitstream will corrupt
  whatever frame is in flight.
- DMA ring buffer sizing: ≥4–8 frames (≈115–230 ms) of headroom so that `tx_scheduler_task`
  jitter (camera/SSDV/LDPC work on other cores) never starves the modulator. Always have an idle
  frame ready to enqueue if the scheduler hasn't produced a new frame in time.
- Bit-order and byte-packing must match exactly what `wenet_build_frame()` produces — a logic
  analyzer comparison (M6, step 1) against a host-computed reference is the cheapest way to catch
  off-by-one bit-ordering bugs before burning RF test time.

---

## 10. Appendices

### Appendix A — Frame/Packet Layout Reference (from wenet wiki)

```
Preamble:  16 × 0x55
UW:        0xAB 0xCD 0xEF 0x01
Payload:   256 bytes (type byte + 255 data bytes, 0x55-padded if shorter)
CRC16:     CRC-CCITT (false), little-endian, over Payload
LDPC:      516 parity bits, zero-padded to 65 bytes (rate-0.8 RA code over Payload+CRC = 258 bytes)
Scramble:  Payload+CRC+Parity (323 bytes) XORed with repeating PN sequence (v2/I2S mode only)
```
Total on-air frame = 16 + 4 + 256 + 2 + 65 = **343 bytes = 2744 bits** ≈ 28.6 ms @ 96 kbaud.

Payload type bytes:
- `0x00` Text Message, `0x01` GPS Telemetry, `0x02` Orientation Telemetry,
  `0x03` Secondary Payload Telemetry, `0x04` Imagery Telemetry, `0x55` SSDV packet,
  `0x56` Idle packet (255 × 0x56).

### Appendix B — Telemetry Packet Field Tables

**0x00 Text Message** (big-endian multi-byte fields):
| Offset | Len | Type | Field |
|---|---|---|---|
| 0 | 1 | u8 | Packet Type (0x00) |
| 1 | 1 | u8 | Message length |
| 2 | 2 | u16 | Message ID (incrementing) |
| 4 | 252 | str | Message, 0x55-padded |

**0x01 GPS Telemetry** (big-endian multi-byte fields):
| Field | Type |
|---|---|
| Packet Type (0x01) | u8 |
| GPS Week Number | u16 |
| GPS Time-of-Week (ms) | u32 |
| GPS Leap Seconds | u8 |
| Latitude (deg) | f32 |
| Longitude (deg) | f32 |
| Altitude (m) | f32 |
| Ground Speed (kph) | f32 |
| Ascent/Descent rate (m/s) | f32 |
| Satellites used | u8 |
| Fix type (0=none,3=3D) | u8 |
| Dynamic model (6=Airborne 1G) | u8 |
| Radio Temperature (°C) | f32 |
| "Board" CPU Temperature (°C) — *ESP32-P4 internal temp sensor* | f32 |
| "Board" Clock Frequency (MHz) | u16 |
| Load average 1/5/15 min — *repurpose as ESP32 task/heap stats, document mapping* | f32 ×3 |
| Disk usage % — *repurpose as flash/SPIFFS usage %, or 0 if N/A* | f32 |
| Lens Position (-999 if N/A) | f32 |
| Sensor Temperature (-999 if N/A) | f32 |
| Autofocus FoM (-999 if N/A) | f32 |

> Field order/sizes above must be double-checked against the live wiki table during M7b — this
> appendix is a convenience summary, not the authoritative source.

### Appendix C — Throughput / Sizing Budget
- Effective payload throughput ≈ 9 kB/s (per wenet README, "theoretical user data rate ... after
  framing and FEC").
- A target on-air image time of, say, 5–10 s implies an SSDV input JPEG of roughly **45–90 kB**.
  Pick OV5647 resolution + JPEG quality to land in this range (e.g., a 1280×720 or 1024×768
  baseline JPEG at moderate quality is a reasonable starting point — measure actual sizes during
  M4 and tune).
- `ssdv_queue` sizing: at 256 B/packet, a 90 kB JPEG → ~450 SSDV packets ≈ 115 kB — allocate from
  PSRAM.

---

## 11. Agent Operating Procedures

1. **Work in milestone order**, but M1–M3 (and M7a) can be parallelized internally since they're
   independent and hardware-free — prioritize getting these to "all host tests green" before
   spending effort on anything HIL-dependent.
2. **Never invent protocol constants.** If a value (scrambler sequence, register bit positions,
   default deviation, etc.) isn't directly extractable from the cited upstream source, mark it in
   `ASSUMPTIONS.md` with: the value chosen, why, and what upstream artifact would resolve it —
   then continue (don't block), but flag it prominently in the milestone's summary.
3. **One component, one README.** Each `components/wenet_*/README.md` states: source ported
   from (repo+path+commit), what changed, current test status (host/HIL), and known limitations.
4. **HIL checkpoints are explicit stop points.** When a milestone's DoD requires hardware the
   agent doesn't have access to, the agent should: (a) complete everything possible without
   hardware (code, host tests, documentation of the exact manual test procedure), (b) write the
   manual test procedure into `docs/rf_validation.md` (for M5/M6) or equivalent, and (c) clearly
   report the milestone as "code-complete, awaiting HIL validation" rather than marking it done.
5. **Do not silently reduce scope on protocol compatibility.** If a shortcut would break
   bit-compatibility with the existing Wenet RX chain (e.g., a different scrambler, different
   frame length), it must be called out as a deviation requiring human sign-off — compatibility
   with the existing ecosystem is a core project goal.
6. **M9 (IMX219) may end negatively** — see §6, M9. A documented "not yet feasible" is a complete
   deliverable for that milestone.
7. Keep `AGENT_LOG.md` as a running, dated log of what was done, what was tested, and what's next
   — this is the primary handoff artifact between agent sessions and to human reviewers.

---

## 12. Risk Register / Open Questions for Human Review

| # | Item | Why it matters | Resolution path |
|---|---|---|---|
| 1 | MIPI-CSI connector pitch/pinout mismatch between chosen ESP32-P4 board and Pi Cam V1/V2 ribbon cables | Blocks M4 entirely if cabling doesn't fit | Human verifies board schematic vs. camera FPC before procurement |
| 2 | I2S clock jitter/continuity at 96 kHz under real-world DMA/cache conditions | Core RF quality issue (M6) | Logic-analyzer verification in M6 step 1; if marginal, consider RMT peripheral as an alternative bit-clock source |
| 3 | Exact SX127x register bits for "DIO2 = data in continuous mode" | RF won't modulate correctly if wrong | Cross-check datasheet vs. `radio_wrappers.py`; HIL carrier test in M5 |
| 4 | IMX219 ISP/demosaic driver maturity | Determines whether M9 succeeds | Feasibility check at start of M9, against the IDF version actually pinned |
| 5 | `ublox.py`'s exact source for GPS week/TOW/leap-seconds (NAV-PVT vs NAV-TIMEGPS) | Affects 0x01 packet correctness | Read upstream source directly in M7a |
| 6 | Repurposing of Pi-specific telemetry fields (CPU temp/load/disk/clock) in the 0x01 packet | Affects downstream dashboards expecting specific semantics | Document chosen mapping in Appendix B / `docs/protocol_reference.md`; human sign-off on whether to repurpose vs. zero-fill |
| 7 | Regulatory: frequency/power must remain within the operator's amateur radio license terms | Legal | No code change needed, but default config values should not be silently altered from licensed values without human confirmation |

---

*End of specification.*
