#include "Arduino.h"
#include "LoRaWan_APP.h"
static int pinv[64]; static int outv[64]; static int adc=3200;
RadioT Radio;
void pinMode(uint8_t,uint8_t){} static int bootRelayOnWrites=0; static bool inSetup=true;
void digitalWrite(uint8_t p,uint8_t v){ if(p>=1&&p<=3){ outv[p]=v; if(inSetup && v==1) bootRelayOnWrites++; } else outv[p]=v; } int digitalRead(uint8_t p){ if(p==34||p==33||p==47||p==48) return !pinv[p]; return pinv[p]; } int analogRead(uint8_t){return adc;}
#include SRC
static int fails=0;
#define CHECK(c,msg) do{ if(!(c)){printf("FAIL: %s\n",msg);fails++;} else printf("ok:   %s\n",msg);}while(0)
static bool moved=false;
void run(int ms){ for(int i=0;i<ms;i++){ g_millis++; loop(); if(outv[2]) moved=true; } }
void holdMenu(){ pinv[38]=0; run(1100); pinv[38]=1; run(60); }
void sendHeight(int8_t idx){ static uint8_t seq=10, ctr=0; CommandPacket c; c.remoteId=1; c.seq=seq++; c.targetHeightIndex=idx; c.waterCommand=-1; c.remoteControlMode=0; c.bypassCommand=-1; c.txCounter=ctr++; OnRxDone((uint8_t*)&c,sizeof(c),-60,7); }
int main(){
  for(int i=0;i<64;i++) pinv[i]=1;
  int bc=BC;
  const char* name[]={"fresh flash, no switch","power cut mid-move, no switch","normal power-off at 5 ft, no switch reading","two switches active at boot","normal power-off, parked on 4.5 switch","NC: all four sensors unplugged","NC: parked on 4 ft, 5 ft sensor wire broken"};
  if(bc==1){ prefs.putBool("Moving",true);  prefs.putFloat("LastHeight",4.5f); }
  if(bc==2){ prefs.putBool("Moving",false); prefs.putFloat("LastHeight",5.0f); }
  if(bc==3){ pinv[34]=0; pinv[47]=0; }
  if(bc==4){ prefs.putBool("Moving",false); prefs.putFloat("LastHeight",4.5f); pinv[33]=0; }
  if(bc==5){ pinv[34]=pinv[33]=pinv[47]=pinv[48]=0; }   // open circuit reads HIGH = "triggered" for NC
  if(bc==6){ prefs.putBool("Moving",false); prefs.putFloat("LastHeight",4.0f); pinv[34]=0; pinv[47]=0; }
  setup(); run(6000);
  printf("--- %s: Ramp_Height=%.1f CAL=%d sensorFault=%d\n",name[bc],state.Ramp_Height,diag.powerLoss,diag.heightSensorFault);
  holdMenu(); holdMenu(); // SET HEIGHT
  moved=false; adc=1960; run(150); adc=3200; run(8000); bool localAuto=moved;
  moved=false; sendHeight(3); run(8000); bool remoteAuto=moved;
  if(bc<=1){
    CHECK(diag.powerLoss,"CAL REQUIRED set");
    CHECK(!localAuto,"a height picked at the ramp does NOT move it");
    CHECK(!remoteAuto,"a height sent from the remote does NOT move it");
    holdMenu(); moved=false; pinv[7]=0; run(3000); pinv[7]=1; run(200);
    CHECK(moved,"manual drive at the ramp still works (to drive onto a switch and calibrate)");
  } else if(bc==2){
    CHECK(diag.powerLoss,"no switch at power-up -> CAL REQUIRED (it may have been moved while off)");
    CHECK(!localAuto,"a height picked at the ramp does NOT move it");
    CHECK(!remoteAuto,"a height sent from the remote does NOT move it");
    holdMenu(); moved=false; pinv[7]=0; run(3000); pinv[7]=1; run(200);
    CHECK(moved,"manual drive at the ramp still works");
    pinv[47]=0; run(200); CHECK(!diag.powerLoss && state.Ramp_Height==5.0f,"driving onto a switch (5 ft) clears CAL and sets the height");
  } else if(bc==3 || bc==5 || bc==6){
    CHECK(diag.heightSensorFault,"sensor fault flagged");
    CHECK(!localAuto && !remoteAuto,"no AUTO moves with the sensor fault");
  } else {
    CHECK(!diag.powerLoss && state.Ramp_Height==4.5f && localAuto,"parked on a switch: known height, AUTO works");
  }
  printf("%s (%d failures)\n",fails?"SOME FAILED":"ALL PASSED",fails); return fails;
}
