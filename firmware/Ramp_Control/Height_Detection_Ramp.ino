// ---------------------------------------------------------------------------
// Height detection from the four limit switches (4ft, 4.5ft, 5ft, 5.5ft)
// ---------------------------------------------------------------------------
//
// SWITCH TYPE - change this one line when you swap to normally-closed sensors.
// The pins use INPUT_PULLUP, with each switch wired between the pin and GND.
//   Normally open   (NO): pin reads LOW  when the switch is triggered
//   Normally closed (NC): pin reads HIGH when the switch is triggered
constexpr uint8_t LIMIT_ACTIVE_LEVEL = HIGH;  // HIGH = NC sensors (what is fitted), LOW = NO sensors

constexpr unsigned long LIMIT_DEBOUNCE_MS = 15;

// No default member initializers on purpose, so this stays a plain aggregate on older ESP32 cores.
struct LimitSwitch
{
  uint8_t pin;
  float height;               // Ramp height this switch represents
  bool active;                // Debounced state (true = triggered)
  bool lastReading;           // Raw state from the previous loop
  unsigned long debounceTimer;
};

LimitSwitch limits[] =
{
  { LMT_4,  4.0f, false, false, 0 },   // bit 0 in diag.heightFaultMask
  { LMT_45, 4.5f, false, false, 0 },   // bit 1
  { LMT_5,  5.0f, false, false, 0 },   // bit 2
  { LMT_55, 5.5f, false, false, 0 }    // bit 3
};

constexpr uint8_t LIMIT_COUNT = sizeof(limits) / sizeof(limits[0]);

// Raw (not debounced) reading of switch i: true = triggered. In simulation mode it comes from the PC instead of the pin.
bool readLimitRaw(uint8_t i)
{
  if (SIM_MODE)
    return simSensorActive(i);
  return digitalRead(limits[i].pin) == LIMIT_ACTIVE_LEVEL;
}


void powerLossRecovery() //This is to aid in the height calibration after power loss.
{
  // Always start from the last saved height (it was saved when the ramp last stopped). If it was MOVING when the power
  // went, that height can't be trusted: flag CAL REQUIRED. (Also the case on the first boot after flashing.)
  state.Ramp_Height = prefs.getFloat("LastHeight", state.Ramp_Height);
  if(prefs.getBool("Moving", true))
  {
    diag.powerLoss = true;
  }
  if(state.Ramp_Height < 4.0f)
  {
    diag.powerLoss = true;   // Never located at all
  }
}


// Latches the fault, records which switches were active together (first event only), and counts it once.
// Requires "uint8_t heightFaultMask = 0;" in the Diagnostics struct.
void setHeightSensorFault(uint8_t mask)
{
  if(!diag.heightSensorFault)
  {
    diag.heightSensorFault = true;
    diag.heightFaultMask = mask;
    diag.faultCount++;
  }
}


// Call once from setup(), after the limit pins are set to INPUT_PULLUP (and after the delay(100) following Vext).
// Reads the switches with no debounce, takes the height from a triggered switch if there is one,
// and syncs Desired_Height to the ramp so the ramp doesn't drive off toward 0 on boot.
void initHeight()
{
  uint8_t activeCount = 0;
  uint8_t activeMask = 0;
  float activeHeight = 0.0f;

  for(uint8_t i = 0; i < LIMIT_COUNT; i++)
  {
    bool raw = readLimitRaw(i);

    limits[i].active = raw;
    limits[i].lastReading = raw;
    limits[i].debounceTimer = millis();

    if(raw)
    {
      activeCount++;
      activeMask |= (1 << i);
      activeHeight = limits[i].height;
    }
  }

  if(activeCount == 1)
  {
    state.Ramp_Height = activeHeight;
    diag.powerLoss = false;   //Parked on a switch, so the position is known and no calibration is needed
  }
  else if(activeCount > 1)
  {
    setHeightSensorFault(activeMask);   //Two switches can't be triggered at once (wiring fault, or NC sensor unplugged)
  }
  else
  {
    // No switch active at power-up: the ramp could have been moved (by hand, or it coasted) while it was off, so the
    // saved height can't be trusted. Require calibration: AUTO and remote moves stay locked out until the ramp is
    // driven onto a switch with the manual buttons at the ramp.
    diag.powerLoss = true;
  }

  state.Desired_Height = state.Ramp_Height;
}


// Call every loop. Ramp_Height is only updated while a switch is triggered; between switches
// it holds the last located height.
void updateHeight()
{
  uint8_t activeCount = 0;
  uint8_t activeMask = 0;
  float activeHeight = 0.0f;

  for(uint8_t i = 0; i < LIMIT_COUNT; i++)
  {
    bool raw = readLimitRaw(i);

    if(raw != limits[i].lastReading)
    {
      limits[i].debounceTimer = millis();
    }
    limits[i].lastReading = raw;

    if((millis() - limits[i].debounceTimer) > LIMIT_DEBOUNCE_MS)
    {
      limits[i].active = raw;
    }

    if(limits[i].active)
    {
      activeCount++;
      activeMask |= (1 << i);
      activeHeight = limits[i].height;
    }
  }

  static uint8_t prevMask = 0;

  if(activeCount == 1)
  {
    state.Ramp_Height = activeHeight;
    state.Pos_Side = 0;       //Sitting on the switch for Ramp_Height
    if(state.Desired_Height < 4.0f)
      state.Desired_Height = activeHeight;   //First time the height is ever found: hold here (no target was set yet)
    diag.powerLoss = false;   //Reaching any switch confirms the real position. Delete this line if you want calibration to be a separate step.
  }
  else if(activeCount > 1)
  {
    setHeightSensorFault(activeMask);   //Ramp_Height is left alone while the readings are contradictory
  }
  else if(prevMask != 0 && state.Motor_Running)
  {
    // Just drove OFF the switch: we're now on that side of Ramp_Height. (Coasting off after the motor has stopped
    // doesn't count, so the ramp never "hunts" back and forth around a switch.)
    state.Pos_Side = state.Motor_Direction_Up ? 1 : -1;
  }

  // Reached a height switch while moving:
  uint8_t newlyHit = activeMask & ~prevMask;
  if(newlyHit != 0 && state.Motor_Running)
  {
    // MISSED SWITCH: an AUTO move reached a switch BEYOND the height it was heading for, so the target's switch never
    // triggered. Stop and flag it - otherwise the ramp would bounce between the switches either side forever.
    if(state.Control_Mode == MODE_AUTO && activeCount == 1 &&
       ((state.Motor_Direction_Up && activeHeight > state.Leg_Target) ||
        (!state.Motor_Direction_Up && activeHeight < state.Leg_Target)))
    {
      uint8_t missed = 0;
      for(uint8_t i = 0; i < LIMIT_COUNT; i++)
        if(limits[i].height == state.Leg_Target) missed = (1 << i);
      setHeightSensorFault(missed);
      motorStop();
    }
    else
    {
      // Measure the step for the Diagnostics page (only a clean switch-to-switch step: motor started ON a switch or
      // passed one, and didn't stop on the way)
      if(state.Step_From_Switch)
        state.Last_Step_Ms = millis() - state.MotorStartTime;
      state.Step_From_Switch = true;
      state.MotorStartTime = millis();   // Restart the motor safety timeout (it's per height step, not per whole move)
    }
  }
  // Switch "zone" measurement for calibrating STOP_OVERRUN_MS: how long a switch stays on while the ramp drives
  // straight through it (motor running the whole time, same direction).
  static unsigned long zoneStart = 0;
  static bool zoneDirUp = false;
  if(!state.Motor_Running)
  {
    zoneStart = 0;
  }
  else if(newlyHit != 0 && activeCount == 1)
  {
    zoneStart = millis();
    zoneDirUp = state.Motor_Direction_Up;
  }
  else if(prevMask != 0 && activeMask == 0 && zoneStart != 0)
  {
    if(zoneDirUp == state.Motor_Direction_Up)
      state.Last_Zone_Ms = millis() - zoneStart;
    zoneStart = 0;
  }

  prevMask = activeMask;

  if(state.Control_Mode == MODE_MANUAL)
  {
    state.Desired_Height = state.Ramp_Height; //This will mean that if you change the height in manual mode, it will stay at whatever height it is set to because the desired height and ramp height
    //continuously update together
  }
}


// ---------------------------------------------------------------------------
// Helpers added for manual drive / diagnostics
// ---------------------------------------------------------------------------

// Bitmask of the switches that are triggered right now (bit 0 = 4ft ... bit 3 = 5.5ft). Same bit order as heightFaultMask.
uint8_t limitMask()
{
  uint8_t mask = 0;
  for(uint8_t i = 0; i < LIMIT_COUNT; i++)
  {
    if(limits[i].active) mask |= (1 << i);
  }
  return mask;
}

// True if the ramp is parked on any limit switch (so its position is known)
bool anyLimitActive()
{
  return limitMask() != 0;
}
