// The board's white LED (GPIO 35) shows the water: on = water on, slow blink = switched on but paused while the ramp
// moves, off = off. On a remote it follows the ramp's reports and is off without a link. Ramp and (-DREMOTE) remote.
#include "Arduino.h"
#include "LoRaWan_APP.h"
static int pinv[64]; static int outv[64]; static int adc=3200;
RadioT Radio;
void pinMode(uint8_t,uint8_t){}
void digitalWrite(uint8_t p,uint8_t v){ outv[p]=v; }
int analogRead(uint8_t){return adc;}
#ifndef REMOTE
// ---- physical ramp model (as in ramp_motion.cpp) ----
static double pos=4.0;
static const double STEP_MS=152.4/2.25*1000;
static double ZONE=8/304.8;
static int coastMs=0; static int lastDir=0;
static const int LP[4]={34,33,47,48}; static const double LH[4]={4.0,4.5,5.0,5.5};
int digitalRead(uint8_t p){ for(int i=0;i<4;i++) if(p==LP[i]) return fabs(pos-LH[i])<=ZONE ? 1 : 0; return pinv[p]; }
void physics(){ int dir=0; if(outv[2]==1){ dir = outv[3]==1 ? 1 : -1; coastMs=60; lastDir=dir; } else if(coastMs>0){ coastMs--; dir=lastDir; }
  pos += dir*0.5/STEP_MS; if(pos<3.9) pos=3.9; if(pos>5.6) pos=5.6; }
#else
int digitalRead(uint8_t p){ return pinv[p]; }
void physics(){}
#endif
#include SRC
#include "radio_test.h"
static int fails=0;
#define CHECK(c,msg) do{ if(!(c)){printf("FAIL: %s\n",msg);fails++;} else printf("ok:   %s\n",msg);}while(0)
static int ledOnMs=0, ledOffMs=0, ledFlips=0, lastLed=-1;
#ifdef REMOTE
static bool rampOn=true; static TelemetryPacket tel;
#endif
void run(long ms){ for(long i=0;i<ms;i++){ g_millis++;
#ifdef REMOTE
    if(rampOn && g_millis%250==0) rxTelemetry(tel);
#endif
    loop(); physics();
    if(outv[35]) ledOnMs++; else ledOffMs++; if(lastLed!=-1 && outv[35]!=lastLed) ledFlips++; lastLed=outv[35]; } }
void watch(long ms){ ledOnMs=ledOffMs=ledFlips=0; run(ms); }
void holdMenu(){ pinv[38]=0; run(700); pinv[38]=1; run(60); }
void goPage(ScreenState s){ for(int i=0;i<8 && currentScreen!=s;i++) holdMenu(); }

int main(){
  for(int i=0;i<64;i++) pinv[i]=1;
#ifndef REMOTE
  setup(); run(6000);
  watch(3000); CHECK(ledOnMs==0,"water off: white LED off");
  goPage(SCREEN_WATER); pinv[39]=0; run(100); pinv[39]=1; run(300);
  CHECK(state.Water_Relay,"(water switched on at the ramp)");
  watch(3000); CHECK(ledOffMs==0,"water on: white LED on, steady");
  goPage(SCREEN_SET_HEIGHT); adc=2610; run(150); adc=3200; run(3000);
  CHECK(state.Motor_Running && waterPausedForMotor(),"(ramp moving to 4.5 ft, water paused)");
  watch(10000); printf("      while moving: on %d ms, off %d ms, %d changes in 10 s\n",ledOnMs,ledOffMs,ledFlips);
  CHECK(ledFlips>=18 && ledFlips<=22 && abs(ledOnMs-ledOffMs)<1100,"water paused while the ramp moves: slow blink (1 s cycle)");
  for(int i=0;i<200000 && (state.Motor_Running || waterPausedForMotor()); i++) run(1);
  CHECK(!state.Motor_Running && state.Water_Relay,"(arrived, water back on)");
  watch(3000); CHECK(ledOffMs==0,"water back on after the move: steady again");
  goPage(SCREEN_WATER); pinv[39]=0; run(1300); pinv[39]=1; run(300);
  CHECK(!state.Water_Relay,"(water off: hold BACK)");
  watch(2000); CHECK(ledOnMs==0,"water off: LED off");
#else
  memset(&tel,0,sizeof tel); tel.packetId=0xA1;
  setup(); run(6000);
  CHECK(state.Remote_Connected,"(ramp online)");
  watch(3000); CHECK(ledOnMs==0,"ramp reports water off: white LED off");
  tel.waterRelay=1; tel.waterRemainMin=60; run(600);
  watch(3000); CHECK(ledOffMs==0,"ramp reports water on: LED on, steady");
  tel.motorRunning=1; run(600);
  watch(10000); CHECK(ledFlips>=18 && ledFlips<=22,"ramp moving (water paused): slow blink");
  tel.motorRunning=0; run(600); watch(2000); CHECK(ledOffMs==0,"move over: steady again");
  rampOn=false; run(2500); watch(2000);
  CHECK(!state.Remote_Connected && ledOnMs==0,"link lost: LED off (we don't know the water state)");
  rampOn=true; run(1000); watch(2000); CHECK(ledOffMs==0,"link back, water still on: LED on");
#endif
  printf("%s (%d failures)\n",fails?"SOME FAILED":"ALL PASSED",fails); return fails;
}
