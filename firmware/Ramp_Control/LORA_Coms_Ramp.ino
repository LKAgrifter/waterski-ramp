// ===========================================================================
// RAMP side LoRa communication (multi-remote)
//
// The ramp broadcasts a TelemetryPacket every TELEMETRY_INTERVAL_MS (4 Hz) to every remote,
// and sends one immediately after every valid CommandPacket it receives (that reply
// carries the ACK for the remote that sent the command, then restarts the 4 Hz timer).
//
// MANUAL DRIVE: a remote that wants the motor to run sets remoteControlMode (1 = up, 2 = down) in EVERY packet
// it sends while the button is held. Each packet re-arms a 1 s dead-man on the ramp (REMOTE_MANUAL_TIMEOUT_MS).
// If the packets stop, or a packet arrives with mode 0 from the same remote, the ramp goes back to AUTO.
// ===========================================================================

// Forward declaration for water control function
void waterControl(bool activate, ControlSource source);

// Height index mapping enum
enum RampHeightIndex : uint8_t {
  Height_4_0 = 0, // Represents 4.0ft
  Height_4_5 = 1, // Represents 4.5ft
  Height_5_0 = 2, // Represents 5.0ft
  Height_5_5 = 3  // Represents 5.5ft
};

// Helper function: Convert index to float height
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
  ACK_LOCKED = 2    // Height / manual command refused (local override, local manual in progress, or bypass active)
};

// Bits in TelemetryPacket.flags. Must match the remote side exactly.
constexpr uint8_t FLAG_BYPASS         = 0x01;   // Height sensor safety bypass is ON (testing)
constexpr uint8_t FLAG_MANUAL         = 0x02;   // Ramp is in manual drive
constexpr uint8_t FLAG_OVERCURRENT    = 0x04;   // diag.overCurrentFault
constexpr uint8_t FLAG_LOCAL_OVERRIDE = 0x08;   // Local height override is locking remotes out
constexpr uint8_t FLAG_REMOTE_MANUAL_OFF = 0x10; // ALLOW_REMOTE_MANUAL is false: remotes can't drive by hand

// waterCommand values in CommandPacket
//   -1 = ignore, 0 = off, 1 = on (1 h timer), 2 = keep-alive (refresh a remote-started timer, never starts water),
//    3 = timer tap (TIMER-mode remote): +1 h exactly like a tap on the ramp's Water page (the tap after 12 h = off)

// On the air (Radio_Security.ino) the telemetry is packed into RS_TELEMETRY_LEN bytes and encrypted:
//   [0] ramp height idx (bits 0-1) | desired height idx (bits 2-3) | limitMask (bits 4-7)
//   [1] bits: 0 waterRelay, 1 motorRunning, 2 motorDirectionUp, 3 powerLoss, 4 heightSensorFault, 5 motorTimeoutFault
//   [2] faultCount   [3] heightFaultMask (bits 0-3) | ackResult (bits 4-7)   [4] ackRemoteId   [5] ackSeq
//   [6] flags        [7-8] waterRemainMin (little-endian)
// packetId and txCounter aren't sent: the packet's sender byte and frame counter do their jobs.
// A command goes as RS_COMMAND_LEN bytes: seq, targetHeightIndex, waterCommand, remoteControlMode, bypassCommand
// (remoteId is in the sender byte, txCounter is the frame counter).
constexpr uint8_t RS_TELEMETRY_LEN = 9;
constexpr uint8_t RS_COMMAND_LEN = 5;

// Telemetry from the Ramp -> all remotes (as built in memory). Must match the remote side exactly.
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
  uint8_t txCounter;     // Increments on every packet the ramp sends. Remotes use gaps in it to measure packet loss.
};

// Command from a remote -> Ramp (as decoded in memory). Must match the remote side exactly.
struct __attribute__((packed)) CommandPacket {
  uint8_t packetId = 0xB2;
  uint8_t remoteId;          // 1..MAX_REMOTES, unique per remote
  uint8_t seq;               // 0 = heartbeat / manual stream / nothing to ACK, 1..255 = command sequence number
  int8_t targetHeightIndex;  // -1 to ignore, or 0-3 for target height index
  int8_t waterCommand;       // -1 = ignore, 0 = turn off, 1 = turn on, 2 = keep-alive, 3 = timer tap (+1 h)
  uint8_t remoteControlMode; // 0 = Auto, 1 = manual Up, 2 = manual Down  (level-triggered: must be repeated)
  int8_t bypassCommand;      // -1 = ignore, 0 = bypass off, 1 = bypass on (testing only)
  uint8_t txCounter;         // Increments on every packet this remote sends. The ramp uses gaps to measure packet loss.
};

// ---------------------------------------------------------------------------
// Settings and state
// ---------------------------------------------------------------------------
constexpr uint8_t MAX_REMOTES = 4;
constexpr unsigned long TELEMETRY_INTERVAL_MS = 250;   // 4 Hz. Keep this at 500 or less to stay at 2 Hz minimum.
constexpr unsigned long REMOTE_TIMEOUT_MS = 10000;     // A remote is "gone" after this long without a packet

volatile bool replyPending = false;   // Set by OnRxDone, consumed by handleLoraBroadcast()
volatile bool txBusy = false;         // Radio is transmitting
uint8_t pendingAckId = 0;
uint8_t pendingAckSeq = 0;
uint8_t pendingAckResult = ACK_NONE;
unsigned long lastTelemetryTx = 0;
uint8_t telemetryCounter = 0;
uint32_t lastTelemetryFcnt = 0;       // Frame counter of the last telemetry we sent (remotes sign their commands with it)
bool telemetrySent = false;
constexpr uint8_t COMMAND_ECHO_WINDOW = 16;   // A command must be signed with one of our last 16 telemetry counters (~4 s)

// Per-remote tracking (index = remoteId - 1).
unsigned long remoteLastSeen[MAX_REMOTES] = {0};
uint8_t remoteLastSeq[MAX_REMOTES] = {0};
uint8_t remoteLastResult[MAX_REMOTES] = {ACK_OK, ACK_OK, ACK_OK, ACK_OK};
uint8_t remotesConnected = 0;

// Link statistics (shown on the NETWORK page)
struct LoraStats {
  uint32_t rxOk = 0;          // Valid command packets received
  uint32_t rxBad = 0;         // Packets that arrived but weren't a valid command (wrong size / ID / remote number)
  uint32_t rxCrcErr = 0;      // CRC errors reported by the radio
  uint32_t txOk = 0;          // Telemetry packets sent
  uint32_t txTimeout = 0;     // Telemetry transmissions that timed out
  uint32_t lost = 0;          // Command packets we know we missed (gaps in the remotes' counters)
  uint32_t rxBadKey = 0;      // Packets not signed with our key (different bind phrase) or with a stale ramp counter
  uint32_t rxReplay = 0;      // Correctly signed but old (played back)
  uint32_t rxOldFw = 0;       // Unencrypted packets from older firmware
  int16_t  rssi = 0;          // Last valid packet
  int8_t   snr = 0;
};
LoraStats loraStats;

int16_t  remoteRssi[MAX_REMOTES] = {0};
int8_t   remoteSnr[MAX_REMOTES] = {0};
uint32_t remoteRxCount[MAX_REMOTES] = {0};
uint32_t remoteLost[MAX_REMOTES] = {0};
uint32_t remoteLastCounter[MAX_REMOTES] = {0};   // For packet loss (frame counter gaps)
bool     remoteHaveCounter[MAX_REMOTES] = {false};
uint32_t remoteLastFcnt[MAX_REMOTES] = {0};      // Newest frame counter accepted from each remote (older = played back)
bool     remoteHaveFcnt[MAX_REMOTES] = {false};


// Call every loop. Sends the 4 Hz broadcast, or the reply to a command that just arrived.
void handleLoraBroadcast()
{
  if (txBusy)
  {
    if (millis() - lastTelemetryTx > 3500)   // Safety net in case no TX callback ever fired
    {
      txBusy = false;
      Radio.Rx(0);
    }
    return;
  }

  bool isReply = replyPending;
  if (!isReply && (millis() - lastTelemetryTx) < TELEMETRY_INTERVAL_MS) return;

  TelemetryPacket packet;
  packet.packetId = 0xA1;
  packet.rampHeightIndex = floatToHeightIndex(state.Ramp_Height);
  packet.desiredHeightIndex = floatToHeightIndex(state.Desired_Height);
  packet.waterRelay = state.Water_Relay ? 1 : 0;
  packet.motorRunning = state.Motor_Running ? 1 : 0;
  packet.motorDirectionUp = state.Motor_Direction_Up ? 1 : 0;
  packet.powerLoss = (diag.powerLoss || state.Ramp_Height < 4.0f) ? 1 : 0;   // Height unknown -> the remote shows it blank
  packet.heightSensorFault = diag.heightSensorFault ? 1 : 0;
  packet.motorTimeoutFault = diag.motorTimeoutFault ? 1 : 0;
  packet.faultCount = diag.faultCount;
  packet.heightFaultMask = diag.heightFaultMask;
  packet.limitMask = limitMask();
  packet.flags = (heightSafetyBypassActive() ? FLAG_BYPASS : 0) |
                 (state.Control_Mode == MODE_MANUAL ? FLAG_MANUAL : 0) |
                 (diag.overCurrentFault ? FLAG_OVERCURRENT : 0) |
                 (state.localOverrideActive ? FLAG_LOCAL_OVERRIDE : 0) |
                 (ALLOW_REMOTE_MANUAL ? 0 : FLAG_REMOTE_MANUAL_OFF);
  packet.waterRemainMin = waterRemainingMinutes();
  packet.txCounter = telemetryCounter++;

  if (isReply)
  {
    packet.ackRemoteId = pendingAckId;
    packet.ackSeq = pendingAckSeq;
    packet.ackResult = pendingAckResult;
    replyPending = false;
  }
  else
  {
    packet.ackRemoteId = 0;
    packet.ackSeq = 0;
    packet.ackResult = ACK_NONE;
  }

  // Pack (see the top of this tab), encrypt and send
  uint8_t plain[RS_TELEMETRY_LEN];
  plain[0] = (uint8_t)((packet.rampHeightIndex & 0x03) | ((packet.desiredHeightIndex & 0x03) << 2) | ((packet.limitMask & 0x0F) << 4));
  plain[1] = (uint8_t)((packet.waterRelay ? 0x01 : 0) | (packet.motorRunning ? 0x02 : 0) | (packet.motorDirectionUp ? 0x04 : 0) |
                       (packet.powerLoss ? 0x08 : 0) | (packet.heightSensorFault ? 0x10 : 0) | (packet.motorTimeoutFault ? 0x20 : 0));
  plain[2] = packet.faultCount;
  plain[3] = (uint8_t)((packet.heightFaultMask & 0x0F) | ((packet.ackResult & 0x0F) << 4));
  plain[4] = packet.ackRemoteId;
  plain[5] = packet.ackSeq;
  plain[6] = packet.flags;
  plain[7] = (uint8_t)(packet.waterRemainMin & 0xFF);
  plain[8] = (uint8_t)(packet.waterRemainMin >> 8);

  uint8_t air[RS_HEADER_LEN + RS_TELEMETRY_LEN + RS_TAG_LEN];
  lastTelemetryFcnt = rsNextFcnt();
  telemetrySent = true;
  uint8_t len = rsSeal(RS_SENDER_RAMP, lastTelemetryFcnt, false, 0, plain, RS_TELEMETRY_LEN, air);

  lastTelemetryTx = millis();   // A reply also restarts the 4 Hz timer
  txBusy = true;
  Radio.Send(air, len);
}

void updateRemoteWatchdog()
{
  uint8_t count = 0;

  for (uint8_t i = 0; i < MAX_REMOTES; i++)
  {
    if (remoteLastSeen[i] != 0 && (millis() - remoteLastSeen[i]) <= REMOTE_TIMEOUT_MS)
    {
      count++;
    }
  }

  remotesConnected = count;
  state.Remote_Connected = (count > 0);
}

//Radio Functions. Every callback must leave the radio listening (or about to transmit).
void OnTxDone(void) {
  loraStats.txOk++;
  txBusy = false;
  Radio.Rx(0); // Return to receive mode
}

void OnTxTimeout(void) {
  loraStats.txTimeout++;
  txBusy = false;
  Radio.Rx(0);
}

void OnRxTimeout(void) {
  Radio.Rx(0);
}

void OnRxError(void) {
  loraStats.rxCrcErr++;
  Radio.Rx(0); // CRC error: without this the radio stays in standby and goes deaf
}

void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr)
{
  if (size == RS_HEADER_LEN + RS_COMMAND_LEN + RS_TAG_LEN &&
      payload[0] > RS_SENDER_REMOTE && payload[0] <= RS_SENDER_REMOTE + MAX_REMOTES)
  {
    uint8_t idx = payload[0] - RS_SENDER_REMOTE - 1;
    uint32_t fcnt = rsPacketFcnt(payload);
    unsigned long now = millis();
    uint8_t plain[RS_COMMAND_LEN];

    // Signed with our key, together with one of the telemetry counters we sent recently? A command recorded earlier
    // and played back now matches none of them (Radio_Security.ino).
    bool ok = false;
    for (uint8_t back = 0; telemetrySent && back < COMMAND_ECHO_WINDOW && !ok; back++)
      ok = rsOpen(payload, size, true, lastTelemetryFcnt - back, plain);
    if (!ok)
    {
      loraStats.rxBadKey++;   // A remote with a different bind phrase, or not one of ours
      Radio.Rx(0);
      return;
    }

    // Frame counters only go up. A remote that was silent for a while may have been erased and reflashed (its counter
    // starts again), so forget its counter then. That's safe: a played-back packet must also carry a recent ramp counter.
    if (remoteLastSeen[idx] == 0 || (now - remoteLastSeen[idx]) > REMOTE_TIMEOUT_MS)
      remoteHaveFcnt[idx] = false;
    if (remoteHaveFcnt[idx] && (int32_t)(fcnt - remoteLastFcnt[idx]) <= 0)
    {
      loraStats.rxReplay++;   // Already had this one (or a newer one): played back
      Radio.Rx(0);
      return;
    }
    remoteLastFcnt[idx] = fcnt;
    remoteHaveFcnt[idx] = true;

    CommandPacket cmd;
    cmd.packetId = 0xB2;
    cmd.remoteId = idx + 1;
    cmd.seq = plain[0];
    cmd.targetHeightIndex = (int8_t)plain[1];
    cmd.waterCommand = (int8_t)plain[2];
    cmd.remoteControlMode = plain[3];
    cmd.bypassCommand = (int8_t)plain[4];
    cmd.txCounter = (uint8_t)fcnt;

    {

      // A remote we haven't heard from in a while may have rebooted, so forget its old sequence number
      if (remoteLastSeen[idx] == 0 || (now - remoteLastSeen[idx]) > REMOTE_TIMEOUT_MS)
      {
        remoteLastSeq[idx] = 0;
        remoteLastResult[idx] = ACK_OK;
        remoteHaveCounter[idx] = false;
      }

      remoteLastSeen[idx] = now;
      state.LastRemoteCommand = now;
      state.Remote_Connected = true;

      // ---- Link statistics ----
      loraStats.rxOk++;
      loraStats.rssi = rssi;
      loraStats.snr = snr;
      remoteRssi[idx] = rssi;
      remoteSnr[idx] = snr;
      remoteRxCount[idx]++;
      if (remoteHaveCounter[idx])
      {
        uint32_t gap = fcnt - remoteLastCounter[idx] - 1;   // Packets this remote sent that we never heard
        if (gap < 20)   // Anything bigger is a reboot or a long outage, not packet loss
        {
          remoteLost[idx] += gap;
          loraStats.lost += gap;
        }
      }
      remoteLastCounter[idx] = fcnt;
      remoteHaveCounter[idx] = true;

      uint8_t result = ACK_OK;
      bool manualLocked = false;

      // ---- MANUAL DRIVE ----
      // Level-triggered, so it is handled for EVERY packet - even a retry of a command we already ACKed -
      // otherwise a retry could let the 1 s dead-man expire.
      bool manualRequested = (cmd.remoteControlMode == 1 || cmd.remoteControlMode == 2);

      if (manualRequested)
      {
        if (localManualActive())
        {
          manualLocked = true;   // Someone is holding a manual button on the ramp itself: they win
        }
        else if (!ALLOW_REMOTE_MANUAL || diag.powerLoss || heightSafetyBypassActive())
        {
          manualLocked = true;   // Remote manual switched off, CAL REQUIRED or sensor bypass on: the ramp is only moved
        }                        // from its own buttons. The remote's MANUAL page says why.
        else
        {
          state.localOverrideActive = false;   // Remote explicitly requesting manual control takes priority over local height lock
          setManualDrive(cmd.remoteControlMode, SOURCE_REMOTE, cmd.remoteId);
        }
      }
      else if (state.Control_Mode == MODE_MANUAL && state.Control_Source == SOURCE_REMOTE && state.ManualRemoteId == cmd.remoteId)
      {
        cancelManualControl();   // The remote that was driving let go (or sent any normal packet): stop now, don't wait for the timeout
      }

      // ---- Everything else is a one-shot command, so a retry must not be applied twice ----
      if (cmd.seq != 0 && cmd.seq == remoteLastSeq[idx])
      {
        // A retry of a command we already handled (our ACK was lost). Re-ACK it, don't apply it twice.
        result = remoteLastResult[idx];
      }
      else
      {
        if (cmd.targetHeightIndex >= 0 && cmd.targetHeightIndex <= 3)
        {
          // Refused while: a local height move is in progress, manual drive is active, or the sensor bypass is on
          if (state.localOverrideActive || manualRequested || heightSafetyBypassActive())
          {
            result = ACK_LOCKED;
          }
          else
          {
            float newTarget = heightIndexToFloat(cmd.targetHeightIndex);
            if (state.Desired_Height != newTarget)   // Last command wins if several remotes disagree
            {
              state.Desired_Height = newTarget;
              state.Control_Source = SOURCE_REMOTE;
              state.Control_Mode = MODE_AUTO;
            }
            state.Height_Request_Active = true;      // Go there - even if it's the height the ramp just left
          }
        }

        if (cmd.waterCommand == 1)
        {
          waterControl(true, SOURCE_REMOTE);
        }
        else if (cmd.waterCommand == 0)
        {
          waterControl(false, SOURCE_REMOTE);
        }
        else if (cmd.waterCommand == 2)
        {
          waterKeepAlive();
        }
        else if (cmd.waterCommand == 3)
        {
          waterTimerTap(cmd.remoteId);   // One-shot: a retry of the same seq is re-ACKed above, never applied twice
        }

        // TESTING ONLY. Ignored unless ALLOW_BYPASS_FROM_REMOTE (and ALLOW_HEIGHT_SAFETY_BYPASS) are true:
        // normally the bypass can only be switched at the ramp itself.
        if (ALLOW_BYPASS_FROM_REMOTE && (cmd.bypassCommand == 0 || cmd.bypassCommand == 1))
        {
          setHeightSafetyBypass(cmd.bypassCommand == 1);
        }

        if (cmd.seq != 0)
        {
          remoteLastSeq[idx] = cmd.seq;
          remoteLastResult[idx] = result;
          Serial.printf("Command from remote %d, seq %d, RSSI: %d\n", cmd.remoteId, cmd.seq, rssi);
        }
      }

      pendingAckId = cmd.remoteId;
      pendingAckSeq = cmd.seq;
      pendingAckResult = manualLocked ? (uint8_t)ACK_LOCKED : result;
      replyPending = true;   // Reply goes out from loop(). OnTxDone re-arms receive afterwards.
      return;
    }
  }

  if ((size == 8 && payload[0] == 0xB2) || (size == 19 && payload[0] == 0xA1))
    loraStats.rxOldFw++;   // Unencrypted packet from older firmware: reflash that unit
  else
    loraStats.rxBad++;     // Not a valid command
  Radio.Rx(0);   // Keep listening
}
