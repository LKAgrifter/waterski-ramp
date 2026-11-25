// Ramp side of the remote water modes: waterCommand 3 (TIMER-mode remote tap) behaves exactly like a tap on the
// ramp's own Water page, is applied once per sequence number, and is never stretched by a BOAT remote's keep-alives.
#include "Arduino.h"
#include "LoRaWan_APP.h"
static int pinv[64]; static int outv[64]; static int adc=3200;
RadioT Radio;
void pinMode(uint8_t,uint8_t){} void digitalWrite(uint8_t p,uint8_t v){outv[p]=v;} int digitalRead(uint8_t p){ if(p==34||p==33||p==47||p==48) return !pinv[p]; return pinv[p]; } int analogRead(uint8_t){return adc;}
#include SRC
static int fails=0;
#define CHECK(c,msg) do{ if(!(c)){printf("FAIL: %s\n",msg);fails++;} else printf("ok:   %s\n",msg);}while(0)
void run(int ms){ for(int i=0;i<ms;i++){ g_millis++; loop(); } }
void jump(unsigned long ms){ g_millis+=ms; run(5); }   // let time pass without simulating every millisecond
void sendCmd(uint8_t remote,int8_t water,uint8_t seq){
  static uint8_t ctr=0; CommandPacket c; c.remoteId=remote; c.seq=seq; c.targetHeightIndex=-1; c.waterCommand=water; c.remoteControlMode=0; c.bypassCommand=-1; c.txCounter=ctr++;
  OnRxDone((uint8_t*)&c,sizeof(c),-60,7); run(20); }
int main(){
  for(int i=0;i<64;i++) pinv[i]=1;
  pinv[34]=0;   // parked on the 4 ft sensor
  setup(); run(6000);

  // ---- TIMER-mode remote taps ----
  sendCmd(2,3,10);
  CHECK(state.Water_Relay && outv[1]==1,"timer tap from a remote turns the water on (pump relay on)");
  CHECK(state.Water_Timeout==1,"first tap = 1 h");
  CHECK(state.Water_Source==SOURCE_LOCAL,"timer water is a fixed timer (not keep-alive water)");
  CHECK(state.Water_TimerRemoteId==2,"ramp remembers which remote set the timer");
  CHECK(waterRemainingMinutes()==60,"60 minutes left");
  sendCmd(2,3,10);
  CHECK(state.Water_Timeout==1,"a retry of the same seq is not applied twice");
  sendCmd(2,3,11);
  CHECK(state.Water_Timeout==2,"next tap (new seq) = 2 h");

  // ---- a BOAT remote's keep-alives and "on" must not touch it ----
  jump(30UL*60UL*1000UL);
  sendCmd(1,2,0);                      // keep-alive heartbeat from a boat remote
  CHECK(waterRemainingMinutes()==90,"keep-alive does not stretch a timer set from a TIMER remote (90 min left)");
  sendCmd(1,1,30);                     // boat remote "on" while the timer runs
  CHECK(state.Water_Timeout==2 && state.Water_Source==SOURCE_LOCAL && waterRemainingMinutes()==90,"boat remote 'on' leaves the running timer alone");

  // ---- the ramp's own Water page adds to the same timer ----
  waterControl(true, SOURCE_LOCAL);
  CHECK(state.Water_Timeout==3 && state.Water_TimerRemoteId==0,"tap at the ramp: 3 h, now shown as set by the ramp");

  // ---- runs out on its own ----
  jump(3UL*60UL*60UL*1000UL);
  CHECK(!state.Water_Relay && outv[1]==0,"water turns off when the timer runs out (no remote needed)");

  // ---- tap after 12 h turns it off, like the ramp ----
  for(uint8_t s=40;s<52;s++) sendCmd(3,3,s);
  CHECK(state.Water_Relay && state.Water_Timeout==12,"12 taps = 12 h");
  sendCmd(3,3,52);
  CHECK(!state.Water_Relay && state.Water_Timeout==0 && outv[1]==0,"13th tap turns the water off");
  sendCmd(3,3,53);
  CHECK(state.Water_Relay && state.Water_Timeout==1,"next tap starts again at 1 h");

  // ---- off from a remote ----
  sendCmd(3,0,54);
  CHECK(!state.Water_Relay && outv[1]==0,"off command turns it off");

  // ---- BOAT remote water still works as before ----
  sendCmd(1,1,31);
  CHECK(state.Water_Relay && state.Water_Source==SOURCE_REMOTE && state.Water_Timeout==1,"boat remote 'on' = 1 h keep-alive water");
  jump(50UL*60UL*1000UL); sendCmd(1,2,0);
  CHECK(waterRemainingMinutes()==60,"boat keep-alive refreshes boat water back to 60 min");
  sendCmd(2,3,12);
  CHECK(state.Water_Source==SOURCE_LOCAL && state.Water_Timeout==2 && state.Water_TimerRemoteId==2,"a timer tap on boat water turns it into a fixed 2 h timer");

  printf("%s (%d failures)\n",fails?"SOME FAILED":"ALL PASSED",fails); return fails;
}
