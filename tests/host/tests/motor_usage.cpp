// Motor usage counters (Diagnostics page): cycles split by height move / manual and up / down, run time, height
// travelled and the average step time. They are saved in flash and loaded again at boot.
#include "Arduino.h"
#include "LoRaWan_APP.h"
static int pinv[64]; static int outv[64]; static int adc=3200;
RadioT Radio;
void pinMode(uint8_t,uint8_t){}
void digitalWrite(uint8_t p,uint8_t v){ outv[p]=v; }
int analogRead(uint8_t){return adc;}
// ---- physical ramp model (as in ramp_motion.cpp) ----
static double pos=4.0;
static const double STEP_MS=152.4/2.25*1000;   // 0.5 ft step = 67.7 s
static double ZONE=ZONE_MM/304.8;
static int coastMs=0; static int lastDir=0;
static const int LP[4]={34,33,47,48}; static const double LH[4]={4.0,4.5,5.0,5.5};
int digitalRead(uint8_t p){ for(int i=0;i<4;i++) if(p==LP[i]) return fabs(pos-LH[i])<=ZONE ? 1 : 0; return pinv[p]; }
void physics(){ int dir=0; if(outv[2]==1){ dir = outv[3]==1 ? 1 : -1; coastMs=60; lastDir=dir; } else if(coastMs>0){ coastMs--; dir=lastDir; }
  pos += dir*0.5/STEP_MS; if(pos<3.9) pos=3.9; if(pos>5.6) pos=5.6; }
#include SRC
static int fails=0;
#define CHECK(c,msg) do{ if(!(c)){printf("FAIL: %s\n",msg);fails++;} else printf("ok:   %s\n",msg);}while(0)
static unsigned long powerOnMs=0;
void run(int ms){ for(int i=0;i<ms;i++){ g_millis++; loop(); physics(); if(outv[2]) powerOnMs++; } }
void holdMenu(){ pinv[38]=0; run(700); pinv[38]=1; run(60); }
void goPage(ScreenState s){ if(state.oledSleeping) holdMenu(); for(int i=0;i<8 && currentScreen!=s;i++) holdMenu(); }
void pressH(int v){ goPage(SCREEN_SET_HEIGHT); adc=v; run(120); adc=3200; run(120); }
bool runUntilStopped(int maxMs){ int quiet=0; for(int i=0;i<maxMs;i++){ run(1); if(!state.Motor_Running && !state.DirectionChangePending && !state.MotorPowerPending) quiet++; else quiet=0; if(quiet>3000) return true; } return false; }
bool diagHas(const char *text){ buildDiagLines(); for(int i=0;i<listLineCount;i++) if(strstr(listLines[i],text)) return true; return false; }
void showDiag(){ buildDiagLines(); for(int i=0;i<listLineCount;i++) if(i>=listLineCount-8) printf("      | %s\n",listLines[i]); }
const int L4=2980,L5=1960;

int main(){
  for(int i=0;i<64;i++) pinv[i]=1;
  setup(); run(6000);
  CHECK(motorUse.cycles==0 && diagHas("Cycles: 0") && diagHas("Avg step: --"),"new board: no cycles, no average step yet");
  CHECK(listLineCount<=LIST_MAX_LINES,"(the Diagnostics list fits its buffer)");

  // 1. A height move 4 -> 5 ft: one cycle (height, up), 1.0 ft travelled, two timed steps
  powerOnMs=0;
  pressH(L5); CHECK(runUntilStopped(400000) && state.Ramp_Height==5.0f,"(moved 4 -> 5 ft)");
  CHECK(motorUse.cycles==1 && motorUse.autoRuns==1 && motorUse.manualRuns==0 && motorUse.upRuns==1 && motorUse.downRuns==0,
        "4 -> 5 ft: 1 cycle, counted as a height move, going up");
  CHECK(motorUse.halfSteps==2 && diagHas("Travelled: 1.0 ft"),"travelled 1.0 ft");
  printf("      run time counted %.1f s, motor power was on %.1f s\n", motorUse.runMs/1000.0, powerOnMs/1000.0);
  CHECK(llabs((long long)motorUse.runMs-(long long)powerOnMs)<200,"run time = the time the motor power relay was on");
  CHECK(motorUse.stepCount>=1 && fabs(motorUse.stepMsSum/(double)motorUse.stepCount-STEP_MS)<2000,"average step time matches the real step time");

  // 2. Manual drive at the ramp: hold DOWN 5 s on the MANUAL page = 1 manual cycle, going down
  goPage(SCREEN_HEIGHT); pinv[6]=0; run(5000); pinv[6]=1; CHECK(runUntilStopped(20000),"(manual drive released)");
  CHECK(motorUse.cycles==2 && motorUse.manualRuns==1 && motorUse.downRuns==1,"5 s manual drive down: a 2nd cycle, manual, down");
  CHECK(motorUse.halfSteps==2,"manual nudge off the 5 ft sensor adds no travel (no new sensor reached)");

  // 3. Back to 4 ft: 5 -> 4 counts 1.0 ft (from the last sensor reached, 5 ft)
  pressH(L4); CHECK(runUntilStopped(400000) && state.Ramp_Height==4.0f,"(moved back to 4 ft)");
  CHECK(motorUse.cycles==3 && motorUse.halfSteps==4 && diagHas("Travelled: 2.0 ft"),"back down to 4 ft: 3 cycles, 2.0 ft travelled in all");
  CHECK(diagHas("Cycles: 3") && diagHas("Height moves: 2") && diagHas("Manual runs: 1") && diagHas("Up 1  Down 2"),"Diagnostics shows the split");
  showDiag();

  // 4. Saved: the counters come back after a reboot
  MotorUsage before = motorUse;
  motorUse = MotorUsage(); loadMotorUsage();
  CHECK(motorUse.cycles==before.cycles && motorUse.autoRuns==before.autoRuns && motorUse.manualRuns==before.manualRuns &&
        motorUse.upRuns==before.upRuns && motorUse.downRuns==before.downRuns && motorUse.halfSteps==before.halfSteps &&
        motorUse.runMs==before.runMs && motorUse.stepCount==before.stepCount && motorUse.stepMsSum==before.stepMsSum,
        "all counters are saved and load back after a reboot");
  CHECK(prefs.getULong("UseCycles",0)==3,"(saved under UseCycles)");

  printf("%s (%d failures)\n",fails?"SOME FAILED":"ALL PASSED",fails); return fails;
}
