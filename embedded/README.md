# Fuji embedded workspace

Fuji firmware is built with ESP-IDF. PlatformIO and the Arduino placeholder
application are no longer part of the supported build.

The XiaoZhi source tree is vendored at `embedded/xiaozhi` as a pinned Git
subtree. Fuji-specific board definitions live inside that tree so the upstream
release tooling can build a uniquely named firmware image.

## Toolchain

Use the ESP-IDF version documented by the pinned XiaoZhi commit. The local
baseline is a clean ESP-IDF 6.0.2 checkout at `~/esp/esp-idf-v6.0.2`, with
tools in the standard `~/.espressif` directory, on the `esp32s3` target. The
generated `dependencies.lock` is committed so registry dependencies resolve to
the same versions and hashes on clean machines.

```zsh
cd /Users/apple/Documents/Skd_Learning/26summer/Maker-X
source embedded/activate_idf.zsh
cd embedded/xiaozhi
idf.py --version
python scripts/release.py fuji-devkit-s3 --name fuji-devkit-s3-probe
```

The version command must print exactly `ESP-IDF v6.0.2`, without a `dirty`
suffix. Use zsh consistently; a different shell may select a different system
Python installation.

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

For the first board-only session, follow
`embedded/FIRST_USB_BRINGUP_zh.md`. Stop after the read-only probe and verified
16 MiB backup; do not flash until those results have been reviewed.

After the USB-only probe passes, follow `embedded/OLED_BRINGUP_zh.md` for the
temporary soldered 0.91-inch OLED. `embedded/DISPLAY_BRINGUP_zh.md` remains the
deferred ST7735S guide and requires a soldered header before use.

After the OLED test is verified, follow `embedded/MICROPHONE_BRINGUP_zh.md` for
the isolated INMP441 receive test. Do not connect the amplifier during that
stage.
