# Waterski Ramp Controller

Firmware for a motorized waterski jump ramp: a **ramp controller** that drives the lift motor, the ramp's water
pump and the height sensors, and handheld **remotes** that talk to it over LoRa radio.

Both run on the **Heltec WiFi LoRa 32 (V3)** board (ESP32-S3 + SX1262 LoRa radio + 128x64 OLED).

> Hardware files (PCB, wiring, CAD) will be added under `hardware/`. Full documentation is coming.

## What's in here

```
firmware/
  Ramp_Control/   Ramp controller sketch (open Ramp_Control.ino in the Arduino IDE)
  Ramp_Remote/    Remote sketch (open Ramp_Remote.ino in the Arduino IDE)
flasher/          Browser flasher page (published to GitHub Pages by the build)
hardware/         PCB, wiring and CAD files (to come)
docs/             USER_GUIDE.md (how to use it), DEVELOPMENT.md (how it works), ARDUINO_IDE_SETUP.md (step-by-step guide)
tests/host/       PC simulation tests (optional - see tests/host/README.md)
.github/          Automatic build: tests, compiles both sketches, publishes the flasher
```

Each sketch folder is a normal Arduino sketch: every `.ino` file in it opens as a tab and they're compiled
together. You never need anything outside the folder to build it.

## Just want to flash a board? Use the browser flasher

No software to install: open the **flasher page** in Chrome or Edge on a computer, plug in the board, click
*Install*: **https://lkagrifter.github.io/waterski-ramp/**

The page always has the latest firmware from this repo, built automatically. It never erases the board, so saved
settings and the board's Heltec radio license are kept.

## Changing the code: Arduino IDE

Full step-by-step guide with pictures: `docs/ARDUINO_IDE_SETUP.md`. The short version:

1. Install the [Arduino IDE](https://www.arduino.cc/en/software) (2.x).
2. **Add the Heltec boards:** File > Preferences > *Additional boards manager URLs*, add
   `https://resource.heltec.cn/download/package_heltec_esp32_index.json`
   then Tools > Board > Boards Manager, search **Heltec ESP32** and install it.
3. **Install the Heltec library:** Tools > Manage Libraries, search **Heltec ESP32 Dev-Boards** and install it.
   It provides `LoRaWan_APP.h` (radio) and `HT_SSD1306Wire.h` (screen). No other libraries are needed.
4. Tools > Board: **Heltec WiFi LoRa 32(V3)**. Pick the board's USB port under Tools > Port.
5. Open `firmware/Ramp_Control/Ramp_Control.ino` (ramp) or `firmware/Ramp_Remote/Ramp_Remote.ino` (remote)
   and click **Upload**.

Versions known to work: *(fill in: Arduino IDE x.y, Heltec ESP32 board package x.y.z, Heltec ESP32 Dev-Boards x.y.z)*

**Flash the ramp and the remotes from the same version of this repo.** They share a radio message format.

## Making another remote

Up to **4 remotes** can talk to one ramp.

1. Build the remote hardware (same board and buttons as the existing remote).
2. Flash it with the browser flasher (or the Arduino IDE). Every remote uses the same firmware.
3. On the new remote, hold MENU to reach the **NETWORK** page, then **hold UP + DOWN together for 2 seconds** to change its ID
   (1 -> 2 -> 3 -> 4). Pick one no other remote uses. It's saved, and kept when you reflash.
4. It shows up on the ramp's NETWORK page.
5. Pick its **water mode**: on the WATER page, **hold UP + DOWN together for 2 seconds** to switch (the page title
   shows WATER (BOAT) or WATER (TIMER)). Also saved and kept when you reflash.
   - **BOAT** (default): for a remote that stays switched on while it's in use. BACK toggles the water; it stays
     on only while this remote is on and in range, and stops within an hour after that.
   - **TIMER**: for a remote that sleeps between presses (e.g. a solar shore remote). BACK sets a timer the same
     way as at the ramp: each tap adds 1 hour (up to 12), the tap after 12 h turns it off, hold BACK 1 s = off.

The radio settings (frequency 915 MHz, SF8, 125 kHz, coding rate 4/6) must match on every unit. They're at the
top of both main sketches. 915 MHz is for North America; Europe uses 868 MHz.

## Using it (quick version)

Full instructions for the ramp controller and the remotes, with screenshots of every page: **[docs/USER_GUIDE.md](docs/USER_GUIDE.md)**.

Hold **MENU** to step through the pages: HOME, WATER, SET HEIGHT, MANUAL, DIAGNOSTICS, NETWORK.
HOME only shows status. Heights can only be set on the **SET HEIGHT** page, so a bumped button can't move the
ramp. See `docs/DEVELOPMENT.md` for the full behaviour, safety logic and settings.

## Safety

This controls a large motorized structure. The firmware has several layers of protection (calibration lockout,
per-step motor timeout, sensor fault detection, a manual dead-man on the remote), but **firmware is not a
substitute for hardware safety**: fit a fuse or motor
thermal cutout, and a way to kill power at the ramp.

## License

To be decided.
