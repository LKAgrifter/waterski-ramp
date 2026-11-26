// Test helpers for the encrypted radio packets: play the OTHER unit (a remote, for ramp tests; the ramp, for remote
// tests). Include AFTER the sketch (#include SRC). The packing below is a second copy of the on-air layout described
// at the top of LORA_Coms_*.ino: radio_security.cpp checks the sketches against it in both directions.
#pragma once

// ---- packing (the on-air layout) ----
inline void testPackTelemetry(const TelemetryPacket &t, uint8_t p[RS_TELEMETRY_LEN])
{
  p[0] = (uint8_t)((t.rampHeightIndex & 3) | ((t.desiredHeightIndex & 3) << 2) | ((t.limitMask & 15) << 4));
  p[1] = (uint8_t)((t.waterRelay ? 1 : 0) | (t.motorRunning ? 2 : 0) | (t.motorDirectionUp ? 4 : 0) | (t.powerLoss ? 8 : 0) |
                   (t.heightSensorFault ? 16 : 0) | (t.motorTimeoutFault ? 32 : 0));
  p[2] = t.faultCount; p[3] = (uint8_t)((t.heightFaultMask & 15) | ((t.ackResult & 15) << 4));
  p[4] = t.ackRemoteId; p[5] = t.ackSeq; p[6] = t.flags; p[7] = (uint8_t)t.waterRemainMin; p[8] = (uint8_t)(t.waterRemainMin >> 8);
}
inline void testUnpackTelemetry(const uint8_t p[RS_TELEMETRY_LEN], TelemetryPacket &t)
{
  memset(&t, 0, sizeof t); t.packetId = 0xA1;
  t.rampHeightIndex = p[0] & 3; t.desiredHeightIndex = (p[0] >> 2) & 3; t.limitMask = p[0] >> 4;
  t.waterRelay = p[1] & 1; t.motorRunning = (p[1] >> 1) & 1; t.motorDirectionUp = (p[1] >> 2) & 1; t.powerLoss = (p[1] >> 3) & 1;
  t.heightSensorFault = (p[1] >> 4) & 1; t.motorTimeoutFault = (p[1] >> 5) & 1;
  t.faultCount = p[2]; t.heightFaultMask = p[3] & 15; t.ackResult = p[3] >> 4; t.ackRemoteId = p[4]; t.ackSeq = p[5];
  t.flags = p[6]; t.waterRemainMin = (uint16_t)(p[7] | (p[8] << 8));
}
inline void testPackCommand(const CommandPacket &c, uint8_t p[RS_COMMAND_LEN])
{
  p[0] = c.seq; p[1] = (uint8_t)c.targetHeightIndex; p[2] = (uint8_t)c.waterCommand; p[3] = c.remoteControlMode; p[4] = (uint8_t)c.bypassCommand;
}
inline void testUnpackCommand(uint8_t sender, const uint8_t p[RS_COMMAND_LEN], CommandPacket &c)
{
  c.packetId = 0xB2; c.remoteId = (uint8_t)(sender - RS_SENDER_REMOTE); c.seq = p[0]; c.targetHeightIndex = (int8_t)p[1];
  c.waterCommand = (int8_t)p[2]; c.remoteControlMode = p[3]; c.bypassCommand = (int8_t)p[4]; c.txCounter = 0;
}

#ifdef REMOTE
// ---- remote tests: we are the ramp ----
static uint32_t g_testRampFcnt = 5000;
// The ramp sends this status packet to the remote
inline void rxTelemetry(const TelemetryPacket &t, int16_t rssi = -60, int8_t snr = 7)
{
  uint8_t p[RS_TELEMETRY_LEN], air[RS_HEADER_LEN + RS_TELEMETRY_LEN + RS_TAG_LEN];
  testPackTelemetry(t, p);
  uint8_t n = rsSeal(RS_SENDER_RAMP, g_testRampFcnt++, false, 0, p, RS_TELEMETRY_LEN, air);
  OnRxDone(air, n, rssi, snr);
}
// The command the remote sent last (false if it doesn't decrypt, or isn't signed with our last status packet)
inline bool sentCommand(CommandPacket &c)
{
  uint8_t p[RS_COMMAND_LEN];
  if (Radio.lastLen != RS_HEADER_LEN + RS_COMMAND_LEN + RS_TAG_LEN) return false;
  if (!rsOpen(Radio.last, (uint16_t)Radio.lastLen, true, g_testRampFcnt - 1, p)) return false;
  testUnpackCommand(Radio.last[0], p, c);
  return true;
}
#else
// ---- ramp tests: we are the remotes ----
static uint32_t g_testRemoteFcnt[5] = {0, 7000, 8000, 9000, 10000};
// A remote sends this command to the ramp, signed with the ramp's last status packet like a real remote
inline void rxCommand(const CommandPacket &c, int16_t rssi = -60, int8_t snr = 7)
{
  uint8_t p[RS_COMMAND_LEN], air[RS_HEADER_LEN + RS_COMMAND_LEN + RS_TAG_LEN];
  testPackCommand(c, p);
  uint8_t id = (c.remoteId >= 1 && c.remoteId <= 4) ? c.remoteId : 1;
  uint8_t n = rsSeal((uint8_t)(RS_SENDER_REMOTE + c.remoteId), g_testRemoteFcnt[id]++, true, lastTelemetryFcnt, p, RS_COMMAND_LEN, air);
  OnRxDone(air, n, rssi, snr);
}
// The status packet the ramp sent last (false if it doesn't decrypt)
inline bool sentTelemetry(TelemetryPacket &t)
{
  uint8_t p[RS_TELEMETRY_LEN];
  if (Radio.lastLen != RS_HEADER_LEN + RS_TELEMETRY_LEN + RS_TAG_LEN) return false;
  if (!rsOpen(Radio.last, (uint16_t)Radio.lastLen, false, 0, p)) return false;
  testUnpackTelemetry(p, t);
  return true;
}
#endif
