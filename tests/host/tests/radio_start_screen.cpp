// Before the radio starts (Mcu.begin(), where Heltec's library checks the board's radio license and loops forever if
// it was erased), the screen already says "Starting radio..." and what to do if it stays there. After start-up the
// normal boot screen replaces it. Ramp and (-DREMOTE) remote.
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
static std::string atMcuBegin; static int vextAtMcuBegin=-1;
int main(){
  for(int i=0;i<64;i++) pinv[i]=1;
#ifndef REMOTE
  pinv[34]=0;
#endif
  outv[Vext]=1;
  g_onMcuBegin=[]{ atMcuBegin=g_displayText; vextAtMcuBegin=outv[Vext]; };
  setup();
  printf("      screen when the radio starts: %s\n", atMcuBegin.c_str());
  CHECK(vextAtMcuBegin==0,"the OLED is powered (Vext on) before the radio starts");
  CHECK(atMcuBegin.find("Starting radio...")!=std::string::npos && atMcuBegin.find("license may be missing")!=std::string::npos,
        "the screen says 'Starting radio...' and what a stuck screen means before Heltec's license check");
  for(int i=0;i<200;i++){ g_millis++; loop(); }
  CHECK(g_displayText.find("Starting radio")==std::string::npos && !g_displayText.empty(),"after start-up the normal screen replaces it");
  printf("%s (%d failures)\n",fails?"SOME FAILED":"ALL PASSED",fails); return fails;
}
