# Arduino IDE setup guide

For changing the code: a different motor, new features, fixes. **If you only need to flash a board, use the
browser flasher instead** (see the main README) - no setup needed.

> DRAFT OUTLINE - screenshots and wording to be finished. Pictures go in `docs/images/` and are referenced as
> `![caption](images/xx-name.png)`.

## 1. Install the Arduino IDE
- Download Arduino IDE 2.x from arduino.cc and install it.
- 📷 `images/01-ide-download.png` - download page

## 2. Add Heltec boards support
- File > Preferences > *Additional boards manager URLs* - paste
  `https://resource.heltec.cn/download/package_heltec_esp32_index.json`
- 📷 `images/02-preferences-url.png`
- Tools > Board > Boards Manager - search **Heltec ESP32** - install version **x.y.z** (the one this repo is
  built with; see `.github/workflows/firmware.yml`, `CORE_VERSION`).
- 📷 `images/03-boards-manager.png`

## 3. Install the Heltec library
- Tools > Manage Libraries - search **Heltec ESP32 Dev-Boards** - install version **x.y.z**
  (`LIBRARY_VERSION` in the workflow file).
- 📷 `images/04-library-manager.png`

## 4. Get the code
- Download this repo (green **Code** button > Download ZIP) and unzip it, or clone it with git.
- Open `firmware/Ramp_Control/Ramp_Control.ino` (ramp) or `firmware/Ramp_Remote/Ramp_Remote.ino` (remote).
  All the other files in that folder open as tabs.
- 📷 `images/05-sketch-tabs.png` - what the tabs look like

## 5. Select the board and port
- Tools > Board > Heltec ESP32 Series Dev-boards > **WiFi LoRa 32(V3)**
- Plug in the board, Tools > Port > pick the new port
- 📷 `images/06-board-port.png`

## 6. Upload
- **Bind phrase:** a board flashed from the web flasher keeps its phrase when you upload from the IDE, so you don't
  need to do anything. For a board that has never been web-flashed, create a file named `bind_phrase.h` in the
  sketch folder containing `#define BIND_PHRASE "your phrase"` (the same phrase as the ramp and the other remotes).
  That file is ignored by git: **never commit it or put the phrase in the code**, the repository is public.
  Without either, the board is unbound ("Bind: NONE (open)" on its NETWORK page).
- Click **Upload** (arrow button). Wait for "Done uploading".
- 📷 `images/07-upload-done.png`
- If it won't connect: hold the board's PRG button while plugging it in.

## 7. Where things are in the code
- Settings you're likely to change are named constants near the top of `Ramp_Control.ino` (timings, relay
  type, sensor type, ladder thresholds). Each has a comment explaining it.
- Which tab does what: see `docs/DEVELOPMENT.md`.
- Changing to a different motor: see "Changing the motor" in `docs/DEVELOPMENT.md`.

## 8. Checking your change (optional)
- Use the Serial Monitor (Tools > Serial Monitor, 115200 baud) for debug output.
- If you have Python and g++, `tests/host/run_tests.py` runs the PC simulation tests.
- After pushing to GitHub, the automatic build compiles both sketches and updates the flasher page.

## Common errors
- *"LoRaWan_APP.h: No such file"* - the Heltec library (step 3) isn't installed.
- *"... was not declared in this scope"* after adding a function - add a forward declaration in the main tab
  (see "Editing the sketches safely" in `docs/DEVELOPMENT.md`).
- Port missing - charge-only USB cable, or the board needs the PRG button held while plugging in.
