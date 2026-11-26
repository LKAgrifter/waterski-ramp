# Notes for Claude Code

Firmware for a motorized waterski ramp: ramp controller + up to 4 remotes, Heltec WiFi LoRa 32 V3 (ESP32-S3).
Read `docs/DEVELOPMENT.md` first: hardware facts, safety rules and the reasons behind them.

## Hard requirements
- **The sketches must stay plain Arduino IDE sketches.** Future maintainers use the Arduino IDE, not VS Code.
  Keep each sketch self-contained in its folder (`firmware/Ramp_Control/`, `firmware/Ramp_Remote/`), `.ino` tabs
  only, no build-system-only features, no extra libraries beyond the Heltec board package + "Heltec ESP32
  Dev-Boards" library. Follow the "Editing the sketches safely" rules in DEVELOPMENT.md (forward declarations for
  custom-type parameters, shared constants in the main tab).
- `Games.ino`, `Radio_Security.ino` and `Boot_Animation.ino` are identical copies in both sketch folders. Edit one, copy to the other.
- Radio packets are encrypted with a key from the bind phrase (`Radio_Security.ino`, DEVELOPMENT.md "Radio protocol").
  Never put a real bind phrase in the repo (`bind_phrase.h` is gitignored): the repo is public.
- Radio packet layouts are shared: change `TelemetryPacket` / `CommandPacket` in both sketches together.
- Safety behaviour is deliberate (see DEVELOPMENT.md "Behaviour and safety rules"). In particular: the direction
  relay must never switch while motor power is on; no sensor at power-up = CAL REQUIRED; heights only settable on
  the SET HEIGHT page; relays are HIGH-triggered (`RELAY_ON = HIGH`); height sensors are NC
  (`LIMIT_ACTIVE_LEVEL = HIGH`). Don't change these without the owner agreeing.

## Testing
- `python3 tests/host/run_tests.py` (add `--quick` to skip the slow games fuzz). Run it after every change and
  add a scenario test for new behaviour. Tests live in `tests/host/tests/`; the PC stand-ins for Arduino/Heltec
  libraries are in `tests/host/stubs/`.
- `python3 tests/host/run_tests.py --trace` prints a millisecond relay trace for manual UP/DOWN.
- The host tests are not the ESP32 compiler. When possible also compile with arduino-cli
  (board `Heltec-esp32:esp32:heltec_wifi_lora_32_V3` - confirm with `arduino-cli board listall heltec`) and test on
  the real boards.

## Builds and flasher
- `.github/workflows/firmware.yml` compiles both sketches with arduino-cli and publishes `flasher/` (ESP Web Tools)
  to GitHub Pages (https://lkagrifter.github.io/waterski-ramp/). It also compile-checks the `SIM_MODE` build. Pin
  `CORE_VERSION` / `LIBRARY_VERSION` to the versions the owner uses (CI currently gets the latest).
- Remote ID is a saved setting (NETWORK page, hold UP + DOWN 2 s), not a code constant. So is each remote's water mode
  (BOAT = keep-alive water / TIMER = taps like the ramp; WATER page, hold UP + DOWN 2 s).

## Related repo
The PC simulator / 3D / AR showcase lives in the separate `waterski-ramp-sim` repo, normally cloned next to this
one. The firmware side of it is `SIM_MODE` (main tab, default false) and `Sim_Mode_Ramp.ino` in THIS repo:
height sensors from USB, relay states to USB, relay pins never switched. Its serial protocol is shared with the
simulator page: change both repos together and bump `SIM_PROTOCOL_VERSION`. Test: `tests/host/tests/sim_mode.cpp`.
The simulator's controller emulator is this repo's `Ramp_Control` sketch compiled to WebAssembly: after changing
it, rebuild the emulator in the sim repo (`python emulator/build.py`, then commit `web/js/firmware_emu.js` there).
A new library call in the sketch needs a stand-in in the sim repo's `emulator/stubs/` as well as `tests/host/stubs/`.
