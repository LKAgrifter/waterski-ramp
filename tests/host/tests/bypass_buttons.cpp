// Height-sensor bypass (testing only) is a RAMP-ONLY control: on the ramp's MANUAL page, hold UP + DOWN together 2 s
// (not BACK). Built for the ramp and (with -DREMOTE) for the remote, which has no bypass control but still shows it.
#include "Arduino.h"
#include "LoRaWan_APP.h"
static int pinv[64]; static int outv[64]; static int adc=3200;
RadioT Radio;
void pinMode(uint8_t,uint8_t){} void digitalWrite(uint8_t p,uint8_t v){outv[p]=v;}
int digitalRead(uint8_t p){
#ifndef REMOTE
  if(p==34||p==33||p==47||p==48) return !pinv[p];
#endif
  return pinv[p]; }
int analogRead(uint8_t){return adc;}
#include SRC
static int fails=0;
#define CHECK(c,msg) do{ if(!(c)){printf("FAIL: %s\n",msg);fails++;} else printf("ok:   %s\n",msg);}while(0)

static bool motorPowered=false;            // ramp: power relay was ever on during a hold
#ifdef REMOTE
static bool rampBypass=false, drove=false; static int bypassRequests=0, seenSent=0; static uint8_t ackSeq=0, telCtr=0;
#endif
void run(int ms){ for(int i=0;i<ms;i++){ g_millis++;
#ifdef REMOTE
    if(Radio.sent!=seenSent){ seenSent=Radio.sent; CommandPacket c; memcpy(&c,Radio.last,sizeof(c));
      if(c.remoteControlMode!=0) drove=true;
      if(c.bypassCommand!=-1) bypassRequests++;
      ackSeq=c.seq; }
    if(g_millis%250==0){ TelemetryPacket t; memset(&t,0,sizeof(t)); ((uint8_t*)&t)[0]=0xA1; t.flags=rampBypass?FLAG_BYPASS:0;
      t.ackRemoteId=REMOTE_ID; t.ackSeq=ackSeq; ackSeq=0; t.txCounter=telCtr++; OnRxDone((uint8_t*)&t,sizeof(t),-60,7); }
#endif
    loop();
#ifndef REMOTE
    if(outv[2]) motorPowered=true;
#endif
  } }
void holdMenu(){ pinv[38]=0; run(800); pinv[38]=1; run(60); }
void holdBoth(int ms,int gap=0){ pinv[7]=0; run(gap); pinv[6]=0; run(ms); pinv[7]=1; pinv[6]=1; run(1500); }

int main(){
  for(int i=0;i<64;i++) pinv[i]=1;
#ifndef REMOTE
  pinv[34]=0;   // parked on the 4 ft sensor
#endif
  setup(); run(6000);
  for(int i=0;i<8 && currentScreen!=SCREEN_HEIGHT;i++) holdMenu();
  CHECK(currentScreen==SCREEN_HEIGHT,"on MANUAL page");

#ifdef REMOTE
  // ---- remote: no bypass control at all ----
  CHECK(!ALLOW_HEIGHT_SAFETY_BYPASS,"remote firmware ships with its bypass control off");
  pinv[39]=0; run(3000); pinv[39]=1; run(1500);
  holdBoth(2300); holdBoth(2300,300); holdBoth(6000);
  CHECK(bypassRequests==0,"holding BACK or UP + DOWN on the remote never asks the ramp for the bypass");
  CHECK(state.Control_Mode==MODE_AUTO,"remote not driving after the holds");
  rampBypass=true; run(1000);
  CHECK(state.Height_Safety_Bypass,"remote still SHOWS when the ramp's bypass is on (switched on at the ramp)");
  holdBoth(2300);
  CHECK(bypassRequests==0,"...and can't switch it off from the remote either");
  (void)drove;
#else
  // ---- ramp: the only place it can be switched ----
  CHECK(!heightSafetyBypassActive(),"bypass starts off");

  pinv[39]=0; run(3000); pinv[39]=1; run(1500);
  CHECK(!heightSafetyBypassActive(),"holding BACK does not toggle the bypass");

  holdBoth(1000);
  CHECK(!heightSafetyBypassActive(),"UP + DOWN for 1 s does not toggle it");

  motorPowered=false;
  holdBoth(2300);
  CHECK(heightSafetyBypassActive(),"hold UP + DOWN together 2 s -> bypass ON");
  CHECK(!motorPowered,"pressed together: motor power never came on");

  holdBoth(6000);
  CHECK(!heightSafetyBypassActive(),"hold again -> OFF (one toggle per hold, even a long one)");

  // UP first, DOWN 300 ms later: the motor may jog for that moment, but stops as soon as both are down
  holdBoth(2300,300);
  CHECK(heightSafetyBypassActive(),"UP then DOWN (300 ms apart), held 2 s -> bypass ON");
  CHECK(!state.Motor_Running && outv[2]==0,"motor stopped after the combo");
  holdBoth(2300);
  CHECK(!heightSafetyBypassActive(),"and back OFF");

  // A bypass request over the radio is ignored (ramp only), whatever firmware the remote runs
  { CommandPacket c; c.remoteId=1; c.seq=77; c.targetHeightIndex=-1; c.waterCommand=-1; c.remoteControlMode=0; c.bypassCommand=1; c.txCounter=0;
    OnRxDone((uint8_t*)&c,sizeof(c),-60,7); run(100); }
  CHECK(!heightSafetyBypassActive(),"a bypass request from a remote is ignored");

  // Other pages: UP + DOWN does not touch the bypass
  holdMenu(); CHECK(currentScreen==SCREEN_DIAGNOSTICS,"on DIAGNOSTICS page");
  holdBoth(2300);
  CHECK(!heightSafetyBypassActive(),"UP + DOWN on another page leaves the bypass alone");
#endif

  printf("%s (%d failures)\n",fails?"SOME FAILED":"ALL PASSED",fails); return fails;
}
