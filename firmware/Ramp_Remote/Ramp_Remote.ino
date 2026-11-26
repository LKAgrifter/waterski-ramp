#include "LoRaWan_APP.h"
#include "Arduino.h"
#include "HT_SSD1306Wire.h"
#include "esp_timer.h"
#include <Preferences.h>
#include <stdarg.h>

//Declare object prefs to store things to non-volatile memory
Preferences prefs;

///OLED SETUP
//rotate only for GEOMETRY_128_64
static SSD1306Wire  display(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED); // addr , freq , i2c group , resolution , rst

/// --- LoRa Radio Configuration ---
// Set your frequency: 915E6 (North America), 868E6 (Europe), 433E6 (Asia)
#define RF_FREQUENCY            915000000 
#define TX_OUTPUT_POWER         21        // dBm
#define LORA_BANDWIDTH          0         // [0: 125 kHz, 1: 250 kHz, 2: 500 kHz]
#define LORA_SPREADING_FACTOR   8         // [SF7..SF12] //change to 8
#define LORA_CODINGRATE         2         // [1: 4/5, 2: 4/6, etc.] //change to 2
#define LORA_PREAMBLE_LENGTH    8
#define LORA_SYMBOL_TIMEOUT     0
#define LORA_FIX_LENGTH_PAYLOAD_ON false
#define LORA_IQ_INVERSION_ON    false

// Encrypted packet layout (Radio_Security.ino has the details). Same values in both sketches.
constexpr uint8_t RS_HEADER_LEN    = 5;      // Sender byte + 4-byte frame counter
constexpr uint8_t RS_TAG_LEN       = 4;      // Signature (Poly1305 tag) bytes at the end
constexpr uint8_t RS_SENDER_RAMP   = 0xA1;   // Sender byte of the ramp's packets
constexpr uint8_t RS_SENDER_REMOTE = 0xB0;   // Sender byte of a remote's packets = this + its ID (0xB1..0xB4)
uint8_t  rsFlashRemoteId = 0;                // Remote ID picked in the web flasher (0 = not set)
uint8_t  rsFlashWaterMode = 0xFF;            // Remote type picked there: 0 BOAT, 1 TIMER (0xFF = not set)
uint32_t rsFlashId = 0;                      // That flash's number (0 = not web-flashed). The ramp ignores all three.

// Radio Events Callbacks
static RadioEvents_t RadioEvents;

// ===========================================================================
// MENU / MANUAL DRIVE / TESTING SETTINGS  (mirrors the ramp controller)
// ===========================================================================
//
// BUTTON ROLES (the 4 AUX buttons)
//   UpButton    (AUX1) : Manual page = MOTOR UP (hold)     | list pages = scroll up
//   DownButton  (AUX2) : Manual page = MOTOR DOWN (hold)   | list pages = scroll down
//   EnterButton (AUX3) : MENU button. HOLD to go to the next page.
//   BackButton  (AUX4) : Water page  = WATER button. BOAT mode: tap = toggle on/off.
//                                      TIMER mode: tap = +1 h (like the ramp), hold 1 s = off
//   UP + DOWN together : Manual page = HOLD 2 s to toggle the height-sensor bypass on the RAMP (only if ALLOW_HEIGHT_SAFETY_BYPASS)
//                        Water page  = HOLD 2 s to switch this remote's water mode (BOAT / TIMER)
//                        Network page = HOLD 2 s to change this remote's ID (1 -> 2 -> 3 -> 4 -> 1)
//   Ladder buttons     : height select. ONLY works on the SET HEIGHT page (so a bumped button can't move the ramp).
//
// PAGES (hold MENU to cycle):  HOME -> WATER -> SET HEIGHT -> MANUAL ADJUST -> DIAGNOSTICS -> NETWORK -> HOME
// Hidden GAMES page: hold UP + DOWN together 2 s on HOME (hold MENU in the games to go back to HOME).
// HOME only shows status. Pressing a height button anywhere but SET HEIGHT just shows a reminder on HOME.
// If the OLED times out the menu returns to HOME.
constexpr unsigned long MENU_HOLD_MS      = 500;    // Hold MENU this long to change page
constexpr unsigned long GAMES_HOLD_MS     = 2000;   // Hold UP + DOWN this long (HOME) to open the games
constexpr unsigned long HEIGHT_HINT_MS    = 2500;   // How long HOME shows "use SET HEIGHT page" after a stray height press
unsigned long heightHintUntil = 0;
unsigned long waterHintUntil = 0;     // Same idea for a BACK/WATER press on HOME
constexpr unsigned long BYPASS_HOLD_MS    = 2000;   // Hold UP + DOWN this long (Manual page) to toggle the bypass
constexpr unsigned long REMOTE_ID_HOLD_MS = 2000;   // Hold UP + DOWN this long (NETWORK page) to change this remote's ID

// REMOTE ID: every remote talking to the same ramp needs a different ID (1-4). It's set on the remote itself:
// NETWORK page, hold UP + DOWN together 2 s to step 1 -> 2 -> 3 -> 4 -> 1. Saved in flash, so the same firmware file works for
// every remote and reflashing keeps the ID. A brand-new board starts as DEFAULT_REMOTE_ID.
constexpr uint8_t DEFAULT_REMOTE_ID = 1;
constexpr uint8_t REMOTE_ID_MAX     = 4;    // Must match MAX_REMOTES on the ramp
uint8_t REMOTE_ID = DEFAULT_REMOTE_ID;

// WATER MODE: also a saved setting on each remote. WATER page, hold UP + DOWN together 2 s to switch.
//   BOAT  (default): BACK toggles the water. While the water is on, this remote's heartbeats keep the ramp's 1 h
//                    timer topped up, so the water stops within an hour of this remote switching off or losing the
//                    link. For a remote that stays powered while it's in use (in the boat).
//   TIMER          : BACK sets a fixed timer exactly like the ramp's own Water page: each tap +1 h (1..12 h), the
//                    tap after 12 h turns it off, hold BACK 1 s = off now. No keep-alives, so the remote can be
//                    switched off / asleep and the water still runs for the time set. For a shore remote that
//                    sleeps between presses (solar + battery).
enum WaterMode : uint8_t
{
  WATER_MODE_BOAT  = 0,
  WATER_MODE_TIMER = 1
};
WaterMode waterMode = WATER_MODE_BOAT;
constexpr unsigned long WATER_MODE_HOLD_MS = 2000;  // Hold UP + DOWN this long (Water page) to switch water mode
constexpr unsigned long WATER_OFF_HOLD_MS  = 1000;  // TIMER mode: hold BACK this long (Water page) = water off (same as the ramp)
constexpr uint8_t       WATER_TAP_QUEUE_MAX = 12;   // TIMER mode: taps waiting to be sent (12 taps = the whole 1..12 h range)
constexpr unsigned long SCREEN_REFRESH_MS = 80;     // Max OLED redraw rate (~12 Hz)

// TESTING ONLY. false (normal): the sensor bypass is only available on the ramp itself, so this remote has no bypass
// control (it still SHOWS when the ramp's bypass is on). true = Manual page, hold UP + DOWN 2 s asks the ramp to
// toggle it - which the ramp only obeys if its own ALLOW_BYPASS_FROM_REMOTE is true too.
constexpr bool ALLOW_HEIGHT_SAFETY_BYPASS = false;

//Buttons
constexpr uint8_t Ladder_pin    = 5; // Resistor Ladder Buttons (For Heigth Selection)

// ---- Resistor ladder calibration -----------------------------------------------------------------------------
// Measured on the 10k ladder: nothing pressed ~3200, 4.0 ft ~2980, 4.5 ft ~2610, 5.0 ft ~1960, 5.5 ft ~0.
// Each threshold is halfway between neighbouring readings. To re-check or re-calibrate:
// 1. Set LADDER_CALIBRATION_MODE = true and flash. The ladder buttons then CAN'T change the height, and the Home
//    screen shows the live ADC reading (also printed to Serial Monitor at 115200, and on the Diagnostics page).
// 2. Write down the reading with nothing pressed and while holding each button.
// 3. Set each threshold halfway between neighbouring readings. Order must be: none > 4 > 4.5 > 5 > 5.5.
// 4. Check every button decodes correctly on the calibration screen, then set the flag back to false and reflash.
constexpr bool LADDER_CALIBRATION_MODE = false;
constexpr int  LADDER_NONE_ABOVE = 3090;   // Above this = nothing pressed   (3200 / 2980)
constexpr int  LADDER_4_ABOVE    = 2795;   // Above this = 4.0 ft            (2980 / 2610)
constexpr int  LADDER_45_ABOVE   = 2285;   // Above this = 4.5 ft            (2610 / 1960)
constexpr int  LADDER_5_ABOVE    = 980;    // Above this = 5.0 ft, at or below = 5.5 ft   (1960 / 0)
int ladderRawAdc = 4095;                   // Last raw (averaged) reading, for the calibration screen / diagnostics
enum LadderButton
{
  BTN_NONE,
  BTN_4FT,
  BTN_45FT,
  BTN_5FT,
  BTN_55FT
};

// What THIS remote is asking the ramp to do. The numbers are the same as the "remoteControlMode" field on the wire.
enum ControlMode
{
  MODE_AUTO,          // 0
  MODE_MANUAL_UP,     // 1
  MODE_MANUAL_DOWN    // 2
};

enum ScreenState //Define screen pages
{
  SCREEN_BOOT,
  SCREEN_HOME,         // Status only: no buttons do anything here
  SCREEN_SET_HEIGHT,   // The ONLY page where the ladder (height) buttons work
  SCREEN_HEIGHT,       // "Manual adjust" page: motor up / down buttons live here
  SCREEN_WATER,
  SCREEN_NETWORK,
  SCREEN_DIAGNOSTICS,
  SCREEN_GAME          // Games.ino (hidden: UP + DOWN 2 s on HOME)
};

ScreenState currentScreen = SCREEN_BOOT;

struct LadderState
{
    LadderButton current = BTN_NONE;
    LadderButton lastReading = BTN_NONE;

    bool pressedEvent = false;
    bool releasedEvent = false;

    bool longPressHandled = true;

    unsigned long debounceTimer = 0;
    unsigned long pressedTime = 0;

};

LadderState ladder;

// Forward declarations (custom types in the parameters, so the IDE's auto-prototypes can't be trusted)
void cycleRemoteId();
void toggleWaterMode();
LadderButton readLadderFromRaw(int adc);
const char *ladderName(LadderButton b);

struct Button
{
    uint8_t pin;

    bool state = false;        // debounced state
    bool lastState = false;    // previous debounced state

    bool pressedEvent = false;
    bool releasedEvent = false;

    unsigned long debounceTimer = 0;
    unsigned long pressedTime = 0;
    bool longPressHandled = true;
    unsigned long repeatTimer = 0;   // auto-repeat timer for the scroll buttons

};

///Global Variables
struct SystemState
{
  int Menu_Page = 0; //Store what page of the menu is shown
  int Menu_Selection = 0; //Select from list in Menu
  
  float Ramp_Height = 0.0; //Currently located Ramp Height
  float Desired_Height = 0.0; //Desired Ramp Height
  float Estimated_Height = 0.0; //This height is estimated based on runtime from since departing last located height. This is just an estimate.

  bool Motor_Running = false; //store motor power condition as global variable
  bool Motor_Direction_Up = false; //store motor direction as global variable

  ControlMode Control_Mode = MODE_AUTO; //What this remote is currently asking for (manual up / down while a motor button is held)

  bool DirectionChangePending = false; //Are we wanting a direction change?
  bool PendingDirectionUp = false; //Which way is the direction change?
  unsigned long DirectionChangeStart = 0; //Timer starting at direction change
  unsigned long MotorStartTime = 0; //Time when motor starts spinning
  bool MotorPowerPending = false; //Used to add further delay beyond directionChangePending

  bool Water_Relay = false; //Water Relay on or off?
  uint16_t Water_RemainMin = 0; //Minutes left on the ramp's water timer (from telemetry)
  
  unsigned long lastActivityTime = 0; //Timer for display timout
  bool oledSleeping = false;

  bool Remote_Connected = false; //Is there an active remote connection?
  bool localOverrideActive = false; //Local override (will disable remote inputs) - mirrored from the ramp

  bool Height_Safety_Bypass = false; //TESTING ONLY: ramp has its height sensor safeties bypassed (mirrored from the ramp)
  bool Manual_Active = false;        //Ramp reports it is in manual drive (mirrored from the ramp)
  bool Remote_Manual_Off = false;    //Ramp doesn't allow manual drive from remotes at all (mirrored from the ramp)
  uint8_t Control_Owner = 0;         //Remote that has control of the ramp, 1..4 (0 = none; mirrored from the ramp)

  bool packetRecieved = false;

};

SystemState state;

//Diagnostics system for fault detection and indentification (all mirrored from the ramp's telemetry)
struct Diagnostics
{
    bool overCurrentFault = false;
    bool motorTimeoutFault = false;
    bool topLimitFault = false;
    bool bottomLimitFault = false;
    bool radioFault = false;
    bool heightSensorFault = false;
    bool powerLoss = false;
    int faultCount = 0;
    uint8_t heightFaultMask = 0;   // bit 0 = 4ft, bit 1 = 4.5ft, bit 2 = 5ft, bit 3 = 5.5ft
    uint8_t limitMask = 0;         // Switches triggered on the ramp right now (same bit order)

    //ErrorCode lastError = ERR_NONE;
};

Diagnostics diag;

//Create button objects
Button UpButton    {7}; //AUX Button 1
Button DownButton  {6}; //AUX Button 2
Button EnterButton {38}; //AUX Button 3
Button BackButton  {39}; //AUX Button 4

// Button roles. Re-map them here if you want a different layout.
Button &MenuButton  = EnterButton;   // HOLD = next page
Button &WaterButton = BackButton;    // Water page: tap = toggle

//Menu list pages (Network / Diagnostics) share these
constexpr uint8_t LIST_MAX_LINES = 20;   // lines a list page can hold
constexpr uint8_t LIST_LINE_LEN  = 26;   // chars per line incl. NUL (the OLED fits ~24 at 10 pt)
constexpr uint8_t LIST_VISIBLE   = 4;    // lines visible at once
char    listLines[LIST_MAX_LINES][LIST_LINE_LEN];
uint8_t listLineCount = 0;
uint8_t listScroll    = 0;

//Manual page: a motor button only drives if its press began on the Manual page (so a wake-up press or a
//button that was already held when you arrived can never start the motor)
bool manualUpArmed   = false;
bool manualDownArmed = false;

//LEDs
constexpr uint8_t LED_1         = 19; // LED 1 (Ramp at 4.5)
constexpr uint8_t LED_2         = 20; // LED 2 (Ramp at 5)
constexpr uint8_t LED_3         = 26; // LED 3 (Ramp At 5.5)
constexpr uint8_t LED_4         = 40; // LED 4 (Ramp at 4)

// WATER LED: the Heltec board's own white LED (GPIO 35, LED_BUILTIN on the V3) shows the water.
//   on = water on   |   slow blink = water switched on but paused while the ramp moves   |   off = water off
// On a remote it shows what the ramp reports, and stays off while there's no link to the ramp.
constexpr bool    WATER_LED_ENABLED = true;   // false = leave the white LED off (e.g. to save power)
constexpr uint8_t WATER_LED         = 35;
constexpr unsigned long WATER_LED_BLINK_MS = 500;   // Half of the slow blink (500 ms on, 500 ms off)

// The web flasher can set the remote ID and type (BOAT / TIMER water mode). They're saved once, on the first boot after
// that flash; after that the NETWORK / WATER pages can change them as usual, until the next web flash sets them again.
void applyFlashedSettings()
{
  if (rsFlashId == 0 || prefs.getULong("FlashCfgId", 0) == rsFlashId)
    return;   // No web-flash settings, or already applied

  if (rsFlashRemoteId >= 1 && rsFlashRemoteId <= REMOTE_ID_MAX)
    prefs.putUChar("RemoteId", rsFlashRemoteId);
  if (rsFlashWaterMode == WATER_MODE_BOAT || rsFlashWaterMode == WATER_MODE_TIMER)
    prefs.putUChar("WaterMode", rsFlashWaterMode);
  prefs.putULong("FlashCfgId", rsFlashId);
}

// The board's white LED shows the ramp's water, as the ramp reports it (WATER_LED above). Off without a link.
// The ramp pauses the water while it moves, so the LED blinks slowly then, like the ramp's own.
void updateWaterLed()
{
  bool on = false;
  if (WATER_LED_ENABLED && state.Remote_Connected && state.Water_Relay)
  {
    if (state.Motor_Running)
      on = ((millis() / WATER_LED_BLINK_MS) % 2) == 0;
    else
      on = true;
  }
  digitalWrite(WATER_LED, on ? HIGH : LOW);
}

// The four height LEDs show the ramp, as it reports it, the same way as the ramp's own LEDs (LEDs_Ramp.ino):
//   stopped = the LED for its height (none while the height isn't known, e.g. CAL REQUIRED)
//   moving  = a strobe through the heights in the direction it's going (4 -> 5.5 going up, 5.5 -> 4 going down)
//   no link to the ramp = all off
constexpr unsigned long HEIGHT_LED_STROBE_MS = 750;    // One strobe sweep through the four LEDs (the original remote LED tab's speed)
void updateHeightLeds()
{
  const uint8_t byHeight[4] = { LED_4, LED_1, LED_2, LED_3 };   // 4.0, 4.5, 5.0, 5.5 ft
  int lit = -1;                                                 // Index into byHeight, -1 = none
  if (state.Remote_Connected && state.Motor_Running)
  {
    int step = (int)((millis() % HEIGHT_LED_STROBE_MS) * 4 / HEIGHT_LED_STROBE_MS);
    lit = state.Motor_Direction_Up ? step : 3 - step;
  }
  else if (state.Remote_Connected && !diag.powerLoss)   // Height known (no CAL REQUIRED)
  {
    if (state.Ramp_Height == 4.0f)      lit = 0;
    else if (state.Ramp_Height == 4.5f) lit = 1;
    else if (state.Ramp_Height == 5.0f) lit = 2;
    else if (state.Ramp_Height == 5.5f) lit = 3;
  }
  for (int i = 0; i < 4; i++)
    digitalWrite(byHeight[i], i == lit ? HIGH : LOW);
}

void setup() {
  Serial.begin(115200);

  //Define Preferences
  prefs.begin("settings", false);
  radioSecurityBegin();   // Radio key from the bind phrase (Radio_Security.ino)
  applyFlashedSettings(); // Remote ID / type picked in the web flasher, saved on the first boot after that flash

  // This remote's ID (set on the NETWORK page)
  REMOTE_ID = prefs.getUChar("RemoteId", DEFAULT_REMOTE_ID);
  if (REMOTE_ID < 1 || REMOTE_ID > REMOTE_ID_MAX)
    REMOTE_ID = DEFAULT_REMOTE_ID;

  // This remote's water mode (set on the WATER page). Anything unexpected falls back to BOAT, the original behaviour.
  waterMode = (prefs.getUChar("WaterMode", WATER_MODE_BOAT) == WATER_MODE_TIMER) ? WATER_MODE_TIMER : WATER_MODE_BOAT;
  
  //Define pin modes
  pinMode(UpButton.pin, INPUT_PULLUP);
  pinMode(DownButton.pin, INPUT_PULLUP);
  pinMode(EnterButton.pin, INPUT_PULLUP);
  pinMode(BackButton.pin, INPUT_PULLUP);
  pinMode(LED_1, OUTPUT);
  pinMode(LED_2, OUTPUT);
  pinMode(LED_3, OUTPUT);
  pinMode(LED_4, OUTPUT);
  pinMode(WATER_LED, OUTPUT);
  digitalWrite(WATER_LED, LOW);

  // 2. Show "Starting radio..." BEFORE Mcu.begin(). Heltec's Mcu.begin() checks the board's Heltec radio license and,
  //    if it has been erased (a full flash erase deletes it), loops forever printing "Please provide a correct
  //    license!" over USB. The screen then says what's wrong instead of staying blank. (Everything after Mcu.begin()
  //    below is unchanged: Vext and the display are set up again there, in case Mcu.begin() touches them.)
  pinMode(Vext, OUTPUT);
  digitalWrite(Vext, LOW); // LOW powers on the Vext rail (OLED) for Heltec V3
  delay(100);
  display.init();
  drawRadioStartScreen();

  // 3. Initialize Heltec V3 MCU and Power on Vext (powers OLED and sensor rails)
  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);
  pinMode(Vext, OUTPUT);
  digitalWrite(Vext, LOW); // LOW powers on the Vext rail for Heltec V3
  delay(100);

  // 4. Initialize OLED Display
  display.init();
  //display.flipScreenVertically();
  drawScreen(); //Draw the default boot screen
  
  // Initialize OLED timeout timer on boot
  state.lastActivityTime = millis();

  // 4. Initialize LoRa Radio using LoRaWan_APP framework
RadioEvents.TxDone    = OnTxDone;
RadioEvents.TxTimeout = OnTxTimeout;
RadioEvents.RxDone    = OnRxDone;
RadioEvents.RxTimeout = OnRxTimeout;
RadioEvents.RxError   = OnRxError;
  
  Radio.Init(&RadioEvents);
  Radio.SetChannel(RF_FREQUENCY);
  Radio.SetTxConfig(MODEM_LORA, TX_OUTPUT_POWER, 0, LORA_BANDWIDTH,
                    LORA_SPREADING_FACTOR, LORA_CODINGRATE,
                    LORA_PREAMBLE_LENGTH, LORA_FIX_LENGTH_PAYLOAD_ON,
                    true, 0, 0, LORA_IQ_INVERSION_ON, 3000);
  
  // Put radio into listening mode
  Radio.Rx(0);

}

void loop() {
  // Required background handler for LoRa events in LoRaWan_APP framework
  Radio.IrqProcess();

  //Poll Buttons for Updates
  updateButton(UpButton);
  updateButton(DownButton);
  updateButton(EnterButton);
  updateButton(BackButton);
  updateLadder(ladder);


handleOLEDTimeout();   // swallows the wake-up press. When the screen sleeps the menu returns to HOME.
processButtons();      // must come after handleOLEDTimeout
handleLoraLink();
drawScreen();
updateWaterLed();     // the board's white LED shows the ramp's water
updateHeightLeds();   // the height LEDs show the ramp's height, strobing while it moves

}
