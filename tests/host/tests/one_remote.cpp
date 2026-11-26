// ONE REMOTE AT A TIME: once the ramp accepts a remote's height pick or manual drive, other remotes' height picks
// and manual drive are refused (ACK_BUSY) until the ramp is idle again plus REMOTE_CONTROL_HOLD_MS. The ramp's own
// buttons take control away. Water from other remotes still works. With -DMULTI the flag is off (last pick wins).
// With -DREMOTE: the remote shows which remote has control.
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
static const double STEP_MS=152.4/2.25*1000;   // 0.5 ft step = 67.7 s
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

#ifndef REMOTE
static uint8_t seqs[5]={0,10,40,70,100}, ctrs[5];
TelemetryPacket lastReply(){ TelemetryPacket t; memset(&t,0,sizeof t); sentTelemetry(t); return t; }
static int txSeen=0;
void run(long ms){ for(long i=0;i<ms;i++){ g_millis++; loop(); if(Radio.sent!=txSeen){ txSeen=Radio.sent; OnTxDone(); } physics(); } }
// A remote's packet; returns the ramp's reply (ackResult)
uint8_t send(uint8_t id, int8_t height, int8_t water=-1, uint8_t mode=0, bool newSeq=true){
  CommandPacket c; c.remoteId=id; c.seq=newSeq?++seqs[id]:0; c.targetHeightIndex=height; c.waterCommand=water; c.remoteControlMode=mode; c.bypassCommand=-1; c.txCounter=ctrs[id]++;
  rxCommand(c); run(5); TelemetryPacket t=lastReply(); return (t.ackRemoteId==id) ? t.ackResult : 255; }
uint8_t ownerFlag(){ run(260); return (uint8_t)(lastReply().flags>>FLAG_OWNER_SHIFT)&7; }
bool runUntilStopped(int maxMs){ int quiet=0; for(int i=0;i<maxMs;i++){ run(1); if(!state.Motor_Running && !state.DirectionChangePending && !state.MotorPowerPending) quiet++; else quiet=0; if(quiet>500) return true; } return false; }
void holdMenu(){ pinv[38]=0; run(700); pinv[38]=1; run(60); }
#endif

int main(){
  for(int i=0;i<64;i++) pinv[i]=1;
#ifndef REMOTE
  setup(); run(6000);
  CHECK(state.Ramp_Height==4.0f,"(parked at 4 ft)");
#ifdef MULTI
  CHECK(send(1,2)==ACK_OK && send(2,0)==ACK_OK && state.Desired_Height==4.0f,"ONE_REMOTE_AT_A_TIME = false: a 2nd remote's pick is accepted at once (last pick wins)");
  CHECK(ownerFlag()==0,"no 'remote in control' in the status packets");
#else
  // 1. Remote 1 picks 5.0 ft and gets control
  CHECK(send(1,2)==ACK_OK,"remote 1 picks 5.0 ft: accepted");
  run(3000); CHECK(state.Motor_Running && controlOwner==1,"ramp moving, remote 1 has control");
  CHECK(ownerFlag()==1,"status packets say remote 1 has control");

  // 2. Remote 2 is refused, its pick ignored
  CHECK(send(2,0)==ACK_BUSY && state.Desired_Height==5.0f,"remote 2 picks 4.0 ft while it moves: refused (BUSY), still going to 5.0");
  CHECK(send(2,-1,-1,2,false)==ACK_BUSY && state.Control_Mode!=MODE_MANUAL,"remote 2 manual DOWN: refused (BUSY), no manual drive");
  CHECK(send(2,-1,1)==ACK_OK && state.Water_Relay,"remote 2 turns the water on: still allowed");
  CHECK(send(1,3)==ACK_OK && state.Desired_Height==5.5f,"remote 1 changes its own pick to 5.5 ft: accepted");

  // 3. Control lasts until the ramp is idle + REMOTE_CONTROL_HOLD_MS
  CHECK(runUntilStopped(400000) && state.Ramp_Height==5.5f,"(arrived at 5.5 ft)");
  CHECK(send(2,0)==ACK_BUSY,"just after it stops: remote 2 still refused (hold time)");
  run(REMOTE_CONTROL_HOLD_MS);
  CHECK(controlOwner==0 && ownerFlag()==0,"hold time over: nobody has control, status packets say so");
  CHECK(send(2,2)==ACK_OK && controlOwner==2,"now remote 2's pick (5.0 ft) is accepted and it has control");
  CHECK(send(1,3)==ACK_BUSY,"...and remote 1 is refused");

  // 4. Manual drive by a remote also takes control
  CHECK(runUntilStopped(400000),"(arrived at 5.0 ft)"); run(REMOTE_CONTROL_HOLD_MS+300);
  for(int i=0;i<10;i++){ uint8_t r=send(3,-1,-1,1,false); if(i==0) CHECK(r==ACK_OK,"remote 3 manual UP: accepted"); run(200); }
  CHECK(state.Control_Mode==MODE_MANUAL && controlOwner==3,"remote 3 is driving by hand and has control");
  CHECK(send(1,0)==ACK_BUSY,"remote 1's pick while remote 3 drives: refused");
  send(3,-1,-1,0,false); run(2000);
  CHECK(controlOwner==3,"remote 3 let go: still in control for the hold time");

  // 5. The ramp's own buttons take control away
  for(int i=0;i<8 && currentScreen!=SCREEN_SET_HEIGHT;i++) holdMenu();
  adc=2980; run(150); adc=3200; run(150);    // 4.0 ft at the ramp
  CHECK(controlOwner==0 && state.Desired_Height==4.0f,"a height picked at the ramp: no remote has control any more");
  CHECK(runUntilStopped(400000),"(local move finished)");
  CHECK(send(1,1)==ACK_OK,"after the local move, remote 1 can pick at once (no hold)");
#endif
#else
  // ---- REMOTE: the screens say which remote has control ----
  setup();
  static uint8_t ctr=0; uint8_t owner=0, ackSeq=0, ackRes=0, ackId=0;
  auto run=[&](long ms){ for(long i=0;i<ms;i++){ g_millis++;
      if(Radio.sent){ CommandPacket c; bool got=sentCommand(c); Radio.sent=0; if(got){ if(c.seq){ ackId=c.remoteId; ackSeq=c.seq; ackRes=ACK_BUSY; } else if(c.remoteControlMode){ ackId=c.remoteId; ackSeq=0; ackRes=ACK_BUSY; } } }
      if(g_millis%250==0){ TelemetryPacket t; memset(&t,0,sizeof t); ((uint8_t*)&t)[0]=0xA1; t.flags=(uint8_t)(owner<<FLAG_OWNER_SHIFT); t.ackRemoteId=ackId; t.ackSeq=ackSeq; t.ackResult=ackRes; ackId=0; t.txCounter=ctr++; rxTelemetry(t); }
      loop(); } };
  run(6000);
  CHECK(state.Remote_Connected && REMOTE_ID==1,"(remote 1, ramp online)");
  owner=2; run(600);
  CHECK(state.Control_Owner==2 && otherRemoteInControl(),"ramp says remote 2 has control");
  for(int i=0;i<8 && currentScreen!=SCREEN_SET_HEIGHT;i++){ pinv[38]=0; run(700); pinv[38]=1; run(60); }
  adc=1960; run(150); adc=3200; run(1200);
  CHECK(heightRequestBusy(),"a height pick is refused as BUSY");
  CHECK(strcmp(controlOwnerText(false),"Remote 2 has control")==0 && strcmp(controlOwnerText(true),"REMOTE 2 IN CONTROL")==0,"screen text: Remote 2 has control");
  CHECK(!heightRequestLocked(),"(not mistaken for 'ramp buttons in use')");
  owner=1; run(600);
  CHECK(!otherRemoteInControl(),"when THIS remote has control it isn't told it's locked out");
  owner=0; run(600);
  CHECK(!otherRemoteInControl(),"nobody in control: nothing shown");
#endif
  printf("%s (%d failures)\n",fails?"SOME FAILED":"ALL PASSED",fails); return fails;
}
