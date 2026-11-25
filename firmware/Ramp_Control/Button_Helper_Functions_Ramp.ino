// ===========================================================================
// Button processing (RAMP side)
// ===========================================================================
//
//  HOME page      : status only, nothing does anything
//  SET HEIGHT     : the ladder buttons select 4 / 4.5 / 5 / 5.5 ft (the ONLY page where they work)
//  MANUAL page    : UP / DOWN = hold-to-run motor.  Hold BACK 2 s = toggle sensor bypass (testing)
//  WATER page     : BACK tap = +1 h (cycles 1..12 h, then off), BACK hold = off
//  NETWORK / DIAG : UP / DOWN scroll
//  Every page     : HOLD the MENU button (Enter) to go to the next page.
//  HOME           : HOLD MENU + BACK together = hidden GAMES page (hold MENU there to leave, back to HOME)

const char *ladderName(LadderButton b)
{
    switch (b)
    {
        case BTN_4FT:  return "4.0ft";
        case BTN_45FT: return "4.5ft";
        case BTN_5FT:  return "5.0ft";
        case BTN_55FT: return "5.5ft";
        default:       return "none";
    }
}

// Calibration mode: print the raw ladder reading to Serial Monitor 4x a second
void printLadderCalibration()
{
    static unsigned long last = 0;
    if (millis() - last < 250)
        return;
    last = millis();
    Serial.printf("Ladder ADC: %4d  ->  %s\n", ladderRawAdc, ladderName(readLadderFromRaw(ladderRawAdc)));
}

void processButtons()
{
    // Height select ONLY works on the SET HEIGHT page. Anywhere else a height press just shows a reminder on HOME.
    // (In ladder calibration mode the buttons only show readings.)
    if (LADDER_CALIBRATION_MODE)
        printLadderCalibration();
    else if (currentScreen == SCREEN_SET_HEIGHT)
        heightSelection();
    else if (ladder.pressedEvent && ladder.current != BTN_NONE)
    {
        heightHintUntil = millis() + HEIGHT_HINT_MS;
        waterHintUntil = 0;
    }

    // Everything below is menu stuff, ignored until the boot screen is done
    if (currentScreen == SCREEN_BOOT)
        return;

    // BACK/WATER does nothing on HOME: show where the water controls are instead
    if (currentScreen == SCREEN_HOME && wasPressed(WaterButton))
    {
        waterHintUntil = millis() + HEIGHT_HINT_MS;
        heightHintUntil = 0;
    }

    // Hidden GAMES page: hold MENU + BACK together on HOME. (Only from HOME, because BACK does things on other pages,
    // e.g. a BACK press on the Water page adds an hour.) While both are down, MENU doesn't count as a page-change hold.
    if (currentScreen == SCREEN_HOME && MenuButton.state && BackButton.state)
    {
        unsigned long since = (MenuButton.pressedTime > BackButton.pressedTime) ? MenuButton.pressedTime : BackButton.pressedTime;
        if (millis() - since >= MENU_HOLD_MS)
        {
            MenuButton.longPressHandled = true;   // Still holding them as the games open: don't page or act again
            BackButton.longPressHandled = true;
            changeScreen(SCREEN_GAME);
        }
        return;
    }

    // Hold MENU -> next page
    if (isHeld(MenuButton, MENU_HOLD_MS))
    {
        nextScreen();
        return;
    }

    switch (currentScreen)
    {
        case SCREEN_HEIGHT:      processManualPage(); break;
        case SCREEN_WATER:       processWaterPage();  break;
        case SCREEN_NETWORK:
        case SCREEN_DIAGNOSTICS: processScrollPage(); break;
        default: break;   // HOME: nothing but height select
    }
}


// ---- Manual adjust page: hold UP / DOWN to drive the motor -------------------------------------
void processManualPage()
{
    // A motor button is only "armed" by a fresh press that happened while on this page.
    if (wasPressed(UpButton))   manualUpArmed = true;
    if (wasPressed(DownButton)) manualDownArmed = true;
    if (!UpButton.state)        manualUpArmed = false;
    if (!DownButton.state)      manualDownArmed = false;

    bool up   = manualUpArmed   && UpButton.state;
    bool down = manualDownArmed && DownButton.state;

    if (up && down)   // Both pressed: stop, and make the user let go and press again
    {
        manualUpArmed = false;
        manualDownArmed = false;
        up = false;
        down = false;
    }

    if (up)
    {
        setManualDrive(1, SOURCE_LOCAL, 0);
    }
    else if (down)
    {
        setManualDrive(2, SOURCE_LOCAL, 0);
    }
    else if (localManualActive())
    {
        cancelManualControl();   // Button released: back to AUTO (Desired = Ramp, so the motor stops)
    }

    // TESTING ONLY: hold BACK to toggle the height sensor bypass
    if (ALLOW_HEIGHT_SAFETY_BYPASS && isHeld(BackButton, BYPASS_HOLD_MS))
    {
        setHeightSafetyBypass(!state.Height_Safety_Bypass);
    }
}


// ---- Water page ---------------------------------------------------------------------------------
void processWaterPage()
{
    if (wasPressed(WaterButton))
    {
        waterControl(true, SOURCE_LOCAL);    // +1 hour each tap, 12 h max, the tap after that turns it off
    }
    if (isHeld(WaterButton, WATER_OFF_HOLD_MS))
    {
        waterControl(false, SOURCE_LOCAL);   // Hold = off right now
    }
}


// ---- Network / Diagnostics pages: scroll --------------------------------------------------------
void processScrollPage()
{
    if (wasPressedOrRepeat(UpButton) && listScroll > 0)
    {
        listScroll--;
    }
    if (wasPressedOrRepeat(DownButton) && (listScroll + LIST_VISIBLE) < listLineCount)
    {
        listScroll++;
    }
}


//Button Functions for Direct GPIO Buttons

void updateButton(Button &btn)
{
    bool reading = digitalRead(btn.pin);

    btn.pressedEvent = false;
    btn.releasedEvent = false;

    // Detect state change for debounce
    if (reading != btn.lastState)
    {
        btn.debounceTimer = millis();
    }

    if ((millis() - btn.debounceTimer) > 25)
    {
        // Active-LOW check: store inverted state (true = pressed, false = released)
        bool logicalState = !reading;

        if (logicalState != btn.state)
        {
            btn.state = logicalState;

            if (btn.state)
            {
                btn.pressedEvent = true;
                btn.pressedTime = millis();  // Button was just pressed
                btn.longPressHandled = false;
            }
            else
            {
                btn.releasedEvent = true;
            }
        }
    }

    btn.lastState = reading;
}

bool isHeld(Button &btn, unsigned long duration)
{
    // Fixed: checks if button is pressed (btn.state is true) and duration met
    if (btn.state && !btn.longPressHandled && (millis() - btn.pressedTime >= duration))
    {
        btn.longPressHandled = true;
        return true;
    }
    return false;
}

bool wasPressed(Button &btn)
{
    return btn.pressedEvent;
}

bool wasReleased(Button &btn)
{
    return btn.releasedEvent; // Fixed: removed unreachable dead code below return
}

// True on the press, then again every 120 ms once the button has been held for 400 ms (used for scrolling)
bool wasPressedOrRepeat(Button &btn)
{
    if (btn.pressedEvent)
    {
        btn.repeatTimer = millis();
        return true;
    }
    if (btn.state && (millis() - btn.pressedTime) > 400 && (millis() - btn.repeatTimer) > 120)
    {
        btn.repeatTimer = millis();
        return true;
    }
    return false;
}


// Ladder Button Functions

LadderButton readLadderFromRaw(int adc)
{
    if(adc > LADDER_NONE_ABOVE) return BTN_NONE;
    if(adc > LADDER_4_ABOVE)    return BTN_4FT;
    if(adc > LADDER_45_ABOVE)   return BTN_45FT;
    if(adc > LADDER_5_ABOVE)    return BTN_5FT;
    return BTN_55FT;
}

LadderButton readLadder()
{
    // Average 4 samples: "nothing pressed" and 4 ft are only ~220 counts apart, so knock the ADC noise down
    int sum = 0;
    for (uint8_t i = 0; i < 4; i++)
        sum += analogRead(Ladder_pin);
    int adc = sum / 4;
    ladderRawAdc = adc;
    return readLadderFromRaw(adc);
}

void updateLadder(LadderState &lad)
{
    LadderButton reading = readLadder();

    lad.pressedEvent = false;
    lad.releasedEvent = false;

    if(reading != lad.lastReading)
    {
        lad.debounceTimer = millis();
    }

    if(millis() - lad.debounceTimer > 25)
    {
        if(reading != lad.current)
        {
            // If we were holding a button and changed to a new one or NONE
            if(lad.current != BTN_NONE)
            {
                lad.releasedEvent = true;
            }

            lad.current = reading;

            if(reading != BTN_NONE)
            {
                lad.pressedEvent = true;
                lad.pressedTime = millis();
                lad.longPressHandled = false;
            }
        }
    }

    lad.lastReading = reading;
}

bool wasPressed(LadderState &lad, LadderButton btn)
{
    return lad.pressedEvent && lad.current == btn;
}

bool wasReleased(LadderState &lad)
{
    return lad.releasedEvent;
}

bool isHeld(LadderState &lad, LadderButton btn, unsigned long duration)
{
    if(lad.current == btn &&
       !lad.longPressHandled &&
       millis() - lad.pressedTime >= duration)
    {
        lad.longPressHandled = true;
        return true;
    }

    return false;
}

// Local height select (ladder buttons). Always live, on every page.
void heightSelection()
{
    if (!ladder.pressedEvent)
        return;

    float target;

    switch (ladder.current)
    {
        case BTN_4FT:  target = 4.0f; break;
        case BTN_45FT: target = 4.5f; break;
        case BTN_5FT:  target = 5.0f; break;
        case BTN_55FT: target = 5.5f; break;
        default:
            return;
    }

    // Auto moves are disabled while the sensor bypass is on (we don't know where the ramp is)
    if (heightSafetyBypassActive())
        return;

    // A height request always ends manual driving and puts the ramp back into AUTO
    if (state.Control_Mode == MODE_MANUAL)
        cancelManualControl();

    state.Desired_Height = target;
    state.Height_Request_Active = true;   // Go there - even if it's the height we just left
    state.Control_Source = SOURCE_LOCAL;
    state.Control_Mode = MODE_AUTO;
    state.localOverrideActive = true; // Flag local priority
}
