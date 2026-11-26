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
void hold(int pin,int ms){ pinv[pin]=0; run(ms); }
void release(int pin,int ms=60){ pinv[pin]=1; run(ms); }
void sendCmd(uint8_t mode,int8_t h=-1,int8_t water=-1,int8_t byp=-1,uint8_t seq=0){
  static uint8_t ctr=0; CommandPacket c; c.remoteId=1; c.seq=seq; c.targetHeightIndex=h; c.waterCommand=water; c.remoteControlMode=mode; c.bypassCommand=byp; c.txCounter=ctr++;
  rxCommand(c,-60,7); }
int main(){
  for(int i=0;i<64;i++){pinv[i]=1;}
  pinv[34]=0;                       // parked on the 4ft switch (active LOW)
  { int lowWrites=0; (void)lowWrites; }
  setup(); inSetup=false;
  CHECK(bootRelayOnWrites==0,"startup never switches a relay ON");
  CHECK(outv[1]==0 && outv[2]==0 && outv[3]==0,"all three relays off after setup()");
  run(6000);
  CHECK(currentScreen==SCREEN_HOME,"boot -> HOME after 5 s");
  CHECK(state.Ramp_Height==4.0f,"ramp height located from switch");

  // 1. Motor buttons dead on HOME
  hold(7,1500); release(7);
  CHECK(!state.Motor_Running && state.Control_Mode==MODE_AUTO,"UP button does nothing on HOME");

  // 2. Hold MENU -> Manual page
  hold(38,1100); release(38); CHECK(currentScreen==SCREEN_WATER,"hold MENU -> WATER page"); hold(38,1100); release(38); CHECK(currentScreen==SCREEN_SET_HEIGHT,"hold MENU -> SET HEIGHT page"); hold(38,1100); CHECK(currentScreen==SCREEN_HEIGHT,"hold MENU again -> MANUAL page"); release(38);
  // a button already held when arriving must not drive
  pinv[7]=0; run(100); release(7,100);
  // 3. Local manual up
  hold(7,1200);
  CHECK(state.Motor_Running && state.Motor_Direction_Up && outv[2]==1,"local UP drives motor up on MANUAL page");
  CHECK(state.Control_Mode==MODE_MANUAL && state.Control_Source==SOURCE_LOCAL,"mode MANUAL/LOCAL while held");
  CHECK(outv[3]==1,"direction relay energized while driving UP");
  release(7,100);
  run(300); CHECK(outv[3]==0 && outv[2]==0,"direction relay returns to normal/off after the motor stops");
  { bool bad=false; pinv[7]=0; for(int i=0;i<800;i++){ g_millis++; loop(); if(outv[2]==1 && outv[3]==0) bad=true; } release(7,100);
    CHECK(!bad,"motor power never applied before the direction relay is energized"); run(300); CHECK(outv[3]==0,"relay off again after second run"); }
  { // ---- water / motor interlock (pump relay NC contact feeds the motor) ----
    run(1500); waterControl(true, SOURCE_REMOTE); run(5);
    CHECK(state.Water_Relay && outv[1]==1,"water on while idle -> pump relay on");
    bool overlap=false; long pumpOffAt=-1, powerOnAt=-1; pinv[7]=0;
    for(int i=0;i<900;i++){ g_millis++; loop(); if(outv[1]==1 && outv[2]==1) overlap=true; if(pumpOffAt<0 && outv[1]==0) pumpOffAt=g_millis; if(powerOnAt<0 && outv[2]==1) powerOnAt=g_millis; }
    CHECK(state.Motor_Running,"motor runs with water on");
    CHECK(!overlap,"pump relay and motor power are never on together");
    CHECK(pumpOffAt>0 && powerOnAt-pumpOffAt>=100,"pump relay drops out >=100 ms before motor power");
    CHECK(state.Water_Relay,"water still switched on (just paused) while moving");
    // turning water on/off mid-move must not energize the pump
    waterControl(false); run(5); waterControl(true, SOURCE_REMOTE); run(5); CHECK(outv[1]==0,"water re-enabled mid-move stays paused");
    pinv[7]=1; long powerOffAt=-1, pumpOnAt=-1; overlap=false;
    for(int i=0;i<1500;i++){ g_millis++; loop(); if(outv[1]==1 && outv[2]==1) overlap=true; if(powerOffAt<0 && outv[2]==0) powerOffAt=g_millis; if(pumpOnAt<0 && outv[1]==1) pumpOnAt=g_millis; }
    CHECK(!overlap && powerOffAt>0 && pumpOnAt>powerOffAt,"pump comes back on only after motor power is off");
    printf("      pump resumed %ld ms after motor stop\n", pumpOnAt-powerOffAt);
    CHECK(pumpOnAt-powerOffAt>=950 && pumpOnAt-powerOffAt<=1100,"pump resumes ~1 s after the motor stops");
    // short jogs: pump must not cycle between them
    int pumpEdges=0, last=outv[1];
    for(int j=0;j<4;j++){ pinv[7]=0; for(int i=0;i<400;i++){ g_millis++; loop(); if(outv[1]!=last){pumpEdges++; last=outv[1];} } pinv[7]=1; for(int i=0;i<300;i++){ g_millis++; loop(); if(outv[1]!=last){pumpEdges++; last=outv[1];} } }
    CHECK(pumpEdges==1,"jogging the ramp: pump drops once and stays off between jogs");
    run(1500); CHECK(outv[1]==1,"pump back on after jogging ends");
    // water OFF to begin with: nothing touches the pump
    waterControl(false); run(5); CHECK(outv[1]==0,"water off -> pump off");
    bool pumpEver=false; pinv[7]=0; for(int i=0;i<800;i++){ g_millis++; loop(); if(outv[1]) pumpEver=true; } pinv[7]=1; for(int i=0;i<1500;i++){ g_millis++; loop(); if(outv[1]) pumpEver=true; }
    CHECK(!pumpEver,"water off: moving the ramp never turns the pump on");
    run(300);
  }
  CHECK(!state.Motor_Running && outv[2]==0 && state.Control_Mode==MODE_AUTO,"release stops motor and returns to AUTO");
  CHECK(!state.Overrun_Active,"manual release on a switch stops at once (no stop overrun)");
  CHECK(state.Desired_Height==state.Ramp_Height,"desired == ramp after manual (no AUTO runaway)");

  // 4. Leaving the page mid-drive stops it
  hold(7,900); CHECK(state.Motor_Running,"driving again");
  pinv[38]=0; run(1100); CHECK(currentScreen==SCREEN_DIAGNOSTICS,"MENU hold while driving -> DIAGNOSTICS page"); CHECK(!state.Motor_Running,"changing page stops the motor");
  release(38); release(7);

  // 5. Remote manual + 1 s dead-man
  g_millis+=10; for(int i=0;i<1200;i++){ if(i%150==0) sendCmd(1); run(1); }
  CHECK(state.Motor_Running && state.Control_Source==SOURCE_REMOTE && state.Control_Mode==MODE_MANUAL,"remote manual UP runs motor while packets arrive");
  run(700); CHECK(state.Control_Mode==MODE_MANUAL && state.Motor_Running,"still driving ~0.85 s after last packet");
  run(250); CHECK(state.Control_Mode==MODE_AUTO && !state.Motor_Running,"1 s with no packet -> AUTO and motor stopped");

  // 6. Remote mode-0 packet stops immediately
  for(int i=0;i<1200;i++){ if(i%150==0) sendCmd(1); run(1); }
  sendCmd(0); run(5);
  CHECK(!state.Motor_Running && state.Control_Mode==MODE_AUTO,"remote mode-0 packet stops at once");

  // 7. Local button beats remote
  { ScreenState order[6]={SCREEN_NETWORK,SCREEN_HOME,SCREEN_WATER,SCREEN_SET_HEIGHT,SCREEN_HEIGHT,SCREEN_DIAGNOSTICS};
    bool good=true; for(int k=0;k<6;k++){ pinv[38]=0; run(1100); release(38); if(currentScreen!=order[k]) good=false; }
    CHECK(good,"full cycle (games hidden): DIAG->NETWORK->HOME->WATER->SET HEIGHT->MANUAL->DIAG"); }
  while(currentScreen!=SCREEN_HEIGHT){ pinv[38]=0; run(1100); release(38); }
  CHECK(currentScreen==SCREEN_HEIGHT,"back on MANUAL");
  hold(7,1200); sendCmd(2); run(10);
  CHECK(state.Control_Source==SOURCE_LOCAL,"remote manual refused while local button held"); release(7);

  // 8. CAL REQUIRED and the bypass: remotes can never drive; at the ramp the bypass ignores the sensor fault; AUTO off.
  //    (Ramp taken off its sensor first: sitting on a sensor would clear CAL REQUIRED straight away.)
  uint8_t lastAck=ACK_NONE;
  auto remoteDrive=[&](){ bool ran=false; for(int i=0;i<1500;i++){ if(i%150==0) sendCmd(1); run(1); if(outv[2]) ran=true; } lastAck=pendingAckResult; sendCmd(0); run(50); return ran; };
  auto localDrive=[&](){ bool ran=false; pinv[7]=0; for(int i=0;i<1500;i++){ run(1); if(outv[2]) ran=true; } release(7,100); return ran; };   // the ramp's own UP button (MANUAL page)
  CHECK(currentScreen==SCREEN_HEIGHT,"(on the ramp's MANUAL page)");
  setHeightSafetyBypass(false);
  pinv[34]=1; run(100);                                    // between sensors
  diag.powerLoss=true; diag.heightSensorFault=true; run(3000);
  CHECK(!remoteDrive(),"remote manual blocked by cal/sensor fault when bypass OFF");
  CHECK(lastAck==ACK_LOCKED,"CAL REQUIRED: the ramp tells the remote its manual drive was refused");
  diag.heightSensorFault=false; run(10);
  CHECK(diag.powerLoss && !remoteDrive() && lastAck==ACK_LOCKED,"CAL REQUIRED alone (no fault): remote manual still refused");
  CHECK(localDrive(),"CAL REQUIRED: manual drive at the ramp still works (that's how you recalibrate)");
  diag.heightSensorFault=true;
  sendCmd(0,-1,-1,1,5); run(10);
  CHECK(!heightSafetyBypassActive(),"a remote can NOT enable the bypass (ramp only)");
  setHeightSafetyBypass(true); run(10);   // as if switched on at the ramp's own Manual page
  CHECK(heightSafetyBypassActive(),"bypass on at the ramp");
  CHECK(!remoteDrive() && lastAck==ACK_LOCKED,"bypass ON + CAL REQUIRED: remote manual refused");
  diag.powerLoss=false; run(10);
  CHECK(!remoteDrive() && lastAck==ACK_LOCKED,"bypass ON (calibrated): remote manual still refused - ramp buttons only");
  diag.powerLoss=true; run(10);
  CHECK(localDrive(),"bypass ON: manual drive AT THE RAMP runs despite cal + sensor fault");
  CHECK(!state.Motor_Running,"releasing the ramp's button stops it");
  sendCmd(0,2,-1,-1,9); CHECK(pendingAckResult==ACK_LOCKED,"remote height cmd refused during bypass");
  sendCmd(0,-1,-1,0,10); CHECK(heightSafetyBypassActive(),"a remote can NOT switch it off either (ramp only)");
  setHeightSafetyBypass(false); CHECK(!heightSafetyBypassActive(),"switched off at the ramp");

  // 9. Per-step motor timeout still applies in bypass (driving with the ramp's own button)
  setHeightSafetyBypass(true); run(1500);
  pinv[7]=0; run(MOTOR_STEP_TIMEOUT_MS+2000);
  CHECK(!state.Motor_Running && diag.motorTimeoutFault,"step motor timeout trips in bypass");
  release(7,100);
  setHeightSafetyBypass(false); diag.motorTimeoutFault=false; diag.powerLoss=false; diag.heightSensorFault=false;
  pinv[34]=0; run(200);                                    // back on the 4 ft sensor

  // 10. Water: remote keep-alive only refreshes remote-started water
  sendCmd(0,-1,1,-1,20); CHECK(state.Water_Relay && state.Water_Source==SOURCE_REMOTE && outv[1]==1,"remote water on (1 h)");
  run(30*60*1000UL/1000); // 30 s only, quick
  unsigned long t0=state.Water_RelayStart; run(500); sendCmd(0,-1,2,-1,0);
  CHECK(state.Water_RelayStart>t0,"keep-alive restarts the timer");
  sendCmd(0,-1,0,-1,21); CHECK(!state.Water_Relay && outv[1]==0,"remote water off");

  // 11. Screen timeout -> HOME
  hold(38,1100); release(38); hold(38,1100); release(38);
  CHECK(currentScreen!=SCREEN_HOME,"on a non-home page before timeout");
  run(61000);
  CHECK(state.oledSleeping && currentScreen==SCREEN_HOME,"OLED timeout -> HOME page");
  hold(7,300); release(7,100);
  CHECK(!state.Motor_Running,"wake-up press is swallowed");

  // 12. Regression: after leaving the 4ft switch, manual DOWN must still be allowed (Ramp_Height is stale at 4.0)
  while(currentScreen!=SCREEN_HEIGHT){ pinv[38]=0; run(1100); release(38); }
  pinv[34]=0; run(100);
  hold(6,1200); CHECK(!state.Motor_Running,"manual DOWN blocked while ON the 4ft switch"); release(6,100);
  pinv[34]=1; run(100);   // ramp has moved up off the bottom switch
  hold(6,1300); CHECK(state.Motor_Running && !state.Motor_Direction_Up,"manual DOWN allowed once off the 4ft switch"); CHECK(outv[3]==0 && outv[2]==1,"driving DOWN: direction relay stays off while motor is powered"); release(6,100); run(300); CHECK(outv[3]==0 && outv[2]==0,"idle after DOWN: both relays off");
  pinv[34]=0; run(100); hold(6,300); CHECK(!state.Motor_Running,"...and stops again when it reaches the 4ft switch"); release(6,100);

  printf("\n%s (%d failures)\n",fails?"SOME FAILED":"ALL PASSED",fails); return fails;
}
