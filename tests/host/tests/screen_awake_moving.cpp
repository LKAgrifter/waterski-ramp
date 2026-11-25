// The screen stays on while the ramp is moving, and times out 1 minute after it stops (not after the last button
// press). On a remote, "moving" comes from the telemetry and only counts while the link is up.
// Built for the ramp and (with -DREMOTE) for the remote.
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

#ifdef REMOTE
static bool rampOn=true; static uint8_t moving=0, ctr=0;
#endif
void run(long ms){ for(long i=0;i<ms;i++){ g_millis++;
#ifdef REMOTE
    if(rampOn && g_millis%250==0){ TelemetryPacket t; memset(&t,0,sizeof t); ((uint8_t*)&t)[0]=0xA1; t.motorRunning=moving; t.motorDirectionUp=1; t.desiredHeightIndex=2; t.txCounter=ctr++; OnRxDone((uint8_t*)&t,sizeof t,-60,7); }
#endif
    loop(); } }

int main(){
  for(int i=0;i<64;i++) pinv[i]=1;
#ifdef REMOTE
  setup(); run(6000);
  CHECK(!state.oledSleeping && state.Remote_Connected,"(remote awake, ramp online)");
  moving=1; run(90000);
  CHECK(state.Motor_Running && !state.oledSleeping,"ramp moving for 90 s with no button pressed: screen still on");
  moving=0; run(30000);
  CHECK(!state.oledSleeping,"30 s after the ramp stopped: still on (the minute counts from the stop)");
  run(31000);
  CHECK(state.oledSleeping,"61 s after the ramp stopped: screen off");
  pinv[38]=0; run(100); pinv[38]=1; run(100);
  CHECK(!state.oledSleeping,"(woken with a press)");
  moving=1; run(2000); rampOn=false; run(65000);
  CHECK(state.oledSleeping,"link lost while the ramp was moving: the remote still sleeps");
  rampOn=true; moving=1; run(30000);
  CHECK(state.oledSleeping,"ramp moving doesn't wake a screen that is already asleep");
#else
  pinv[34]=0;   // parked on the 4 ft sensor
  setup(); run(6000);
  for(int i=0;i<8 && currentScreen!=SCREEN_SET_HEIGHT;i++){ pinv[38]=0; run(700); pinv[38]=1; run(60); }
  CHECK(currentScreen==SCREEN_SET_HEIGHT,"(on SET HEIGHT)");
  adc=1960; run(150); adc=3200; run(2000);           // pick 5.0 ft: the ramp starts moving up
  CHECK(state.Motor_Running,"(ramp moving up)");
  run(85000);                                          // the sensors don't change here, so it keeps going until the step timeout
  CHECK(state.Motor_Running && !state.oledSleeping,"ramp moving for 87 s with no button pressed: screen still on");
  run(15000);
  CHECK(!state.Motor_Running,"(stopped by the per-step motor timeout)");
  run(30000);
  CHECK(!state.oledSleeping,"30 s after the ramp stopped: still on (the minute counts from the stop)");
  run(31000);
  CHECK(state.oledSleeping && currentScreen==SCREEN_HOME,"61 s after it stopped: screen off, back on HOME");
#endif
  printf("%s (%d failures)\n",fails?"SOME FAILED":"ALL PASSED",fails); return fails;
}
