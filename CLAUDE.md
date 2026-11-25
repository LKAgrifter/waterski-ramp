# Notes for Claude Code

Firmware for a motorized waterski ramp: ramp controller + up to 4 remotes, Heltec WiFi LoRa 32 V3 (ESP32-S3).
Read `docs/DEVELOPMENT.md` first: hardware facts, safety rules and the reasons behind them.

## Hard requirements
- **The sketches must stay plain Arduino IDE sketches.** Future maintainers use the Arduino IDE, not VS Code.
  Keep each sketch self-contained in its folder (`firmware/Ramp_Control/`, `firmware/Ramp_Remote/`), `.ino` tabs
  only, no build-system-only features, no extra libraries beyond the Heltec board package + "Heltec ESP32
  Dev-Boards" library. Follow the "Editing the sketches safely" rules in DEVELOPMENT.md (forward declarations for
  custom-type parameters, shared constants in the main tab).
- `Games.ino` is an identical copy in both sketch folders. Edit one, copy to the other.
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
  to GitHub Pages. It has not been run yet: check it on the first push and fix if needed. Pin `CORE_VERSION` /
  `LIBRARY_VERSION` to the versions the owner uses.
- Remote ID is a saved setting (NETWORK page, hold BACK 2 s), not a code constant.

## Related repo
The PC simulator / 3D / AR showcase lives in the separate `waterski-ramp-sim` repo, normally cloned next to this
one. A firmware "simulation mode" for it (limit switches from USB, relay states to USB) belongs in THIS repo,
behind a compile-time flag that defaults to off.
