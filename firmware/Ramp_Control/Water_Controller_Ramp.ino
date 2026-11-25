// ---------------------------------------------------------------------------
// Water pump control
//
//  LOCAL  (Water page on the ramp): BACK tap = +1 h (1..12 h). The tap after 12 h turns it off. BACK hold = off now.
//  Each remote has its own saved "water mode" (set on the remote):
//  BOAT remote  (waterCommand 1): on = a 1 h timer that the remote keeps refreshing with keep-alives
//         (waterCommand 2) while it can hear the ramp. If the remote dies / goes out of range the water stops
//         within an hour. For a remote that stays powered while it's in use (in the boat).
//  TIMER remote (waterCommand 3): exactly like a tap on the ramp's own Water page (+1 h, 1..12 h, the tap after 12 h
//         turns it off). No keep-alives needed. For a remote that sleeps between presses (solar, on shore).
//  Off (waterCommand 0) = off immediately, whoever turned it on.
//  A remote's keep-alive only refreshes water that a BOAT remote started, so a timer set at the ramp or from a
//  TIMER remote is never extended behind your back by a remote that happens to be switched on.
//
//  MOTOR INTERLOCK: state.Water_Relay means "water is switched on". The pump relay itself is driven by
//  servicePumpRelay() (Motor_and_Failsafes_Ramp.ino), which drops it out before the motor gets power and turns it
//  back on WATER_RESUME_DELAY_MS after the motor stops. The water timer keeps counting while it is paused.
// ---------------------------------------------------------------------------

void waterOn() //Turn on thew water and set timer start
{
  if(!state.Water_Relay)
  {
  state.Water_RelayStart = millis(); //Set Relay Timer
  state.Water_Relay = true; //Declare water on state
  servicePumpRelay(); //Relay on now - unless the ramp is moving, then it comes on once the motor has stopped
  }
}

void waterOff() //Turn off the water relay and state
{
  state.Water_Relay = false;
  state.Water_Timeout = 0;   //So the next local tap starts again at 1 h (it used to keep counting from the old value)
  servicePumpRelay(); //Relay off now
}

void updateWaterControl() //Run this every cycle to check the timer
{ 
  if(!state.Water_Relay)
    return;

  const uint32_t ONE_HOUR = 60UL * 60UL * 1000UL; //Constant to calculate millis from hours

  if(millis() - state.Water_RelayStart >= (uint32_t)state.Water_Timeout * ONE_HOUR) //Shutoff timer
  {
    waterOff();
  }
}

// Minutes left on the water timer (rounded up). 0 if the water is off.
uint16_t waterRemainingMinutes()
{
  if(!state.Water_Relay)
    return 0;

  const uint32_t ONE_HOUR = 60UL * 60UL * 1000UL;
  uint32_t total   = (uint32_t)state.Water_Timeout * ONE_HOUR;
  uint32_t elapsed = millis() - state.Water_RelayStart;
  uint32_t remaining = (elapsed >= total) ? 0 : (total - elapsed);

  return (uint16_t)((remaining + 59999UL) / 60000UL);
}

// Remote keep-alive (waterCommand 2). Restarts the timer, but only for water a remote started.
void waterKeepAlive()
{
  if(state.Water_Relay && state.Water_Source == SOURCE_REMOTE)
  {
    state.Water_RelayStart = millis();
  }
}

// One tap on a fixed timer: +1 h (1..12 h), the tap after 12 h turns the water off.
// remoteId 0 = the ramp's own Water page, 1..MAX_REMOTES = a remote in TIMER mode (waterCommand 3).
// Either way the water becomes a fixed timer (Water_Source = SOURCE_LOCAL), so keep-alives never stretch it.
void waterTimerTap(uint8_t remoteId)
{
  state.Water_Source = SOURCE_LOCAL;
  state.Water_TimerRemoteId = remoteId;
  if(state.Water_Timeout < 12) //If the water timer is set to less than 12 hours, increase timer up
  {
    state.Water_Timeout ++;
    waterOn();
  }
  else
  {
    waterOff(); //12 h reached: turn water off (also resets the timer to 0)
  }
}

void waterControl(bool activate = false, ControlSource source = SOURCE_LOCAL)
{
  if(activate)
  {
    if(source == SOURCE_LOCAL)
    {
      waterTimerTap(0);
      return;
    }
    else
    {
      if(state.Water_Relay)
      {
        // Already running. If a remote owns it this just refreshes the timer; if the ramp owns it, leave its timer alone.
        waterKeepAlive();
        return;
      }
      state.Water_Source = SOURCE_REMOTE;
      state.Water_Timeout = 1; //Will time out if water command is not recieved by controller for 60 minutes (Basically water auto-turns off if controller is powered off)
    }
    waterOn(); 
  }

  else
  {
    waterOff();
  }
}
