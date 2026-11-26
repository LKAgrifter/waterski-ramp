#include "Arduino.h"
#include "LoRaWan_APP.h"
static int pinv[64]; static int outv[64]; static int adc=3200;
RadioT Radio;
void pinMode(uint8_t,uint8_t){} static int bootRelayOnWrites=0; static bool inSetup=true;
void digitalWrite(uint8_t p,uint8_t v){ if(p>=1&&p<=3){ outv[p]=v; if(inSetup && v==1) bootRelayOnWrites++; } else outv[p]=v; } int digitalRead(uint8_t p){ if(p==34||p==33||p==47||p==48) return !pinv[p]; return pinv[p]; } int analogRead(uint8_t){return adc;}
#include SRC
#include "radio_test.h"
static int fails=0;
#define CHECK(c,msg) do{ if(!(c)){printf("FAIL: %s\n",msg);fails++;} else printf("ok:   %s\n",msg);}while(0)
void run(int ms){ for(int i=0;i<ms;i++){ g_millis++; loop(); } }
int main(){
  for(int i=0;i<64;i++) pinv[i]=1;
#ifndef REMOTE
  int boot=BOOTCASE;
  if(boot==1){ prefs.putBool("Moving",false); prefs.putFloat("LastHeight",5.0f); }   // stopped on 5 ft last time, no switch reading now
  if(boot==2){ prefs.putBool("Moving",true);  prefs.putFloat("LastHeight",4.5f); }   // power cut mid-move
  setup(); run(6000);
  if(boot==0){
    CHECK(!nowHeightKnown() && !setHeightKnown() && heightStr(state.Ramp_Height,nowHeightKnown())=="","fresh boot, no switch: Set and Now blank");
    pinv[34]=0; run(100);   // ramp reaches the 4 ft switch
    CHECK(nowHeightKnown() && setHeightKnown() && heightStr(state.Ramp_Height,true)=="4.0 ft","on the 4 ft switch: Now 4.0 ft, Set 4.0 ft");
  }
  if(boot==1){ CHECK(!nowHeightKnown() && !setHeightKnown(),"reboot with no switch active: Set/Now blank (CAL REQUIRED)"); }
  if(boot==2){ CHECK(!nowHeightKnown() && !setHeightKnown(),"power cut mid-move: Now/Set blank (CAL REQUIRED)");
               state.Desired_Height=5.0f; CHECK(setHeightKnown() && !nowHeightKnown(),"...picking a different height shows Set, Now stays blank"); }
#else
  setup(); run(6000);
  CHECK(!nowHeightKnown() && !setHeightKnown(),"remote, no ramp heard: Set and Now blank");
  TelemetryPacket t; memset(&t,0,sizeof(t)); ((uint8_t*)&t)[0]=0xA1; t.rampHeightIndex=1; t.desiredHeightIndex=1; t.powerLoss=1;
  rxTelemetry(t,-60,7); run(10);
  CHECK(state.Remote_Connected && !nowHeightKnown() && !setHeightKnown(),"ramp heard but height not located: still blank");
  t.powerLoss=0; t.desiredHeightIndex=2; t.txCounter=1; rxTelemetry(t,-60,7); run(10);
  CHECK(nowHeightKnown() && setHeightKnown() && state.Ramp_Height==4.5f && state.Desired_Height==5.0f,"ramp located: Now 4.5, Set 5.0 shown");
  run(20000); CHECK(!state.Remote_Connected && !nowHeightKnown() && !setHeightKnown(),"link lost: blank again");
#endif
  printf("%s (%d failures)\n",fails?"SOME FAILED":"ALL PASSED",fails); return fails;
}
