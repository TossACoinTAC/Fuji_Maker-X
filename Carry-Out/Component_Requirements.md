# Fuji Component Requirements

## 1. Scope and build target

This document turns the current Fuji design into a practical prototype bill of materials. It is written for the existing `DNESP32S3` / ESP32-S3 N16R8 environment and the XiaoZhi voice base described in `Design/FullDesign_DesignThinking.md`.

The first physical prototype is a small, rounded companion that can sit on a shoulder strap, shirt, bag, lanyard, or desk dock. It must support:

- local face, light, touch, haptic, mute, battery, and connection states;
- voice interaction through the existing XiaoZhi audio path or a documented audio adapter;
- BLE control and status to a phone;
- phone-side routing of Fuji responses to Bluetooth earphones when selected;
- phone-side music pause/duck/restore and supported playback commands;
- a safe magnetic mount with a mechanical tether option;
- an honest offline and disconnected mode.

The phone is the Bluetooth and media hub. Fuji does **not** directly stream A2DP audio to earphones in the first prototype.

## 2. Existing board reuse rule

The current XiaoZhi board is treated as an existing, working platform. Based on the provided board feature list and the 78/xiaozhi-esp32 feature set, reuse these capabilities instead of purchasing duplicates or reimplementing them:

- ESP32-S3, Flash, PSRAM, Wi-Fi, and the ML307 Cat.1 4G path;
- ESP-SR offline wake word, streaming ASR/LLM/TTS, OPUS audio, and speaker recognition;
- the onboard microphone, speaker, amplifier, OLED/LCD, battery display, charger, and power-management path;
- the ESP32-S3 BLE controller for the Fuji-to-phone control link;
- existing LED/GPIO/device-side MCP control and custom asset support;
- the iPhone + AirPods pair as the first phone/earphone validation setup.

Before ordering any fallback part, inspect the actual board and XiaoZhi carrier schematic. Only buy a duplicate audio, display, battery, or power component if the existing implementation cannot be accessed, is damaged, or cannot fit the Fuji enclosure.

## 3. Requirement levels

| Level | Meaning                                                               |
| ----- | --------------------------------------------------------------------- |
| P0    | Required to validate the first lovable food-choice and quiet-use loop |
| P1    | Required for the first useful companion pilot after P0 is stable      |
| P2    | Optional experimentation; do not block the first field trial          |

## 4. Recommended prototype bill of materials

Part numbers below are concrete starting points, not purchase commitments. Confirm package, voltage, availability, and the actual XiaoZhi carrier schematic before ordering production quantities. Simple enclosure, mount, spacer, cap, grille, and cable-retention geometry is fabricated with the available FDM printer and is not a purchase item.

### 4.1 Compute, wireless, and programming

| Item              | Recommended component                                 |      Qty | Level | Requirement / notes                                                                                                                     |
| ----------------- | ----------------------------------------------------- | -------: | ----- | --------------------------------------------------------------------------------------------------------------------------------------- |
| Main board        | Existing DNESP32S3 / ESP32-S3 N16R8 development board | existing | reuse | Keep the current PlatformIO target; do not buy another board for P0. Confirm the actual carrier board pinout before wiring peripherals. |
| Flash / PSRAM     | Existing 16 MB Flash + 8 MB PSRAM configuration       | existing | reuse | Required for the current build assumptions.                                                                                             |
| Phone link        | ESP32-S3 built-in Bluetooth LE controller             | existing | reuse | Add Fuji GATT services; no external Bluetooth module is needed.                                                                         |
| Network           | Existing Wi-Fi and ML307 Cat.1 4G path                | existing | reuse | Use the iPhone hotspot for the first phone/earphone validation; 4G is not a new Fuji purchase.                                          |
| Programming/debug | USB-C cable plus the board's native USB/UART path     | existing | reuse | Keep serial logging at 115200                                                                                                           |

**Hardware gate:** the repository contains a placeholder `src/main.cpp` and a prebuilt XiaoZhi binary, but it does not contain the board schematic or audio pin map. Before connecting any peripheral, record the exact board revision, exposed GPIOs, I2C pins, SPI pins, I2S pins, 5 V/VBUS pin, 3V3 pin, and reserved boot/USB pins in `include/board_pins.h`.

### 4.2 Audio input and output

Use the existing XiaoZhi microphone, amplifier, speaker, OPUS path, and voice stack. The rows below record the reused path; audio fallback parts are not P0 purchases.

| Item               | Recommended component                       |      Qty | Level    | Requirement / notes                                                                                      |
| ------------------ | ------------------------------------------- | -------: | -------- | -------------------------------------------------------------------------------------------------------- |
| Voice microphone   | Existing XiaoZhi microphone and audio input | existing | reuse    | Do not buy INMP441 unless the board audio path is unavailable or cannot be routed through the enclosure. |
| Audio amplifier    | Existing XiaoZhi amplifier/audio path       | existing | reuse    | Do not add MAX98357A without confirming the existing I2S path is unusable.                               |
| Local speaker      | Existing XiaoZhi speaker                    | existing | reuse    | Repackage or relocate only after acoustic and mechanical checks.                                         |
| Earphone route     | iPhone Bluetooth audio route to AirPods     | existing | reuse    | Fuji sends control/status to the iPhone; the iPhone manages private audio and music focus.               |
| Audio test fixture | Existing wired audio fixture if available   |   verify | optional | Only buy a separate fixture if current-board debugging cannot isolate audio faults.                      |

Do not add a camera, GPS, or Bluetooth Classic audio module to P0. Location, map handoff, music playback, and private audio are phone responsibilities.

### 4.3 Face, status, and user input

| Item          | Recommended component                                                                               |               Qty | Level       | Requirement / notes                                                                                                             |
| ------------- | --------------------------------------------------------------------------------------------------- | ----------------: | ----------- | ------------------------------------------------------------------------------------------------------------------------------- |
| Face display  | Existing XiaoZhi OLED/LCD and emoji display path                                                    |          existing | reuse       | Do not buy SH1107 unless the existing display cannot be detached or addressed from Fuji firmware.                               |
| Status light  | Existing board LED/GPIO/device-side MCP                                                             |          existing | reuse       | Do not add an RGB LED for P0 unless the existing indicator cannot express route/mute state.                                     |
| Touch input   | TTP223B capacitive touch controller with a copper or stainless touch pad                            |       1 if absent | P0 new      | First Fuji-specific input to add if the current board has no usable touch channel.                                              |
| Physical mute | Latching SPST slide switch, e.g. SS-12D00G class, plus TPS22919/AO3401A if a power gate is required |       1 if absent | P0 new      | Visible and tactile. Wire to the existing MIC_EN input when available; otherwise gate microphone power after electrical review. |
| User button   | Existing board button, or 6x6 mm KMR2-class tactile switch                                          | existing / verify | conditional | Add only if the board has no usable pairing/recovery input.                                                                     |
| Haptic driver | Existing GPIO/device-side MCP plus AO3400A and SS14                                                 |             1 set | P0 new      | Drive a simple ERM motor. Defer DRV2605L until richer waveforms are justified.                                                  |
| Haptic motor  | 10 mm, 3 V coin ERM vibration motor                                                                 |                 1 | P0 new      | Isolate it mechanically from the microphone and display.                                                                        |

If the selected ESP32-S3 carrier exposes a reliable capacitive-touch channel, compare it against TTP223B during Prototype 1. Keep TTP223B as the fallback because it gives a stable digital event and avoids depending on undocumented board touch routing.

### 4.4 Motion and physical-state sensing

| Item                    | Recommended component                                                                  |      Qty | Level              | Requirement / notes                                                                                                                      |
| ----------------------- | -------------------------------------------------------------------------------------- | -------: | ------------------ | ---------------------------------------------------------------------------------------------------------------------------------------- |
| IMU                     | Bosch BMI270, I2C or SPI, 1.8/3.3 V breakout with regulator/level shifting as required |        1 | P1, early P0 spike | Detect tilt for idle expressions, orientation changes, and controlled movement tests. Do not infer an external action from motion alone. |
| Removal / mount sensing | Hall-effect switch, e.g. DRV5032 class                                                 |        1 | P1                 | Optional. Only add after the magnet geometry is fixed; the magnet field must be tested so the sensor does not falsely report removal.    |
| Battery voltage         | Existing XiaoZhi fuel-gauge/power-management path                                      | existing | reuse              | Only add MAX17048 if the current board cannot expose usable battery state to Fuji firmware.                                              |
| Temperature             | ESP32-S3 internal temperature or a small NTC near the battery                          |        1 | P1                 | Use an NTC for battery/charging safety if the charger board exposes the input.                                                           |

**Correction before ordering:** the Hall-effect row intentionally remains an optional sensor. Select a specific part only after the magnet/backplate pair is chosen and the expected field at the sensor location is measured. A common first candidate is an omnipolar digital Hall switch such as `DRV5032`, but the exact suffix and supply range must match the selected rail.

### 4.5 Power, charging, and protection

| Item                          | Recommended component                                                                     |               Qty | Level       | Requirement / notes                                                                                                                  |
| ----------------------------- | ----------------------------------------------------------------------------------------- | ----------------: | ----------- | ------------------------------------------------------------------------------------------------------------------------------------ |
| Battery                       | Existing protected battery, or replacement 1S LiPo pouch, 3.7 V nominal, 500-800 mAh      | existing / verify | conditional | Check capacity, connector, protection, physical size, and charge current before buying a replacement.                                |
| Charger / power path          | Existing XiaoZhi charger and power-management circuit                                     |          existing | reuse       | Do not buy BQ24074 unless the current board cannot safely charge the selected battery.                                               |
| 5 V boost for dev board       | TPS61023-based 5 V boost module, only if the board input path requires it                 |     1 if required | conditional | Do not connect a LiPo directly to a 5 V/VIN pin. Reuse the existing board power path first.                                          |
| 3.3 V rail for custom carrier | 600 mA or higher low-noise 3.3 V regulator, e.g. TPS62162-class buck or buck-boost design |                 1 | P1          | A custom carrier should power the ESP32-S3 and peripherals from a controlled 3.3 V rail rather than chaining unnecessary regulators. |
| Battery disconnect            | Existing board switch/load path, or TPS22919-class replacement                            | existing / verify | conditional | Add only if the current board cannot be safely stored or transported.                                                                |
| Decoupling                    | 100 nF and 10 uF parts for new touch/mute/haptic wiring                                   |         as needed | P0 new      | Place close to newly added loads; do not redesign the existing board rails unnecessarily.                                            |
| Fuse / protection             | Existing charger/battery protection                                                       | existing / verify | reuse       | Add a resettable fuse only if the present protection is absent or inadequate for the motor load.                                     |

**Power rule:** reuse the existing documented XiaoZhi battery, charger, and board input path first. Only select a replacement protected battery or converter after the current capacity, connector, charge current, thermal behavior, and enclosure fit are measured.

### 4.6 Magnet, mount, enclosure, and tether

| Item                | Recommended component                                                                                        |   Qty | Level           | Requirement / notes                                                                                                                                                  |
| ------------------- | ------------------------------------------------------------------------------------------------------------ | ----: | --------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Body magnets        | Two N35 neodymium disc magnets, target 10 mm diameter x 3 mm thick                                           |     2 | P0              | Keep them fully captured in the body with a mechanical pocket and adhesive. Start at N35 rather than the strongest grade to reduce unnecessary field and snag force. |
| Backplate           | Mild-steel or low-carbon-steel disc/plate, target 15 mm diameter x 1-1.5 mm thick                            |   1-2 | P0              | Rounded edges, laminated or coated against rust. Test cotton, denim, knit, synthetic fabric, and bag straps.                                                         |
| Optional clip mount | 3D-printed PETG clip body with optional TPU contact pad and captive M2 screw                                 | print | P0 fabricate    | Model at least two jaw gaps after measuring target fabrics. Do not purchase a finished clip; use purchased M2 hardware for the load-bearing closure.                 |
| Tether              | 1-2 mm braided nylon cord or coated stainless lanyard, with a breakaway or captive loop                      |     1 | P0              | Attach to an internal anchor, not a glued shell feature. It prevents loss if the mount slips.                                                                        |
| Soft outer skin     | 3D-printed removable TPU 85A skin                                                                            | print | P0 fabricate    | Must not block microphone, touch pad, display, mute switch, or charging contacts. Do not purchase a silicone sleeve for P0.                                          |
| Inner carrier       | 3D-printed PETG or nylon frame                                                                               | print | P0 fabricate    | Holds board, battery, speaker, magnets, and strain relief. Avoid brittle PLA for the final wearable test.                                                            |
| Desk dock           | 3D-printed PETG dock with a USB-C opening                                                                    | print | P1 fabricate    | Do not purchase a finished dock. Add purchased pogo pins only if contact charging is later justified.                                                                |
| Small fitment set   | 3D-printed magnet pockets, backplate guard, touch cap, speaker grille/bezel, battery spacer, and cable clips | print | P0/P1 fabricate | Print local fit coupons first. Use TPU where a part touches the battery, cable, skin, or garment; no finished fitment parts are purchased.                           |

**Magnet acceptance tests:** no separation during 30 minutes of ordinary walking, stairs, bending, and a controlled clothing snag; no sharp edge or pinch hazard; one-handed removal; no damage to the garment; and a warning label for cards, magnetic storage, and implanted medical devices. Final force and geometry must be measured, not assumed from the nominal magnet grade.

**Fabrication rule:** the clip, skin, inner carrier, dock body, locating pockets, caps, grille, spacers, and cable retainers are CAD/printing tasks, not procurement lines. The non-printable functional elements remain purchased parts: magnets, steel backplate, tether, heat-set inserts, screws, wires, conductive touch electrode, and any pogo pins.

### 4.7 Interconnect and assembly materials

- 28-30 AWG silicone wire for low-current signals and 24-26 AWG wire for battery/speaker power;
- JST-PH 2.0 locking battery connector with a 3D-printed TPU strain-relief feature;
- 0.8-1.0 mm two-layer or four-layer custom PCB only after the dev-board pin map is proven;
- M2 brass heat-set inserts and nylon screws for the inner carrier;
- 3D-printed TPU grommets/spacers, or adhesive-backed foam only where printing cannot provide the required damping;
- conformal coating only on the electronics carrier after the charging and connector design is verified; never coat the microphone port, switches, display window, or battery pouch;
- test points for 3V3, battery, I2C, I2S clock/data, ground, mute, and reset.

## 5. Interfaces to reserve

The exact GPIO numbers are intentionally not fixed in this document because the current repository does not include the DNESP32S3/XiaoZhi carrier schematic. Reserve these logical interfaces and fill the pin map after hardware inspection:

| Interface           | Signals                                          | Connected parts                                                                  |
| ------------------- | ------------------------------------------------ | -------------------------------------------------------------------------------- |
| Existing I2C bus    | SDA, SCL, 3V3, GND                               | Existing XiaoZhi display/power devices; optional BMI270 only after address audit |
| Existing I2S input  | BCLK, LRCLK/WS, DIN                              | Existing XiaoZhi microphone/audio input                                          |
| Existing I2S output | BCLK, LRCLK/WS, DOUT                             | Existing XiaoZhi amplifier/speaker path                                          |
| SPI optional        | SCLK, MOSI, CS, DC, RST                          | Only if the face display changes to an ST7789/GC9A01 display                     |
| GPIO input          | MUTE_SENSE, TOUCH_EVENT, USER_BUTTON, HALL_EVENT | Mute switch, TTP223B, recovery button, optional DRV5032                          |
| GPIO output         | MIC_EN, HAPTIC_EN, optional STATUS_LED           | Mute gate, MOSFET-driven ERM motor, existing LED/GPIO if available               |
| PWM / driver        | HAPTIC_EN                                        | Simple ERM motor; DRV2605L only as a P1 upgrade                                  |
| BLE GATT            | Controller internal                              | Phone control, state notification, config, route status                          |

### Pin-map rules

1. Do not use ESP32-S3 strapping pins, native USB pins, flash/PSRAM pins, or XiaoZhi-reserved audio pins without checking the carrier schematic.
2. Put every assignment in one `board_pins.h` file; drivers must not contain raw GPIO literals.
3. Mark unavailable peripherals as `PIN_UNUSED` and make the firmware compile in a bench-only mode.
4. Add a continuity test and a power-off resistance check before inserting the LiPo.

## 6. Items explicitly out of the first build

- Camera module;
- GPS module;
- additional cellular modem beyond the existing ML307 path;
- direct Fuji-to-earphone Bluetooth audio module;
- high-force N52 magnet stack;
- large touchscreen;
- unprotected LiPo pouch;
- cloud-only feature that has no local error or quiet fallback.

## 7. Procurement and bring-up order

1. Verify the existing ESP32-S3/XiaoZhi board, USB cable, audio, display, battery, charger, and serial logging setup.
2. TTP223B if no usable touch input exists, hardware mute switch, user button, ERM motor, and simple MOSFET driver.
3. Buy two N35 magnets, a steel backplate, tether, and M2 hardware; model and print the clip alternative plus at least three mount variants.
4. Replacement protected LiPo or power parts only if the existing battery/power path fails inspection.
5. BMI270 and optional Hall switch after the core interaction works.
6. iPhone + AirPods route test; use the existing pair rather than buying another reference set.

No wearable field test begins until the battery protection, mute behavior, magnet retention, and tether are independently checked.
