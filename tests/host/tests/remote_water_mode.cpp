// Remote water mode (saved per remote): BOAT = toggle + keep-alives (original behaviour), TIMER = taps like the
// ramp's Water page, no keep-alives. Switched on the WATER page by holding UP + DOWN 2 s.
// A small fake ramp answers the remote: it ACKs commands, applies each sequence number once and counts taps.
#include "Arduino.h"
#include "LoRaWan_APP.h"
static int pinv[64]; static int outv[64]; static int adc=3200;
RadioT Radio;
void pinMode(uint8_t,uint8_t){} void digitalWrite(uint8_t p,uint8_t v){outv[p]=v;} int digitalRead(uint8_t p){return pinv[p];} int analogRead(uint8_t){return adc;}
#include SRC
static int fails=0;
#define CHECK(c,msg) do{ if(!(c)){printf("FAIL: %s\n",msg);fails++;} else printf("ok:   %s\n",msg);}while(0)

// ---- fake ramp ----
static bool rampOnline=true, dropAcks=false;
static bool rampWater=false; static int rampTaps=0, rampOns=0, rampOffs=0, keepAlives=0;
static int lastSeq=-1, seenSent=0; static uint8_t ackId=0, ackSeq=0; static uint8_t telCtr=0;
void rampHears(const CommandPacket &c){
  if(c.waterCommand==2) keepAlives++;
  if(c.seq!=0 && c.seq==lastSeq) { }                 // retry: re-ACK, don't apply
  else {
    if(c.waterCommand==3){ rampTaps++; rampWater=true; }
    if(c.waterCommand==1){ rampOns++; rampWater=true; }
    if(c.waterCommand==0){ rampOffs++; rampWater=false; }
    if(c.seq!=0) lastSeq=c.seq;
  }
  if(!dropAcks){ ackId=c.remoteId; ackSeq=c.seq; }
}
void run(int ms){ for(int i=0;i<ms;i++){ g_millis++;
    if(Radio.sent!=seenSent){ seenSent=Radio.sent; CommandPacket c; memcpy(&c,Radio.last,sizeof(c)); if(rampOnline) rampHears(c); }
    if(rampOnline && g_millis%250==0){ TelemetryPacket t; memset(&t,0,sizeof(t)); ((uint8_t*)&t)[0]=0xA1;
      t.waterRelay=rampWater; t.waterRemainMin=rampWater?60:0; t.ackRemoteId=ackId; t.ackSeq=ackSeq; ackId=0; ackSeq=0; t.txCounter=telCtr++;
      OnRxDone((uint8_t*)&t,sizeof(t),-60,7); }
    loop(); } }
void press(int pin,int ms=80){ pinv[pin]=0; run(ms); pinv[pin]=1; run(80); }
void holdMenu(){ pinv[38]=0; run(1100); pinv[38]=1; run(60); }
void goPage(ScreenState s){ if(state.oledSleeping) holdMenu(); for(int i=0;i<8 && currentScreen!=s;i++) holdMenu(); }
void holdUpDown(int ms){ pinv[7]=0; pinv[6]=0; run(ms); pinv[7]=1; pinv[6]=1; run(100); }

int main(){
  for(int i=0;i<64;i++) pinv[i]=1;
  setup(); run(6000);
  CHECK(waterMode==WATER_MODE_BOAT,"a new remote starts in BOAT mode (original behaviour)");

  // ---- BOAT mode: unchanged ----
  goPage(SCREEN_WATER); CHECK(currentScreen==SCREEN_WATER,"on WATER page");
  press(39); run(2000);
  CHECK(rampWater && rampOns==1 && rampTaps==0,"BOAT: BACK tap sends 'on' (1 h keep-alive water)");
  keepAlives=0; run(10000);
  CHECK(keepAlives>0,"BOAT: heartbeats carry the water keep-alive while the water is on");
  press(39); run(2000);
  CHECK(!rampWater && rampOffs==1,"BOAT: next tap turns it off");

  // ---- switching mode ----
  holdUpDown(1000); CHECK(waterMode==WATER_MODE_BOAT,"holding UP + DOWN 1 s does not switch mode");
  pinv[7]=0; run(300); pinv[7]=1; run(100); CHECK(waterMode==WATER_MODE_BOAT && !state.Motor_Running,"UP alone on the WATER page does nothing");
  holdUpDown(5000); CHECK(waterMode==WATER_MODE_TIMER,"hold UP + DOWN 2 s -> TIMER mode (once per hold, even a long one)");
  CHECK(prefs.getUChar("WaterMode",9)==WATER_MODE_TIMER,"TIMER mode saved to flash");

  // ---- TIMER mode: taps, no keep-alives ----
  rampTaps=0; press(39); run(2000);
  CHECK(rampWater && rampTaps==1 && rampOns==1,"TIMER: BACK tap sends a timer tap (+1 h), not 'on'");
  keepAlives=0; run(10000);
  CHECK(keepAlives==0,"TIMER: no keep-alives while the water is on (the remote can sleep)");
  rampTaps=0; for(int k=0;k<3;k++) press(39,60);
  CHECK(waterTapsPending()>=2,"quick taps are queued while the first is on its way");
  run(5000);
  CHECK(rampTaps==3,"3 quick taps -> exactly 3 x +1 h at the ramp (none lost, none doubled)");
  CHECK(waterTapsPending()==0,"queue empty afterwards");

  // lost ACKs: the remote retries the same seq, the ramp applies it once
  rampTaps=0; uint32_t retriesBefore=loraStats.cmdRetries; dropAcks=true; press(39); run(700); dropAcks=false; run(3000);
  CHECK(loraStats.cmdRetries>retriesBefore,"(the ACK really was lost and the tap was resent)");
  CHECK(rampTaps==1,"a tap whose ACK was lost is retried but applied once");

  // hold BACK = off now (the press itself is a tap first, exactly like the ramp's own page)
  rampOffs=0; pinv[39]=0; run(1300); pinv[39]=1; run(3000);
  CHECK(!rampWater && rampOffs==1,"TIMER: hold BACK 1 s turns the water off");

  // ramp out of range: taps are given up on, not fired later
  rampOnline=false; run(2000); rampTaps=0; uint32_t failsBefore=loraStats.cmdFails;
  press(39); press(39); run(4000);
  CHECK(loraStats.cmdFails>failsBefore && waterTapsPending()==0,"(with no link the tap timed out and the queue was cleared)");
  rampOnline=true; run(6000);
  CHECK(rampTaps==0,"taps made with no ramp link are dropped, not sent when the link comes back");

  // ---- power cycle keeps the mode, toggle back ----
  waterMode=WATER_MODE_BOAT; setup(); run(6000);
  CHECK(waterMode==WATER_MODE_TIMER,"after a power cycle it is still in TIMER mode");
  goPage(SCREEN_WATER); holdUpDown(2300);
  CHECK(waterMode==WATER_MODE_BOAT && prefs.getUChar("WaterMode",9)==WATER_MODE_BOAT,"hold UP + DOWN again -> back to BOAT, saved");
  prefs.putUChar("WaterMode",7); setup(); run(1000);
  CHECK(waterMode==WATER_MODE_BOAT,"a corrupt saved mode falls back to BOAT");

  printf("%s (%d failures)\n",fails?"SOME FAILED":"ALL PASSED",fails); return fails;
}
