// Remote ID is a saved setting: NETWORK page, hold BACK 2 s steps 1 -> 2 -> 3 -> 4 -> 1. Survives a power cycle.
#include "Arduino.h"
#include "LoRaWan_APP.h"
static int pinv[64]; static int outv[64]; static int adc=3200;
RadioT Radio;
void pinMode(uint8_t,uint8_t){} void digitalWrite(uint8_t p,uint8_t v){outv[p]=v;} int digitalRead(uint8_t p){return pinv[p];} int analogRead(uint8_t){return adc;}
#include SRC
static int fails=0;
#define CHECK(c,msg) do{ if(!(c)){printf("FAIL: %s\n",msg);fails++;} else printf("ok:   %s\n",msg);}while(0)
static uint8_t telCtr=0;
void run(int ms){ for(int i=0;i<ms;i++){ g_millis++;
    if(g_millis%250==0){ TelemetryPacket t; memset(&t,0,sizeof(t)); ((uint8_t*)&t)[0]=0xA1; t.rampHeightIndex=0; t.desiredHeightIndex=0; t.txCounter=telCtr++; OnRxDone((uint8_t*)&t,sizeof(t),-60,7); }
    loop(); } }   // a ramp sending telemetry at 4 Hz
void holdMenu(){ pinv[38]=0; run(800); pinv[38]=1; run(60); }
void goPage(ScreenState s){ if(state.oledSleeping) holdMenu(); for(int i=0;i<8 && currentScreen!=s;i++) holdMenu(); }
uint8_t sentRemoteId(){   // press a height button on SET HEIGHT and look at the command that goes out
  goPage(SCREEN_SET_HEIGHT); int before=Radio.sent; adc=2610; run(150); adc=3200;
  for(int i=0;i<2000 && Radio.sent==before;i++) run(1);
  CommandPacket c; memcpy(&c,Radio.last,sizeof(c)); run(3000); return c.remoteId; }
int main(){
  for(int i=0;i<64;i++) pinv[i]=1;
  setup(); run(3000);
  CHECK(REMOTE_ID==1,"new board starts as remote 1");
  CHECK(sentRemoteId()==1,"its commands go out as remote 1");
  goPage(SCREEN_NETWORK); CHECK(currentScreen==SCREEN_NETWORK,"on NETWORK page");
  pinv[39]=0; run(1000); pinv[39]=1; run(100); CHECK(REMOTE_ID==1,"a 1 s BACK hold does not change the ID");
  pinv[39]=0; run(2300); pinv[39]=1; run(100); CHECK(REMOTE_ID==2,"hold BACK 2 s -> remote 2");
  pinv[39]=0; run(5000); pinv[39]=1; run(100); CHECK(REMOTE_ID==3,"one long hold only steps once (-> 3)");
  CHECK(prefs.getUChar("RemoteId",0)==3,"saved to flash");
  CHECK(sentRemoteId()==3,"commands now go out as remote 3");
  goPage(SCREEN_NETWORK); for(int k=0;k<2;k++){ pinv[39]=0; run(2300); pinv[39]=1; run(200); }
  CHECK(REMOTE_ID==1,"4 wraps round to 1");
  pinv[39]=0; run(2300); pinv[39]=1; run(200); CHECK(REMOTE_ID==2,"(set to 2)");
  // power cycle: same flash, fresh RAM
  REMOTE_ID=1; setup(); run(3000);
  CHECK(REMOTE_ID==2,"after a power cycle it is still remote 2");
  prefs.putUChar("RemoteId",9); setup(); run(1000);
  CHECK(REMOTE_ID==1,"a corrupt saved ID falls back to 1");
  printf("%s (%d failures)\n",fails?"SOME FAILED":"ALL PASSED",fails); return fails;
}
