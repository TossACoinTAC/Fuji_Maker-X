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
- QMI8658 reports WHO_AM_I `0x05`, revision `0x7C`, and changing acceleration
  samples with approximately 1 g magnitude
- PCF85063 responds at `0x51`; its low-voltage flag remains set while the
  battery and RTC backup source are intentionally disconnected
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
`/Volumes/Mac_DiskExtension/EmbeddedCache/Maker-X/xiaozhi`. On 2026-07-26,
final no-change builds of probe, display, microphone, speaker and full firmware
took 4.47, 4.28, 4.20, 4.01 and 4.14 seconds respectively. All five final
merged images and release ZIPs were regenerated successfully. Host static
coverage is 35 tests.

## Pin map

| Function | GPIO / expander |
| --- | --- |
| I2C SDA / SCL | GPIO11 / GPIO10 |
| LCD QSPI clock / CS | GPIO40 / GPIO21 |
| LCD QSPI data 0..3 | GPIO46 / GPIO45 / GPIO42 / GPIO41 |
| LCD backlight | GPIO5 |
| LCD reset | TCA9554 EXIO2 |
| Touch reset / interrupt | TCA9554 EXIO1 / GPIO4 |
| QMI8658 interrupts | TCA9554 EXIO4 / EXIO5 |
| PCF85063 interrupt | GPIO9 |
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

## Full firmware verification

The full firmware passed hotspot provisioning, activation, MQTT login, local
wake-word detection, ASR, conversation and speaker playback on 2026-07-26.
Listening confirmed clear voice with no notable pop or distortion. This is the
same board audio path used by the normal XiaoZhi state machine, not a loopback
or diagnostic-only substitute.

The normal board firmware also initializes a project-owned QMI8658 layer and a
read-only PCF85063 layer. QMI8658 uses 4 g / 120 Hz acceleration and 256 dps /
120 Hz gyroscope ranges. RTC reads never reset or set the chip; a retained-time
gate remains deferred while its low-voltage flag is set and the battery is
disconnected.

Short-pressing the side PWR key toggles the backlight. Holding it for three
seconds stops the audio service, disables codec I/O, turns off the backlight,
waits for key release, configures GPIO6 as the deep-sleep wake source and
releases GPIO7 power hold. A following short press performs a clean cold boot;
display, IMU, Wi-Fi and wake-word recovery were verified on hardware.

## Custom expressions

The project-owned `FujiExpressionController` maps the live XiaoZhi state to
`idle`, `listening`, `thinking`, `connecting`, `speaking`, `success`, `error`,
`offline` and `muted`. Its fixed priority is screen-off/deep-sleep, fatal error
or offline, mute, device state, then a server emotion hint. A server hint cannot
override an active or safety state.

Place transparent 320x320 images in `assets/` using the `fuji_<state>.png`
names. Listening, thinking, connecting and speaking may instead use GIF files
with the same stem; every GIF frame delay must limit playback to 12 fps or less.
Files are packed into the existing assets partition, while decoded image cache
uses PSRAM when available. Missing, malformed, incorrectly sized or too-fast
assets fall back to allocation-stable LVGL geometry.

State changes stop and rewind the old animation before selecting the next one.
The PWR short-press and controlled shutdown paths pause the expression timer
before the backlight is disabled. Wake resumes from the current application,
network and mute state rather than continuing the prior animation frame.

The host policy test verifies the priority ordering. Static host coverage is
now 37 tests, plus the standalone expression policy test.

Physical expression acceptance passed on 2026-07-26 with the placeholder PNG
pack. The face remained upright, centered and complete on the round panel with
no corruption or flicker. The normal wake, ASR, conversation and TTS path
remained audible and complete during repeated state changes. A first hardware
run exposed a roughly 310 ms synchronous first-decode delay on the listening
path; core PNGs are now prewarmed before the audio service starts and server
emotion hints are handed to the LVGL timer without taking the display lock on
the audio state path. Subsequent listening expression changes measured 30 to
100 ms, while recording no longer waits for the display refresh.

The final uninterrupted run lasted 30 minutes under extensive Wi-Fi, ASR, TTS
and expression switching. The five-minute warm baseline was 99,007 bytes of
free internal-capable RAM and 5,226,828 bytes of free PSRAM. At minute 30, after
the voice session had returned to idle, drift was 672 and 1,008 bytes
respectively, both below the 8 KiB gate. Active voice/network samples showed
temporary queue and decoder allocations but returned after the session; there
was no monotonic growth, reset, watchdog, display fault or speaker underrun.
Long listening windows did produce the existing input encode-queue
"drop oldest" warning, without an audible playback defect; that audio
backpressure is tracked separately from expression rendering.

Short PWR presses fully stopped the expression refresh before backlight-off.
Wake restored the current speaking/listening state without a stale frame or
visible flash. The user confirmed the final idle face still had correct crop
and orientation, and that continuous conversation had no pop, stutter or
obvious missing speech. Final art requirements, including the planned
speaking-interrupt transition and phone workflow states, are documented in
`Images/Fuji_Expression_Asset_Requirements.md` at the repository root.

The same test also found that the currently selected cloud TTS voice produced
Japanese and Korean subtitles without audio, while French and German audio
played normally. Since subtitle JSON and Opus audio are separate and the
device decoder is language-agnostic, this remains a XiaoZhi TTS provider/voice
language-coverage check rather than an embedded expression or codec failure.
