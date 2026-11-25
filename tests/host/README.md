# PC simulation tests (optional)

These build the real ramp and remote sketches **for your PC** (not the ESP32) using small stand-ins for the
Arduino and Heltec libraries, then run scenario tests: pressing buttons, sending LoRa packets, triggering height
sensors, moving a simulated ramp at the real motor speed, injecting faults and power cycles.

You don't need these to build or flash the boards. They're for checking logic changes before flashing.

## Running

Needs Python 3 and g++ (Linux or macOS; on Windows use WSL or MSYS2).

```
python3 run_tests.py            # everything (the games fuzz tests take a minute or two)
python3 run_tests.py --quick    # skip the slow games fuzz tests
python3 run_tests.py -k motion  # only tests whose name contains "motion"
python3 run_tests.py -v         # show every individual check
python3 run_tests.py --trace    # print a millisecond relay trace for manual UP/DOWN
```

Build output goes in `build/` (ignored by git).

## Layout

- `run_tests.py`: builds sketch variants and runs every test.
- `build_sketch.py`: joins a sketch's tabs the way the Arduino IDE does (main tab first, then alphabetical) and
  adds function prototypes.
- `stubs/`: PC stand-ins for `Arduino.h`, the Heltec radio and OLED libraries, and `Preferences`.
- `tests/`: the scenario tests. Each `#include`s the joined sketch (`SRC`) and drives `setup()` / `loop()` with
  a simulated clock (`g_millis`).
- `tools/relay_trace.cpp`: the `--trace` tool.

## What they can't tell you

They don't use the ESP32 compiler or the real libraries, and they can't see wiring, real relay timing, radio
range or power problems. Always finish with the real hardware.
