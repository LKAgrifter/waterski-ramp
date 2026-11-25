#include "Arduino.h"
#include "LoRaWan_APP.h"
static int pinv[64]; static int adc=3200;
RadioT Radio;
void pinMode(uint8_t,uint8_t){} void digitalWrite(uint8_t,uint8_t){} int digitalRead(uint8_t p){ if(p==34||p==33||p==47||p==48) return !pinv[p]; return pinv[p]; } int analogRead(uint8_t){return adc;}
#include SRC
static int fails=0;
#define CHECK(c,msg) do{ if(!(c)){printf("FAIL: %s\n",msg);fails++;} else printf("ok:   %s\n",msg);}while(0)
void run(int ms){ for(int i=0;i<ms;i++){ g_millis++; loop(); } }
int main(){
  for(int i=0;i<64;i++) pinv[i]=1; setup(); run(6000);
#if !CALMODE
  // Heights only work on the SET HEIGHT page (HOME -> WATER -> SET HEIGHT)
  for(int i=0;i<2;i++){ pinv[38]=0; run(1100); pinv[38]=1; run(60); }
  if(currentScreen!=SCREEN_SET_HEIGHT){ printf("FAIL: could not reach SET HEIGHT\n"); fails++; }
#endif
  const int v[4]={2980,2610,1960,0}; const float h[4]={4.0f,4.5f,5.0f,5.5f};
  for(int k=0;k<4;k++){ float before=state.Desired_Height; adc=v[k]; run(150); adc=3200; run(150);
#if CALMODE
    CHECK(state.Desired_Height==before,"remote cal mode: ladder press sends no height");
#else
    (void)before; char m[64]; snprintf(m,64,"remote: button at %d requests %.1f ft",v[k],h[k]); CHECK(state.Desired_Height==h[k],m);
#endif
    run(3000); }
  printf("%s (%d failures)\n",fails?"SOME FAILED":"ALL PASSED",fails); return fails;
}
