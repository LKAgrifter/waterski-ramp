#include "Arduino.h"
#include "LoRaWan_APP.h"
static int pinv[64]; static int outv[64]; static int adc=3200;
RadioT Radio;
void pinMode(uint8_t,uint8_t){} static int bootRelayOnWrites=0; static bool inSetup=true;
void digitalWrite(uint8_t p,uint8_t v){ if(p>=1&&p<=3){ outv[p]=v; if(inSetup && v==1) bootRelayOnWrites++; } else outv[p]=v; } int analogRead(uint8_t){return adc;}
// ---- physical ramp model ----
static double pos=4.0;            // ft
static const double STEP_MS=152.4/2.25*1000; // 135 rpm x 1 mm/rev = 2.25 mm/s -> 0.5 ft step = 67.7 s
static double ZONE=ZONE_MM/304.8;   // magnetic sensor active within +/-ZONE_MM of its height
static int coastMs=0; static int lastDir=0; static bool dead5=false, dead55=false;
static const int LP[4]={34,33,47,48}; static const double LH[4]={4.0,4.5,5.0,5.5};
int digitalRead(uint8_t p){ for(int i=0;i<4;i++) if(p==LP[i]){ if(i==2&&dead5) return 0; if(i==3&&dead55) return 0; return fabs(pos-LH[i])<=ZONE ? 1 : 0; } return pinv[p]; }
void physics(){ int dir=0; if(outv[2]==1){ dir = outv[3]==1 ? 1 : -1; coastMs=60; lastDir=dir; } else if(coastMs>0){ coastMs--; dir=lastDir; }
  pos += dir*0.5/STEP_MS; if(pos<3.9) pos=3.9; if(pos>5.6) pos=5.6; }
#include SRC
#include "radio_test.h"
bool onSw(double h){ return fabs(pos-h)<=ZONE; }
static int fails=0;
#define CHECK(c,msg) do{ if(!(c)){printf("FAIL: %s\n",msg);fails++;} else printf("ok:   %s\n",msg);}while(0)
static int starts=0; static int prevPow=0;
static int wrongDir=0; static int lastDirOut=0; static unsigned long dirChangedAt=0;
void run(int ms){ for(int i=0;i<ms;i++){ g_millis++; loop(); if(outv[3]!=lastDirOut){ lastDirOut=outv[3]; dirChangedAt=g_millis; if(outv[2]) wrongDir++; } if(outv[2] && (outv[3]!=(int)state.Motor_Direction_Up || g_millis-dirChangedAt<95)) wrongDir++; physics(); if(outv[2]&&!prevPow) starts++; prevPow=outv[2]; } }
void holdMenu(){ pinv[38]=0; run(1100); pinv[38]=1; run(60); }
void goPage(ScreenState s){ if(state.oledSleeping) holdMenu(); for(int i=0;i<8 && currentScreen!=s;i++) holdMenu(); }
void pressH(int v){ goPage(SCREEN_SET_HEIGHT); adc=v; run(120); adc=3200; run(120); }
const int L4=2980,L45=2610,L5=1960,L55=0;
bool runUntilStopped(int maxMs){ int quiet=0; for(int i=0;i<maxMs;i++){ run(1); if(!state.Motor_Running && !state.DirectionChangePending && !state.MotorPowerPending) quiet++; else quiet=0; if(quiet>3000) return true; } return false; }
void sendHeight(int8_t idx){ static uint8_t seq=10, ctr=0; CommandPacket c; c.remoteId=1; c.seq=seq++; c.targetHeightIndex=idx; c.waterCommand=-1; c.remoteControlMode=0; c.bypassCommand=-1; c.txCounter=ctr++; rxCommand(c,-60,7); }
// Stop overrun set LONGER than the sensor range (build with -DZONE_MM=1): the stop lands past the sensor.
// Expect: Diagnostics "Last stop: OFF sw!" and no hunting back and forth.
int main(){
  for(int i=0;i<64;i++) pinv[i]=1;
  setup(); inSetup=false; run(6000);
  pressH(L45); CHECK(runUntilStopped(500000),"move 4 -> 4.5 finishes");
  printf("      stopped %.1f mm past 4.5 (sensor range +/-%.0f mm)\n",(pos-4.5)*304.8,(double)ZONE_MM);
  CHECK(!state.Last_Stop_On_Switch && !onSw(4.5),"overrun too long: stops past the sensor, Diagnostics shows OFF sw!");
  int s0=starts; run(60000); CHECK(starts==s0,"...and it stays put (no hunting back)");
  printf("%s (%d failures)\n",fails?"SOME FAILED":"ALL PASSED",fails); return fails;
}
