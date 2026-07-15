# Fuji - Full Product Design (Design Thinking)

## 0. Design brief and product definition

Fuji is a small, soft-looking AI companion that can be magnetically worn on a shoulder or bag. It combines a cute character with a few useful, hands-free services. The first platform is a DNESP32S3 running on top of the XiaoZhi AI voice base, paired with a phone hotspot and a phone-side bridge for maps and other external services.

The product promise is:

> Ask Fuji for the next small thing, and Fuji helps you do it while keeping the moment warm.

The promise has two equal parts:

- **Companion:** Fuji responds with a consistent, expressive personality through voice, a face, light, touch, and small movements.
- **Helper:** Fuji resolves low-stakes coordination tasks without requiring the user to take out a phone.

Fuji is not a phone replacement, a general smart-home hub, a therapist, or a safety-critical navigation device. The first release should be narrow enough to feel reliable and charming.

### Product hypothesis

If a character-oriented user can use Fuji in one recurring everyday situation (especially choosing food alone or with close friends) in less than two minutes, and the interaction feels more delightful and less demanding than operating a phone, they will carry it again. Repeat utility earns the right to add translation, reminders, and richer character content.

### Relationship to the current prototype

The repository currently contains an Arduino entry point for the ESP32-S3 and a prebuilt XiaoZhi image in `firmware/`. The custom entry point is a placeholder: emotion, touch, motion, and voice features are not implemented yet. This document therefore separates:

- **Product intent:** the experience Fuji should provide;
- **Prototype scope:** what can be tested with a phone, a speaker, a printed shell, or mocked services;
- **Implementation scope:** what should eventually be layered into the ESP32-S3 firmware and the phone bridge.

### Bluetooth feasibility and recommended topology

Yes, Fuji can support a phone connection and private earphone use at the same time, but the recommended design has the **phone as the Bluetooth hub**:

```text
Fuji -- BLE control and status --> phone -- phone audio stack --> Bluetooth earphones
                                      |
                                      +--> music player and media controls
```

In this topology, the phone keeps a BLE link to Fuji while it routes audio to the earphones using its normal Bluetooth audio support. Fuji sends requests, control/status updates, and playback commands to the phone; the phone bridge produces or receives the response content and the phone's audio session decides whether to pause or duck music, play Fuji privately, and restore music afterward. The phone app must show whether the earphone route is actually active.

This is different from making Fuji connect directly to both the phone and the earphones. Multiple BLE control connections can be possible with the ESP32-S3 stack, but a BLE GATT link is not an earphone audio link. Ordinary earphones generally expect a Bluetooth Classic A2DP source, while the current ESP32-S3/XiaoZhi platform should be treated as a BLE control endpoint until a supported audio profile is proven. Some newer earphones support LE Audio, but that still requires a compatible controller, stack, and product-level validation.

Therefore:

- **Supported design target:** Fuji to phone over BLE; phone to earphones over the phone's Bluetooth stack; phone to music player through supported platform media APIs.
- **Not a P0 promise:** Fuji directly streaming audio to earphones while also maintaining a phone link.
- **If direct audio becomes essential:** choose a hardware and software platform with a verified Bluetooth Classic A2DP or LE Audio source implementation, or add a dedicated audio bridge.

The exact simultaneous behavior must be tested on the first supported Android/iOS version and representative earphone models. Pairing, audio focus, microphone routing, reconnection, and background restrictions vary by phone and earphone.

---

## 1. Empathize

Design Thinking starts with observed behaviour rather than the attractive object. The primary target is Lin, a somewhat introverted ACG enthusiast described in `TargetUser_Persona.md`. Lin is a focused beachhead, not a claim that all ACG or cute-object buyers behave the same way.

### 1.1 What we currently believe

| Assumption | Why it matters | Evidence needed |
|---|---|---|
| Lin is willing to carry or wear a character object outside | A companion has no value if it stays on a desk, but public attention may feel costly | Observe what accessories people already carry or wear and where they remove them |
| Food choice is a frequent, frustrating micro-decision | It is the strongest candidate for a repeatable first utility | Diary study of solo meals and meals with close friends; count decision time and phone use |
| Voice is useful when attention or energy is low | This is the central low-friction promise, not only a group shortcut | Compare a Fuji mock-up with normal map/search use alone and in small groups |
| Cuteness changes willingness to reuse the helper | Personality must be more than a skin | Test identical utility with neutral versus expressive responses |
| A phone hotspot is acceptable if setup is simple | The hardware is not intended to carry a full independent connection | Measure setup completion, reconnection, and user frustration |
| Small, editable memory improves recommendations | Preference history should create continuity without feeling invasive | Test explicit memory prompts and deletion controls |
| Public volume and attention are a concern | A loud mascot will make the object embarrassing | Offer quiet, haptic, and face-only modes in early prototypes |
| Private audio is valuable outside | The user may want Fuji's response without broadcasting it, while music is already using the phone's audio route | Test phone-to-earphone routing, music duck/pause, and a non-speaking fallback |

These are hypotheses. They must not be written up as user research findings until they are tested.

### 1.2 Research plan

Run a lightweight discovery round before committing to enclosure tooling or a paid cloud dependency.

1. **Eight semi-structured interviews** with ACG enthusiasts who own character accessories or follow character culture, split between students and early-career workers with different comfort levels around public attention. Ask about real moments of indecision, voice-assistant use, charging habits, and what they would never want an object to do in public.
2. **Five daily-life observations or diary entries.** Follow a meal, shopping, commute, waiting, or low-energy decision from trigger to resolution, alone and with close friends. Record time, phone interactions, social energy, and moments of embarrassment or delight.
3. **A one-week Wizard-of-Oz trial.** Give users a non-functional or partially functional object. A researcher silently supplies recommendations and expressions from a phone while the user believes they are interacting with a Fuji prototype. This tests the experience before engineering quality can bias the result.
4. **A short pricing interview.** Show a real-size mock-up and three capability bundles. Ask what the user would remove before lowering the price; do not rely on a stated purchase intention alone.

### 1.3 Interview prompts

- Tell me about the last time you did not want to decide what to eat. Were you alone or with someone, and what happened minute by minute?
- When do you use voice instead of tapping a phone? When do you avoid voice?
- What character object do you carry or wear, and what makes it worth the space it takes?
- What would make a cute device feel comforting when you are alone? What would make it feel childish, demanding, or embarrassing?
- If Fuji remembered one preference, what should it remember? What should it never remember?
- What would you expect to happen when Fuji cannot hear, cannot connect, or gives a bad recommendation?
- Where would you attach it, and what would make you remove it?

### 1.4 Current journey and opportunity

| Moment | Current behaviour | Friction | Fuji opportunity |
|---|---|---|---|
| User notices hunger or low energy | User asks, "What should we eat?" alone or with close friends | Too many options and too little energy to search or negotiate | Wake Fuji with a neutral, playful chooser |
| Options are searched | One person opens a map and reads reviews | Conversation pauses; phone is passed around; options are too many | Ask for only the missing constraint and return two or three options |
| User chooses | User starts navigation and checks route | Confirmation and map handoff are unclear | Repeat destination, ask for confirmation, then launch the phone route |
| User walks or waits | User looks down repeatedly or does not want to talk to anyone | Attention is split, energy is low, and the device may attract attention | Give short prompts, quiet acknowledgement, and return to idle |
| Decision ends | The helper disappears | No emotional continuity; next use starts from zero | Offer a small acknowledgement, record only an explicit preference, and return to idle |

### 1.5 Empathy statement

Lin needs to feel that a small decision is easy and emotionally low-pressure, not that a machine or another person has taken over. Lin wants a companion that is expressive enough to feel personal but restrained enough to leave both private and social space intact.

---

## 2. Define

### 2.1 Point-of-view statement

**Lin is a somewhat introverted ACG enthusiast who needs hands-free help with small decisions and gentle company during ordinary low-energy moments, because opening a phone or initiating a social interaction can feel like too much effort, while ordinary assistants do not feel worth carrying.**

### 2.2 How-might-we questions

1. How might we make Fuji useful in under two minutes without turning it into another screen to manage?
2. How might we make a recommendation feel like a friendly opinion while keeping the user in control of the decision?
3. How might we make Fuji expressive without demanding a public performance or attracting unwanted attention?
4. How might we make a shoulder-mounted device safe, comfortable, removable, and trustworthy?
5. How might we show the boundary between Fuji's own response and an external action such as navigation?

### 2.3 Design principles

1. **Warmth before cleverness.** A small, well-timed expression is more valuable than a long answer or a novelty feature.
2. **One missing question at a time.** Ask for the smallest constraint needed to proceed; never conduct a form-filling interview by voice.
3. **Suggest, then confirm.** Fuji may recommend a restaurant or route, but it must not trigger an external action without a clear confirmation.
4. **Quiet is a first-class mode.** The user must be able to receive face, light, or haptic feedback without broadcasting a conversation.
5. **Visible state builds trust.** The user should always know whether Fuji is idle, listening, thinking, waiting for confirmation, acting through the phone, or disconnected.
6. **Small memory, user-owned.** Remember explicit preferences only, explain why a preference was used, and make deletion easy.
7. **Recover honestly.** A failed handoff is better than a confident lie. Say what failed and give the next best option.
8. **Character consistency over randomness.** Fuji can be playful, but the voice, words, expressions, and boundaries must feel like one character.
9. **Design for the real body.** Magnet strength, clothing, hair, rain, heat, and one-handed removal are product requirements, not late hardware details.
10. **Build the smallest lovable loop first.** Food choice plus navigation is the first loop. Translation, reminders, and richer actions follow evidence of repeat use.
11. **Treat the phone as the media hub.** Use Fuji's BLE link for control and state; route private speech and music through the phone unless direct audio support is verified separately.

### 2.4 Product scope

#### P0 - first lovable prototype

- Wake-word or push-to-talk voice interaction through the XiaoZhi voice base;
- expressive face or LED fallback with idle, listening, thinking, success, error, and quiet states;
- touch-to-wake, touch-to-cancel, and a hardware or software mute state;
- phone hotspot pairing and visible connection status;
- food-choice flow with location, distance, budget, dietary constraint, and known preference inputs;
- spoken recommendation of two or three nearby options;
- explicit confirmation and a phone-side map handoff;
- short route prompts while a route is active, with the phone remaining the source of truth;
- a BLE phone control channel with visible output-route status; the P0 prototype must not claim direct Fuji-to-earphone audio;
- basic offline personality responses and a clear disconnected message;
- a shoulder/bag magnetic mount prototype with a retention test.

#### P1 - useful companion expansion

- short phrase translation;
- simple reminders and read-back confirmation;
- explicit preference memory and deletion;
- quiet mode with haptic and face-only feedback;
- private Fuji responses through the phone's connected earphones, with music duck/pause and restore behavior;
- basic play/pause/next music commands through supported phone media APIs;
- orientation-aware idle expressions and a gentle "welcome back" response;
- a phone companion screen for setup, permissions, transcript visibility, and privacy controls.

#### P2 - only after validation

- multiple character skins or voice packs;
- richer gesture or small body movement;
- group voting and shared preference profiles;
- additional external-service integrations;
- accessory ecosystem and creator customization.

### 2.5 Explicit non-goals

- Guaranteed turn-by-turn navigation or emergency guidance;
- unrestricted control of arbitrary phone apps;
- always-on cloud recording;
- medical, therapeutic, or crisis advice;
- a fully independent cellular product in the first hardware revision;
- a large display or text-heavy interface on the device;
- a subscription gate around basic companionship or core interaction.

---

## 3. Ideate

### 3.1 Concept directions considered

| Direction | Strength | Risk | Decision |
|---|---|---|---|
| Desk pet | Easier battery, audio, and enclosure | Does not travel with the user or help during ordinary transitions | Keep as a charging/idle use case, not the main form |
| Shoulder fairy | Distinctive, personal, portable, and aligned with the brief | Magnet safety, public attention, and battery become central | **Select as the primary concept** |
| Wrist assistant | Easy to reach and see | Competes with watches and feels like another screen | Keep only as an alternative mounting accessory |
| Phone-only character | Fastest software validation | No physical attachment or "alive" feeling | Use for Wizard-of-Oz and service prototyping |
| Plush keychain with speaker | Familiar and emotionally safe | Limited input/output and weak helper identity | Borrow softness and accessory language, but add active states |

### 3.2 Selected concept: the shoulder fairy

Fuji is a rounded, palm-sized "soft dumpling" with a simple face. It sits on a magnetic base attached to a shoulder strap, shirt, bag, or lanyard. The body should look like a character first and an electronics enclosure second. The object can be removed with one hand and placed on a desk or charging dock.

The face is intentionally simple. Two eyes, a mouth or blush state, and a small status element are enough to communicate listening, thinking, success, and error. A small screen or LED matrix is preferable for expressive tests, but a light ring plus voice/haptics is an acceptable hardware fallback.

### 3.3 Why food choice is the anchor loop

Food choice is frequent, low stakes, personal or social, and easy to evaluate. It exercises location, preference memory, external search, natural language, confirmation, and navigation handoff in one coherent task. It also makes Fuji's role clear: Fuji proposes; the user or small group decides.

Translation and reminders remain important, but they are secondary until the food loop demonstrates that users carry Fuji and trust its recommendations.

### 3.4 Feature prioritisation test

For every proposed feature, ask:

1. Does it solve a recurring moment for Lin?
2. Can it be completed without looking at a screen?
3. Can the user tell what Fuji is doing and undo it?
4. Does it strengthen the companion identity rather than add generic assistant surface area?
5. Can we test it without committing to a fragile deep integration?

Features that fail two or more questions stay out of P0.

---

## 4. Experience design

### 4.1 Character direction

Fuji is soft, curious, and lightly mischievous. It is not hyperactive. It can make a tiny joke after a successful action, but it should become concise when the user is walking or asks for quiet.

#### Character rules

- Speak in short, concrete sentences first; add personality after the useful content.
- Use gentle self-reference (for example, "Fuji found two nearby places") without pretending to be human.
- Never guilt the user for ignoring, muting, or correcting it.
- Never claim to have feelings, private intentions, or consciousness.
- Treat a correction as useful information, not rejection.
- Match the user's language and speech rate where the voice system supports it.
- Do not use a loud catchphrase on every wake; repetition becomes irritating.

#### Example voice style

| Situation | Useful response | Expression |
|---|---|---|
| Wake | "I'm here. What are we solving?" | Eyes open, soft pulse |
| Thinking | "One tiny moment. I'm checking nearby options." | Eyes glance, slow pulse |
| Recommendation | "Two good fits: noodle shop, 6 minutes; rice bowl, 8 minutes. Want a closer look?" | Curious eyes |
| Confirmation needed | "Start walking to the noodle shop?" | Eyes hold, confirmation pulse |
| Success | "Route sent. I'll tell you when we're close." | Bright smile, short chime |
| Corrected | "Got it: no spicy food. I'll remember that for this search." | Small nod |
| Failure | "I lost the map connection. I can repeat the address, or we can try again." | Concerned face, no alarm |
| Quiet mode | "Quiet mode on. I'll use light and touch." | Dim face, small acknowledgement |

### 4.2 Interaction channels

| Channel | Primary use | Constraints |
|---|---|---|
| Voice | Natural requests, recommendations, translation, reminders | Noise, privacy, accents, and network dependency |
| Touch | Wake, cancel, repeat, mute, reassurance | Must be learnable without a screen; use distinct press durations |
| Face / light | State and emotion | Must not imply listening when Fuji is not listening |
| Haptic | Quiet acknowledgement, warning, confirmation | Keep patterns short and distinguishable |
| Orientation / motion | Idle animation, arrival, removal, fall detection | Never use motion alone for an external action |
| Phone companion | Pairing, permissions, map UI, transcript, data controls | Should be a setup and recovery surface, not required for every exchange |
| Earphones through phone | Private Fuji responses and route prompts | Phone must verify the earphone route; Fuji must not unexpectedly speak aloud if the route disappears |
| Music player through phone | Play, pause, next, and restore after a Fuji response | Only supported platform media APIs; do not promise arbitrary app control |

### 4.3 State model

Fuji uses a small, explicit state machine. Every state has a visual/audio fallback so the user can understand what is happening.

| State | Entry | Face/light | Audio | Allowed actions |
|---|---|---|---|---|
| `IDLE` | Startup, completed task, timeout | Gentle breathing animation | None | Wake, touch, mute |
| `LISTENING` | Wake word or press-to-talk | Listening indicator stays visible | Captures request | Cancel, finish speech |
| `THINKING` | Speech captured | Slow thinking animation | Optional short acknowledgement | Cancel |
| `CLARIFYING` | One required constraint is missing | Eyes focus on user | Asks one question | Answer, cancel |
| `CONFIRMING` | External or consequential action is ready | Held confirmation face | Repeats destination/action | Confirm, reject, edit |
| `ACTING` | Phone bridge or service call started | Progress animation | Short progress message | Cancel where supported |
| `SUCCESS` | Action completed and verified | Smile/celebration under 1.5 s | Result and next step | Repeat, idle |
| `ERROR` | Service, permission, or input failure | Concerned but calm | Cause plus fallback | Retry, repeat, open phone |
| `QUIET` | User toggles quiet mode | Dim face or haptic only | No device speaker; earphone audio only if the phone route is verified | Touch or phone to exit |
| `MUTED` | Hardware mute | Clear muted indicator | No capture | Hardware unmute only |
| `DISCONNECTED` | Hotspot or bridge unavailable | Offline face | Local responses only | Reconnect, phone setup |

Rules:

- `LISTENING` must never be visually indistinguishable from `IDLE`.
- `CONFIRMING` must precede map launches, reminders that will be saved, or any purchase/booking action.
- A timeout returns to `IDLE` or `QUIET`; it does not keep the microphone open.
- A hardware mute overrides all software modes and cannot be undone by voice.

#### Output route states

The phone bridge tracks the output route independently from Fuji's interaction state:

| Route | Meaning | Safe fallback |
|---|---|---|
| `DEVICE_SPEAKER` | Fuji may speak through its own speaker | Use only when normal volume is enabled |
| `PHONE_EARPHONES` | The phone has verified an active earphone route | Send Fuji audio privately and use the phone's audio focus rules |
| `FACE_HAPTIC` | No spoken output is allowed or available | Show state through face and haptic patterns |

`PHONE_EARPHONES` is never assumed from a previous session. If the phone reports a disconnect or route change, Fuji falls back to `FACE_HAPTIC` or asks for permission before using the device speaker.

### 4.4 Key flow A: first-time setup

1. User powers on Fuji; the face shows a short welcome animation and a pairing code.
2. User opens the Fuji companion app or an onboarding link and selects Fuji.
3. Fuji advertises a local setup channel over BLE or the supported XiaoZhi pairing mechanism. The phone supplies the hotspot credentials without showing them on Fuji.
4. App explains microphone/listening state, data retention, map handoff, and hardware mute before requesting permissions.
5. User chooses language, voice volume, quiet-mode default, output route, and whether Fuji may remember explicit food preferences.
6. If earphones are connected, the app tests a short private response and reports whether music was paused or ducked and restored. If no earphones are present, this test is skipped rather than treated as a failure.
7. User tests wake, cancel, touch, mute, and one sample recommendation.
8. App reports "ready" only after a real round-trip voice test succeeds. If network setup fails, Fuji remains usable for local expressions and gives a concrete recovery step.

**Acceptance criteria:** a first-time user can complete setup without developer help; the app never displays a success state before Fuji can hear and respond; a failed hotspot leaves no confusing half-paired device.

### 4.5 Key flow B: "What should we eat?"

1. User says, "Fuji, what should we eat?"
2. Fuji enters `LISTENING` and asks only for missing context: "How far can we walk, and any food to avoid?"
3. Phone bridge obtains approximate location, current time, and opted-in preference memory. The user may answer "ten minutes, no spicy food, under 50."
4. Recommendation service filters for open or likely-open places, distance, budget, cuisine constraints, and any small-group preferences. Fuji receives a small ranked result, not an unbounded search page.
5. Fuji speaks at most three options. Each option includes name, approximate walking time, price cue, and one reason it fits. The phone may show details, but the voice exchange remains complete without it.
6. User says "the first one," "show another," or a correction such as "closer." Fuji updates the shortlist.
7. Fuji asks, "Start walking to [place]?" and enters `CONFIRMING`.
8. On confirmation, the bridge opens the supported map app with the destination. The phone reports whether the handoff succeeded.
9. Fuji says only what is verified: "Route sent" or "I could not open the map. The address is [address]."
10. During the walk, Fuji remains quiet until a route event arrives or the user asks. It gives short prompts and returns to `IDLE`.
11. At completion, Fuji offers "Remember that you liked this kind of place?" The default is no memory without explicit consent.

**Failure cases:** no location permission, no nearby result, stale opening data, map app missing, bridge disconnected, or disagreement with a close friend. Each case gives a fallback such as repeating the address, offering a random choice from the returned list, or asking the user to open the phone.

### 4.6 Key flow C: silent outside with earphones and music

1. The user pairs Fuji to the phone through BLE. The user pairs earphones to the phone using the phone's normal Bluetooth settings; there is no direct Fuji-to-earphone pairing in this flow.
2. The Fuji app shows two independent statuses: `Fuji connected` and `Earphones connected`. A phone-to-earphone audio route is not reported as ready until the operating system confirms it.
3. The user selects private audio and chooses how music should behave: pause during Fuji speech, duck volume, or leave music playing only when the response is face/haptic-only.
4. While outside, the user asks Fuji for a recommendation, translation, reminder, or route prompt. Fuji sends the intent over BLE; the phone bridge handles the service and routes the response to the earphones.
5. Before speaking, the phone app requests audio focus or uses the supported media-control mechanism. It pauses or ducks the active music player only according to the user's setting, then restores it after the response.
6. Fuji gives a face/haptic acknowledgement so the user can tell that a private response is being delivered. Playback commands such as play, pause, and next are sent through supported phone media APIs, not directly to an arbitrary music application.
7. If the earphones disconnect, Fuji does not suddenly speak sensitive content through its own speaker. It uses `FACE_HAPTIC`, asks the user to reconnect, or waits for an explicit request to use the device speaker.

**Acceptance criteria:** the user can tell whether Fuji and the earphones are connected; a short Fuji response is audible privately; music resumes only when expected; and a route failure never causes an unexpected public announcement.

**Scope note:** phone OS audio focus, background execution, media-player permissions, and earphone multipoint behavior vary. Start with one supported phone platform and one reference earphone pair before broadening compatibility.

### 4.7 Key flow D: translation

1. User says, "Fuji, translate this," then speaks or plays a short phrase.
2. Fuji identifies source and target language if possible; otherwise asks one clarification.
3. Fuji gives the translation at a selectable speed and offers one repeat.
4. Fuji labels uncertainty or missing context instead of presenting a guess as authoritative.

Translation is P1 because language detection and audio quality need dedicated testing. It should initially support short phrases rather than long simultaneous interpretation.

### 4.8 Key flow E: reminder

1. User says, "Fuji, remind me to submit the form at 8 tonight."
2. Fuji repeats the action and time: "Reminder at 8:00 PM today: submit the form. Save it?"
3. On confirmation, the phone bridge creates the reminder and reports the result.
4. At reminder time, Fuji uses the user's current mode: voice in normal mode, face/haptic in quiet mode, and no reminder claim if the phone bridge was unavailable.

Reminders must be idempotent. A retry must not silently create duplicates.

### 4.9 Key flow F: emotional check-in

If the user says they are having a bad day, Fuji can acknowledge and offer a small choice: "That sounds heavy. Want quiet company, a tiny distraction, or help with one next step?" It should not diagnose, make promises, or imply that it can replace human or professional support. If the user expresses imminent danger or self-harm, Fuji should encourage contacting local emergency or crisis support and a trusted person, using the phone for location-specific help when available.

### 4.10 Play and "sell cute" loop

The playful layer is not a separate game. It is a low-cost reward around useful moments:

- head tap while idle: a brief face change or tiny sound;
- tilt: Fuji "leans" through an eye animation, but does not speak unless invited;
- successful decision, alone or with others: a short celebration under 1.5 seconds;
- long idle: a subtle self-contained animation, never an unsolicited call for attention;
- user says "show me cute": one short, interruptible action.

Every animation has a quiet-mode equivalent and a maximum duration. Cute behaviour must not block an urgent cancel or an external-action confirmation.

---

## 5. Industrial and interaction design

### 5.1 Form target

The first physical prototype should target a rounded body roughly 45-60 mm across, with a soft or satin outer surface, a stable magnetic back, and a mass that remains comfortable on light clothing. Exact dimensions and mass must be validated with a dummy before electronics are packaged.

The shape should have:

- a front face readable at arm's length;
- a top or front touch area that can be found without looking;
- a tactile mute switch with a clear physical position;
- a speaker opening that faces the wearer rather than the street;
- microphone openings protected from clothing and hair;
- a bottom or rear charging interface that works when Fuji is docked;
- no sharp corners, exposed pinch points, or decorative pieces that can detach.

### 5.2 Magnetic mount safety

The magnet is a core interaction and a failure risk.

- Use a two-part mount: a body magnet and a clothing/strap backplate with a broad load area.
- Test cotton, denim, knitwear, bag straps, and thin synthetic fabric separately.
- Test walking, stairs, bending, a controlled snag, and a short drop. Define a retention threshold before user trials.
- Offer a low-profile tether for public trials and an alternative clip mount for users who do not trust magnets.
- Make removal a deliberate one-handed slide or lift, not a forceful pull that stretches fabric.
- Document magnetic interference warnings for cards, medical devices, and sensitive equipment.

### 5.3 Indicative hardware blocks

| Block | Role | Prototype decision |
|---|---|---|
| DNESP32S3 / ESP32-S3 N16R8 | Main control, local state, sensor and audio coordination | Use the existing PlatformIO target and PSRAM configuration; isolate Fuji features behind a hardware adapter |
| XiaoZhi voice base | Existing voice transport and assistant foundation | Treat as the voice baseline; confirm its audio, wake-word, and network hooks before customising |
| Microphone array or digital mic | Voice capture | Test placement on worn clothing, not only on a bench |
| Speaker | Spoken response and route prompts | Tune toward near-field intelligibility at low volume |
| Face display or LED matrix | Expressions and state visibility | Prototype both a tiny display and light-only fallback |
| Capacitive touch or force sensor | Wake, cancel, reassurance, mode changes | Use press duration patterns that do not require precise aiming |
| IMU | Tilt and movement effects; possible removal/drop detection | Do not infer a user command from motion alone |
| Haptic motor | Quiet feedback | Keep patterns distinguishable and low power |
| BLE radio / GATT link | Low-power control and status link to the phone | Use for Fuji-to-phone commands; do not treat it as an A2DP audio source |
| Battery, charge IC, fuel gauge | Wearable power | Measure real idle/listening/acting profiles before promising runtime |
| Magnet and mount | Wearability | Prototype independently before final enclosure |

The final sensor list should follow observed value. A camera is not part of P0: it adds privacy, power, and enclosure complexity while the brief can be fulfilled with voice, touch, and orientation.

### 5.4 Power and charging concept

Fuji should have an idle mode, a listening mode, an acting/network mode, and a low-battery mode. It should warn before the battery becomes unusable and preserve the last safe state through a restart. A charging dock is preferable because it reinforces the desk-pet identity, but a standard cable is required for the prototype.

Do not publish an hours-per-charge promise until measurements exist for:

- an ordinary day with idle animations and several short interactions;
- a day with repeated voice searches and route prompts;
- poor network conditions, which can keep radios active longer;
- quiet mode and muted mode.

### 5.5 Accessibility

- Push-to-talk or touch-to-wake must work if wake-word recognition fails.
- Speech rate, output volume, and language must be configurable.
- Critical state changes must have a visual and haptic path, not audio alone.
- The phone app should expose transcripts or text results for users who cannot rely on audio.
- Avoid relying on colour alone for listening, mute, or error state.
- Test speech recognition with accents, background music, wind, and multiple speakers.

---

## 6. System design

### 6.1 Three-part architecture

```text
User voice / touch / motion
             |
             v
     Fuji device (ESP32-S3)
     - local state machine
     - wake / mute / touch
     - face, light, haptic, local audio
     - connection and battery status
             |
          BLE GATT
             v
       Phone companion bridge
       - pairing and permissions
       - location and map handoff
       - reminders and transcript
       - privacy and memory controls
             |
       phone Bluetooth audio route
              |
       Bluetooth earphones <--> music player
              |
       network services / XiaoZhi / search
       - speech and intent processing
       - recommendation data
       - translation
```

The exact BLE transport can follow the XiaoZhi base image and the supported phone integration path. The important boundary is that Fuji remains understandable if the phone or network fails, the phone remains the authority for external actions, and the phone remains the audio hub for earphones and music. Fuji's own speaker is a fallback output, not the direct source for Bluetooth earphone audio in P0.

### 6.2 Responsibility split

#### On-device

- Wake, mute, cancellation, touch, and haptic feedback;
- explicit state machine and visible listening state;
- audio playback and microphone control;
- local character expressions and offline phrases;
- battery, connection, and sensor monitoring;
- safe timeout and restart behaviour.

#### Phone bridge

- One-time pairing and hotspot configuration;
- permissions for location, maps, reminders, and notifications;
- map launch and handoff confirmation;
- reminder creation and duplicate prevention;
- maintaining the Fuji BLE GATT link while the phone streams audio to earphones;
- output-route verification, audio focus, music duck/pause/restore, and supported media commands;
- user-facing transcript, memory, privacy, and error recovery;
- a local cache of the last safe status and a small set of offline intents.

#### Network / assistant services

- Speech recognition and language understanding where supported;
- restaurant search and ranking;
- translation;
- response generation constrained by Fuji's character and safety rules.

No service may report an external action as complete until the phone bridge returns a verifiable result.

### 6.3 Intent envelope

Use a small, versioned message contract rather than coupling firmware to one cloud response format. The shape below is illustrative:

```json
{
  "version": 1,
  "request_id": "local-uuid",
  "intent": "food_search",
  "state": "CONFIRMING",
  "parameters": {
    "distance_minutes": 10,
    "budget_rmb": 50,
    "avoid": ["spicy"],
    "location_permission": "granted",
    "output_route": "phone_earphones",
    "music_policy": "duck_then_restore"
  },
  "requires_confirmation": true,
  "expires_at": "2026-07-13T12:30:00+08:00"
}
```

Responses should include `request_id`, `status`, a concise spoken message, a display/expression hint, and an explicit error code when applicable. The device should ignore stale or duplicate responses.

### 6.4 Food recommendation ranking

The initial ranker should be explainable and conservative:

1. Filter by available location, distance limit, dietary exclusions, and open/likely-open status.
2. Remove places with missing critical data rather than inventing values.
3. Score distance, budget fit, explicit preference match, recent user correction, and group constraints.
4. Diversify the top results so three options are not the same cuisine or chain.
5. Return a reason and a freshness timestamp when available.

The assistant may phrase a result playfully, but the underlying ranking should remain inspectable in the phone bridge for debugging.

### 6.5 Offline and degraded modes

| Condition | Fuji behaviour |
|---|---|
| Network unavailable | Local wake/touch/expressions, "I'm offline," and a retry/reconnect path |
| Phone bridge unavailable | No map/reminder claims; repeat a cached address only if freshness is clear |
| Speech recognition uncertain | Ask for a repeat or offer touch/push-to-talk; do not guess a destructive action |
| Search returns nothing | Explain that no matching result was found and relax one constraint with permission |
| Map handoff fails | State failure and provide name/address through voice and phone text |
| Earphones unavailable or disconnected | Do not speak private content through Fuji automatically; use face/haptic feedback and ask the user to reconnect or explicitly enable the device speaker |
| Music player does not expose a supported control API | Keep Fuji's response route available, but explain that play/pause/next control is unavailable |
| Battery low | Reduce animation and unsolicited behaviour; warn early; preserve mute state |
| Firmware/service restart | Return to a safe idle state; never resume listening or an external action silently |

### 6.6 Privacy and data model

The minimum useful data is a transient request, approximate location for an active search, explicit preferences, and the result of an external handoff. Default policies:

- no continuous recording;
- hardware mute disables capture regardless of software state;
- a visible indicator for active listening;
- location used for an active request and not retained by default;
- food preferences stored only after a clear "remember this" confirmation;
- transcripts visible and deletable from the phone bridge;
- no contacts, messages, or arbitrary app access in P0;
- no raw music audio passes through Fuji; the phone remains the media route and only requested playback intents are shared;
- clear network/service disclosure during setup;
- export and delete controls before any public beta.

The product should use a privacy review checklist for every new integration. "AI companion" is not a reason to collect more data than a single task requires.

---

## 7. Prototype plan

### 7.1 Prototype levels

#### Prototype 0: experience only

Use a phone, a printed or foam body, and a researcher operating a hidden assistant. Test the meal-decision script, social reactions, voice length, expressions, and quiet-mode expectations. Do not spend time on enclosure polish.

#### Prototype 1: physical interaction

Connect the ESP32-S3 to the XiaoZhi voice base or a controlled voice stub. Add touch, a mute switch, an LED/face, haptic feedback, and a printed mount. Test the state machine and physical wearability with no real external automation required.

#### Prototype 2: service bridge

Add a minimal phone bridge with hotspot status, location permission, one supported map handoff, a fake or constrained restaurant data source, and one reference earphone route. Measure end-to-end latency, audio focus, music duck/restore, and failure recovery.

#### Prototype 3: integrated pilot

Use the real recommendation path, route handoff, preference memory, battery measurements, and production-like enclosure. Run a one-week field trial with explicit consent and a clear data deletion path.

### 7.2 Experiment backlog

| Hypothesis | Test | Pass signal | Failure response |
|---|---|---|---|
| Users will carry a shoulder companion | One-week field trial with a wearable dummy | At least 4 active days in week two for most participants | Test bag/desk mount or reposition the target context |
| Food choice is a repeatable anchor | Compare three meal decisions per participant | Most users complete a flow and name a next use | Revisit the highest-frequency micro-decision |
| Voice or touch beats phone operation when energy is low | Time matched phone and Fuji tasks alone and in small groups | Lower effort and comparable success | Shorten prompts; add push-to-talk; narrow task |
| Expressions increase reuse | A/B test neutral versus character responses | Character version improves delight without increased interruption | Reduce animation frequency or rewrite personality |
| Quiet mode protects public comfort | Trial normal, quiet, and haptic-only modes | Users can identify state and choose mode without help | Improve indicators and tactile controls |
| Phone can keep Fuji and earphones usable together | Test Fuji BLE control while the phone streams music to a reference earphone pair | Fuji response is private, music behavior matches the chosen policy, and disconnects fall back safely | Narrow supported OS/device matrix or use face/haptic-only output |
| Explicit memory is trusted | Offer save/delete after food flow | Users understand and can change the preference | Remove implicit learning and simplify wording |
| Map handoff is understandable | Inject permission and app failures | Users know whether route launched and what to do next | Add phone status screen and stronger confirmation |

### 7.3 Moderated usability script

1. Give the participant Fuji and no feature explanation beyond "ask it for help."
2. Ask them to prepare Fuji for an outing.
3. Ask them to resolve a meal decision alone or with one trusted person, using one constraint.
4. Introduce a correction ("closer," "no spicy food") and observe whether they know how to recover.
5. Trigger a map handoff and then a simulated failure.
6. Pair reference earphones to the phone, play music, and test a private Fuji response, then disconnect the earphones.
7. Ask them to turn on quiet mode and mute.
8. Let them use Fuji freely for ten minutes.
9. Ask the one test question from `TargetUser_Persona.md` and record the next recurring moment they name.

Do not coach the participant toward the intended gesture. Record the first attempt, the words they use, the state they think Fuji is in, and where they look for reassurance.

### 7.4 Minimum telemetry for the pilot

Collect only what is necessary and disclose it:

- request type and success/failure code;
- elapsed time between states;
- disconnect and retry count;
- Fuji BLE, phone audio-route, and earphone-route state transitions;
- audio-focus result, music policy selected, and whether music restored after a response;
- whether the user confirmed, rejected, or corrected a suggestion;
- battery level at start and end of a session;
- optional user-rated delight, interruption, and trust scores.

Do not retain raw audio by default. A separate, explicit consent flow is required for recorded research sessions.

---

## 8. Build and validation roadmap

### Phase 0 - align (1-2 weeks)

- Confirm XiaoZhi audio/network hooks and the ESP32-S3 board configuration;
- create the state machine and message-envelope stubs;
- interview target users and run the phone-only Wizard-of-Oz;
- decide the first supported phone platform, map handoff path, and reference earphone pair.

**Exit:** the team can demonstrate the food flow with a researcher behind the scenes and has evidence for the first physical mount.

### Phase 1 - feel (2-4 weeks)

- Implement local state feedback, touch, mute, and expressions;
- build three printed body/mount variants;
- measure audio, wake, battery, and magnetic retention in realistic positions;
- test character scripts, quiet mode, and private earphone output with target users.

**Exit:** users can understand all states and wear the object for an outing without coaching.

### Phase 2 - help (3-6 weeks)

- Implement phone pairing and connection recovery;
- add constrained food search and one map handoff;
- add the phone-earphone output route, music policy, and basic supported media controls;
- implement confirmation, stale-result handling, and error states;
- collect pilot telemetry with explicit consent.

**Exit:** the end-to-end meal flow completes reliably enough for a one-week pilot, including simulated failures.

### Phase 3 - habit (2-4 weeks)

- Add explicit preference memory, reminders, and translation as separate modules;
- improve battery and charging experience;
- run week-two and week-four retention checks;
- revise hardware based on wear, snag, rain, and attention feedback.

**Exit:** the product has at least two repeatable jobs and a justified P1 roadmap.

### Phase 4 - harden

- Security and privacy review;
- accessibility and noisy-environment testing;
- manufacturing and enclosure review;
- documentation for setup, data deletion, support, and known integration limits.

**Exit:** no unresolved P0 safety, privacy, or misleading-success defects remain.

---

## 9. Definition of done for the first release

### Experience

- A target user can wake Fuji, ask for food, answer one clarification, choose an option, and confirm navigation without a tutorial.
- Fuji's character is recognisable but does not delay the useful answer.
- Every external action has a confirmation and a verified result.
- Quiet mode, hardware mute, cancellation, and failure recovery are discoverable.

### Hardware

- The mount survives the defined walking, stairs, snag, and drop tests for the target clothing set.
- The body can be removed one-handed and does not create sharp edges or exposed pinch hazards.
- Microphone and speaker remain usable when worn, not just when held.
- Battery measurements support the published usage claim.

### Software and service

- Device, phone bridge, and service messages are versioned and reject stale responses.
- Reconnect, timeout, low battery, and restart paths return to a safe state.
- Map and reminder operations are idempotent and report actual completion.
- The phone can maintain the Fuji BLE link while streaming Fuji responses and music to the reference earphones; route loss falls back without an unexpected public announcement.
- Music pause/duck/restore behavior is explicit, user-configurable, and limited to supported phone media APIs.
- No raw audio is retained by default, and user data can be reviewed/deleted.

### Validation

- The one-week pilot includes target users, not only makers or team members.
- The team records a real recurring use case, not just a novelty reaction.
- Food recommendations have a reason, a freshness boundary, and a fallback when data is incomplete.
- Results are compared with the persona measures: repeat use, interruption, relevance, and trust.
- Silent-use results include private audibility, route reliability, music restoration, and safe behavior after earphone disconnect.

---

## 10. Risks and open decisions

| Risk / open decision | Why it is unresolved | Next decision test |
|---|---|---|
| Exact map integration path | Different phones and regions expose different handoff APIs | Select one supported phone path for P0 and document the fallback |
| Phone and earphone simultaneous Bluetooth behavior | OS audio focus, background limits, Bluetooth profiles, and earphone multipoint differ by model | Select a reference phone/earphone pair, test Fuji BLE plus music streaming, and publish the supported matrix |
| Direct Fuji-to-earphone audio | The current ESP32-S3/XiaoZhi target is planned as a BLE control endpoint, not a verified A2DP/LE Audio source | Keep phone-hub topology; change hardware only if direct audio becomes a validated requirement |
| XiaoZhi customisation boundary | The prebuilt base image may limit local state or audio hooks | Inspect and test supported extension points before firmware architecture is fixed |
| Face hardware | Display is expressive but costs power and enclosure depth; LEDs are simpler | Compare recognition and delight with a tiny display versus light-only body |
| Battery size versus shoulder comfort | More capacity increases mass and changes the mount | Test dummy weights and measure real workload before selecting a cell |
| Magnetic force | Stronger retention can affect comfort and safety | Run mount matrix across clothing, straps, and controlled snags |
| Character voice | A voice can feel too childish, too generic, or culturally awkward | Co-write and test scripts with the primary persona before recording or tuning a voice |
| Cloud cost and latency | Long responses or unreliable network destroy the hands-free promise | Set a response-latency budget and prototype a constrained local fallback |
| Memory model | Implicit learning may feel invasive | Start with explicit save/delete only; add learning only with evidence |
| Public privacy | Shoulder placement may capture nearby speech | Hardware mute, visible listening, short capture windows, and a public-use study |

### Final design stance

Fuji should feel like a little fairy that happens to be useful, not a generic assistant hidden inside a mascot. The design succeeds when Lin can keep it nearby through an ordinary day, alone or with trusted people, ask for help in a natural sentence, understand what Fuji is doing, and feel a small amount of warmth when the task is over. The engineering plan should protect that loop: dependable states, honest handoffs, safe mounting, discreet modes, and a narrow first release are what allow the character to become lovable rather than disposable.
