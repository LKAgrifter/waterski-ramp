// OLED Timeout Configuration (1 minute in milliseconds)
constexpr unsigned long OLED_TIMEOUT_MS = 1UL * 60UL * 1000UL; 

void handleOLEDTimeout()
{
  // Check if any button was just pressed this cycle
  bool anyPressed = UpButton.pressedEvent || 
                    DownButton.pressedEvent || 
                    EnterButton.pressedEvent || 
                    BackButton.pressedEvent || 
                    ladder.pressedEvent;

  // A motor button that is being held counts as activity too, so the screen can't time out (and kick you
  // off the Manual page) in the middle of a long drive.
  bool motorButtonHeld = UpButton.state || DownButton.state;

  if (anyPressed || motorButtonHeld)
  {
    // Any button activity resets the inactivity timer
    state.lastActivityTime = millis();
  }

  if (anyPressed)
  {
    if (state.oledSleeping)
    {
      // Wake up the OLED display
      display.displayOn();
      state.oledSleeping = false;

      // Consume/clear the press events so they do NOT execute their intended functions
      UpButton.pressedEvent = false;
      DownButton.pressedEvent = false;
      EnterButton.pressedEvent = false;
      BackButton.pressedEvent = false;
      ladder.pressedEvent = false;

      // ...and make sure holding that wake-up button doesn't turn into a long-press action either
      UpButton.longPressHandled = true;
      DownButton.longPressHandled = true;
      EnterButton.longPressHandled = true;
      BackButton.longPressHandled = true;
      ladder.longPressHandled = true;
      
      return; // Exit early so the wake-up press is completely absorbed
    }
  }

  // Check if it's time to put the display to sleep
  if (!state.oledSleeping)
  {
    if (millis() - state.lastActivityTime >= OLED_TIMEOUT_MS)
    {
      display.displayOff();
      state.oledSleeping = true;
      changeScreen(SCREEN_HOME);   // Screen timed out: the menu goes back to the main page
    }
  }
}

// ---------------------------------------------------------------------------
// Menu navigation
// ---------------------------------------------------------------------------

// Page order: HOME -> WATER -> SET HEIGHT -> MANUAL ADJUST -> DIAGNOSTICS -> NETWORK -> HOME
// GAMES is hidden: hold MENU + BACK together on HOME to open it; hold MENU there to go back to HOME.
void nextScreen()
{
    switch (currentScreen)
    {
        case SCREEN_HOME:        changeScreen(SCREEN_WATER);       break;
        case SCREEN_WATER:       changeScreen(SCREEN_SET_HEIGHT);  break;
        case SCREEN_SET_HEIGHT:  changeScreen(SCREEN_HEIGHT);      break;
        case SCREEN_HEIGHT:      changeScreen(SCREEN_DIAGNOSTICS); break;
        case SCREEN_DIAGNOSTICS: changeScreen(SCREEN_NETWORK);     break;
        case SCREEN_NETWORK:     changeScreen(SCREEN_HOME);        break;
        case SCREEN_GAME:        changeScreen(SCREEN_HOME);        break;   // GAMES is hidden: MENU + BACK on HOME
        default:                 changeScreen(SCREEN_HOME);        break;
    }
}

// 1..6, shown in the page title
uint8_t pageNumber()
{
    switch (currentScreen)
    {
        case SCREEN_HOME:        return 1;
        case SCREEN_WATER:       return 2;
        case SCREEN_SET_HEIGHT:  return 3;
        case SCREEN_HEIGHT:      return 4;
        case SCREEN_DIAGNOSTICS: return 5;
        case SCREEN_NETWORK:     return 6;
        case SCREEN_GAME:        return 0;   // Hidden page (draws its own title bar)
        default:                 return 0;
    }
}

void changeScreen(ScreenState screen)
{
    if (currentScreen == SCREEN_HEIGHT && screen != SCREEN_HEIGHT)
    {
        releaseManualDrive();   // Leaving the Manual page can never leave the ramp driving
    }

    manualUpArmed = false;
    manualDownArmed = false;
    listScroll = 0;
    currentScreen = screen;
}

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------

// Append one line to the list buffer (printf style)
void addLine(const char *fmt, ...)
{
    if (listLineCount >= LIST_MAX_LINES)
        return;

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(listLines[listLineCount], LIST_LINE_LEN, fmt, ap);
    va_end(ap);
    listLineCount++;
}

// "4 4.5 5.5" style list of the heights in a switch mask
void maskToString(uint8_t mask, char *out, size_t n)
{
    const char *names[4] = {"4", "4.5", "5", "5.5"};
    out[0] = '\0';

    for (uint8_t i = 0; i < 4; i++)
    {
        if (mask & (1 << i))
        {
            if (out[0] != '\0') strncat(out, " ", n - strlen(out) - 1);
            strncat(out, names[i], n - strlen(out) - 1);
        }
    }

    if (out[0] == '\0') strncpy(out, "none", n);
}

// Title bar with the page number on the right
void drawTitle(const char *title)
{
    char pg[8];
    snprintf(pg, sizeof(pg), "%u/6", pageNumber());

    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.drawString(0, 0, title);
    display.setTextAlignment(TEXT_ALIGN_RIGHT);
    display.drawString(127, 0, pg);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.drawLine(0, 12, 127, 12);
}

// Row 0..3 under the title bar
void drawRow(uint8_t row, const char *text)
{
    display.drawString(0, 14 + row * 12, text);
}

// Draws the lines currently in listLines[] with a scroll bar. Used by the Network and Diagnostics pages.
void drawListPage(const char *title)
{
    uint8_t maxScroll = (listLineCount > LIST_VISIBLE) ? (listLineCount - LIST_VISIBLE) : 0;
    if (listScroll > maxScroll) listScroll = maxScroll;

    display.clear();
    drawTitle(title);

    for (uint8_t i = 0; i < LIST_VISIBLE && (listScroll + i) < listLineCount; i++)
    {
        drawRow(i, listLines[listScroll + i]);
    }

    if (listLineCount > LIST_VISIBLE)   // Scroll bar
    {
        const int trackTop = 14;
        const int trackH = 48;
        int barH = (trackH * LIST_VISIBLE) / listLineCount;
        if (barH < 6) barH = 6;
        int barY = trackTop + ((trackH - barH) * listScroll) / maxScroll;

        display.drawVerticalLine(126, trackTop, trackH);
        display.fillRect(125, barY, 3, barH);
    }

    display.display();
}

// ---------------------------------------------------------------------------
// Pages
// ---------------------------------------------------------------------------

void drawBootScreen()
{
  if(millis() < 5000)
  {
    display.setFont(ArialMT_Plain_10);
    display.clear();
    display.drawString(0, 0, "Ramp-O-Matic Initiallizing");
    display.drawString(0, 15, "Remote Controller");
    display.display();
    return;
  }
  if(currentScreen == SCREEN_BOOT) //Change from the boot screen to the home screen.
  {
    currentScreen = SCREEN_HOME;
  }
}

// ---- Heights on screen: shown BLANK when they aren't known ----
// "Now" is unknown with no ramp link, or when the ramp hasn't located its height (never located / CAL REQUIRED).
bool nowHeightKnown()
{
    return state.Remote_Connected && !diag.powerLoss;
}
// "Set" is blank until there is a real target (one we just picked counts, even before the ramp answers).
bool setHeightKnown()
{
    if (heightRequestPending()) return true;
    if (!state.Remote_Connected) return false;
    return nowHeightKnown() || state.Desired_Height != state.Ramp_Height;
}
// "4.5 ft", or "" when unknown
String heightStr(float h, bool known)
{
    return known ? String(h, 1) + " ft" : String("");
}

// SET HEIGHT page: the only page where the ladder (height) buttons do anything
void drawSetHeightScreen()
{
    char buf[LIST_LINE_LEN];

    display.clear();
    drawTitle("SET HEIGHT");

    snprintf(buf, sizeof(buf), "Set %-8s Now %s", heightStr(state.Desired_Height, setHeightKnown()).c_str(),
             heightStr(state.Ramp_Height, nowHeightKnown()).c_str());
    drawRow(0, buf);
    drawRow(1, "Press a height button");
    drawRow(2, "4.0 / 4.5 / 5.0 / 5.5 ft");

    const char *s = "Ready";
    if (!state.Remote_Connected)
        s = "NO RAMP LINK!";
    else if (heightRequestPending())
        s = "Sending...";
    else if (heightRequestLocked())
        s = "Ramp buttons in use";
    else if (heightRequestFailed())
        s = "No response, retry";
    else if (state.Height_Safety_Bypass)
        s = "BYPASS: heights off";
    else if (diag.powerLoss)
        s = "Ramp CAL REQUIRED";
    else if (state.Motor_Running)
        s = state.Motor_Direction_Up ? "Moving UP..." : "Moving DOWN...";
    else if (nowHeightKnown() && abs(state.Ramp_Height - state.Desired_Height) < 0.1)
        s = "At height";
    drawRow(3, s);

    display.display();
}

// Ladder calibration mode: live raw reading in big digits, plus what it decodes to with the current thresholds
void drawLadderCalScreen()
{
    display.clear();
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.setFont(ArialMT_Plain_10);
    display.drawString(0, 0, "LADDER CAL (height off)");
    display.setFont(ArialMT_Plain_24);
    display.drawString(0, 14, String(ladderRawAdc));
    display.setFont(ArialMT_Plain_16);
    display.drawString(70, 18, ladderName(readLadderFromRaw(ladderRawAdc)));
    display.setFont(ArialMT_Plain_10);
    display.drawString(0, 42, "N>" + String(LADDER_NONE_ABOVE) + " 4>" + String(LADDER_4_ABOVE));
    display.drawString(0, 52, "4.5>" + String(LADDER_45_ABOVE) + " 5>" + String(LADDER_5_ABOVE) + " else 5.5");
    display.display();
}

void drawHomeScreen() //Draw the home screen. Status only: no buttons do anything here.
{
    if (LADDER_CALIBRATION_MODE)
    {
        drawLadderCalScreen();
        return;
    }

    // 5 rows of 10 pt text, 13 px apart: y = 0, 13, 26, 39, 51 (the last row ends exactly at the bottom, y = 63).
    // Text on the bottom row must stay left of x = 115 so it doesn't run into the "next page" arrow.
    display.clear();
    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_LEFT);

    // Row 0: target height, fault count on the right
    display.drawString(0, 0, "Set: " + heightStr(state.Desired_Height, setHeightKnown()));
    display.setTextAlignment(TEXT_ALIGN_RIGHT);
    display.drawString(127, 0, "Faults: " + String(diag.faultCount));
    display.setTextAlignment(TEXT_ALIGN_LEFT);

    // Row 1: last located height
    display.drawString(0, 13, "Now: " + heightStr(state.Ramp_Height, nowHeightKnown()));

    // Row 2: water
    if (!state.Water_Relay)
        display.drawString(0, 26, "Water: OFF");
    else
    {
        uint16_t mins = state.Water_RemainMin;
        display.drawString(0, 26, "Water: " + String(mins / 60) + "h " + String(mins % 60) + "m");
    }

    // Row 3: status
    const char *status = "READY";
    if (!state.Remote_Connected)
        status = "NO RAMP LINK";
    else if (heightRequestLocked())
        status = "RAMP IN USE";        // Someone is using the buttons on the ramp
    else if (heightRequestFailed())
        status = "NO RESPONSE";
    else if (diag.heightSensorFault && !state.Height_Safety_Bypass)
        status = "SENSOR FAULT";
    else if (diag.motorTimeoutFault)
        status = "MOTOR TIMEOUT";
    else if (diag.powerLoss && !state.Height_Safety_Bypass)
        status = "CAL REQUIRED";
    else if (state.Motor_Running)
        status = state.Motor_Direction_Up ? "MOVING UP" : "MOVING DOWN";
    else if (state.Height_Safety_Bypass)
        status = "BYPASS ON!";
    display.drawString(0, 39, status);

    // Row 4: link status - or, just after a stray height / water press, which page does that
    if ((long)(heightHintUntil - millis()) > 0)
        display.drawString(0, 51, "Set height: page 3");
    else if ((long)(waterHintUntil - millis()) > 0)
        display.drawString(0, 51, "Water: page 2");
    else
        display.drawString(0, 51, String("Ramp: ") + (state.Remote_Connected ? "ONLINE" : "OFFLINE"));

    // "Next page" arrow, bottom right
    display.drawLine(118, 57, 126, 57);
    display.drawLine(123, 54, 126, 57);
    display.drawLine(123, 60, 126, 57);

    display.display();
}


void drawHeightScreen() //Manual adjust page: hold UP / DOWN to drive the ramp's motor
{
    char buf[LIST_LINE_LEN];

    display.clear();
    drawTitle("MANUAL ADJUST");

    // Row 0: last located height
    snprintf(buf, sizeof(buf), "Last height: %s%s", heightStr(state.Ramp_Height, nowHeightKnown()).c_str(),
             (diag.powerLoss && !state.Height_Safety_Bypass) ? "(CAL REQ)" : "");
    drawRow(0, buf);

    // Row 1: how to use it, or what is wrong
    if (!state.Remote_Connected)
        drawRow(1, "NO RAMP LINK!");
    else if (diag.powerLoss)
        drawRow(1, "CAL: drive at the ramp");   // The ramp refuses remote manual drive until it has found a sensor
    else if (state.Height_Safety_Bypass)
        drawRow(1, "BYPASS: ramp btns only");   // ...and while its sensor bypass is on
    else if (manualBlocked())
        drawRow(1, "Ramp buttons in use!");
    else
        drawRow(1, "Hold UP / DOWN to drive");

    // Row 2: what the motor is doing (as reported by the ramp)
    const char *mstat = "STOPPED";
    if (state.Motor_Running)
        mstat = state.Motor_Direction_Up ? "MOVING UP" : "MOVING DOWN";
    else if (state.Manual_Active)
        mstat = "REVERSING...";
    snprintf(buf, sizeof(buf), "Ramp motor: %s", mstat);
    drawRow(2, buf);

    // Row 3: testing bypass
    if (ALLOW_HEIGHT_SAFETY_BYPASS)
    {
        if (state.Height_Safety_Bypass)
            drawRow(3, "!SENSORS BYPASSED!");
        else
            drawRow(3, "Hold UP+DN 2s: bypass");
    }

    display.display();
}

void drawWaterScreen()
{
    char buf[LIST_LINE_LEN];

    display.clear();
    bool timerMode = (waterMode == WATER_MODE_TIMER);
    drawTitle(timerMode ? "WATER (TIMER)" : "WATER (BOAT)");

    bool wantOn  = (cmdPending && requestedWater >= 0) ? (requestedWater != 0) : state.Water_Relay;
    bool sending = (cmdPending && requestedWater >= 0);

    if (state.Water_Relay)
    {
        drawRow(0, sending ? "WATER: ON  (sending...)" : "WATER: ON");
        snprintf(buf, sizeof(buf), "Left: %uh %02um", state.Water_RemainMin / 60, state.Water_RemainMin % 60);
        drawRow(1, buf);
    }
    else
    {
        drawRow(0, sending ? "WATER: OFF (sending...)" : "WATER: OFF");
        if (timerMode)
            drawRow(1, "Tap BACK to start 1 h");
    }

    if (!state.Remote_Connected)
        drawRow(2, "NO RAMP LINK!");
    else if (waterTapsPending() > 0)
    {
        snprintf(buf, sizeof(buf), "Adding +%u h...", waterTapsPending());
        drawRow(2, buf);
    }

    if (timerMode)
        drawRow(3, "BACK: +1h   HOLD: OFF");
    else
        drawRow(3, wantOn ? "BACK: turn water OFF" : "BACK: turn water ON");

    display.display();
}

// LoRa statistics (scroll with UP / DOWN)
void buildNetworkLines()
{
    listLineCount = 0;

    unsigned long age = millis() - lastHeardTime;
    addLine("Link: %s", state.Remote_Connected ? "ONLINE" : "OFFLINE");
    addLine("RSSI %d  SNR %d", loraStats.rssi, loraStats.snr);
    addLine("Heard: %lu.%lus ago", age / 1000UL, (age % 1000UL) / 100UL);
    addLine("RTT: %u ms", loraStats.rttMs);

    uint32_t total = loraStats.rxTelemetry + loraStats.lost;
    float lossPct = total ? (100.0f * loraStats.lost / total) : 0.0f;
    addLine("RX ok: %lu", (unsigned long)loraStats.rxTelemetry);
    addLine("Lost: %lu (%.1f%%)", (unsigned long)loraStats.lost, lossPct);
    addLine("CRC %lu  Oth %lu", (unsigned long)loraStats.rxCrcErr, (unsigned long)(loraStats.rxOther + loraStats.rxUnknown));
    addLine("Cmd TX: %lu", (unsigned long)loraStats.cmdTx);
    addLine("Retry:%lu Fail:%lu", (unsigned long)loraStats.cmdRetries, (unsigned long)loraStats.cmdFails);

    const char *bw[3] = {"125", "250", "500"};
    addLine("Remote ID: %u", REMOTE_ID);
    addLine("UP+DN 2s: change ID");
    addLine("Water mode: %s", waterMode == WATER_MODE_TIMER ? "TIMER" : "BOAT");
    addLine("(WATER pg: UP+DN 2s)");
    addLine("%.0fMHz SF%d BW%s", RF_FREQUENCY / 1000000.0f, LORA_SPREADING_FACTOR, bw[LORA_BANDWIDTH]);
}

void drawNetworkScreen()
{
    buildNetworkLines();
    drawListPage("NETWORK (LoRa)");
}

// Fault / status list (scroll with UP / DOWN). Same lines, same order as the ramp's own diagnostics page.
void buildDiagLines()
{
    listLineCount = 0;
    char m[20];

    addLine("Faults: %d", diag.faultCount);
    addLine("Calibrate: %s", diag.powerLoss ? "NEEDED" : "OK");
    addLine("Height sens: %s", diag.heightSensorFault ? "FAULT" : "OK");
    if (diag.heightSensorFault)
    {
        maskToString(diag.heightFaultMask, m, sizeof(m));
        addLine("Fault sw: %s", m);
    }
    maskToString(diag.limitMask, m, sizeof(m));
    addLine("Sw on: %s", m);
    addLine("Motor timeout: %s", diag.motorTimeoutFault ? "FAULT" : "OK");
    addLine("Overcurrent: %s", diag.overCurrentFault ? "FAULT" : "OK");
    addLine("Sensor bypass: %s", state.Height_Safety_Bypass ? "ON!" : "off");
    addLine("Motor: %s", state.Motor_Running ? (state.Motor_Direction_Up ? "UP" : "DOWN") : "STOP");
    addLine("Mode: %s", state.Manual_Active ? "MANUAL" : "AUTO");
    addLine("Now %s  Set %s", heightStr(state.Ramp_Height, nowHeightKnown()).c_str(), heightStr(state.Desired_Height, setHeightKnown()).c_str());
    addLine("Ramp link: %s", state.Remote_Connected ? "ONLINE" : "OFFLINE");
    addLine("Ladder %d %s", ladderRawAdc, ladderName(readLadderFromRaw(ladderRawAdc)));
    unsigned long s = millis() / 1000UL;
    addLine("Uptime: %luh %02lum", s / 3600UL, (s % 3600UL) / 60UL);
}

void drawDiagnosticsScreen()
{
    buildDiagLines();
    drawListPage("DIAGNOSTICS");
}

void drawScreen()
{
    if (state.oledSleeping)
        return;

    // The games do their own timing and need to be called every loop (Games.ino)
    if (currentScreen == SCREEN_GAME) { skiGameFrame(); return; }

    // Don't hammer the I2C bus every loop: the radio and button code share this loop
    static unsigned long lastDraw = 0;
    if ((millis() - lastDraw) < SCREEN_REFRESH_MS)
        return;
    lastDraw = millis();

    switch(currentScreen)
    {
        case SCREEN_BOOT:
            drawBootScreen();
            break;
        case SCREEN_HOME:
            drawHomeScreen();
            break;

        case SCREEN_SET_HEIGHT:
            drawSetHeightScreen();
            break;

        case SCREEN_HEIGHT:
            drawHeightScreen();
            break;

        case SCREEN_WATER:
            drawWaterScreen();
            break;

        case SCREEN_NETWORK:
            drawNetworkScreen();
            break;

        case SCREEN_DIAGNOSTICS:
            drawDiagnosticsScreen();
            break;

        case SCREEN_GAME:      // Handled above
            break;
    }
}
