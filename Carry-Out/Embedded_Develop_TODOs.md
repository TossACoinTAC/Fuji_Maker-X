# Fuji Embedded Development TODOs

## 0. Waveshare 1.46 migration checkpoint (2026-07-25)

The current target is the Waveshare `ESP32-S3-Touch-LCD-1.46`, replacing the
earlier breadboard audio prototype. Project-owned support lives in
`embedded/xiaozhi/main/boards/fuji-waveshare-1p46`; the upstream Waveshare
board remains unchanged.

Completed gates:

- The physical board identifies as ESP32-S3 rev 0.2 with 16 MiB Flash and
  8 MiB Octal PSRAM. Its original 16 MiB image is backed up as
  `firmware/waveshare-1p46-original-2026-07-25.bin`, SHA-256
  `2c68b9c9de9cd5d9ae5cf24850658be9908f6c41ecd588b9c1baafec1a93ca9b`.
- The isolated probe verified chip, Flash, PSRAM and board identity without
  initializing NVS, display, audio or networking.
- I2C scan found TCA9554 `0x20`, PCF85063 `0x51`, SPD2010 touch `0x53` and
  QMI8658 `0x6B`.
- The 412x412 display passed color, orientation, round-boundary and backlight
  checks at 40 MHz QSPI mode 3. Touch passed center, four-edge and continuous
  drag checks. LCD and touch resets are controlled independently through the
  TCA9554.
- The microphone passed an RX-only controlled capture using the upstream pin
  and slot map with Philips I2S framing. The valid 11-second PCM WAV has
  SHA-256
  `5f59f4ff64c2990ec6729141fbaf278543e23d96c0ba655487e9dfce7f679bd0`;
  quiet/speech/snap RMS was 34.6/85.6/110.6 with zero read failures. Listening
  confirmed clear speech and finger snaps with no notable background noise.

Build status:

- Five ESP-IDF 6.0.2 variants exist: probe, display test, microphone test,
  speaker test and full firmware. Each has an isolated build directory and
  sdkconfig under the external build root.
- The default build root is
  `/Volumes/Mac_DiskExtension/EmbeddedCache/Maker-X/xiaozhi`, configurable with
  `XIAOZHI_BUILD_ROOT` or `release.py --build-root`, with a project-local
  fallback when the volume is absent.
- No-change builds for probe/display/microphone/speaker/full measured
  4.49/4.41/4.67/4.46/4.44 seconds. All five merged binaries and release ZIPs
  were generated.
- Host static coverage is 33 tests. It checks pins, variants, partitions,
  single board registration, diagnostic isolation, Philips audio framing and
  the no-upstream-modification constraint.

Remaining gates, in order:

1. With the hardware volume at minimum, run the BOOT-armed TX-only speaker
   diagnostic and check its short tone and speech for pops, distortion, noise
   and correct silence after completion.
2. Only after the speaker passes, flash the full board firmware and verify
   Wi-Fi provisioning, wake, conversation and playback end to end.
3. Add QMI8658, PCF85063, power-key and hold/shutdown behavior after the voice
   loop is stable.
4. Keep the battery disconnected until voltage, polarity, protection and the
   connector have been checked separately. TF card and external breadboard
   modules are outside this migration.

## 1. Current baseline and constraints

The repository currently has:

- platformio.ini targeting esp32-s3-devkitc-1 with Arduino;
- BOARD_HAS_PSRAM, qio_opi, 16 MB Flash, and 8 MB PSRAM build assumptions;
- a placeholder src/main.cpp that only starts serial logging;
- a prebuilt XiaoZhi image under firmware/ and an existing board already running the 78/xiaozhi-esp32 feature set;
- reusable XiaoZhi Wi-Fi/4G, ESP-SR wake word, OPUS audio, ASR/LLM/TTS, speaker recognition, OLED/LCD, battery/power, LED/GPIO, and MCP paths;
- no committed carrier schematic, exact GPIO map, phone bridge, or Fuji-specific touch/mute/haptic/mount layer.

The implementation must begin by matching the existing board to its 78/xiaozhi-esp32 board configuration and extension points. Reuse the existing display, audio, wake-word, network, power, and MCP drivers instead of creating duplicate hardware drivers. The remaining discovery gate is how the local prebuilt image and upstream source/configuration map onto this repository.

### 1.1 Embedded responsibility boundary

The ESP32-S3 firmware owns:

- microphone mute enforcement, touch, button, motion, haptic, face, LED, speaker, battery, and local state;
- BLE GATT control and status to the phone;
- local expressions and safe offline phrases;
- request IDs, timeouts, duplicate rejection, and safe restart behavior.

The phone bridge owns:

- hotspot setup and network/cloud access;
- location, restaurant search, maps, reminders, and translation services;
- output routing to Bluetooth earphones;
- music-player audio focus and supported play/pause/next commands;
- user-visible permissions, transcripts, memory, and privacy controls.

Fuji does not directly provide Bluetooth Classic A2DP audio to earphones in this implementation. The phone is the Bluetooth and media hub.

### 1.2 Non-negotiable embedded rules

1. Hardware mute must disable microphone capture even if software is wedged.
2. LISTENING must have an unambiguous face/light state.
3. No external action is considered successful until the phone bridge confirms it.
4. No stale, duplicated, or expired command may trigger speech, navigation, reminders, or media actions.
5. A lost earphone route must never cause private content to be spoken aloud automatically.
6. No raw audio is persisted by default.
7. A battery fault, brownout, watchdog reset, or BLE disconnect returns to a safe local state.
8. Drivers must use logical names from board_pins.h; no raw GPIO literals inside feature modules.

---

## 2. Target source layout

Create the following structure after the board pin map is known:

~~~text
include/
  board_pins.h
  fuji_config.h
  fuji_types.h
  fuji_events.h
  fuji_state.h
  fuji_protocol.h
  driver_interfaces.h
  xiaozhi_adapter.h
src/
  main.cpp
  core/fuji_app.cpp
  core/fuji_state.cpp
  core/fuji_events.cpp
  core/fuji_config.cpp
  drivers/board_probe.cpp
  adapters/xiaozhi_audio.cpp
  adapters/xiaozhi_display.cpp
  drivers/touch_ttp223.cpp
  drivers/mute_switch.cpp
  drivers/imu_bmi270.cpp
  drivers/haptic_gpio.cpp
  adapters/xiaozhi_power.cpp
  adapters/xiaozhi_status_led.cpp
  services/ble_link.cpp
  services/phone_protocol.cpp
  services/output_route.cpp
  services/storage_nvs.cpp
  services/power_manager.cpp
  services/xiaozhi_adapter.cpp
test/
  test_protocol.cpp
  test_state_machine.cpp
  test_output_route.cpp
~~~

Do not create all files as empty stubs and then claim the feature is complete. Add each module when its acceptance test is ready.

---

## 3. Shared types and event model

### 3.1 Fuji states

Define one state enum in include/fuji_state.h:

~~~cpp
enum class FujiState : uint8_t {
  IDLE,
  LISTENING,
  THINKING,
  CLARIFYING,
  CONFIRMING,
  ACTING,
  SUCCESS,
  ERROR,
  QUIET,
  MUTED,
  DISCONNECTED
};
~~~

The output route is independent from the interaction state:

~~~cpp
enum class OutputRoute : uint8_t {
  DEVICE_SPEAKER,
  PHONE_EARPHONES,
  FACE_HAPTIC
};
~~~

PHONE_EARPHONES is valid only after the phone bridge has sent a current route verification event. It must not survive a reboot as an assumed route.

### 3.2 Events

Define a fixed event type and bounded payload in include/fuji_events.h:

~~~cpp
enum class FujiEventType : uint8_t {
  BOOT,
  TICK,
  TOUCH_DOWN,
  TOUCH_LONG,
  USER_BUTTON,
  MUTE_CHANGED,
  IMU_TILT,
  BATTERY_UPDATE,
  BLE_CONNECTED,
  BLE_DISCONNECTED,
  BLE_COMMAND,
  BLE_ROUTE_UPDATE,
  SPEECH_STARTED,
  SPEECH_PARTIAL,
  SPEECH_FINISHED,
  ASSISTANT_RESPONSE,
  ACTION_CONFIRMED,
  ACTION_REJECTED,
  ACTION_COMPLETED,
  ACTION_FAILED,
  TIMEOUT,
  LOW_BATTERY,
  FATAL_ERROR
};
~~~

Every event contains a monotonic timestamp, event type, source, optional request ID, bounded payload length, and no heap-owned pointer that outlives the producer.

### 3.3 Queue rules

- ISR handlers only capture a GPIO edge and enqueue a small event.
- I2C, BLE callbacks, audio APIs, and display rendering never run in a GPIO ISR.
- Start with a 32-event queue and log overflow.
- A full queue drops repeated TICK events first, never MUTE_CHANGED, BLE_COMMAND, or ACTION_CONFIRMED.
- Every event consumer must be non-blocking or have an explicit timeout.

---

## 4. Board and pin bring-up gate

### 4.1 First task: identify the actual carrier

Before adding a sensor:

1. Record the exact DNESP32S3 board revision.
2. Obtain the XiaoZhi carrier schematic or module documentation.
3. Record GPIOs reserved by Flash/PSRAM, native USB, boot strapping, UART logging, and XiaoZhi audio.
4. Identify the battery/USB input path and whether the board already has a charger or fuel gauge.
5. Confirm whether I2C, I2S, and SPI buses are already initialized by XiaoZhi.
6. Record voltage levels for every exposed header.
7. Write the result into include/board_pins.h and commit the map before wiring.

### 4.2 Logical pin map

Use symbolic assignments like these, replacing GPIO_UNUSED only after inspection:

~~~cpp
struct FujiPins {
  int i2c_sda;
  int i2c_scl;
  int i2s_mic_bclk;
  int i2s_mic_ws;
  int i2s_mic_data;
  int i2s_spk_bclk;
  int i2s_spk_ws;
  int i2s_spk_data;
  int display_reset;
  int touch_event;
  int mute_sense;
  int mic_enable;
  int amp_enable;
  int status_led;
  int user_button;
  int charger_status;
};
~~~

Required checks:

- no logical signal maps to an unavailable or boot-critical pin;
- existing XiaoZhi I2C addresses are recorded before adding anything; BMI270 commonly uses 0x68/0x69, while TTP223B is a digital GPIO input;
- shared I2S clocks are proven with the actual XiaoZhi driver;
- the display and haptic driver do not block audio timing;
- all external inputs are pulled to a known state during reset.

### 4.3 Board probe module

Implement drivers/board_probe.cpp first:

~~~cpp
bool board_probe_i2c(FujiBusReport* report);
bool board_probe_audio(FujiAudioReport* report);
bool board_probe_power(FujiPowerReport* report);
void board_probe_print(Stream& out);
~~~

The probe reports detected I2C addresses, configured bus speed, audio availability, battery input, and selected board revision. It must not silently continue with an unverified pin map in wearable mode.

---

## 5. Driver modules

### 5.1 Face display: xiaozhi_display adapter

Component: existing XiaoZhi OLED/LCD display and emoji renderer. Do not buy or add a second SH1107 display unless the existing display cannot be placed in the Fuji enclosure or cannot be controlled by the firmware.

Required functions:

~~~cpp
bool display_begin_existing();
void display_set_state(FujiState state, OutputRoute route, bool ble_connected);
void display_set_battery(uint8_t percent, bool charging);
void display_set_error(const char* code);
void display_draw_face(FujiFace face);
void display_tick(uint32_t now_ms);
~~~

Implementation rules:

- keep a framebuffer in RAM and update at 15-30 Hz;
- show listening, mute, earphone route, disconnected, and low-battery states without relying on color;
- never render from a BLE callback;
- dim or stop animation during QUIET and low battery;
- retain a simple face-only fallback if the display fails.

Acceptance test: a user can identify IDLE, LISTENING, MUTED, PHONE_EARPHONES, FACE_HAPTIC, ERROR, and DISCONNECTED without audio.

### 5.2 Touch input: touch_ttp223

Component: TTP223B capacitive touch controller with a copper or stainless pad under the silicone shell.

Required functions:

~~~cpp
bool touch_begin(const FujiPins& pins);
void touch_poll(uint32_t now_ms);
bool touch_read_event(FujiEvent* event);
~~~

Gesture policy:

- short press: wake or reassure when idle;
- long press, 700-1200 ms: toggle quiet mode;
- press while speaking: cancel local output;
- press while confirming: reject the pending action;
- never make touch silently confirm navigation, reminders, or media actions.

Debounce for at least 50 ms and test wet fingers, fabric contact, charger noise, and movement.

### 5.3 Physical mute switch: mute_switch

Component: latching SPST slide switch, SS-12D00G class, plus a hardware microphone enable/power gate.

Required functions:

~~~cpp
bool mute_begin(const FujiPins& pins);
bool mute_is_active();
void mute_poll(uint32_t now_ms);
void mute_apply_hardware(bool muted);
~~~

Requirements:

- mute_apply_hardware(true) disables the microphone power/enable path independently of the assistant state machine;
- firmware enters MUTED, stops capture, cancels pending speech, and shows a clear indicator;
- unmuting never automatically resumes a previous request;
- test the state after power loss and reboot;
- no voice command can defeat hardware mute.

### 5.4 Haptics: haptic_gpio

Component: new 10 mm, 3 V coin ERM vibration motor driven by an available GPIO/MCP output through a small MOSFET and flyback diode. DRV2605L is P1 only and should not be purchased for P0.

Required functions:

~~~cpp
bool haptic_begin_gpio(const FujiPins& pins);
void haptic_play(HapticPattern pattern);
void haptic_stop();
void haptic_tick(uint32_t now_ms);
~~~

Initial patterns:

| Pattern | Meaning |
|---|---|
| SHORT_PULSE | Touch acknowledged |
| DOUBLE_PULSE | Confirmation required |
| LONG_LOW | Error or low battery |
| TRIPLE_LIGHT | Success / route sent |
| MUTE_TOGGLE | Hardware mute changed |

Patterns must be short, non-startling, and distinguishable without sound.

### 5.5 IMU: imu_bmi270

Component: Bosch BMI270 breakout, I2C or SPI.

Required functions:

~~~cpp
bool imu_begin(const FujiPins& pins);
void imu_poll(uint32_t now_ms);
bool imu_read_event(FujiEvent* event);
~~~

P1 behavior:

- tilt affects idle eye animation;
- a gentle orientation change may trigger a local acknowledgement;
- sudden motion can be logged for mount testing;
- motion never confirms an external action;
- use a low-pass filter and deadband so walking does not generate chatter.

If BMI270 integration delays P0, compile a no-op implementation and keep the state machine functional.

### 5.6 Battery and charger: xiaozhi_power adapter

Components: existing XiaoZhi battery display, charger, and power-management path. A replacement protected 1S LiPo is conditional on inspection; MAX17048 and BQ24074 are not P0 purchases.

Required functions:

~~~cpp
bool power_begin_existing();
BatteryStatus power_read_existing();
void battery_tick(uint32_t now_ms);
void power_request_sleep(SleepReason reason);
void power_handle_charger_event(bool charging);
~~~

Thresholds to validate on the actual cell:

- LOW_WARNING: reduce animation, show a warning, notify phone;
- CRITICAL: stop nonessential audio/animation and request safe shutdown;
- CHARGING: show charging state and reject high-power field behavior if hot;
- MUTED_LOW_POWER: prioritize mute and wake/reconnect paths.

Never use only analog voltage to claim a percentage. Log fuel-gauge readings, charge current, temperature, and runtime for idle, voice, Wi-Fi, BLE, display, speaker, and haptic workloads.

### 5.7 Status LED: xiaozhi_status_led adapter

Component: existing board LED/GPIO/device-side MCP output. Do not add a separate RGB LED for P0 unless the existing indicator cannot express listening, mute, route, and error states.

Required functions:

~~~cpp
bool status_led_begin_existing();
void status_led_set(LedPattern pattern);
void status_led_tick(uint32_t now_ms);
~~~

The LED is supplementary. It must not be the only microphone/listening indicator, and it must be dimmed in QUIET and low-power modes.

### 5.8 Audio adapter: xiaozhi_audio

Preferred and P0 path: call the existing XiaoZhi audio hooks for the onboard microphone, speaker, amplifier, ESP-SR wake word, OPUS transport, and ASR/LLM/TTS stream. INMP441/MAX98357A are bench-only fallback parts and are not part of the reduced procurement list.

Required interface:

~~~cpp
bool audio_begin(const FujiPins& pins);
bool audio_start_capture();
void audio_stop_capture();
bool audio_is_capturing();
bool audio_play_local(const uint8_t* data, size_t length);
void audio_stop_playback();
void audio_set_local_speaker_enabled(bool enabled);
void audio_tick(uint32_t now_ms);
~~~

Rules:

- audio_start_capture() returns false while hardware mute is active;
- no audio buffer is written to persistent storage;
- use bounded ring buffers and report overflow/underrun;
- local speaker output is allowed only for DEVICE_SPEAKER;
- PHONE_EARPHONES means the phone receives the response path; it is not an ESP32-S3 Bluetooth audio stream.

---

## 6. XiaoZhi integration adapter

### 6.1 Integration gate

Determine which is true before implementing assistant behavior:

1. XiaoZhi is source/components that can be linked into the PlatformIO build;
2. XiaoZhi is a separate binary flashed to the board and Fuji code cannot coexist in the same image;
3. XiaoZhi exposes a documented event/API boundary that custom firmware can call;
4. the current binary is a reference image and the project must port its required components.

Do not place Fuji logic inside undocumented binary offsets or assume a second setup()/loop() can run alongside the prebuilt image.

### 6.2 Adapter interface

Create include/xiaozhi_adapter.h:

~~~cpp
struct XiaoZhiCallbacks {
  void (*on_listening_started)();
  void (*on_partial_text)(const char* text);
  void (*on_final_text)(const char* text);
  void (*on_response_text)(const char* text);
  void (*on_response_audio)(const uint8_t* data, size_t length);
  void (*on_error)(const char* code);
};

bool xiaozhi_begin(const XiaoZhiCallbacks& callbacks);
void xiaozhi_poll(uint32_t now_ms);
bool xiaozhi_start_listening();
void xiaozhi_stop_listening();
bool xiaozhi_send_text(const char* text);
bool xiaozhi_network_ready();
void xiaozhi_cancel_request(const char* request_id);
~~~

If real XiaoZhi hooks are unavailable, implement xiaozhi_mock.cpp for state-machine and BLE tests. The mock must generate deterministic success, timeout, and network-failure events.

### 6.3 Assistant rules at the device boundary

- the device requests a service but does not independently launch maps, reminders, or media;
- the phone bridge sends a request ID, expiration, and required confirmation state;
- the adapter reports response completion or failure, not merely network-call start;
- a response after timeout is ignored and logged;
- cancel stops local speech and sends cancellation to the phone/service when supported.

---

## 7. BLE phone protocol

### 7.1 GATT service

Use one custom 128-bit service. Generate UUIDs once and store them in fuji_config.h. Do not change them between firmware builds without a migration plan.

| Characteristic | Direction | Purpose |
|---|---|---|
| CMD_RX | Phone writes | Requests, confirmation, cancel, settings, route policy |
| EVENT_TX | Device notifies | State changes, transcripts/status, action result, errors |
| STATE_RO | Phone reads/notifies | Current state, battery, mute, route, firmware version |
| CONFIG_RW | Phone reads/writes | Language, quiet default, output route, music policy |
| FW_VERSION_RO | Phone reads | Firmware/build/schema compatibility |

Functions:

~~~cpp
bool ble_link_begin();
void ble_link_poll(uint32_t now_ms);
void ble_start_advertising();
void ble_stop_advertising();
bool ble_is_connected();
bool ble_send_event(const FujiEvent& event);
bool ble_send_state(const FujiStateSnapshot& snapshot);
bool ble_set_output_route(OutputRoute route);
void ble_on_command_bytes(const uint8_t* data, size_t length);
void ble_on_disconnect();
~~~

### 7.2 Payload and reliability

Use UTF-8 JSON for the first phone bridge because it is inspectable. Add framing because BLE writes/notifications may be smaller than a full command:

~~~text
version | message_type | request_id | chunk_index | chunk_count | payload_length | payload
~~~

Required message fields:

- version;
- request_id for every transaction;
- intent such as food_search, route_start, translate, reminder_create, media_control, or output_route_set;
- state and expires_at;
- requires_confirmation;
- output_route and music_policy when audio is involved;
- explicit status and error_code in responses.

Reject invalid JSON, unsupported schema, oversized payloads, duplicate request IDs, expired commands, unconfirmed route/media actions, and any output route the phone has not verified.

### 7.3 Pairing and reconnection

1. Advertise only during setup or after the pairing button is pressed.
2. Use a per-device name and short pairing code shown on the face/phone.
3. Store the bonded phone identity in NVS only after setup completes.
4. On reconnect, send a complete state snapshot, not old events.
5. Clear PHONE_EARPHONES on every disconnect or reboot.
6. Use exponential backoff so a dead battery does not keep the radio active forever.

---

## 8. Phone-route and media-control contract

This is an embedded/phone boundary, not a direct earphone implementation.

### Device sends

- output_route_request: DEVICE_SPEAKER, PHONE_EARPHONES, or FACE_HAPTIC;
- music_policy_request: pause_then_restore, duck_then_restore, or face_haptic_only;
- media_control_request: play, pause, next, or none;
- current Fuji state, mute, battery, and request ID.

### Phone returns

- output_route_verified with current route and timestamp;
- music_policy_applied with started, ducked, paused, restored, or unavailable;
- media_control_result with supported/unsupported/error status;
- earphone_disconnected or audio_route_changed;
- phone_permission_required when the user must act in the app.

Device behavior:

| Phone result | Device behavior |
|---|---|
| PHONE_EARPHONES verified | Keep private responses on the phone route; acknowledge face/haptic |
| No earphones | Use FACE_HAPTIC in quiet mode; use DEVICE_SPEAKER only if explicitly allowed |
| Music control unavailable | Continue Fuji response if output route works; report unavailable playback control |
| Route lost during response | Stop/suppress private content, show error/haptic, never announce loudly automatically |

---

## 9. State machine implementation

### 9.1 Core functions

Implement in core/fuji_state.cpp:

~~~cpp
void fuji_state_init();
FujiState fuji_state_get();
OutputRoute fuji_output_route_get();
bool fuji_dispatch_event(const FujiEvent& event);
void fuji_state_enter(FujiState next, const char* reason);
void fuji_state_tick(uint32_t now_ms);
bool fuji_request_confirmation(const FujiAction& action);
void fuji_cancel_active_request(const char* reason);
bool fuji_is_safe_to_shutdown();
~~~

### 9.2 Transition requirements

| Current | Event | Next | Side effects |
|---|---|---|---|
| IDLE | wake word / touch | LISTENING | Show listening state; start capture unless muted |
| IDLE | BLE command | THINKING | Validate request ID and start bounded request |
| LISTENING | speech finished | THINKING | Stop capture; start assistant request |
| THINKING | missing constraint | CLARIFYING | Ask one question; set timeout |
| THINKING | external action ready | CONFIRMING | Speak/display destination; require confirmation |
| CONFIRMING | confirm | ACTING | Send confirmed action to phone; no duplicate retry |
| CONFIRMING | reject / timeout | IDLE or QUIET | Cancel action and clear pending request |
| ACTING | verified success | SUCCESS | Show result; restore music through phone if requested |
| ACTING | failure | ERROR | Explain failure and provide fallback |
| any | mute on | MUTED | Stop capture/output; show mute state |
| MUTED | mute off | IDLE | Clear pending action; do not resume listening |
| any | BLE disconnect | DISCONNECTED or local state | Clear phone route and cancel unverified phone action |
| any | low battery | current / safe idle | Warn, reduce load, request shutdown if critical |

### 9.3 Timeouts

Define in fuji_config.h:

~~~cpp
constexpr uint32_t LISTEN_TIMEOUT_MS = 8000;
constexpr uint32_t THINK_TIMEOUT_MS = 15000;
constexpr uint32_t CONFIRM_TIMEOUT_MS = 10000;
constexpr uint32_t ACTING_TIMEOUT_MS = 20000;
constexpr uint32_t BLE_RECONNECT_BACKOFF_MAX_MS = 60000;
~~~

Tune with telemetry. Every timeout clears the request ID and route-related pending state.

---

## 10. Storage and configuration

Use Arduino Preferences/NVS or the equivalent ESP-IDF NVS API. Store settings and explicit preferences only, never raw audio.

| Key | Type | Default |
|---|---|---|
| schema | uint16 | 1 |
| language | string | onboarding language |
| quiet_default | bool | false |
| output_route | enum | FACE_HAPTIC until verified |
| music_policy | enum | duck_then_restore |
| remember_food | bool | false |
| volume | uint8 | conservative near-field level |
| bonded_phone | bytes | empty until pairing |
| last_fw_version | string | current build |

Functions:

~~~cpp
bool storage_begin();
bool storage_load(FujiConfig* config);
bool storage_save_config(const FujiConfig& config);
bool storage_forget_phone();
bool storage_clear_explicit_preferences();
~~~

Use schema migration. A firmware update must not accidentally restore the device speaker after the user selected earphones.

---

## 11. Main loop and scheduling

Start with one cooperative loop so timing and state are easy to inspect. Split into FreeRTOS tasks only after audio or BLE measurements show a need.

### 11.1 setup() order

Implement src/main.cpp in this order:

~~~cpp
void setup() {
  Serial.begin(115200);
  board_probe_print(Serial);
  storage_begin();
  power_manager_begin();
  display_begin_existing();
  status_led_begin_existing();
  haptic_begin_gpio(g_pins);
  mute_begin(g_pins);
  touch_begin(g_pins);
  power_begin_existing();
  audio_begin(g_pins);
  ble_link_begin();
  xiaozhi_begin(g_xiaozhi_callbacks);
  fuji_state_init();
}
~~~

The actual order may change if XiaoZhi owns audio, but every initialization failure must be logged and reflected in the local error state.

### 11.2 loop() budget

~~~cpp
void loop() {
  const uint32_t now = millis();
  input_poll(now);
  ble_link_poll(now);
  xiaozhi_poll(now);
  power_manager_poll(now);
  fuji_state_tick(now);
  display_tick(now);
  status_led_tick(now);
  haptic_tick(now);
  audio_tick(now);
  delay(1);
}
~~~

Initial timing targets:

- input and mute: 5-10 ms;
- BLE: event-driven, polled at least every 10 ms;
- state machine: every loop;
- display: 15-30 Hz;
- haptic: every loop until complete;
- battery: 1 Hz;
- board probe: boot only;
- serial logging: rate limited so it cannot starve audio.

### 11.3 Watchdog and faults

- Enable the ESP32 watchdog after boot diagnostics pass.
- Feed it only from the main scheduler.
- Persist reset reason and boot count.
- After repeated crashes, enter recovery mode with diagnostics and no microphone capture.
- Never auto-resume a pending map, reminder, media, or speech action after reboot.

---

## 12. Embedded implementation sequence

### Phase 0 - hardware and build inventory

- [ ] Record board revision and XiaoZhi audio ownership.
- [ ] Create board_pins.h with no guessed GPIOs.
- [ ] Confirm Arduino build, serial logging, PSRAM, and reset behavior.
- [ ] Decide whether the XiaoZhi image is integrated source, separate firmware, or reference-only.
- [ ] Add a BOARD_PROBE_ONLY build flag.

Exit: board probe prints verified memory, GPIO, I2C, audio, power, and firmware integration status.

### Phase 1 - safe hardware drivers

- [ ] Verify the existing XiaoZhi OLED/LCD face display and emoji renderer.
- [ ] Verify existing LED/GPIO/MCP outputs with non-color-only state labels.
- [ ] Bring up TTP223B touch and physical mute gate.
- [ ] Verify mute with a logic probe: microphone enable is inactive while muted.
- [ ] Bring up the ERM motor through an available GPIO/MCP output and MOSFET.
- [ ] Read and log the existing battery/charger/power-management state.
- [ ] Add BMI270 only after core drivers are stable.

Exit: the device can show state, accept touch, mute physically, vibrate, and report battery without XiaoZhi/cloud access.

### Phase 2 - local state machine

- [ ] Add FujiState, OutputRoute, events, queue, and timeout constants.
- [ ] Implement idle/listening/thinking/confirming/success/error/quiet/muted/disconnected.
- [ ] Add deterministic mock assistant responses.
- [ ] Add tests for confirmation, timeout, duplicate, stale, mute, and reboot behavior.

Exit: all P0 state transitions can be demonstrated offline from touch, button, and mock events.

### Phase 3 - BLE phone control

- [ ] Implement GATT service and characteristics.
- [ ] Implement framing, schema validation, request IDs, expiry, and duplicate rejection.
- [ ] Implement pairing/reconnect and complete state snapshot.
- [ ] Implement output_route_set and output_route_verified without assuming earphone presence.
- [ ] Log BLE RSSI, reconnect count, queue overflow, and protocol errors.

Exit: a simple phone test client can connect, read state, send a command, receive an event, and verify route loss without local speaker leakage.

### Phase 4 - XiaoZhi/audio integration

- [ ] Implement the documented XiaoZhi adapter or replacement audio adapter.
- [ ] Connect listening start/stop, partial/final text, response audio/text, error, cancel, and network callbacks.
- [ ] Keep hardware mute above the adapter so no callback can re-enable capture.
- [ ] Measure capture latency, response latency, I2S underruns, and RAM usage.

Exit: Fuji completes a short local voice exchange and recovers from network loss without hanging the state machine.

### Phase 5 - phone services and private audio

- [ ] Add phone-side food-search request/response.
- [ ] Add map handoff confirmation.
- [ ] Add phone output-route verification.
- [ ] Add earphone response routing through phone audio APIs.
- [ ] Add music pause, duck, restore, and supported play/pause/next results.
- [ ] Inject earphone disconnects and confirm FACE_HAPTIC fallback.

Exit: Fuji BLE and phone-earphone audio work simultaneously on the documented iPhone + AirPods reference pair.

### Phase 6 - P1 services

- [ ] Add translation intent and bounded response.
- [ ] Add reminder create/read/delete with confirmation and idempotency keys.
- [ ] Add explicit food-preference save/delete.
- [ ] Add quiet-mode default and phone settings synchronization.

Exit: at least two repeatable jobs work with explicit data and action boundaries.

### Phase 7 - power, enclosure, and field hardening

- [ ] Measure current in idle, listening, Wi-Fi, BLE, display, speaker, haptic, and charging states.
- [ ] Measure the existing battery; only compare 500-800 mAh protected replacements if its mass/runtime is unsuitable.
- [ ] Model and print the clip, magnet pockets, backplate guard, inner carrier, outer skin, spacers, and cable retainers; do not source finished versions of these parts.
- [ ] Test the two N35 magnets, steel backplate, printed clip alternative, and tether.
- [ ] Run walking, stairs, bending, snag, drop, heat, and rain/splash checks.
- [ ] Enable watchdog, crash reason, safe shutdown, and firmware version reporting.

Exit: the wearable prototype is safe enough for a consented one-week trial and has measured battery and mount behavior.

---

## 13. Test plan

### 13.1 Unit tests

- JSON/frame parsing and chunk reassembly;
- schema/version/request expiry;
- duplicate command rejection;
- state transitions and timeout clearing;
- output-route safety rules;
- music policy result mapping;
- NVS migration and preference deletion;
- battery threshold hysteresis;
- haptic pattern selection.

### 13.2 Hardware-in-loop tests

- mute switch physically prevents microphone capture;
- TTP223B does not trigger during speaker playback or charging;
- Existing OLED/LCD continues to render while BLE notifications arrive;
- GPIO-driven ERM motor does not corrupt microphone samples;
- Existing battery percentage and low-battery transitions match a measured supply;
- ESP32-S3 remains stable during Wi-Fi transmit plus BLE connection;
- speaker transients do not reset the board;
- earphone route loss cannot enable the local speaker without permission.

### 13.3 Field acceptance tests

1. Setup completes on the reference iPhone without a developer.
2. Fuji can be used alone for a food choice or translation request.
3. Fuji can be used with one trusted friend without requiring the phone screen.
4. The iPhone and AirPods remain usable while Fuji BLE is connected.
5. Music pauses/ducks and restores according to the selected policy.
6. Disconnect, no-network, no-location, and no-result failures are understandable.
7. Hardware mute, quiet mode, face state, and haptic feedback are discoverable.
8. The mount and tether pass physical checks before public wear.

---

## 14. Definition of embedded done

The embedded implementation is ready for the first integrated pilot when:

- the board pin map and XiaoZhi integration boundary are documented;
- every P0 driver has a bring-up log and a failure state;
- src/main.cpp contains a non-blocking scheduler rather than feature code hidden in loop();
- state, protocol, audio, power, and storage modules have clear interfaces;
- BLE commands are versioned, bounded, expiring, and idempotent;
- physical mute is effective without trusting the network or assistant;
- the phone can verify Fuji and earphone route status independently;
- direct Fuji-to-earphone audio is not accidentally implemented as an unsupported assumption;
- battery, magnet, tether, microphone privacy, and restart behavior have measured acceptance results;
- no raw audio or private response is persisted by default;
- firmware reports version, reset reason, battery, mute, BLE, and output-route state for support.

Only after these criteria pass should the team spend time on character animation variety, extra music integrations, or a custom production PCB.
