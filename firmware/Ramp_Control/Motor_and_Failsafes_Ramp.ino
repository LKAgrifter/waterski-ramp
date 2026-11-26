//Write motor driections
//MANUAL DRIVE: call setManualDrive(1 /*up*/ or 2 /*down*/, source, remoteId) every time you see the request
//(buttons: every loop while held; remote: every packet). To stop, call cancelManualControl() - or, for a remote,
//just stop sending: the 1 s dead-man in updateMotorControl() drops back to AUTO.
//updateMotorControl() does the direction-change delay and runs all the failsafes, so the motor is never driven directly.
void clearDirectionChange()
{
    state.DirectionChangePending = false;
    state.MotorPowerPending = false;
}

void requestMotorUp(ControlSource source = SOURCE_LOCAL, bool manual = false) 
{
    if(!state.Motor_Direction_Up)
    {
        if (!state.DirectionChangePending || state.Motor_Running) 
        {
            motorStop();
            // The reversal delay is counted from when the motor STOPPED (so it has spun down), not from this press:
            // if it's been sitting still for a while, it reverses straight away.
            state.DirectionChangeStart = state.MotorStoppedAt;
            state.DirectionChangePending = true; //We are now waiting on a direction change
            state.MotorPowerPending = true; //We also are waiting to power the motor
            // (no return: if the motor has already been stopped long enough, the change completes right below)
        }
        if (millis() - state.DirectionChangeStart >= Change_Delay)
          {
              state.Motor_Direction_Up = true;   // (the direction relay itself is only energized while the motor runs - see runMotor)
              state.DirectionChangePending = false; //Direction change has been completed!
          }
        else
          {
            return; //Still waiting, don't want to set state to manual yet
          }
    }

    else if (state.DirectionChangePending)
    {
        // Already facing Up, so a pending reversal to Down was abandoned
        clearDirectionChange();
    }
    
    state.Control_Mode = manual ? MODE_MANUAL : MODE_AUTO;
    state.Control_Source = source;
}

void requestMotorDown(ControlSource source = SOURCE_LOCAL, bool manual = false)
{
    if (state.Motor_Direction_Up)
    {
        if (!state.DirectionChangePending || state.Motor_Running)
        {
            motorStop();
            state.DirectionChangeStart = state.MotorStoppedAt;   // Counted from when the motor stopped (see requestMotorUp)
            state.DirectionChangePending = true;
            state.MotorPowerPending = true;
        }

        if (millis() - state.DirectionChangeStart >= Change_Delay)
        {
            state.Motor_Direction_Up = false;
            state.DirectionChangePending = false;
        }
        else
        {
            return;
        }
    }
    else if (state.DirectionChangePending)
    {
        clearDirectionChange();
    }

    state.Control_Mode = manual ? MODE_MANUAL : MODE_AUTO;
    state.Control_Source = source;
}

void motorStop() 
{
  if(state.Motor_Running)
  {
    // Only call the position "known" if we stopped ON a limit switch. A stop anywhere else (manual release,
    // fault, reversal) leaves the ramp somewhere between heights, so keep "Moving" set: if power is lost
    // from here the next boot will ask for calibration. (Manual drive makes between-height stops common.)
    prefs.putBool("Moving", !anyLimitActive());
    prefs.putFloat("LastHeight", state.Ramp_Height);
    prefs.putBool("MotorUp",state.Motor_Direction_Up);
    state.MotorStoppedAt = millis();   // The reversal delay counts from here
  }

  state.localOverrideActive = false; // Once the ramp finishes its move, allow remote control again, always
  
  setRelay(Power_Relay, false);  //Deactivate Motor Power
  state.Motor_Running = false; //Set state of motor to stopped

  // The direction relay is only energized while driving UP. Let it drop out shortly AFTER the power relay opens
  // (so it never switches under load); serviceDirRelay() releases it.
  if (state.DirRelayOn && !state.DirRelayDropPending)   // (motorStop() is called every loop while idle - only arm the timer once)
  {
    state.DirRelayDropPending = true;
    state.DirRelayDropAt = millis() + DIR_RELAY_DROP_MS;
  }

}

// Drop out of manual drive and go back to AUTO. Desired_Height is set to the current height so AUTO just sits still
// (updateHeight() already keeps them equal while manual is active, this makes it explicit).
void cancelManualControl()
{
    state.Manual_Request = 0;
    state.Control_Mode = MODE_AUTO;
    state.Desired_Height = state.Ramp_Height;
    state.Height_Request_Active = false;   // Stay wherever manual left it (even between heights) until a new height is picked
    clearDirectionChange();
}

// True while a person is holding a manual button on the ramp itself (local always wins over a remote)
bool localManualActive()
{
    return state.Control_Mode == MODE_MANUAL && state.Control_Source == SOURCE_LOCAL;
}

// Ask for manual drive. dir: 1 = up, 2 = down.
// Mode is switched to MANUAL right away (not after the direction-change delay) so the AUTO logic can't
// cancel a pending direction flip. The real work happens in updateMotorControl().
void setManualDrive(uint8_t dir, ControlSource source, uint8_t remoteId)
{
    if (dir != 1 && dir != 2)
        return;

    state.Manual_Request = dir;
    state.Control_Mode = MODE_MANUAL;
    state.Control_Source = source;
    state.ManualRemoteId = remoteId;
    state.LastManualPacket = millis();   // Feeds the 1 s remote dead-man
    if (source == SOURCE_LOCAL)
        releaseRemoteControl();          // Driving at the ramp takes control away from a remote
}

// TESTING ONLY. True when the height sensor safeties are bypassed.
bool heightSafetyBypassActive()
{
    return ALLOW_HEIGHT_SAFETY_BYPASS && state.Height_Safety_Bypass;
}

// Turn the bypass on/off (from the ramp menu or from a remote). Always stops the motor and drops to a clean AUTO state,
// so nothing can start moving the instant the safeties come back (or go away).
void setHeightSafetyBypass(bool on)
{
    if (!ALLOW_HEIGHT_SAFETY_BYPASS)
        on = false;

    if (state.Height_Safety_Bypass == on)
        return;

    state.Height_Safety_Bypass = on;

    cancelManualControl();
    motorStop();
    state.localOverrideActive = false;

    Serial.println(on ? "*** HEIGHT SENSOR SAFETY BYPASS ON (testing only) ***" : "Height sensor safety bypass off");
}

// Releases the direction relay (back to its normal, de-energized state) a moment after the motor stops, so the coil
// isn't left drawing power while the ramp sits idle. Called every loop from updateMotorControl().
void serviceDirRelay()
{
  if (state.DirRelayDropPending && !state.Motor_Running && (long)(millis() - state.DirRelayDropAt) >= 0)
  {
    setRelay(Dir_Relay, false);
    state.DirRelayOn = false;
    state.DirRelayDropPending = false;
  }
}

// The pump relay's NC contact is wired in the motor's supply, so the pump relay must be OFF for the motor to run.
// It is held off while the motor runs or is starting, and for WATER_RESUME_DELAY_MS after the motor last ran (that
// also keeps it off through a direction reversal, and stops it short-cycling during manual jogging). It is only ever
// switched while the motor power relay is open, so its contacts never make or break motor current.
// If the water is switched off it simply stays off. Called every loop from updateMotorControl() and from the water code.
void servicePumpRelay()
{
  bool want = state.Water_Relay;

  // (Also held off during a pending direction reversal, however long Change_Delay is.)
  if (state.Motor_Running || state.DirectionChangePending || state.MotorPowerPending ||
      (millis() - state.MotorBusyAt) < WATER_RESUME_DELAY_MS)
    want = false;

  if (want != state.PumpRelayOn)
  {
    setRelay(Pump_Relay, want);
    state.PumpRelayOn = want;
    if (!want)
      state.PumpRelayOffAt = millis();
  }
}

bool waterPausedForMotor()   // Water is switched on but held off because the ramp is moving (for the screens)
{
  return state.Water_Relay && !state.PumpRelayOn;
}

void runMotor()
{
  state.MotorBusyAt = millis();   // Keeps the water paused while we run (and for WATER_RESUME_DELAY_MS after)

  if (!state.Motor_Running) 
  {
    // 1) Water relay off first (its NC contact feeds the motor). Power relay is still open, so this switches no load.
    if (state.PumpRelayOn)
    {
      setRelay(Pump_Relay, false);
      state.PumpRelayOn = false;
      state.PumpRelayOffAt = millis();
    }

    // Set the direction relay for this run (energized = Up, normal/off = Down) and give it time to settle
    // BEFORE motor power is applied.
    if (state.Motor_Direction_Up != state.DirRelayOn)
    {
      setRelay(Dir_Relay, state.Motor_Direction_Up);
      state.DirRelayOn = state.Motor_Direction_Up;
      state.DirRelaySetAt = millis();
    }
    state.DirRelayDropPending = false;   // Starting again: don't let a pending release drop it mid-run

    // 2) Only apply motor power once both relays have had time to settle
    if (millis() - state.DirRelaySetAt < Relay_Settle_Delay || millis() - state.PumpRelayOffAt < Relay_Settle_Delay)
      return;

    state.MotorStartTime = millis();
    state.Step_From_Switch = anyLimitActive();   // Starting on a switch = this step can be timed
    state.Leg_Target = state.Desired_Height;   // For the missed-switch check in updateHeight()

    prefs.putBool("Moving", true); //Save to flash that the ramp is moving, so if the power goes out, we know it was between heights!
    prefs.putFloat("LastHeight", state.Ramp_Height); //Store the last located ramp height
    prefs.putBool("MotorUp",state.Motor_Direction_Up); //Store the motor direction (we know if it's above or below last height)
  }

  state.Motor_Running = true;  //Since safety checks passed, we're good to go!
  setRelay(Power_Relay, true);
}

//Ramp Motion and Failsafes

void updateMotorControl()  //This function handles ALL processing relating to motor Power
{
  serviceDirRelay();
  servicePumpRelay();

  const bool bypass = heightSafetyBypassActive();

  bool overrunning = false;   // AUTO stop overrun (set below): lets the ramp run its short overrun past the end switches

  // ---- Remote manual dead-man: no manual packet for 1 s -> back to AUTO (which stops the motor) ----
  if (state.Control_Mode == MODE_MANUAL && state.Control_Source == SOURCE_REMOTE &&
      (millis() - state.LastManualPacket) > REMOTE_MANUAL_TIMEOUT_MS)
  {
    cancelManualControl();
  }

  if (state.Control_Mode == MODE_MANUAL)
  {
    // ---- MANUAL: direction comes from the manual request, no target height ----
    if (state.Manual_Request == 1)
    {
      requestMotorUp(state.Control_Source, true);
    }
    else if (state.Manual_Request == 2)
    {
      requestMotorDown(state.Control_Source, true);
    }
    else
    {
      cancelManualControl();
      motorStop();
      return;
    }
  }
  else
  {
    // ---- AUTO ----

    // TESTING: with the height sensor safeties bypassed we don't know where the ramp is, so AUTO never moves it
    if (bypass)
    {
      motorStop();
      clearDirectionChange();
      return;
    }

    // STOP OVERRUN in progress: keep driving the same way until STOP_OVERRUN_MS has passed, then stop.
    // (Cancelled if the target changes or the motor was stopped by something else.)
    if (state.Overrun_Active)
    {
      if (state.Motor_Running && state.Desired_Height == state.Overrun_Target)
      {
        if (millis() - state.Overrun_Start < STOP_OVERRUN_MS)
        {
          overrunning = true;
        }
        else
        {
          state.Overrun_Active = false;
          state.Last_Stop_On_Switch = anyLimitActive();
          motorStop();
          clearDirectionChange();
          return;
        }
      }
      else
      {
        state.Overrun_Active = false;
      }
    }

    if (!overrunning)
    {
      // At the target? If we've moved off Ramp_Height's switch and a height request is active (e.g. you picked 5 ft and
      // then went back to 4 ft before it got there), we're NOT there yet - drive back to it.
      bool atTarget = (state.Ramp_Height == state.Desired_Height) &&
                      (state.Pos_Side == 0 || !state.Height_Request_Active);
      if (atTarget)
      {
        // Only an AUTO move to a picked height overruns. A manual release, a remote stop or the dead-man must stop NOW.
        const bool arrivedFromRequest = state.Height_Request_Active;
        state.Height_Request_Active = false;

        if (arrivedFromRequest && state.Motor_Running && STOP_OVERRUN_MS > 0 && anyLimitActive())
        {
          // Just arrived on the target's switch: don't stop at its first trigger (the edge of the magnet's range),
          // keep going a little to park nearer the middle.
          state.Overrun_Active = true;
          state.Overrun_Start = millis();
          state.Overrun_Target = state.Desired_Height;
          overrunning = true;
        }
        else
        {
          if (state.Motor_Running)
            state.Last_Stop_On_Switch = anyLimitActive();
          motorStop();
          clearDirectionChange();
          return;
        }
      }
    }

    if (overrunning)
    {
      // Same direction as we arrived
      if (state.Motor_Direction_Up) requestMotorUp(); else requestMotorDown();
    }
    else
    {
      state.Leg_Target = state.Desired_Height;   // Follow target changes mid-move (e.g. 5 ft -> 5.5 ft while still going up)

      //Choose the motor direction (Pos_Side says which side of Ramp_Height we are on when the target equals it)
      if (state.Desired_Height > state.Ramp_Height ||
          (state.Desired_Height == state.Ramp_Height && state.Pos_Side < 0))
      {
        requestMotorUp();
      } 
      else
      {
        requestMotorDown();
      }
    }
  }

  // Safety Checks. Ramp_Height only changes when a limit switch triggers, so after driving up off the 4ft switch it
  // still reads 4.0. AUTO therefore uses the last located height, but MANUAL stops only when the ramp is actually ON the
  // end switch - otherwise you could never drive back down after moving off the bottom (or back up off the top).
  // (Skipped during the stop overrun: that's a few seconds past the 4 / 5.5 ft switch on purpose, limited by STOP_OVERRUN_MS.)
  if (!bypass && !overrunning)
  {
    const bool manual = (state.Control_Mode == MODE_MANUAL);
    // (AUTO: "at 5.5" only if we haven't moved below it since, so it can drive back UP to 5.5 after a reversal)
    const bool atTop    = manual ? limits[LIMIT_COUNT - 1].active : (state.Ramp_Height >= 5.5 && state.Pos_Side >= 0);
    const bool atBottom = manual ? limits[0].active               : (state.Ramp_Height <= 4   && state.Pos_Side <= 0);

    if (state.Motor_Direction_Up && atTop) 
    {
      motorStop();
      return;
    }

    if (!state.Motor_Direction_Up && atBottom) 
    {
      motorStop();
      return;
    }
  }

  //Overcurrent
  // TODO: read Current_Sense here and set diag.overCurrentFault (+ faultCount) when it trips. The diagnostics
  // screens and the remote link already carry the flag.

  //Timeout PER HEIGHT STEP (restarts at every height switch - see updateHeight()). Applies in bypass too: it protects the motor.
  if(state.Motor_Running && millis() - state.MotorStartTime > MOTOR_STEP_TIMEOUT_MS)
  {
      motorStop();
      if(!diag.motorTimeoutFault)
      {
        diag.motorTimeoutFault = true;
        diag.faultCount++;
      }
      return;
  }
  if(diag.motorTimeoutFault)
  {
    motorStop();
    return;
  }

  if(!bypass && diag.heightSensorFault)
  {
    motorStop();
    return;
  }

  //General System Error Flags

  //CAL REQUIRED (power loss during motion / no sensor at power-up): no automatic moves, and no remote manual drive -
  //only a person at the ramp may drive it to a sensor to recalibrate. Applies even with the bypass on.
  //Sensor bypass on: remote manual drive is never allowed either (end limits and sensor faults are being ignored, so
  //only someone standing at the ramp drives it). The radio handler already refuses these; this is the backstop.
  //ALLOW_REMOTE_MANUAL false: remote manual drive is never allowed.
  if((diag.powerLoss && (state.Control_Mode == MODE_AUTO || state.Control_Source == SOURCE_REMOTE)) ||
     ((bypass || !ALLOW_REMOTE_MANUAL) && state.Control_Mode == MODE_MANUAL && state.Control_Source == SOURCE_REMOTE))
  {
    motorStop();
    return;
  }

  // Never apply power while a direction change is still pending (the direction flag hasn't flipped yet)
  if (state.DirectionChangePending)
    return;

  // Delay between motor state flip
  if (state.MotorPowerPending)
    {
        if(millis() - state.DirectionChangeStart >= (Change_Delay + Relay_Settle_Delay))
        {
        state.MotorPowerPending = false;
        }
        else
        {
            return;
        }
    }

  runMotor(); //Because everything is good and dandy and all variables are set, we can run the motor!
}
