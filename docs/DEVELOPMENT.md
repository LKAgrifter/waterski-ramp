# Development notes

How the firmware works, the hardware facts it depends on, and why things are the way they are.
Read this before changing behaviour. Settings are named constants near the top of the main sketches.

## System overview

- **Ramp controller** (`firmware/Ramp_Control`): drives the lift motor through a power relay and a direction
  relay, switches the ramp's water pump, reads four magnetic height sensors (4.0 / 4.5 / 5.0 / 5.5 ft), has its own
  buttons and OLED menu, and is the single source of truth for everything.
- **Remotes** (`firmware/Ramp_Remote`, up to 4): same board, buttons and menu layout. A remote never decides
  anything: it sends requests and shows what the ramp reports.
- **Radio:** LoRa point-to-multipoint, 915 MHz, SF8, 125 kHz, CR 4/6, 21 dBm.

### Radio protocol (`LORA_Coms_Ramp.ino` / `LORA_Coms_Remote.ino`)

| Packet | ID | Direction | When |
|---|---|---|---|
| `TelemetryPacket` (19 bytes) | `0xA1` | ramp -> all remotes | every 250 ms (4 Hz), plus an immediate reply to each command |
| `CommandPacket` (8 bytes) | `0xB2` | remote -> ramp | on a button action, keep-alives, and every ~150 ms while a manual button is held |

- Telemetry carries heights (as indexes 0-3), water state and minutes left, motor state, faults, the limit-switch
  mask and an ACK (`ackRemoteId`, `ackSeq`, `ackResult`) answering the last command.
- Commands carry a sequence number (`seq`, 0 = nothing to ACK). The ramp ignores repeats of a seq it has already
  handled, so remotes can resend until they see the ACK.
- `waterCommand`: -1 ignore, 0 off, 1 on (BOAT remote: 1 h timer kept alive), 2 keep-alive (heartbeats from a BOAT
  remote while the water is on; only refreshes water a BOAT remote started), 3 timer tap (TIMER remote: +1 h,
  exactly like a tap on the ramp's Water page). A ramp without TIMER support just ignores 3.
- **Manual drive from a remote is level-triggered:** `remoteControlMode` (1 = up, 2 = down) must be repeated in
  every packet. If the ramp hears nothing for `REMOTE_MANUAL_TIMEOUT_MS` (1 s) it stops (dead-man). Releasing the
  button sends `MANUAL_STOP_REPEATS` "stop" packets so it normally stops within one packet.
- `txCounter` on both sides lets each end measure packet loss (NETWORK page).
- **Packet layouts must match on all units.** Change both sketches together and reflash everything.

## Hardware facts the code depends on

| Item | Detail |
|---|---|
| Board | Heltec WiFi LoRa 32 V3 (ESP32-S3, SX1262, 128x64 SSD1306 OLED) |
| Relays (ramp) | **High-level triggered** (HIGH = on). Power relay GPIO 2, direction relay GPIO 3, pump relay GPIO 1. Low-level boards powered at 5 V don't switch off from a 3.3 V pin - that's why. One setting: `RELAY_ON` / `RELAY_OFF`; every write goes through `setRelay()`. |
| Direction relay | Energized = UP, released = DOWN. |
| Pump relay | Its NC contact is wired into the motor supply, so the motor can only run while the pump relay is off (hardware interlock and a "ramp is moving" indicator). |
| Height sensors (ramp) | Magnetic, **normally closed**, wired pin-to-GND with INPUT_PULLUP: HIGH = at that height. GPIO 34 (4 ft), 33 (4.5), 47 (5), 48 (5.5). Setting: `LIMIT_ACTIVE_LEVEL`. A broken wire reads as "triggered" (caught as a sensor fault when two read on). |
| Buttons (both units) | UP GPIO 7, DOWN GPIO 6, MENU/ENTER GPIO 38, BACK/WATER GPIO 39 (INPUT_PULLUP). |
| Height buttons | Resistor ladder (10 k) on GPIO 5. Measured ADC: none ~3200, 4.0 ft ~2980, 4.5 ~2610, 5.0 ~1960, 5.5 ~0. Thresholds halfway between, 4-sample average. |
| Ramp LEDs | GPIO 40 (4 ft), 19 (4.5), 20 (5), 26 (5.5). |
| Current sense | GPIO 4 (AC current sensor), ramp only. |
| Motor | ~135 rpm x 1 mm/rev = ~2.25 mm/s. A 0.5 ft step (152 mm) is ~68 s if jack travel equals height change. 4 -> 5.5 ft is ~3.4 min. |

## Behaviour and safety rules (decisions made on purpose)

**Menus (both units, identical):** hold MENU (0.7 s) to step HOME -> WATER -> SET HEIGHT -> MANUAL ->
DIAGNOSTICS -> NETWORK -> HOME. The screen times out after 1 minute and always returns to HOME.
- HOME shows status only. A stray height press there shows "Set height: page 3"; a BACK/WATER press shows
  "Water: page 2".
- **Height buttons only work on SET HEIGHT** (glovebox protection on the remote).
- UP/DOWN motor buttons only work on MANUAL. A press must *start* on that page (armed buttons).
- Hidden GAMES page: hold MENU + BACK together on HOME. `Games.ino` is the same file in both sketches.
- Heights show **blank** when unknown (never located, CAL REQUIRED, or no ramp link on a remote).
- **Remote ID** (1-4, must be unique per ramp) is a saved setting on each remote: NETWORK page, hold BACK 2 s to
  step it. Stored in flash (`Preferences` key `RemoteId`), so one firmware file serves every remote and
  reflashing keeps it (unless "erase device" is ticked in the flasher).
- **Water mode** is also a saved setting on each remote (`Preferences` key `WaterMode`): WATER page, hold UP + DOWN
  together 2 s to switch; the page title shows it.
  - **BOAT** (default, the original behaviour): BACK toggles. "On" is a 1 h timer that the remote's heartbeats keep
    refreshing, so the water stops within an hour of the remote being switched off or going out of range. For the
    remote that's always powered while its boat is in use.
  - **TIMER**: BACK works like the ramp's own Water page: tap = +1 h (1..12 h), the tap after 12 h = off, hold 1 s =
    off. It sends no keep-alives, so the water runs for the time set even if the remote sleeps or goes away. For
    a shore remote on solar + battery that sleeps between presses. Each tap is its own command (applied once per
    seq); taps made while one is in flight are queued, and dropped if the ramp stops answering.
  - On the ramp, timer water (from the ramp or a TIMER remote) is a fixed timer that BOAT keep-alives never
    stretch. The ramp's Water page shows who set it ("Timer by: REMOTE n").

**Position knowledge:** the ramp only *knows* its height while sitting on a sensor. `Ramp_Height` updates only
when a sensor triggers. `Pos_Side` (-1/0/+1) records which side of that sensor the ramp moved to, so picking the
height it just left drives back to it.

**Startup / calibration (`diag.powerLoss`, shown as CAL REQUIRED):**
- No sensor active at power-up -> CAL REQUIRED. The ramp may have been moved while off; the saved height is not
  trusted. Also on a power cut mid-move and on first boot after flashing.
- While CAL REQUIRED, automatic and remote height moves are locked out. Manual drive at the ramp still works; the
  first sensor reached clears it.
- Two sensors active at once -> height sensor fault, automatic moves stop.

**Motor sequencing (`Motor_and_Failsafes_Ramp.ino`):**
- Start: pump relay off and direction relay set, wait `Relay_Settle_Delay` (100 ms), then power relay on.
- Stop: power relay off, direction relay released `DIR_RELAY_DROP_MS` (100 ms) later. The direction relay is only
  energized while driving up, so it never sits drawing power.
- Reversal: the motor must have been stopped `Change_Delay` (1 s), counted from when it stopped. Power is never
  applied while a direction change is pending. **The direction relay must never switch under load** (an early
  wiring mix-up did exactly that and damaged one).
- Water: if the water is on, the pump relay drops out before motor power and comes back on
  `WATER_RESUME_DELAY_MS` (1 s) after the motor last ran (also held off through reversals and jogging).

**Stops and timeouts:**
- **Stop overrun:** an automatic move keeps going `STOP_OVERRUN_MS` past the target sensor's first trigger, so it
  parks near the middle of the magnet's range (and still reads after a power cycle). Only after a picked height
  is reached - manual release, remote stop, dead-man and faults stop immediately. Calibrate with Diagnostics:
  "Zone" = time a sensor stays on when driven straight through; set the overrun to about half. "Last stop: OFF sw!"
  means it's too long.
- **Per-step motor timeout** `MOTOR_STEP_TIMEOUT_MS` (100 s): restarts at every sensor reached. Diagnostics shows
  the measured "Step" time; set ~1.5x the slowest step.
- **Missed-sensor stop:** an automatic move that reaches a sensor *beyond* its target stops and flags the target
  sensor (a dead sensor would otherwise make it bounce between neighbours).
- Manual drive soft limits use the end sensors directly (`limits[0]` / `limits[3]` active).

**Testing bypass:** `ALLOW_HEIGHT_SAFETY_BYPASS` (master switch, false for real use) lets you hold UP + DOWN together 2 s on the
MANUAL page to ignore the height sensors for manual-only testing. Automatic moves are disabled while it's on; the
motor timeout still applies.

**Ladder calibration mode:** `LADDER_CALIBRATION_MODE = true` shows the raw reading on HOME (and Serial at 115200)
and disables height selection. Set back to false afterwards.

**Simulation mode:** `SIM_MODE = true` (ramp sketch only, false for real use) turns the ramp board into the heart of
the PC simulator (separate `waterski-ramp-sim` repo). The four height sensors are read from USB serial instead of
their pins, and relay changes are reported over USB **instead of switching the relays** (the relay pins stay off, so
a simulation build can't move a real ramp). The screens show "SIM". Everything else is the normal firmware: menus,
buttons, remotes, LoRa, safety logic and timeouts. At boot it waits up to 1 s for the PC's first sensor reading so a
ramp parked on a (virtual) sensor is found like the real one. If the PC goes quiet for 500 ms every sensor reads
off. All the code is in `Sim_Mode_Ramp.ino`, including the serial protocol (`@S` sensors in, `@R` relays and `@T`
status out, `@HELLO` with `SIM_PROTOCOL_VERSION`). Change the protocol in both repos together and bump the version.

## Editing the sketches safely (Arduino IDE rules)

The Arduino IDE glues all tabs of a sketch into one file: **the main tab first, then the others alphabetically**,
and auto-generates function prototypes. That auto-generation is unreliable, so:
- Functions that use custom types (`ControlSource`, `LadderButton`, ...) in their parameters, or have default
  arguments, need a hand-written forward declaration in the main tab (see the "Forward declarations" block).
- Constants and globals used by several tabs live in the main tab.
- Don't use `namespace` or file-scope `static` functions across tabs.
- `Games.ino` must stay an identical copy in both sketch folders (the tests check this).

## Tests

`tests/host/run_tests.py` builds the real sketches for a PC with stand-in libraries and runs ~30 scenario tests
(menus, radio packets, a simulated ramp moving at the real speed with magnetic sensor zones, faults, power cycles,
the games, simulation mode). It needs Python 3 and g++. It catches logic bugs; it cannot catch wiring, real relay timing, radio
range or library-version problems. See `tests/host/README.md`.

## Builds and the browser flasher

- `.github/workflows/firmware.yml` runs on every push to `main` (firmware, flasher or tests changed), on a
  published release, or by hand: host tests first, then `arduino-cli` compiles both sketches for
  `Heltec-esp32:esp32:heltec_wifi_lora_32_V3` (plus a compile-only check of the `SIM_MODE` build), and the
  `flasher/` page plus the binaries are published to GitHub Pages. On a release, a zip of the binaries is attached too.
- **Pin the board package and library versions** in the workflow (`CORE_VERSION`, `LIBRARY_VERSION`) to the ones
  that work, and use the same in the Arduino IDE guide.
- The flasher (ESP Web Tools, pinned version) writes four parts: bootloader at 0x0, partition table 0x8000,
  `boot_app0` 0xE000, app 0x10000. The NVS area (saved settings) is left alone unless the user ticks "erase".
- Each sketch folder has a `sketch.yaml` with the default board for `arduino-cli`; the Arduino IDE ignores it.

## Changing the motor (e.g. DC instead of the AC induction motor)

Everything motor-specific is in `Motor_and_Failsafes_Ramp.ino` plus a few constants:
- **Switching:** `runMotor()` (start sequence), `motorStop()`, `serviceDirRelay()`, and `setRelay()` (all relay
  writes). Today: a power relay plus a direction relay (energized = up). A **DC motor with a reversing relay**
  (DPDT swapping polarity) can keep this exact scheme - just rewire. A **DC motor on a PWM H-bridge** needs new
  start/stop code (direction pin + PWM, ideally a soft start) in those functions; keep the same rules: never
  reverse while powered, settle before applying power.
- **Reversal delay** `Change_Delay` (1 s) lets an induction motor spin down. Keep a delay for DC too (shorter is
  fine with braking).
- **Speed-dependent settings:** `MOTOR_STEP_TIMEOUT_MS` (~1.5x the slowest step) and `STOP_OVERRUN_MS` (about half
  the sensor "Zone" time). Re-measure both from the Diagnostics page after any motor or gearing change.
- **Current sensing:** GPIO 4 is wired for an AC current sensor (not implemented yet). A DC motor needs a DC sensor
  (shunt or Hall) and different reading code.
- **Water interlock:** the pump relay's NC contact in the motor feed is wiring, not code. Rewire it into the new
  motor's supply or drop it (the code still pauses the water while moving).

## Known limitations / ideas

- If an NC sensor wire breaks while the ramp is *between* heights, the ramp believes it's at that height until it
  reaches a real sensor (then two read on -> sensor fault). Diagnostics "Sw on:" shows which sensors read on.
- Games high scores are kept in RAM only.
- Current sensing (GPIO 4) is not implemented yet: the overcurrent fault and its screen/radio flags exist, but nothing sets it (TODO in `updateMotorControl()`).
- Remote LEDs: there's no LED tab for the remote yet.
