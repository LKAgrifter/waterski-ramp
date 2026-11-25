// Height-sensor bypass (testing only) is toggled on the MANUAL page by holding UP + DOWN together 2 s - not BACK.
// Built for the ramp and (with -DREMOTE) for the remote, which asks the ramp over the radio.
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
static bool rampBypass=false, drove=false; static int seenSent=0; static int lastSeq=-1; static uint8_t ackSeq=0, telCtr=0;
#endif
void run(int ms){ for(int i=0;i<ms;i++){ g_millis++;
#ifdef REMOTE
    if(Radio.sent!=seenSent){ seenSent=Radio.sent; CommandPacket c; memcpy(&c,Radio.last,sizeof(c));
      if(c.remoteControlMode!=0) drove=true;
      if(c.seq!=0 && c.seq!=lastSeq){ if(c.bypassCommand==0||c.bypassCommand==1) rampBypass=(c.bypassCommand==1); lastSeq=c.seq; }
      ackSeq=c.seq; }
    if(g_millis%250==0){ TelemetryPacket t; memset(&t,0,sizeof(t)); ((uint8_t*)&t)[0]=0xA1; t.flags=rampBypass?FLAG_BYPASS:0;
      t.ackRemoteId=REMOTE_ID; t.ackSeq=ackSeq; ackSeq=0; t.txCounter=telCtr++; OnRxDone((uint8_t*)&t,sizeof(t),-60,7); }
#endif
    loop();
#ifndef REMOTE
    if(outv[2]) motorPowered=true;
#endif
  } }
bool bypassOn(){
#ifdef REMOTE
  return state.Height_Safety_Bypass;
#else
  return heightSafetyBypassActive();
#endif
}
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
  CHECK(!bypassOn(),"bypass starts off");

  pinv[39]=0; run(3000); pinv[39]=1; run(1500);
  CHECK(!bypassOn(),"holding BACK no longer toggles the bypass");

  holdBoth(1000);
  CHECK(!bypassOn(),"UP + DOWN for 1 s does not toggle it");

  motorPowered=false;
#ifdef REMOTE
  drove=false;
#endif
  holdBoth(2300);
  CHECK(bypassOn(),"hold UP + DOWN together 2 s -> bypass ON");
#ifdef REMOTE
  CHECK(!drove,"pressed together: the remote never asked the ramp to drive");
#else
  CHECK(!motorPowered,"pressed together: motor power never came on");
#endif

  holdBoth(6000);
  CHECK(!bypassOn(),"hold again -> OFF (one toggle per hold, even a long one)");

  // UP first, DOWN 300 ms later: the motor may jog for that moment, but stops as soon as both are down
  holdBoth(2300,300);
  CHECK(bypassOn(),"UP then DOWN (300 ms apart), held 2 s -> bypass ON");
#ifdef REMOTE
  CHECK(state.Control_Mode==MODE_AUTO,"remote back to not driving after the combo");
#else
  CHECK(!state.Motor_Running && outv[2]==0,"motor stopped after the combo");
#endif
  holdBoth(2300);
  CHECK(!bypassOn(),"and back OFF");

  // Other pages: UP + DOWN does not touch the bypass
  holdMenu(); CHECK(currentScreen==SCREEN_DIAGNOSTICS,"on DIAGNOSTICS page");
  holdBoth(2300);
  CHECK(!bypassOn(),"UP + DOWN on another page leaves the bypass alone");

  printf("%s (%d failures)\n",fails?"SOME FAILED":"ALL PASSED",fails); return fails;
}
