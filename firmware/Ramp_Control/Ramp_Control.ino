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
#define LORA_SPREADING_FACTOR   8         // [SF7..SF12]
#define LORA_CODINGRATE         2         // [1: 4/5, 2: 4/6, etc.]
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
// MENU / MANUAL DRIVE / TESTING SETTINGS
// ===========================================================================
//
// BUTTON ROLES (the 4 AUX buttons)
//   UpButton    (AUX1) : Manual page = MOTOR UP (hold)     | list pages = scroll up
//   DownButton  (AUX2) : Manual page = MOTOR DOWN (hold)   | list pages = scroll down
//   EnterButton (AUX3) : MENU button. HOLD to go to the next page.
//   BackButton  (AUX4) : Water page  = WATER button (tap = +1 h, hold = off)
//   UP + DOWN together : Manual page = HOLD 2 s to toggle the height-sensor bypass
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
constexpr unsigned long WATER_OFF_HOLD_MS = 1000;   // Hold WATER this long (Water page) to turn the water off
constexpr unsigned long BYPASS_HOLD_MS    = 2000;   // Hold UP + DOWN this long (Manual page) to toggle the bypass
constexpr unsigned long SCREEN_REFRESH_MS = 80;     // Max OLED redraw rate (~12 Hz). Redrawing every loop() starves the radio/motor code.

// A remote that asks for manual drive must keep sending packets. If nothing arrives for this long
// the ramp drops back to AUTO (which stops the motor, because Desired_Height tracks Ramp_Height in manual).
constexpr unsigned long REMOTE_MANUAL_TIMEOUT_MS = 1000;

// REMOTE MANUAL DRIVE. true = remotes can drive the motor by hand from their MANUAL page (hold UP / DOWN), except
// while CAL REQUIRED or the sensor bypass is on. false = manual drive ONLY with the ramp's own buttons: every remote
// manual request is refused (whatever firmware the remote runs) and the remotes' MANUAL page says
// "Manual: at the ramp only" (they learn it from the telemetry). Height picks from remotes are not affected.
// Only the ramp needs reflashing to change it.
constexpr bool ALLOW_REMOTE_MANUAL = true;

// ONE REMOTE AT A TIME. true = when the ramp accepts a remote's height pick or manual drive, that remote "has control"
// until the ramp has stopped moving and REMOTE_CONTROL_HOLD_MS more has passed. Until then height picks and manual drive
// from every OTHER remote are refused, and their screens say which remote has control (so the boat and the shore remote
// can't fight over the ramp). Water isn't affected. The ramp's own buttons always win: using them takes control away
// from the remote. false = every remote can send heights at any time (the last one wins).
constexpr bool ONE_REMOTE_AT_A_TIME = true;
constexpr unsigned long REMOTE_CONTROL_HOLD_MS = 5000;

// ---------------------------------------------------------------------------
// *** TESTING ONLY: HEIGHT SENSOR SAFETY BYPASS ***
// While the bypass is ON the ramp ignores: the height-sensor fault and the top/bottom soft limits. AUTO mode is
// disabled (height buttons are ignored); only manual drive AT THE RAMP works - remotes can't drive while it's on.
// The per-step motor timeout (MOTOR_STEP_TIMEOUT_MS) still applies. The bypass is never saved - it is OFF after every reboot unless
// HEIGHT_SAFETY_BYPASS_AT_BOOT is true.
//   ALLOW_HEIGHT_SAFETY_BYPASS    : master switch. false = the feature is dead (menu, remote and boot flag all ignored).
//   HEIGHT_SAFETY_BYPASS_AT_BOOT  : true = ramp boots with the bypass already ON (bench testing without sensors).
//   ALLOW_BYPASS_FROM_REMOTE      : false = only the ramp's own Manual page can turn it on/off; bypass requests that
//                                   arrive over the radio are ignored (whatever firmware the remote runs).
// Toggle it at runtime on the ramp's Manual page: hold UP + DOWN together 2 s.
// ---------------------------------------------------------------------------
constexpr bool ALLOW_HEIGHT_SAFETY_BYPASS   = true;
constexpr bool HEIGHT_SAFETY_BYPASS_AT_BOOT = false;
constexpr bool ALLOW_BYPASS_FROM_REMOTE     = false;

// ---------------------------------------------------------------------------
// *** SIMULATION MODE (for the PC simulator - leave false for real use) ***
// true = this board pretends to be on the ramp for the simulator page (separate "waterski-ramp-sim" repo):
//   - the four height sensors are read from USB serial ("@S" lines from the PC) instead of the sensor pins
//   - relay changes are reported over USB ("@R" lines) INSTEAD of switching the relays: the relay pins stay OFF,
//     so a simulation build can't move a real ramp
//   - the screens show "SIM"
// Everything else (menus, buttons, remotes, LoRa, safety logic, timeouts) is the normal firmware.
// Details and the serial protocol: Sim_Mode_Ramp.ino. Never leave a simulation build on the real ramp.
// ---------------------------------------------------------------------------
constexpr bool SIM_MODE = false;
constexpr uint8_t SIM_PROTOCOL_VERSION = 1;   // Sent in "@HELLO". Change it whenever the serial protocol changes.

/// --- Schematic Peripherals Pin Mapping (J2 / J3 Headers) ---

//Random Variables
constexpr unsigned long Change_Delay = 1000; //direction change delay in ms (motor must be stopped this long before reversing)
constexpr unsigned long Relay_Settle_Delay = 100; // Direction and pump relays must have switched and stopped bouncing this long before motor power is applied
// Motor safety timeout PER HEIGHT STEP: restarts every time the ramp reaches a height switch.
// Motor ~135 rpm x 1 mm/rev = ~2.25 mm/s, so a 0.5 ft (152 mm) step is ~68 s if the jack travel equals the height change.
// 100 s = ~1.5x that. The Diagnostics page shows the measured "Last step" time: set this to ~1.5x your slowest step.
constexpr unsigned long MOTOR_STEP_TIMEOUT_MS = 100000;
// STOP OVERRUN: magnetic height sensors trigger at the EDGE of their range, so an AUTO move keeps driving this long past
// the target switch's first trigger before stopping, to park near the middle of the magnet's range (so it still reads
// after a power cycle weeks later). ~2.25 mm/s, so 2 s = ~4.5 mm.
// TO CALIBRATE: do a move that drives straight THROUGH a switch (e.g. 4 -> 5.5 passes 4.5 and 5). Diagnostics shows
// "Zone" = how long that switch stayed on. Set this to about HALF the zone time: the ramp then stops near the middle
// whichever direction it arrives from. "Last stop: OFF sw!" on Diagnostics means it's too long (it drove out the far side).
constexpr unsigned long STOP_OVERRUN_MS = 2000;
static_assert(STOP_OVERRUN_MS <= 10000, "STOP_OVERRUN_MS: keep it to a few seconds");
constexpr unsigned long DIR_RELAY_DROP_MS = 100;       // Direction relay releases this long after the motor power relay opens
constexpr unsigned long WATER_RESUME_DELAY_MS = 1000;   // Water comes back on this long after the motor stops (if it was on)

//Limit Stops
constexpr uint8_t LMT_55        = 48; // 5.5ft Height Limit Sensor
constexpr uint8_t LMT_5         = 47; // 5ft Height Limit Sensor
constexpr uint8_t LMT_45        = 33;  // 4.5ft Height Limit Sensor
constexpr uint8_t LMT_4         = 34; // 4ft Height Limit Sensor

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

enum ControlSource //defines were the control input is coming from, remote is from the LORA connection, could be expanded to add wifi
{
  SOURCE_LOCAL,
  SOURCE_REMOTE
};

enum ControlMode
{
  MODE_AUTO,
  MODE_MANUAL
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

Button UpButton    {7}; //AUX Button 1
Button DownButton  {6}; //AUX Button 2
Button EnterButton {38}; //AUX Button 3
Button BackButton  {39}; //AUX Button 4

// Button roles. Re-map them here if you want a different layout.
Button &MenuButton  = EnterButton;   // HOLD = next page
Button &WaterButton = BackButton;    // Water page: tap = +1 h, hold = off

//Menu list pages (Network / Diagnostics) share these
constexpr uint8_t LIST_MAX_LINES = 26;   // lines a list page can hold
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

//Relays and Current Sensor
constexpr uint8_t Power_Relay   = 2; //Motor Power Relay      (swapped with Dir to match the PCB)
constexpr uint8_t Dir_Relay     = 3; //Motor Direction Relay  (energized = UP)
constexpr uint8_t Pump_Relay    = 1; //Ramp Water Pump Relay

// RELAY BOARDS ARE HIGH-LEVEL TRIGGERED: driving the pin HIGH turns the relay ON. (Low-level boards on 5 V don't
// switch OFF from a 3.3 V ESP32 pin. If you ever use low-level relays powered at 3.3 V, swap these two lines -
// every relay write in the code goes through setRelay().)
constexpr uint8_t RELAY_ON  = HIGH;
constexpr uint8_t RELAY_OFF = LOW;
void simSetRelay(uint8_t pin, bool on);   // Sim_Mode_Ramp.ino
void setRelay(uint8_t pin, bool on)
{
  if (SIM_MODE)
  {
    digitalWrite(pin, RELAY_OFF);   // Simulation build: never switch a real relay, just tell the PC
    simSetRelay(pin, on);
    return;
  }
  digitalWrite(pin, on ? RELAY_ON : RELAY_OFF);
}
constexpr uint8_t Current_Sense = 4; //AC Current Sensor

///Global Variables
struct SystemState
{
  int Menu_Page = 0; //Store what page of the menu is shown
  int Menu_Selection = 0; //Select from list in Menu
  
  float Ramp_Height = 0.0; //Currently located Ramp Height
  float Desired_Height = 0.0; //Desired Ramp Height
  int8_t Pos_Side = 0;        // Where the ramp is relative to Ramp_Height: 0 = at it, +1 = above it, -1 = below it
                              // (Ramp_Height only changes ON a switch, so this says which side of it we've moved to)
  unsigned long Last_Step_Ms = 0;
  bool Step_From_Switch = false;    // The current step started on a switch (so its time is a full step)   // Measured time of the last full switch-to-switch step (Diagnostics)
  bool Overrun_Active = false;      // Driving the extra STOP_OVERRUN_MS past the target switch
  unsigned long Overrun_Start = 0;
  float Overrun_Target = 0.0f;
  bool Last_Stop_On_Switch = true;  // Did the last AUTO stop end with its switch still on? (Diagnostics)
  unsigned long Last_Zone_Ms = 0;   // How long a switch stayed on while driving straight through it (Diagnostics)
  float Leg_Target = 0.0f;    // The height the motor was heading for when it last started (missed-switch check)
  bool Height_Request_Active = false; // A height was selected and the ramp hasn't arrived yet (lets it drive BACK to the
                                      // height it just left; cleared on arrival and by manual drive)
  float Estimated_Height = 0.0; //This height is estimated based on runtime from since departing last located height. This is just an estimate.

  bool Motor_Running = false; //store motor power condition as global variable
  bool Motor_Direction_Up = false; //store motor direction as global variable

  ControlSource Control_Source = SOURCE_LOCAL; //Default control
  ControlMode Control_Mode = MODE_AUTO;
  unsigned long LastRemoteCommand = 0;

  // Manual drive (local buttons or a remote). Control_Mode == MODE_MANUAL while this is active.
  uint8_t Manual_Request = 0;          // 0 = none, 1 = up, 2 = down
  uint8_t ManualRemoteId = 0;          // Which remote owns the manual drive (only meaningful when Control_Source == SOURCE_REMOTE)
  unsigned long LastManualPacket = 0;  // millis() of the last remote packet that asked for manual drive (1 s dead-man)

  bool Height_Safety_Bypass = (HEIGHT_SAFETY_BYPASS_AT_BOOT && ALLOW_HEIGHT_SAFETY_BYPASS); // TESTING ONLY, see top of file

  bool DirectionChangePending = false; //Are we wanting a direction change?
  bool PendingDirectionUp = false; //Which way is the direction change?
  unsigned long DirectionChangeStart = 0; //Timer starting at direction change
  unsigned long MotorStartTime = 0; //Time when motor starts spinning
  unsigned long MotorStoppedAt = 0;      // When the motor last stopped (the direction-change delay counts from here)
  bool PumpRelayOn = false;              // Pump relay coil actually energized? (Water_Relay = water switched on)
  unsigned long PumpRelayOffAt = 0;      // When the pump relay last dropped out (settle time before motor power)
  unsigned long MotorBusyAt = 0;         // Last time the motor was running or starting (water resumes after a delay)
  bool DirRelayOn = false;               // Direction relay coil energized? (only while driving Up)
  bool DirRelayDropPending = false;      // Release it shortly after the motor stops
  unsigned long DirRelayDropAt = 0;
  unsigned long DirRelaySetAt = 0;       // When it last changed (settle time before motor power)
  bool MotorPowerPending = false; //Used to add further delay beyond directionChangePending

  bool Water_Relay = false; //Water Relay on or off?
  unsigned long Water_RelayStart = 0; //Timer starting at water power on
  int Water_Timeout = 0; //timout in units hours
  ControlSource Water_Source = SOURCE_LOCAL; //SOURCE_REMOTE = a BOAT-mode remote's water: only lives while that remote keeps refreshing it. SOURCE_LOCAL = fixed timer.
  uint8_t Water_TimerRemoteId = 0; //Fixed timer last set from: 0 = the ramp's own Water page, 1..4 = a TIMER-mode remote (shown on the Water page)
  
  
  unsigned long lastActivityTime = 0; //Timer for display timout
  bool oledSleeping = false;

  bool Remote_Connected = false; //Is there an active remote connection?
  bool localOverrideActive = false; //Local override (will disable remote inputs)

};

SystemState state;

//Diagnostics system for fault detection and indentification
struct Diagnostics
{
    bool overCurrentFault = false;   // Placeholder: current sensing isn't implemented yet (see updateMotorControl)
    bool motorTimeoutFault = false;
    bool topLimitFault = false;
    bool bottomLimitFault = false;
    bool radioFault = false;
    bool heightSensorFault = false;
    bool powerLoss = false;
    int faultCount = 0;
    uint8_t heightFaultMask = 0;   // bit 0 = 4ft, bit 1 = 4.5ft, bit 2 = 5ft, bit 3 = 5.5ft

    //ErrorCode lastError = ERR_NONE;
};

Diagnostics diag;

// MOTOR USAGE counters, shown at the bottom of the Diagnostics page. Saved in flash, so they survive power cuts and
// reflashing (only a full flash erase clears them; the web flasher never erases). A CYCLE is one motor run, from start to stop: a
// height move is usually one cycle, each press-and-hold on a MANUAL page is one, and a move that reverses is two.
struct MotorUsage
{
  uint32_t cycles = 0;        // Every motor run
  uint32_t autoRuns = 0;      //   ...that were height moves (SET HEIGHT page or a remote's height pick)
  uint32_t manualRuns = 0;    //   ...that were manual drive (MANUAL page, at the ramp or on a remote)
  uint32_t upRuns = 0;
  uint32_t downRuns = 0;
  uint32_t halfSteps = 0;     // Height travelled, in 0.5 ft steps (counted on reaching a different height sensor)
  uint64_t runMs = 0;         // Total time the motor has been on
  uint32_t stepCount = 0;     // Timed sensor-to-sensor steps, for the average step time
  uint64_t stepMsSum = 0;
  unsigned long runStart = 0; // (not saved) millis() when the current run started
  float lastArrival = -1.0f;  // (not saved) the height sensor last reached (-1 = none yet)
};

MotorUsage motorUse;

// Forward declarations for functions that have default arguments. The Arduino IDE joins the .ino files alphabetically and
// does NOT auto-generate prototypes for functions with default arguments, so without these the Button file (which comes
// before Water_Controller_Ramp) can't see waterControl(). The defaults themselves stay on the definitions - don't repeat them here.
void waterControl(bool activate, ControlSource source);
void servicePumpRelay();
LadderButton readLadderFromRaw(int adc);
const char *ladderName(LadderButton b);
bool waterPausedForMotor();
void requestMotorUp(ControlSource source, bool manual);
void requestMotorDown(ControlSource source, bool manual);

void setup() {
  // RELAYS OFF FIRST, before anything else. They're low-level triggered, so a pin left LOW would switch the motor or
  // the pump ON. Set the "off" level, make the pin an output, then set it again (some ESP32 core versions ignore a
  // write to a pin that isn't an output yet). The pin is only LOW for microseconds - far too short for a relay to pull in.
  setRelay(Power_Relay, false);
  setRelay(Dir_Relay, false);
  setRelay(Pump_Relay, false);
  pinMode(Power_Relay, OUTPUT);
  pinMode(Dir_Relay, OUTPUT);
  pinMode(Pump_Relay, OUTPUT);
  setRelay(Power_Relay, false);
  setRelay(Dir_Relay, false);
  setRelay(Pump_Relay, false);

  Serial.begin(115200);
  simBegin();   // Simulation mode only (does nothing otherwise)

  //Define Preferences
  prefs.begin("settings", false);
  loadMotorUsage();   // Motor usage counters for the Diagnostics page
  radioSecurityBegin();   // Radio key from the bind phrase (Radio_Security.ino)

  //Check for Power Loss
  powerLossRecovery();

  // 1. Configure Hardware Peripherals from Schematic
  
  //(Relays were already set up and switched off at the very top of setup())

  //Now for pin modes
  pinMode(Current_Sense, INPUT);
  pinMode(UpButton.pin, INPUT_PULLUP);
  pinMode(DownButton.pin, INPUT_PULLUP);
  pinMode(EnterButton.pin, INPUT_PULLUP);
  pinMode(BackButton.pin, INPUT_PULLUP);
  pinMode(LMT_4, INPUT_PULLUP);
  pinMode(LMT_45, INPUT_PULLUP);
  pinMode(LMT_5, INPUT_PULLUP);
  pinMode(LMT_55, INPUT_PULLUP);
  pinMode(LED_1, OUTPUT);
  pinMode(LED_2, OUTPUT);
  pinMode(LED_3, OUTPUT);
  pinMode(LED_4, OUTPUT);
  

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
  simWaitForSensors();   // Simulation mode only: wait (max 1 s) for the PC's first sensor reading
  initHeight();

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

  if (heightSafetyBypassActive())
  {
    Serial.println("*** WARNING: HEIGHT SENSOR SAFETY BYPASS IS ON (testing only) ***");
  }
}

void loop() {
  // Required background handler for LoRa events in LoRaWan_APP framework
  Radio.IrqProcess();

  // Simulation mode only: read the PC's sensor states, report relays and status (does nothing otherwise)
  simService();
  
  //Update Current Ramp Height
  updateHeight();

  //Poll Buttons for Updates
  updateButton(UpButton);
  updateButton(DownButton);
  updateButton(EnterButton);
  updateButton(BackButton);
  updateLadder(ladder);

  //OLED Timeout Handler, this will cancel any first input presses if the screen is off and wake it up. It will also turn the screen off after 1 minute.
  //When the screen goes to sleep the menu returns to the HOME page.
  handleOLEDTimeout();

  //Process any button presses
  processButtons();

  // Handle LoRa periodic telemetry broadcast and connection watchdog
  handleLoraBroadcast();
  updateRemoteWatchdog();

  // Display stuff on the OLED only if awake
  drawScreen();

  //Set Motor Direction, Check Failsafes, Drive Motor if Applicable 
  updateMotorControl();
  updateWaterControl();

  updateLEDs();
}
