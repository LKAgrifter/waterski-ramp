// ===========================================================================
// REMOTE side LoRa communication (multi-remote)
//
// Ramp -> everyone : TelemetryPacket broadcast at 4 Hz, plus an immediate one after every
//                    command the ramp receives. Remotes only listen for status.
// Remote -> ramp   : CommandPacket, sent only in the quiet gap right after a telemetry packet
//                    (so it can't collide with the ramp's next broadcast), plus a heartbeat every
//                    few seconds so the ramp knows this remote exists.
// A command is confirmed when telemetry arrives carrying THIS remote's ID and the command's sequence number.
//
// Three kinds of packet go out:
//   * COMMAND   (seq 1..255): height / water / bypass request. Retried until the ramp ACKs it.
//                             TIMER-mode water taps (+1 h each) go one per command; extra taps wait in a queue.
//   * MANUAL    (seq 0)     : while a motor button is held, a packet with mode 1/2 goes out every time the
//                             channel is quiet (~4-5 per second). The ramp stops the motor if none arrives for 1 s.
//                             When the button is released a couple of mode-0 packets tell it to stop right away.
//   * HEARTBEAT (seq 0)     : every few seconds; also carries the water keep-alive (BOAT water mode only).
// Every packet carries remoteControlMode, so a command sent while a motor button is held doesn't cancel the drive.
// ===========================================================================

// *** SET THIS UNIQUELY ON EACH REMOTE: 1 to MAX_REMOTES (see the ramp side, currently 4) ***
// REMOTE_ID lives in Ramp_Remote.ino (it's a saved setting, changed on the NETWORK page)

// Height index mapping enum (matches ramp side)
enum RampHeightIndex : uint8_t {
  Height_4_0 = 0, // Represents 4.0ft
  Height_4_5 = 1, // Represents 4.5ft
  Height_5_0 = 2, // Represents 5.0ft
  Height_5_5 = 3  // Represents 5.5ft
};

// Helper function: Convert index to float height for UI/display
inline float heightIndexToFloat(uint8_t idx) {
  switch(idx) {
    case 0: return 4.0f;
    case 1: return 4.5f;
    case 2: return 5.0f;
    case 3: return 5.5f;
    default: return 4.0f;
  }
}

// Helper function: Convert float height to index
inline uint8_t floatToHeightIndex(float h) {
  if (h <= 4.0f) return Height_4_0;
  if (h <= 4.5f) return Height_4_5;
  if (h <= 5.0f) return Height_5_0;
  return Height_5_5;
}

// Result carried in the telemetry ACK fields
enum AckResult : uint8_t {
  ACK_NONE   = 0,   // Periodic broadcast, not an answer to anyone
  ACK_OK     = 1,   // Command accepted
  ACK_LOCKED = 2    // Height / manual command refused (ramp local override, local manual in progress, or bypass active)
};

// Bits in TelemetryPacket.flags. Must match the ramp side exactly.
constexpr uint8_t FLAG_BYPASS         = 0x01;   // Height sensor safety bypass is ON (testing)
constexpr uint8_t FLAG_MANUAL         = 0x02;   // Ramp is in manual drive
constexpr uint8_t FLAG_OVERCURRENT    = 0x04;   // diag.overCurrentFault
constexpr uint8_t FLAG_LOCAL_OVERRIDE = 0x08;   // Local height override is locking remotes out

// Telemetry packet sent from Ramp -> all remotes (19 bytes). Must match the ramp side exactly.
struct __attribute__((packed)) TelemetryPacket {
  uint8_t packetId = 0xA1;
  uint8_t rampHeightIndex;
  uint8_t desiredHeightIndex;
  uint8_t waterRelay;
  uint8_t motorRunning;
  uint8_t motorDirectionUp;
  uint8_t powerLoss;
  uint8_t heightSensorFault;
  uint8_t motorTimeoutFault;
  uint8_t faultCount;
  uint8_t heightFaultMask;
  uint8_t ackRemoteId;   // Which remote this packet answers (0 = nobody)
  uint8_t ackSeq;        // Sequence number of the command being answered
  uint8_t ackResult;     // AckResult
  uint8_t limitMask;     // Limit switches triggered right now (bit 0 = 4ft ... bit 3 = 5.5ft)
  uint8_t flags;         // FLAG_* bits
  uint16_t waterRemainMin; // Minutes left on the water timer (0 = off)
  uint8_t txCounter;     // Increments on every packet the ramp sends. Gaps = packets we missed.
};

// Command packet sent from a remote -> Ramp (8 bytes). Must match the ramp side exactly.
struct __attribute__((packed)) CommandPacket {
  uint8_t packetId = 0xB2;
  uint8_t remoteId;          // 1..MAX_REMOTES, unique per remote
  uint8_t seq;               // 0 = heartbeat / manual stream / nothing to ACK, 1..255 = command sequence number
  int8_t targetHeightIndex;  // -1 to ignore, or 0-3 for target height index
  int8_t waterCommand;       // -1 = ignore, 0 = turn off, 1 = turn on, 2 = keep-alive, 3 = timer tap (+1 h)
  uint8_t remoteControlMode; // 0 = Auto, 1 = manual Up, 2 = manual Down  (level-triggered: repeated while held)
  int8_t bypassCommand;      // -1 = ignore, 0 = bypass off, 1 = bypass on (testing only)
  uint8_t txCounter;         // Increments on every packet this remote sends. The ramp uses gaps to measure loss.
};

// ---------------------------------------------------------------------------
// Link settings
// ---------------------------------------------------------------------------
constexpr unsigned long HEARTBEAT_INTERVAL_MS = 4000;  // Ramp drops a remote after 10 s of silence
constexpr unsigned long ACK_TIMEOUT_MS        = 500;   // Reply normally arrives ~160-250 ms after the send
constexpr unsigned long REQUEST_TIMEOUT_MS    = 3000;  // Give up on a command after this long
constexpr unsigned long LINK_TIMEOUT_MS       = 1500;  // No telemetry for this long = ramp shown offline
constexpr long          TX_JITTER_MS          = 25;    // Random delay after a telemetry packet (the gap is ~150 ms)
constexpr long          RETRY_BACKOFF_MS      = 300;   // Random extra wait after a missed ACK, so two remotes stop colliding
constexpr unsigned long MANUAL_SEND_INTERVAL_MS = 150; // Minimum spacing of manual-drive packets (the ramp's dead-man is 1 s)
constexpr uint8_t       MANUAL_STOP_REPEATS   = 2;     // "I let go" packets sent when a motor button is released

// ---------------------------------------------------------------------------
// Link state
// ---------------------------------------------------------------------------
TelemetryPacket lastTelemetry;          // Most recent telemetry heard
volatile bool telemetryReceived = false;
int16_t lastRssi = 0;
unsigned long lastHeardTime = 0;

// Link statistics (shown on the NETWORK page)
struct LinkStats {
  uint32_t rxTelemetry = 0;   // Telemetry packets received
  uint32_t rxCrcErr = 0;      // CRC errors reported by the radio
  uint32_t rxOther = 0;       // Other remotes' commands we overheard (harmless, shows how busy the channel is)
  uint32_t rxUnknown = 0;     // Anything else
  uint32_t lost = 0;          // Telemetry packets we know we missed (gaps in the ramp's counter)
  uint32_t cmdTx = 0;         // Packets we sent
  uint32_t cmdRetries = 0;    // Commands whose ACK didn't come back in time
  uint32_t cmdFails = 0;      // Commands we gave up on
  int16_t  rssi = 0;          // Last telemetry packet
  int8_t   snr = 0;
  uint16_t rttMs = 0;         // Last send -> reply time
  bool     haveCounter = false;
  uint8_t  lastCounter = 0;
};
LinkStats loraStats;

uint8_t txCounter = 0;                  // Goes out in every CommandPacket
unsigned long lastSendTime = 0;

// One pending "command" can carry a height, a water command and a bypass command together
uint8_t cmdSeq = 0;                     // Sequence number of the current command
bool cmdPending = false;                // A command is waiting for the ramp to confirm
int8_t requestedHeightIndex = -1;       // -1 = no height in this command
int8_t requestedWater = -1;             // -1 = none, 0 = off, 1 = on, 3 = timer tap (+1 h)
uint8_t waterTapsQueued = 0;            // TIMER mode: taps still to send after the current water command
int8_t requestedBypass = -1;            // -1 = none, 0 = off, 1 = on
unsigned long requestDeadline = 0;
bool waitingAck = false;                // A command is on the air and we're waiting for its ACK
unsigned long ackDeadline = 0;
unsigned long retryNotBefore = 0;

bool txArmed = false;                   // A send is scheduled inside the current quiet gap
unsigned long txAt = 0;
unsigned long nextHeartbeatAt = 0;

// Manual drive
uint8_t manualStopBurst = 0;            // "I let go" packets still to send
unsigned long nextManualAt = 0;
unsigned long manualBlockedTime = 0;    // Last time the ramp refused our manual request (someone is on the local buttons)

bool commandFailed = false;
bool commandLocked = false;
unsigned long commandFailedTime = 0;


// ---------------------------------------------------------------------------
// Called from the button code (processButtons)
// ---------------------------------------------------------------------------
void startCommand()
{
  if (cmdSeq == 0) cmdSeq = (uint8_t)random(1, 256);   // Random start so a reboot doesn't reuse the last sequence number
  else cmdSeq = (cmdSeq % 255) + 1;                    // 1..255, never 0

  cmdPending = true;
  requestDeadline = millis() + REQUEST_TIMEOUT_MS;
  retryNotBefore = millis();
  commandFailed = false;
  commandLocked = false;
}

void requestRemoteHeight(float height)
{
  requestedHeightIndex = (int8_t)floatToHeightIndex(height);
  startCommand();

  // Show the choice right away. Telemetry won't overwrite it while the request is pending.
  state.Desired_Height = heightIndexToFloat(requestedHeightIndex);
}

void requestRemoteWater(bool on)
{
  requestedWater = on ? 1 : 0;
  waterTapsQueued = 0;   // Off (or on) replaces any timer taps still waiting
  startCommand();
}

// BOAT mode water button: flip whatever the water is (or is about to be)
void requestRemoteWaterToggle()
{
  bool current = (cmdPending && requestedWater >= 0) ? (requestedWater != 0) : state.Water_Relay;
  requestRemoteWater(!current);
}

// TIMER mode water button: one tap = +1 h on the ramp's timer, the same as a tap on the ramp's own Water page.
// Each tap is its own command (the ramp applies a sequence number once), so taps made while one is still on its
// way are queued and sent one after another - none are lost and none are applied twice.
void requestRemoteWaterTap()
{
  if (cmdPending && requestedWater >= 0)
  {
    if (waterTapsQueued < WATER_TAP_QUEUE_MAX) waterTapsQueued++;
    return;
  }
  requestedWater = 3;
  startCommand();
}

// TIMER mode: how many +1 h taps are still on their way to the ramp (for the Water page)
uint8_t waterTapsPending()
{
  return (cmdPending && requestedWater == 3) ? (uint8_t)(1 + waterTapsQueued) : 0;
}

// TESTING ONLY: ask the ramp to turn its height sensor bypass on / off
void requestRemoteBypass(bool on)
{
  if (!ALLOW_HEIGHT_SAFETY_BYPASS)
    return;

  requestedBypass = on ? 1 : 0;
  startCommand();
}

// Manual drive request from the motor buttons. MODE_AUTO = not driving.
void setRemoteManual(ControlMode mode)
{
  if (mode == state.Control_Mode)
    return;

  // Letting go sends a couple of "stop" packets; the ramp's 1 s dead-man is the backstop if they get lost
  manualStopBurst = (mode == MODE_AUTO) ? MANUAL_STOP_REPEATS : 0;
  state.Control_Mode = mode;
  nextManualAt = millis();
}

// Called when leaving the Manual page: never leave the ramp driving
void releaseManualDrive()
{
  setRemoteManual(MODE_AUTO);
}

bool heightRequestPending()
{
  return cmdPending && requestedHeightIndex >= 0;
}

bool heightRequestFailed()   // True for 3 s after a request timed out or was locked out
{
  return commandFailed && (millis() - commandFailedTime) < 3000;
}

bool heightRequestLocked()   // True for 3 s after the ramp said its local override is active
{
  return heightRequestFailed() && commandLocked;
}

bool manualBlocked()         // True for 1.5 s after the ramp refused our manual drive
{
  return manualBlockedTime != 0 && (millis() - manualBlockedTime) < 1500;
}


// ---------------------------------------------------------------------------
// Transmit
// ---------------------------------------------------------------------------
void sendCommand(int8_t heightIndex, int8_t waterCommand, int8_t bypassCommand, uint8_t controlMode, uint8_t seq)
{
  CommandPacket cmd;
  cmd.packetId = 0xB2;
  cmd.remoteId = REMOTE_ID;
  cmd.seq = seq;
  cmd.targetHeightIndex = heightIndex;
  cmd.waterCommand = waterCommand;
  cmd.remoteControlMode = controlMode;
  cmd.bypassCommand = bypassCommand;
  cmd.txCounter = txCounter++;

  loraStats.cmdTx++;
  lastSendTime = millis();
  Radio.Send((uint8_t *)&cmd, sizeof(cmd));
}

// Is a manual-drive (or "I let go") packet due?
bool manualSendDue(unsigned long now)
{
  return (state.Control_Mode != MODE_AUTO || manualStopBurst > 0) && (long)(now - nextManualAt) >= 0;
}


// ---------------------------------------------------------------------------
// Call every loop
// ---------------------------------------------------------------------------
void handleLoraLink()
{
  unsigned long now = millis();

  // 1. Telemetry heard (status broadcast, and possibly an ACK for us)
  if (telemetryReceived)
  {
    telemetryReceived = false;
    lastHeardTime = now;
    state.Remote_Connected = true;   // On the remote this means "ramp is answering"

    state.Ramp_Height        = heightIndexToFloat(lastTelemetry.rampHeightIndex);
    state.Water_Relay        = lastTelemetry.waterRelay != 0;
    state.Water_RemainMin    = lastTelemetry.waterRemainMin;
    state.Motor_Running      = lastTelemetry.motorRunning != 0;
    state.Motor_Direction_Up = lastTelemetry.motorDirectionUp != 0;
    state.Height_Safety_Bypass = (lastTelemetry.flags & FLAG_BYPASS) != 0;
    state.Manual_Active      = (lastTelemetry.flags & FLAG_MANUAL) != 0;
    state.localOverrideActive = (lastTelemetry.flags & FLAG_LOCAL_OVERRIDE) != 0;
    diag.overCurrentFault    = (lastTelemetry.flags & FLAG_OVERCURRENT) != 0;
    diag.powerLoss           = lastTelemetry.powerLoss != 0;
    diag.heightSensorFault   = lastTelemetry.heightSensorFault != 0;
    diag.motorTimeoutFault   = lastTelemetry.motorTimeoutFault != 0;
    diag.faultCount          = lastTelemetry.faultCount;
    diag.heightFaultMask     = lastTelemetry.heightFaultMask;
    diag.limitMask           = lastTelemetry.limitMask;

    // Round trip time: our last send -> the ramp's reply to us
    if (lastTelemetry.ackRemoteId == REMOTE_ID && lastSendTime != 0)
    {
      unsigned long rtt = now - lastSendTime;
      loraStats.rttMs = (rtt > 9999) ? 9999 : (uint16_t)rtt;
    }

    // The ramp refused a manual drive request (someone is holding a manual button at the ramp)
    if (lastTelemetry.ackRemoteId == REMOTE_ID && lastTelemetry.ackSeq == 0 &&
        lastTelemetry.ackResult == ACK_LOCKED && state.Control_Mode != MODE_AUTO)
    {
      manualBlockedTime = now;
    }

    // Is this the ramp answering our command?
    if (cmdPending && lastTelemetry.ackRemoteId == REMOTE_ID && lastTelemetry.ackSeq == cmdSeq)
    {
      cmdPending = false;
      waitingAck = false;
      requestedHeightIndex = -1;
      requestedWater = -1;
      requestedBypass = -1;

      if (lastTelemetry.ackResult == ACK_LOCKED)
      {
        commandFailed = true;
        commandLocked = true;
        commandFailedTime = now;
      }

      // TIMER mode: send the next queued +1 h tap
      if (waterTapsQueued > 0)
      {
        waterTapsQueued--;
        requestedWater = 3;
        startCommand();
      }
    }

    // Only take the ramp's target when we aren't waiting on our own height request
    if (!heightRequestPending())
    {
      state.Desired_Height = heightIndexToFloat(lastTelemetry.desiredHeightIndex);
    }

    // The ramp just finished transmitting, so the channel is quiet for about 150 ms.
    // If we have something to send, schedule it inside that gap.
    bool cmdDue       = cmdPending && !waitingAck && (long)(now - retryNotBefore) >= 0;
    bool manualDue    = manualSendDue(now);
    bool heartbeatDue = !waitingAck && (long)(now - nextHeartbeatAt) >= 0;

    if (!txArmed && (cmdDue || manualDue || heartbeatDue))
    {
      txArmed = true;
      txAt = now + random(0, TX_JITTER_MS);
    }
  }

  // 2. A command went unanswered: back off randomly, then retry in a later gap
  if (waitingAck && (long)(now - ackDeadline) >= 0)
  {
    waitingAck = false;
    loraStats.cmdRetries++;
    retryNotBefore = now + random(0, RETRY_BACKOFF_MS);
  }

  // 3. Give up on a command that never got through
  if (cmdPending && (long)(now - requestDeadline) >= 0)
  {
    cmdPending = false;
    waitingAck = false;
    requestedHeightIndex = -1;
    requestedWater = -1;
    requestedBypass = -1;
    waterTapsQueued = 0;   // The ramp isn't answering: don't fire old taps at it later
    loraStats.cmdFails++;
    commandFailed = true;
    commandLocked = false;
    commandFailedTime = now;
  }

  // 4. No telemetry for a while: ramp is offline
  if (state.Remote_Connected && (now - lastHeardTime) > LINK_TIMEOUT_MS)
  {
    state.Remote_Connected = false;
  }

  // 5. Fire the scheduled transmission
  if (txArmed && (long)(now - txAt) >= 0)
  {
    txArmed = false;

    bool cmdDue       = cmdPending && !waitingAck && (long)(now - retryNotBefore) >= 0;
    bool manualDue    = manualSendDue(now);
    bool heartbeatDue = !waitingAck && (long)(now - nextHeartbeatAt) >= 0;

    if (cmdDue || manualDue || heartbeatDue)
    {
      int8_t heightIdx = -1;
      int8_t waterCmd  = -1;
      int8_t bypassCmd = -1;
      uint8_t seq = 0;

      if (cmdDue)
      {
        heightIdx = requestedHeightIndex;
        waterCmd  = requestedWater;
        bypassCmd = requestedBypass;
        seq = cmdSeq;
      }
      else if (state.Water_Relay && waterMode == WATER_MODE_BOAT)
      {
        waterCmd = 2;   // Keep-alive: refreshes the ramp's 1 h water timer if a BOAT remote started the water. Never starts water.
      }                 // (TIMER mode sends none: its water runs on the fixed timer, like water set at the ramp.)

      // The current manual mode rides along on EVERY packet (0 = not driving)
      sendCommand(heightIdx, waterCmd, bypassCmd, (uint8_t)state.Control_Mode, seq);

      if (cmdDue)
      {
        waitingAck = true;
        ackDeadline = now + ACK_TIMEOUT_MS;
      }

      if (manualDue)
      {
        nextManualAt = now + MANUAL_SEND_INTERVAL_MS;
        if (state.Control_Mode == MODE_AUTO && manualStopBurst > 0) manualStopBurst--;
      }

      nextHeartbeatAt = now + HEARTBEAT_INTERVAL_MS + random(0, 500);   // Any packet also tells the ramp we exist
    }
  }
}


// ---------------------------------------------------------------------------
// Radio callbacks. Every one of these must leave the radio listening again.
// ---------------------------------------------------------------------------
void OnTxDone(void)
{
  Radio.Rx(0);   // Back to listening (the ramp's reply comes next)
}

void OnTxTimeout(void)
{
  Radio.Rx(0);
}

void OnRxTimeout(void)
{
  Radio.Rx(0);
}

void OnRxError(void)
{
  loraStats.rxCrcErr++;
  Radio.Rx(0);   // CRC error: without this the radio stays in standby and goes deaf
}

void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr)
{
  if (size == sizeof(TelemetryPacket) && payload[0] == 0xA1)
  {
    memcpy(&lastTelemetry, payload, sizeof(lastTelemetry));
    lastRssi = rssi;
    telemetryReceived = true;   // Processed in handleLoraLink()

    // Link statistics
    loraStats.rxTelemetry++;
    loraStats.rssi = rssi;
    loraStats.snr = snr;
    if (loraStats.haveCounter)
    {
      uint8_t gap = (uint8_t)(lastTelemetry.txCounter - loraStats.lastCounter - 1);   // Ramp packets we never heard
      if (gap < 20)   // Anything bigger is a reboot or a long outage, not packet loss
      {
        loraStats.lost += gap;
      }
    }
    loraStats.lastCounter = lastTelemetry.txCounter;
    loraStats.haveCounter = true;
  }
  // Other remotes' commands are also heard here; they're a different size/ID and get ignored.
  else if (size == sizeof(CommandPacket) && payload[0] == 0xB2)
  {
    loraStats.rxOther++;
  }
  else
  {
    loraStats.rxUnknown++;
  }

  Radio.Rx(0);
}

// NETWORK page, hold BACK: step this remote's ID 1 -> 2 -> 3 -> 4 -> 1 and save it.
// Any command still waiting for an ACK is dropped (the ACK would come back addressed to the old ID).
void cycleRemoteId()
{
  REMOTE_ID = (REMOTE_ID >= REMOTE_ID_MAX) ? 1 : (uint8_t)(REMOTE_ID + 1);
  prefs.putUChar("RemoteId", REMOTE_ID);
  cmdPending = false;
  waterTapsQueued = 0;
}

// WATER page, hold UP + DOWN 2 s: switch this remote between BOAT and TIMER water mode and save it.
void toggleWaterMode()
{
  waterMode = (waterMode == WATER_MODE_BOAT) ? WATER_MODE_TIMER : WATER_MODE_BOAT;
  prefs.putUChar("WaterMode", (uint8_t)waterMode);
}
