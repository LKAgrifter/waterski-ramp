#include "Arduino.h"
#include "LoRaWan_APP.h"
static int pinv[64]; static int adc=3200;
RadioT Radio;
void pinMode(uint8_t,uint8_t){} void digitalWrite(uint8_t,uint8_t){} int digitalRead(uint8_t p){ if(p==34||p==33||p==47||p==48) return !pinv[p]; return pinv[p]; } int analogRead(uint8_t){return adc;}
#include SRC
static int fails=0;
#define CHECK(c,msg) do{ if(!(c)){printf("FAIL: %s\n",msg);fails++;} else printf("ok:   %s\n",msg);}while(0)
void run(int ms){ for(int i=0;i<ms;i++){ g_millis++; loop(); } }
void holdMenu(){ pinv[38]=0; run(1100); pinv[38]=1; run(60); }
int main(){
  for(int i=0;i<64;i++) pinv[i]=1;
  setup(); run(6000);
  CHECK(!state.Remote_Connected,"no ramp link (no telemetry ever received)");
  CHECK(currentScreen==SCREEN_HOME,"boot -> HOME after 5 s with no ramp");
  ScreenState want[6]={SCREEN_WATER,SCREEN_SET_HEIGHT,SCREEN_HEIGHT,SCREEN_DIAGNOSTICS,SCREEN_NETWORK,SCREEN_HOME};
  const char* nm[6]={"WATER","SET HEIGHT","MANUAL","DIAGNOSTICS","NETWORK","HOME"};
  for(int i=0;i<6;i++){ holdMenu(); char m[64]; snprintf(m,64,"hold MENU -> %s page (ramp disconnected)",nm[i]); CHECK(currentScreen==want[i],m); }
  // short tap must NOT change page
  pinv[38]=0; run(200); pinv[38]=1; run(60); CHECK(currentScreen==SCREEN_HOME,"short tap does not change page");
  // wake-from-sleep: press is swallowed, then paging works again
  holdMenu(); run(61000); CHECK(state.oledSleeping && currentScreen==SCREEN_HOME,"timeout -> sleeping, back on HOME");
  pinv[38]=0; run(1100); pinv[38]=1; run(60); CHECK(currentScreen==SCREEN_HOME,"first hold only wakes the screen (does not change page)");
  holdMenu(); CHECK(currentScreen==SCREEN_WATER,"next hold changes page normally");
  printf("\n%s (%d failures)\n",fails?"SOME FAILED":"ALL PASSED",fails); return fails;
}
