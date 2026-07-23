# Fuji DevKit S3

This board profile targets the current breadboard prototype built around an
ESP32-S3-N16R8 module. It is intentionally independent from the upstream
`atk-dnesp32s3` profiles.

## Safety gates

1. Start with only the USB-C port marked `COM`. Disconnect every peripheral and
   keep the battery disconnected.
2. Build and flash `fuji-devkit-s3-probe`. Confirm 16 MiB Flash, 8 MiB PSRAM,
   reset and download behavior, then back up the original Flash.
3. Connect and test only the display.
4. Connect the microphone and amplifier only after the display test passes.
5. Do not connect the bare-wire LiPo to `5V`, `3V3`, or either USB power path.
   A multimeter and a verified protected charging/power path are required first.

## Initial wiring

| Module | Module pin | ESP32-S3 | Notes |
|---|---|---|---|
| ST7735S | VCC | 3V3 | Never use 5V unless the exact module is verified |
| ST7735S | GND | GND | Common ground |
| ST7735S | SCL | GPIO12 | SPI SCK, not I2C SCL |
| ST7735S | SDA | GPIO11 | SPI MOSI, not I2C SDA |
| ST7735S | RES | GPIO14 | Reset |
| ST7735S | DC | GPIO13 | Data/command |
| ST7735S | CS | GPIO10 | Chip select |
| ST7735S | BLK | GPIO9 | PWM backlight |
| INMP441 | VDD | 3V3 | |
| INMP441 | GND | GND | Common ground |
| INMP441 | SCK | GPIO17 | Shared I2S BCLK |
| INMP441 | WS | GPIO18 | Shared I2S word select |
| INMP441 | SD | GPIO16 | Microphone data to ESP32 |
| INMP441 | L/R | GND | Select left channel |
| MAX98357A | VIN | 5V | USB power only for first tests |
| MAX98357A | GND | GND | Common ground |
| MAX98357A | BCLK | GPIO17 | Shared I2S BCLK |
| MAX98357A | LRC | GPIO18 | Shared I2S word select |
| MAX98357A | DIN | GPIO15 | Audio data from ESP32 |
| MAX98357A | SD | GPIO8 | Software amplifier enable |
| MAX98357A | GAIN | GND | Initial low-gain setting |
| Button | one side | GPIO4 | Active low; internal pull-up |
| Button | other side | GND | |

GPIO constants belong in `config.h`; board and driver code must not duplicate
numeric GPIO values.

## Build variants

- `fuji-devkit-s3-probe`: logs board storage and reset information, then idles
  without initializing display, audio, button, or Wi-Fi.
- `fuji-devkit-s3-self-test`: runs the wired peripheral self-test before the
  normal application. Use only after the wiring gates above are complete.
- `fuji-devkit-s3`: normal Xiaozhi application.

The initial ST7735S offsets are `x=2`, `y=3`, which is common for 1.44-inch
128x128 panels. If the image is shifted, confirm the tab/controller variant and
adjust only the board constants.
