# Fuji embedded workspace

Fuji firmware is built with ESP-IDF. PlatformIO and the Arduino placeholder
application are no longer part of the supported build.

The XiaoZhi source tree is vendored at `embedded/xiaozhi` as a pinned Git
subtree. Fuji-specific board definitions live inside that tree so the upstream
release tooling can build a uniquely named firmware image.

## Toolchain

Use the ESP-IDF version documented by the pinned XiaoZhi commit. The initial
baseline is ESP-IDF 6.0.2 on the `esp32s3` target.

```sh
. "$IDF_PATH/export.sh"
cd embedded/xiaozhi
idf.py set-target esp32s3
idf.py build
```

Do not flash `firmware/v2.0.3_atk-dnesp32s3.bin` to the Fuji breadboard. That
image targets a different carrier with an ES8388 codec, XL9555 I/O expander,
and 320x240 display.

## Hardware gates

1. Start with the ESP32-S3 board alone on the USB-C port marked `COM`.
2. Read the chip, flash, and PSRAM information and back up the original flash.
3. Flash the board-probe build before wiring the display.
4. Verify the display before wiring the microphone and amplifier.
5. Keep the LiPo disconnected until its voltage, polarity, protection, charger,
   and connector have been checked with a multimeter.

See `embedded/xiaozhi/main/boards/fuji-devkit-s3/README.md` after the subtree
and board definition are present for the exact signal wiring.
