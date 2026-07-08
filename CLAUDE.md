# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this project is

"Fuji" (软萌团子 bot) — a small, magnetic, wearable AI companion robot aimed at ACG/二次元 users. It is currently in the **design/brainstorming phase**: there is no custom firmware source yet. The repo holds design notes and a prebuilt base firmware that runs on the target hardware.

- `docs/Brainstorm0.md` / `docs/FujiDesign.md` — product concept and feature notes (Chinese). Read these for intent: core features are voice interaction (keyword + free-form dialog), a "是啊吃什么" nearby-food recommender, voice navigation broadcast, translation/reminders, and an emotion/touch/motion "卖萌" system.
- Base stack: **DNESP32S3 board + 小智AI (XiaoZhi AI)** firmware. Custom features are expected to be built on top of this.

## Repo layout

- `src/` — custom firmware source (`main.cpp` is a stub entry point).
- `include/`, `lib/`, `test/` — standard PlatformIO dirs (currently empty placeholders).
- `docs/` — design/brainstorm notes.
- `firmware/` — prebuilt base firmware image (see below).
- `device.txt` — machine-specific serial port; git-ignored, don't rely on it being present.

## Hardware target

- Board: ALIENTEK (正点原子) **DNESP32S3**, ESP32-S3 in **N16R8** config — 16MB QIO Flash + 8MB Octal PSRAM.
- Serial port (this machine): `/dev/cu.usbmodem101` — see `device.txt` (git-ignored). Re-check with `ls /dev/cu.usbmodem*` if flashing fails; the suffix changes across reconnects.
- Monitor baud: `115200`.

## Tooling

- `.venv/` holds the only installed toolchain: **esptool v5.3.1** and **pyserial** (Python 3.14). Always invoke via `.venv/bin/...` — there is no global install.
- `platformio.ini` targets `esp32-s3-devkitc-1` (Arduino framework, `qio_opi` memory, USB-CDC-on-boot). **PlatformIO itself is not installed** — build commands below only work after `pip install platformio` and once source exists under `src/`.

## Firmware image

`firmware/v2.0.3_atk-dnesp32s3.bin` is a **complete merged image** (bootloader + partition table + app, ESP-IDF v5.5), not an app-only binary. Flash it to offset `0x0`, not `0x10000`. Verify with `.venv/bin/esptool image-info firmware/v2.0.3_atk-dnesp32s3.bin`.

## Common commands

Flash the base firmware (esptool v5 uses hyphenated subcommands, e.g. `write-flash`):

```bash
.venv/bin/esptool --chip esp32s3 --port /dev/cu.usbmodem101 --baud 921600 write-flash 0x0 firmware/v2.0.3_atk-dnesp32s3.bin
```

Serial monitor:

```bash
.venv/bin/pyserial-miniterm /dev/cu.usbmodem101 115200
```

Erase flash (destructive — wipes all NVS/wifi config; confirm before running):

```bash
.venv/bin/esptool --chip esp32s3 --port /dev/cu.usbmodem101 erase-flash
```

Build custom firmware (only after PlatformIO is installed and `src/` exists):

```bash
pio run                 # build
pio run -t upload       # build + flash
pio run -t monitor      # serial monitor at 115200
```

## Networking note

Per the design, the device connects to a **phone hotspot** (not a fixed AP), so any Wi-Fi/provisioning work must not assume stable SSID/credentials.
