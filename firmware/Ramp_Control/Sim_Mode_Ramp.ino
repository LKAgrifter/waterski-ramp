// ---------------------------------------------------------------------------
// SIMULATION MODE: talks to the PC simulator page over USB serial. Off unless the page asks for it at start-up (or
// SIM_MODE is true, main tab): the sketch then behaves exactly as normal, apart from answering the page.
// ---------------------------------------------------------------------------
//
// The simulator page (separate "waterski-ramp-sim" repo) plays the part of the ramp: it moves a virtual ramp from
// the relay states this board reports, and sends back which height sensors the virtual ramp is triggering.
//
// SERIAL PROTOCOL (version SIM_PROTOCOL_VERSION, 115200 baud, one text line each, ending in a newline)
// Lines starting with '@' are simulator traffic. Anything else (debug prints) is just shown in the page's log.
//   Switching to simulation mode (normal firmware):
//     ramp -> PC  @SIM?      At start-up, straight after Serial.begin() (before the settings are loaded or a sensor
//                            or relay is used). The ramp then listens for SIM_ASK_MS.
//     PC -> ramp  @SIM ON    Answer within SIM_ASK_MS: the ramp runs in simulation mode until its next reset or
//                            power cut. Too late (or never), it runs normally.
//     ramp -> PC  @NORMAL <version>   A ramp running normally answers @HELLO? (and a late @SIM ON) with this. The page
//                            then resets the board through the USB chip, so it starts again and asks @SIM?.
//   In simulation mode:
//   PC -> ramp:
//     @S <mask>              Height sensors: bit 0 = 4 ft, bit 1 = 4.5 ft, bit 2 = 5 ft, bit 3 = 5.5 ft (1 = triggered).
//                            Decimal, 0-15. Sent on every change and every 50 ms.
//     @HELLO?                Asks the ramp to send @HELLO again (the page connected after the board booted).
//   ramp -> PC:
//     @HELLO <version>       At boot and when asked.
//     @R <power> <dir> <pump>   Relay states, 1 = energized (dir: 1 = UP). On every change and every 100 ms.
//     @T key=value ...       Status for the page's display, every 250 ms. Heights are in tenths of a foot
//                            (45 = 4.5 ft, 0 = never located). Keys: see simSendStatus(). The page ignores keys it
//                            doesn't know, so new keys can be added without changing the protocol version.
// If no @S line arrives for SIM_SENSOR_TIMEOUT_MS, every sensor reads "not triggered" (a closed browser tab looks
// like "no sensor", which the firmware already handles safely).
// If you change any of this, change the simulator page too and bump SIM_PROTOCOL_VERSION.

constexpr unsigned long SIM_SENSOR_TIMEOUT_MS = 500;
constexpr unsigned long SIM_RELAY_REPORT_MS   = 100;
constexpr unsigned long SIM_STATUS_REPORT_MS  = 250;
constexpr unsigned int  SIM_BOOT_WAIT_MS      = 1000;   // setup() waits up to this long for the first @S, so a ramp
                                                        // parked on a (virtual) sensor is found at boot like the real one
constexpr uint8_t       SIM_LINE_MAX          = 32;     // Longest PC line accepted (longer lines are ignored)
constexpr unsigned int  SIM_ASK_MS            = 400;    // How long the normal firmware listens for "@SIM ON" at start-up

uint8_t simSensorMask = 0;          // Sensor states from the last @S line
bool simHaveSensors = false;        // An @S line arrived within SIM_SENSOR_TIMEOUT_MS
unsigned long simLastSensorAt = 0;
bool simPower = false;              // Relay states the firmware asked for (the real relay pins stay off)
bool simDir = false;
bool simPump = false;
bool simSerialReady = false;        // Serial.begin() has run
unsigned long simLastRelayReport = 0;
unsigned long simLastStatusReport = 0;
char simLine[SIM_LINE_MAX];
uint8_t simLineLen = 0;
bool simLineTooLong = false;
bool simAsking = false;             // Start-up: listening for the page's "@SIM ON"

// @HELLO in simulation mode; running normally, @NORMAL (the page then resets the board so it asks @SIM? again)
void simSendHello()
{
  Serial.printf(simActive ? "@HELLO %u\n" : "@NORMAL %u\n", (unsigned)SIM_PROTOCOL_VERSION);
}

void simSendRelays()
{
  Serial.printf("@R %d %d %d\n", simPower ? 1 : 0, simDir ? 1 : 0, simPump ? 1 : 0);
  simLastRelayReport = millis();
}

// Called by setRelay() for every relay write. setRelay() is called every loop while idle, so only a CHANGE is sent.
void simSetRelay(uint8_t pin, bool on)
{
  bool changed = false;
  if (pin == Power_Relay && simPower != on)     { simPower = on; changed = true; }
  else if (pin == Dir_Relay && simDir != on)    { simDir = on;   changed = true; }
  else if (pin == Pump_Relay && simPump != on)  { simPump = on;  changed = true; }

  if (changed && simSerialReady)
    simSendRelays();
}

// Sensor i (0 = 4 ft ... 3 = 5.5 ft) as the PC last reported it. Used by readLimitRaw() in Height_Detection_Ramp.
bool simSensorActive(uint8_t i)
{
  return simHaveSensors && (simSensorMask & (1 << i)) != 0;
}

void simHandleLine(const char *line)
{
  if (strcmp(line, "@SIM ON") == 0)
  {
    if (simAsking)
      simActive = true;   // The page answered at start-up: simulation mode until the next reset
    else
      simSendHello();     // Already decided: @HELLO (simulation mode) or @NORMAL (too late: the page resets the board)
  }
  else if (!simActive)
  {
    if (strcmp(line, "@HELLO?") == 0)
      simSendHello();     // @NORMAL: tells the page this board runs normally
  }
  else if (strncmp(line, "@S ", 3) == 0)
  {
    char *end;
    long mask = strtol(line + 3, &end, 10);
    if (end != line + 3 && *end == '\0' && mask >= 0 && mask <= 15)   // Ignore anything malformed
    {
      simSensorMask = (uint8_t)mask;
      simHaveSensors = true;
      simLastSensorAt = millis();
    }
  }
  else if (strcmp(line, "@HELLO?") == 0)
  {
    simSendHello();
  }
}

// Reads whatever the PC has sent, a line at a time. Never waits.
void simReadSerial()
{
  while (Serial.available() > 0)
  {
    char c = (char)Serial.read();
    if (c == '\r')
      continue;
    if (c == '\n')
    {
      simLine[simLineLen] = '\0';
      if (!simLineTooLong)
        simHandleLine(simLine);
      simLineLen = 0;
      simLineTooLong = false;
    }
    else if (simLineLen < SIM_LINE_MAX - 1)
    {
      simLine[simLineLen++] = c;
    }
    else
    {
      simLineTooLong = true;
    }
  }
}

// From setup(), straight after Serial.begin(), before the settings are loaded or a sensor or relay is used:
// asks the simulator page whether to run in simulation mode (unless SIM_MODE already says so)
void simBegin()
{
  simSerialReady = true;
  if (!simActive)
  {
    Serial.println("@SIM?");
    simAsking = true;
    for (unsigned int i = 0; i < SIM_ASK_MS && !simActive; i++)
    {
      simReadSerial();
      delay(1);
    }
    simAsking = false;
    if (!simActive)
      return;   // No simulator page: start normally
  }

  Serial.println("*** SIMULATION MODE: height sensors come from USB, relays are NOT switched ***");
  simSendHello();
  simSendRelays();
}

// From setup(), just before initHeight(): give the PC a moment to send its first sensor reading
void simWaitForSensors()
{
  if (!simActive)
    return;

  for (unsigned int i = 0; i < SIM_BOOT_WAIT_MS && !simHaveSensors; i++)
  {
    simReadSerial();
    delay(1);
  }
}

// Height in tenths of a foot for @T (0 when unknown)
int simTenths(float height)
{
  return (int)(height * 10.0f + 0.5f);
}

const char *simScreenName()
{
  switch (currentScreen)
  {
    case SCREEN_BOOT:        return "BOOT";
    case SCREEN_HOME:        return "HOME";
    case SCREEN_SET_HEIGHT:  return "SET_HEIGHT";
    case SCREEN_HEIGHT:      return "MANUAL";
    case SCREEN_WATER:       return "WATER";
    case SCREEN_NETWORK:     return "NETWORK";
    case SCREEN_DIAGNOSTICS: return "DIAGNOSTICS";
    case SCREEN_GAME:        return "GAME";
  }
  return "?";
}

// @T keys:
//   h   located height (tenths of a ft)     t   target height        side  -1 below / 0 on / +1 above h
//   cal CAL REQUIRED                        sf  height sensor fault  sfm   sensors in that fault (bit mask)
//   to  motor timeout fault                 fc  fault count          mv    motor running   up  direction up
//   man manual drive (0 none, 1 up, 2 down) w   water switched on    wm    water minutes left
//   pg  screen page                         rc  remote connected     byp   sensor bypass on
//   step / zone  last measured step and sensor-zone time (ms)       last  last AUTO stop ended on a switch
void simSendStatus()
{
  Serial.printf("@T h=%d t=%d side=%d cal=%d sf=%d sfm=%u to=%d fc=%d mv=%d up=%d man=%u w=%d wm=%u pg=%s rc=%d byp=%d "
                "step=%lu zone=%lu last=%d\n",
                simTenths(state.Ramp_Height), simTenths(state.Desired_Height), (int)state.Pos_Side,
                diag.powerLoss ? 1 : 0, diag.heightSensorFault ? 1 : 0, (unsigned)diag.heightFaultMask,
                diag.motorTimeoutFault ? 1 : 0, diag.faultCount,
                state.Motor_Running ? 1 : 0, state.Motor_Direction_Up ? 1 : 0,
                (unsigned)(state.Control_Mode == MODE_MANUAL ? state.Manual_Request : 0),
                state.Water_Relay ? 1 : 0, (unsigned)waterRemainingMinutes(), simScreenName(),
                state.Remote_Connected ? 1 : 0, heightSafetyBypassActive() ? 1 : 0,
                state.Last_Step_Ms, state.Last_Zone_Ms, state.Last_Stop_On_Switch ? 1 : 0);
  simLastStatusReport = millis();
}

// From loop(), before updateHeight()
void simService()
{
  simReadSerial();   // Running normally this only answers the page's @HELLO? / @SIM ON (with @NORMAL)
  if (!simActive)
    return;

  if (simHaveSensors && (millis() - simLastSensorAt) > SIM_SENSOR_TIMEOUT_MS)
  {
    simHaveSensors = false;   // PC went quiet: every sensor reads "not triggered"
    simSensorMask = 0;
  }

  if ((millis() - simLastRelayReport) >= SIM_RELAY_REPORT_MS)
    simSendRelays();
  if ((millis() - simLastStatusReport) >= SIM_STATUS_REPORT_MS)
    simSendStatus();
}
