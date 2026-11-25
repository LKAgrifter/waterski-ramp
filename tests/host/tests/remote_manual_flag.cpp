// ALLOW_REMOTE_MANUAL (ramp main tab): false = remotes can't drive the motor by hand at all, only the ramp's own
// buttons can. The ramp refuses the requests and announces it in the telemetry (FLAG_REMOTE_MANUAL_OFF), so the
// remotes can say "Manual: at the ramp only". Built as: ramp with it on (default), ramp with it off (-DOFF, variant
// ramp_no_remote_manual) and the remote (-DREMOTE).
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
void run(int ms){ for(int i=0;i<ms;i++){ g_millis++; loop(); } }

#ifdef REMOTE
void telemetry(uint8_t flags){ static uint8_t ctr=0; TelemetryPacket t; memset(&t,0,sizeof(t)); ((uint8_t*)&t)[0]=0xA1; t.flags=flags; t.txCounter=ctr++;
  OnRxDone((uint8_t*)&t,sizeof(t),-60,7); run(20); }
int main(){
  for(int i=0;i<64;i++) pinv[i]=1;
  setup(); run(6000);
  telemetry(0);
  CHECK(!state.Remote_Manual_Off,"ramp allows remote manual -> remote shows normal manual page");
  telemetry(FLAG_REMOTE_MANUAL_OFF);
  CHECK(state.Remote_Manual_Off,"ramp says remote manual is off -> remote knows (\"Manual: at the ramp only\")");
  telemetry(0);
  CHECK(!state.Remote_Manual_Off,"and back on when the ramp says so");
  printf("%s (%d failures)\n",fails?"SOME FAILED":"ALL PASSED",fails); return fails;
}
#else
void sendCmd(uint8_t mode,int8_t h=-1,uint8_t seq=0){ static uint8_t ctr=0; CommandPacket c; c.remoteId=1; c.seq=seq; c.targetHeightIndex=h; c.waterCommand=-1; c.remoteControlMode=mode; c.bypassCommand=-1; c.txCounter=ctr++;
  OnRxDone((uint8_t*)&c,sizeof(c),-60,7); }
uint8_t lastAck=ACK_NONE;
bool remoteDrive(){ bool ran=false; for(int i=0;i<2000;i++){ if(i%150==0) sendCmd(1); run(1); if(outv[2]) ran=true; } lastAck=pendingAckResult; sendCmd(0); run(1500); return ran; }
uint8_t telemetryFlags(){ run(4000); TelemetryPacket t; memcpy(&t,Radio.last,sizeof(t)); return t.flags; }   // last broadcast the ramp sent
void holdMenu(){ pinv[38]=0; run(1100); pinv[38]=1; run(60); }
int main(){
  for(int i=0;i<64;i++) pinv[i]=1;
  pinv[34]=0;   // parked on the 4 ft sensor (calibrated)
  setup(); run(6000);
  CHECK(!diag.powerLoss && !heightSafetyBypassActive(),"(calibrated, bypass off)");
#ifdef OFF
  CHECK(!ALLOW_REMOTE_MANUAL,"(this build: ALLOW_REMOTE_MANUAL = false)");
  CHECK(telemetryFlags() & FLAG_REMOTE_MANUAL_OFF,"telemetry tells the remotes manual drive is off");
  CHECK(!remoteDrive(),"remote manual UP never powers the motor");
  CHECK(lastAck==ACK_LOCKED,"the ramp answers the remote's manual request with a refusal");
  for(int i=0;i<8 && currentScreen!=SCREEN_HEIGHT;i++) holdMenu();
  { bool ran=false; pinv[7]=0; for(int i=0;i<1500;i++){ run(1); if(outv[2]) ran=true; } pinv[7]=1; run(1500);
    CHECK(ran,"manual drive with the ramp's own UP button still works"); }
  sendCmd(0,1,40); run(20);
  CHECK(pendingAckResult==ACK_OK && state.Desired_Height==4.5f,"height picks from a remote are not affected");
#else
  CHECK(ALLOW_REMOTE_MANUAL,"(default build: ALLOW_REMOTE_MANUAL = true)");
  CHECK(!(telemetryFlags() & FLAG_REMOTE_MANUAL_OFF),"telemetry does not set the 'remote manual off' flag");
  CHECK(remoteDrive(),"remote manual UP drives the motor (as before)");
#endif
  printf("%s (%d failures)\n",fails?"SOME FAILED":"ALL PASSED",fails); return fails;
}
#endif
