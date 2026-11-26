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
int main(){
  for(int i=0;i<64;i++) pinv[i]=1;
  setup(); run(6000);
  CHECK(state.Ramp_Height==4.0f && !diag.powerLoss,"boots parked on the 4 ft switch");
  holdMenu(); holdMenu(); CHECK(currentScreen==SCREEN_SET_HEIGHT,"on SET HEIGHT page");

  // 1. change your mind mid-move: 4 -> 5, then back to 4 before it gets there
  pressH(L5); run((int)(STEP_MS*0.3)); CHECK(state.Motor_Running && state.Motor_Direction_Up && pos>4.1,"heading up toward 5 ft");
  double turnPos=pos; pressH(L4);
  CHECK(runUntilStopped(500000),"comes to rest");
  printf("      picked 4 ft again at %.3f ft; ended at %.4f ft, Ramp_Height %.1f\n",turnPos,pos,state.Ramp_Height);
  CHECK(onSw(4.0) && state.Ramp_Height==4.0f && !state.Motor_Running,"drives BACK to 4 ft and stops on its switch");
  int s0=starts; run(60000); CHECK(starts==s0 && !state.Motor_Running,"stays put (no hunting around the switch)");
  printf("      parked %.1f mm from the 4 ft centre (sensor range +/-%.0f mm)\n",(pos-4.0)*304.8,(double)ZONE_MM);

  // 2. 4 -> 5.5 in one go: 36 s of travel, longer than the 30 s timeout, but each step is 12 s
  starts=0; pressH(L55); CHECK(runUntilStopped(400000),"4 -> 5.5 completes");
  printf("      ended at %.4f ft, timeout fault=%d\n",pos,diag.motorTimeoutFault);
  CHECK(onSw(5.5) && state.Ramp_Height==5.5f && !diag.motorTimeoutFault,"4 -> 5.5 (~3.4 min) arrives with NO timeout: timer restarts at each height");
  run(15000); CHECK(!state.Motor_Running,"still parked at 5.5");
  printf("      5.5 ft (end switch, arrived going UP): parked %.1f mm from centre; zone measured %.1f s\n",(pos-5.5)*304.8,state.Last_Zone_Ms/1000.0);
#if ZONE_MM > 4
  { double edge=-ZONE_MM, expect=edge+STOP_OVERRUN_MS/1000.0*2.25;
    CHECK(fabs((pos-5.5)*304.8-expect)<1.0,"end switch: ran the full overrun past first trigger (not cut short by the end check)");
    double zexp=2*ZONE_MM/2.25; CHECK(fabs(state.Last_Zone_Ms/1000.0-zexp)<0.5,"zone time measured while driving through 4.5 / 5 matches the sensor range");
    CHECK(state.Last_Stop_On_Switch,"Diagnostics: last stop on switch"); }
#endif
  printf("      measured last step: %.1f s (model step %.1f s)\n", state.Last_Step_Ms/1000.0, STEP_MS/1000.0);
  CHECK(fabs(state.Last_Step_Ms - STEP_MS) < 2000,"Diagnostics step time matches the real step time");

  // 3. from 5.5 pick 4, then change back to 5.5 between 5.5 and 5 (end-of-travel check must allow going back up)
  pressH(L4); run((int)(STEP_MS*0.3)); CHECK(state.Motor_Running && !state.Motor_Direction_Up && pos<5.45,"heading down from 5.5");
  pressH(L55); CHECK(runUntilStopped(500000),"comes to rest");
  CHECK(onSw(5.5) && state.Ramp_Height==5.5f,"drives back UP to 5.5 ft");

  // 4. reverse twice: 5.5 -> 4.5, back to 5.5, then 4.5 again; ends at 4.5
  pressH(L45); run((int)(STEP_MS*0.3)); pressH(L55); run((int)(STEP_MS*0.2)); pressH(L45); CHECK(runUntilStopped(500000),"comes to rest");
  CHECK(onSw(4.5) && state.Ramp_Height==4.5f,"changed mind twice: ends at the last pick (4.5)");
  printf("      4.5 ft arrived going DOWN: parked %.1f mm from centre\n",(pos-4.5)*304.8);

#if ZONE_MM > 4
  // overrun interrupted by a new pick: 4.5 -> 5, and pick 4 during the overrun at 5
  pressH(L5); { unsigned long t0=g_millis; while(!state.Overrun_Active && g_millis-t0<200000) run(1); }
  CHECK(state.Overrun_Active,"overrun starts when the 5 ft switch first triggers");
  run(800); pressH(L4); CHECK(runUntilStopped(500000),"comes to rest");
  CHECK(onSw(4.0) && state.Ramp_Height==4.0f && !state.Overrun_Active,"new pick during the overrun is obeyed (went to 4 ft)");
  printf("      4 ft (end switch, arrived going DOWN): parked %.1f mm from centre\n",(pos-4.0)*304.8);
  pressH(L45); CHECK(runUntilStopped(500000) && onSw(4.5),"(back at 4.5)");
#else
  CHECK(!state.Last_Stop_On_Switch,"overrun longer than the sensor range: Diagnostics shows Last stop OFF sw!");
#endif
  // 5. manual stop between heights: AUTO must NOT pull it back on its own
  goPage(SCREEN_HEIGHT); CHECK(currentScreen==SCREEN_HEIGHT,"manual page");
  pinv[7]=0; run((int)(STEP_MS*0.5)); pinv[7]=1; run(100);
  double mpos=pos; CHECK(runUntilStopped(500000) && pos>4.55 && pos<4.95,"manual UP then release: stops between 4.5 and 5");
  CHECK(fabs(pos-mpos)<0.01,"AUTO leaves it where manual stopped it");
  // ...until you pick a height. Remote picks 4.5 (the height it just left)
  sendHeight(1); CHECK(runUntilStopped(500000),"comes to rest");
  CHECK(onSw(4.5) && state.Ramp_Height==4.5f,"remote request for the height it just left drives back to 4.5");

  // 6. timeout still protects a single step: dead 5 ft switch
  for(int i=0;i<7 && currentScreen!=SCREEN_SET_HEIGHT;i++) holdMenu();
  // 4.5 -> 5 with a dead 5 ft switch: the step timeout (1.5 steps) fires before it can reach 5.5
  dead5=true; pressH(L5);
  unsigned long t0=g_millis; while(!diag.motorTimeoutFault && !diag.heightSensorFault && g_millis-t0<400000) run(1);
  double tf=(g_millis-t0)/1000.0; run(3000);
  printf("      dead 5 ft switch: stopped after %.1f s at %.3f ft (timeout=%d, missed-switch=%d)\n",tf,pos,diag.motorTimeoutFault,diag.heightSensorFault);
  CHECK(diag.motorTimeoutFault && !state.Motor_Running && pos>5.0 && pos<5.5,"dead 5 ft switch: step timeout stops it between 5 and 5.5");
  CHECK(tf > 99 && tf < 103,"...100 s after it left 4.5");
  // 6b. reset faults, park at 4.5 again
  diag.heightSensorFault=false; diag.heightFaultMask=0; diag.motorTimeoutFault=false; dead5=false;
  pressH(L45); CHECK(runUntilStopped(500000) && onSw(4.5),"(back at 4.5 ft after clearing the fault)");
  // 7. raise the target while already going up: 4.5 -> 5, then 5.5 before reaching 5 (no false missed-switch fault)
  pressH(L5); run((int)(STEP_MS*0.3)); pressH(L55); CHECK(runUntilStopped(500000),"comes to rest");
  CHECK(onSw(5.5) && !diag.heightSensorFault,"target raised mid-move: carries on to 5.5, no false fault");
  // 8. dead TOP switch: nothing beyond it, so the per-step timeout has to catch it
  pressH(L5); CHECK(runUntilStopped(500000) && onSw(5.0),"(at 5 ft)");
  dead55=true; pressH(L55); unsigned long t1=g_millis; while(!diag.motorTimeoutFault && g_millis-t1<300000) run(1);
  printf("      dead 5.5 ft switch: motor timeout after %.1f s\n",(g_millis-t1)/1000.0);
  CHECK(diag.motorTimeoutFault && !state.Motor_Running,"dead top switch: motor timeout stops it");
  CHECK(g_millis-t1 > MOTOR_STEP_TIMEOUT_MS-1000 && g_millis-t1 < MOTOR_STEP_TIMEOUT_MS+3000,"...about one step-timeout after it left the last height");
  CHECK(wrongDir==0,"never: motor power on with the direction relay wrong, unsettled, or switching under power");
  printf("%s (%d failures)\n",fails?"SOME FAILED":"ALL PASSED",fails); return fails;
}
