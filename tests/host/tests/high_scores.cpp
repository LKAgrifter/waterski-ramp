// The games' high scores are saved in flash (Preferences), so they survive a reboot; only a new best is written, and
// scores saved under another GM_SCORES_VERSION are cleared. Ramp and (-DREMOTE) remote.
#include "Arduino.h"
#include "LoRaWan_APP.h"
static int pinv[64]; static int adc=3200;
RadioT Radio;
void pinMode(uint8_t,uint8_t){} void digitalWrite(uint8_t,uint8_t){}
int digitalRead(uint8_t p){
#ifndef REMOTE
  if(p==34||p==33||p==47||p==48) return !pinv[p];
#endif
  return pinv[p]; }
int analogRead(uint8_t){return adc;}
#include SRC
static int fails=0;
#define CHECK(c,msg) do{ if(!(c)){printf("FAIL: %s\n",msg);fails++;} else printf("ok:   %s\n",msg);}while(0)
// A reboot: RAM is cleared, the flash (prefs) keeps its values
static void reboot(){ for(int g=0;g<=GM_COUNT;g++) gmHigh[g]=0; gmHighLoaded=false; }

int main(){
  for(int i=0;i<64;i++) pinv[i]=1;
#ifndef REMOTE
  pinv[34]=0;
#endif
  setup(); for(int i=0;i<3000;i++){ g_millis++; loop(); }

  gmLoadHighs();
  CHECK(gmHigh[1]==0 && gmHigh[5]==0,"a new board starts with no high scores");
  CHECK(gmNewHigh(2,17) && gmHigh[2]==17,"a first score is a new best");
  int w=prefs.writes;
  CHECK(!gmNewHigh(2,12) && !gmNewHigh(2,17) && gmHigh[2]==17,"a lower or equal score isn't a new best");
  CHECK(prefs.writes==w,"... and nothing is written to flash for it");
  CHECK(gmNewHigh(2,25) && gmNewHigh(4,140) && gmNewHigh(5,9),"higher scores are new bests");
  CHECK(prefs.writes==w+3,"each new best is written to flash once");

  reboot(); gamesFrame();
  CHECK(gmHigh[2]==25 && gmHigh[4]==140 && gmHigh[5]==9 && gmHigh[1]==0 && gmHigh[3]==0,
        "after a reboot, opening the games page brings back the saved high scores");
  CHECK(!gmNewHigh(2,20) && gmNewHigh(2,30) && gmHigh[2]==30,"and they still count against new games");

  prefs.putUChar("GmHiVer", GM_SCORES_VERSION + 1);   // saved by firmware with other scoring
  reboot(); gmLoadHighs();
  CHECK(gmHigh[2]==0 && gmHigh[4]==0 && gmHigh[5]==0,"scores saved under another GM_SCORES_VERSION are cleared");
  reboot(); gmLoadHighs();
  CHECK(gmHigh[2]==0 && prefs.getUChar("GmHiVer",0)==GM_SCORES_VERSION,"... for good (the version is updated)");
  printf("%s (%d failures)\n",fails?"SOME FAILED":"ALL PASSED",fails); return fails;
}
