#include "Arduino.h"
#include "LoRaWan_APP.h"
static int pinv[64]; static int outv[64]; static int adc=3200; static int noise=0;
RadioT Radio;
void pinMode(uint8_t,uint8_t){} static int bootRelayOnWrites=0; static bool inSetup=true;
void digitalWrite(uint8_t p,uint8_t v){ if(p>=1&&p<=3){ outv[p]=v; if(inSetup && v==1) bootRelayOnWrites++; } else outv[p]=v; } int digitalRead(uint8_t p){ if(p==34||p==33||p==47||p==48) return !pinv[p]; return pinv[p]; }
int analogRead(uint8_t){ int v=adc + (noise? (rand()%(2*noise+1))-noise : 0); return v<0?0:(v>4095?4095:v); }
#include SRC
static int fails=0;
#define CHECK(c,msg) do{ if(!(c)){printf("FAIL: %s\n",msg);fails++;} else printf("ok:   %s\n",msg);}while(0)
int main(){
  for(int i=0;i<64;i++) pinv[i]=1;
  const int v[5]={3200,2980,2610,1960,0}; const LadderButton want[5]={BTN_NONE,BTN_4FT,BTN_45FT,BTN_5FT,BTN_55FT};
  const char* nm[5]={"none (3200)","4.0 ft (2980)","4.5 ft (2610)","5.0 ft (1960)","5.5 ft (0)"};
  for(int n: {0,60,100}){ noise=n; for(int k=0;k<5;k++){ adc=v[k]; int bad=0; for(int t=0;t<5000;t++) if(readLadder()!=want[k]) bad++;
      char m[96]; snprintf(m,96,"%s decodes correctly with +/-%d counts of noise on every sample (%d/5000 wrong)",nm[k],n,bad); CHECK(bad==0,m); } }
  printf("%s (%d failures)\n",fails?"SOME FAILED":"ALL PASSED",fails); return fails;
}
