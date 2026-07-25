# Fuji Waveshare 1.46

Project-owned board support for the Waveshare `ESP32-S3-Touch-LCD-1.46`.
It intentionally does not modify the upstream
`waveshare/esp32-s3-touch-lcd-1.46` board identity.

## Verified hardware baseline

- ESP32-S3 revision 0.2
- 16 MiB external flash
- 8 MiB embedded Octal PSRAM
- 412x412 SPD2010 QSPI LCD and SPD2010 touch controller
- Factory display and touch demo worked before custom flashing
- I2C scan: TCA9554 `0x20`, PCF85063 RTC `0x51`, SPD2010 touch
  `0x53`, QMI8658 IMU `0x6B`
- Display test passed red/green/blue/white/black fields, orientation, round
  boundary and continuous touch at the center and four edges
- Factory Flash backup SHA-256:
  `2c68b9c9de9cd5d9ae5cf24850658be9908f6c41ecd588b9c1baafec1a93ca9b`

The battery must remain disconnected until its polarity, voltage, protection,
and connector have been checked separately.

## Build variants

- `fuji-waveshare-1p46-probe`: chip, flash, PSRAM and reset reason only
- `fuji-waveshare-1p46-display-test`: I2C scan, separate LCD/touch reset,
  solid colors, round boundary and touch coordinates
- `fuji-waveshare-1p46-mic-test`: BOOT-armed RX-only controlled WAV capture
  to the 1 MiB `mic_capture` partition
- `fuji-waveshare-1p46-speaker-test`: BOOT-armed TX-only low-volume tone and
  speech; keep the hardware volume control at minimum before pressing BOOT
- `fuji-waveshare-1p46`: normal Xiaozhi application

All variants require ESP-IDF 6.0.2. The canonical release command places each
variant in its own directory below `XIAOZHI_BUILD_ROOT`:

```sh
source ../activate_idf.zsh
python3 scripts/release.py fuji-waveshare-1p46 \
    --name fuji-waveshare-1p46-probe
```

The default external build root is
`/Volumes/Mac_DiskExtension/EmbeddedCache/Maker-X/xiaozhi`. On 2026-07-25,
no-change builds of probe, display, microphone, speaker and full firmware took
4.49, 4.41, 4.67, 4.46 and 4.44 seconds respectively.

## Pin map

| Function | GPIO / expander |
| --- | --- |
| I2C SDA / SCL | GPIO11 / GPIO10 |
| LCD QSPI clock / CS | GPIO40 / GPIO21 |
| LCD QSPI data 0..3 | GPIO46 / GPIO45 / GPIO42 / GPIO41 |
| LCD backlight | GPIO5 |
| LCD reset | TCA9554 EXIO2 |
| Touch reset / interrupt | TCA9554 EXIO1 / GPIO4 |
| Microphone WS / BCLK / DIN | GPIO2 / GPIO15 / GPIO39 |
| Speaker LRCK / BCLK / DOUT | GPIO38 / GPIO48 / GPIO47 |
| BOOT / power key / power hold | GPIO0 / GPIO6 / GPIO7 |

The display starts at 40 MHz QSPI mode 3. Increasing the clock is deferred
until display and touch stability are verified on the physical board.
On a Type-C cold boot, initialize the TCA9554 and pulse the dedicated touch
reset before scanning the full I2C bus. A missing pre-panel-reset touch
response is non-fatal; the touch layer verifies the controller by reading its
firmware version after the display reset.

## Microphone verification

The diagnostic and normal firmware use the same board wiring and channel
selection as the upstream Waveshare board: I2S1, GPIO15/2/39, 32-bit mono,
right slot. The diagnostic explicitly uses Philips I2S framing. Do not change
it to the IDF MSB preset: that preset produced a half-scale DC offset and a
noise-dominated WAV on this board.

The valid 2026-07-25 Philips-framed capture is 11 seconds of 16 kHz,
16-bit mono PCM. Its SHA-256 is
`5f59f4ff64c2990ec6729141fbaf278543e23d96c0ba655487e9dfce7f679bd0`.
Measured RMS was 34.6 quiet, 85.6 speech and 110.6 clap; I2S read failures were
zero. Subjective listening confirmed clear speech and finger snaps with no
notable background noise, so the microphone gate passed on 2026-07-25.

The microphone test pre-erases its Flash capture area before LVGL starts and
disables the touch interrupt before recording. These restrictions apply only
to the diagnostic firmware; the normal firmware keeps touch enabled and does
not persist raw audio.

## Speaker verification

The TX-only diagnostic is intentionally one-shot. It waits for BOOT before
creating I2S0, plays a 0.3-second 660 Hz prompt and the built-in welcome
speech, writes trailing silence, then disables and deletes the I2S channel.
Reset the board to re-arm it. The diagnostic uses a 32 KiB task because the
normal XiaoZhi Opus task uses 24 KiB and the diagnostic also performs OGG
demuxing; the demuxer buffer is allocated on the heap.

On 2026-07-26 the diagnostic decoded all 35 welcome-speech packets and
finished with 24,740 bytes of minimum remaining stack. With the hardware
volume raised slightly from minimum, listening confirmed intelligible speech,
no notable pop or distortion, and silence after completion. The speaker gate
passed. Keep the hardware volume low for subsequent bring-up.
