#include "Arduino.h"
#include "LoRaWan_APP.h"
static int pinv[64]; static int outv[64]; static int adc=3200;
RadioT Radio;
static int prevOut[64];
void pinMode(uint8_t,uint8_t){} int digitalRead(uint8_t p){ if(p==34||p==33||p==47||p==48) return !pinv[p]; return pinv[p]; } int analogRead(uint8_t){return adc;}
static int bootRelayOnWrites=0; static bool inSetup=true;
void digitalWrite(uint8_t p,uint8_t v){ if(p>=1&&p<=3){ outv[p]=v; if(inSetup && v==1) bootRelayOnWrites++; } else outv[p]=v; }
#include SRC
void logEdges(const char* tag){ const int P[3]={2,3,1}; const char* N[3]={"POWER","DIR","PUMP"};
  for(int i=0;i<3;i++) if(outv[P[i]]!=prevOut[P[i]]){ printf("  t=%6lu ms  %-5s -> %s   (%s)\n",g_millis,N[i],outv[P[i]]?"ON":"off",tag); prevOut[P[i]]=outv[P[i]]; } }
void run(int ms,const char* tag){ for(int i=0;i<ms;i++){ g_millis++; loop(); logEdges(tag);} }
void holdMenu(){ pinv[38]=0; run(1100,"menu"); pinv[38]=1; run(60,"menu"); }
int main(){
  for(int i=0;i<64;i++) pinv[i]=1;
  setup(); run(6000,"boot");
  printf("boot: CAL=%d bypass=%d\n",diag.powerLoss,heightSafetyBypassActive());
  while(currentScreen!=SCREEN_HEIGHT) holdMenu();
  pinv[7]=0; pinv[6]=0; run(2300,"hold UP+DOWN"); pinv[7]=1; pinv[6]=1; run(100,"bypass");
  printf("on MANUAL page, bypass=%d\n",heightSafetyBypassActive());
  auto remote=[&](uint8_t mode,int ms,const char* tag){ static uint8_t seq=50,ctr=0; for(int k=0;k<ms;k++){ if(k%150==0){ CommandPacket c; c.remoteId=1; c.seq=seq; c.targetHeightIndex=-1; c.waterCommand=2; c.remoteControlMode=mode; c.bypassCommand=-1; c.txCounter=ctr++; OnRxDone((uint8_t*)&c,sizeof(c),-60,7);} g_millis++; loop(); logEdges(tag);} };
  printf("--- LOCAL: hold UP 3 s, release, hold DOWN 3 s\n");
  pinv[7]=0; run(3000,"local UP held"); pinv[7]=1; run(2000,"local UP released");
  pinv[6]=0; run(3000,"local DOWN held"); pinv[6]=1; run(2000,"local DOWN released");
  printf("--- REMOTE: hold UP 3 s, release, hold DOWN 3 s\n");
  remote(1,3000,"remote UP"); remote(0,2000,"remote released");
  remote(2,3000,"remote DOWN"); remote(0,2000,"remote released");
  printf("--- LOCAL DOWN tapped for 0.8 s right after an UP\n");
  pinv[7]=0; run(2000,"local UP held"); pinv[7]=1; run(500,"local UP released");
  pinv[6]=0; run(800,"local DOWN (short)"); pinv[6]=1; run(1500,"local DOWN released");
}
