#include "Arduino.h"
#include "LoRaWan_APP.h"
static int pinv[64]; static int outv[64]; static int adc=3200;
RadioT Radio;
void pinMode(uint8_t,uint8_t){} static int bootRelayOnWrites=0; static bool inSetup=true;
void digitalWrite(uint8_t p,uint8_t v){ if(p>=1&&p<=3){ outv[p]=v; if(inSetup && v==1) bootRelayOnWrites++; } else outv[p]=v; } int digitalRead(uint8_t p){ if(p==34||p==33||p==47||p==48) return !pinv[p]; return pinv[p]; } int analogRead(uint8_t){return adc;}
#include SRC
static int fails=0;
#define CHECK(c,msg) do{ if(!(c)){printf("FAIL: %s\n",msg);fails++;} else printf("ok:   %s\n",msg);}while(0)
void run(int ms){ for(int i=0;i<ms;i++){ g_millis++; loop(); } }
void holdMenu(){ pinv[38]=0; run(1100); pinv[38]=1; run(60); }
int main(){
  for(int i=0;i<64;i++) pinv[i]=1; pinv[34]=0;
  setup(); run(6000);
#if CALMODE
  float d0=state.Desired_Height; adc=1960; run(300); adc=3200; run(300);
  CHECK(state.Desired_Height==d0 && !state.Motor_Running,"cal mode: pressing a ladder button does NOT change height or move the ramp");
  adc=1960; run(100); CHECK(ladderRawAdc==1960 && readLadderFromRaw(ladderRawAdc)==BTN_5FT,"cal mode: raw reading captured and decoded");
  adc=3200; run(100);
  for(int i=0;i<4;i++) holdMenu(); CHECK(currentScreen==SCREEN_DIAGNOSTICS,"reach diagnostics");
  bool found=false; buildDiagLines(); for(int i=0;i<listLineCount;i++) if(strstr(listLines[i],"Ladder 3200")) found=true;
  CHECK(found,"diagnostics shows the ladder ADC line");
#else
  // ladder normal mode still selects heights
  holdMenu(); holdMenu();   // WATER -> SET HEIGHT page
  adc=1960; run(200); adc=3200; run(200); CHECK(state.Desired_Height==5.0f,"normal mode: 5 ft ladder button selects 5.0 ft");
  adc=2980; run(200); adc=3200; run(200); CHECK(state.Desired_Height==4.0f,"normal mode: 4 ft button selects 4.0 ft");
  run(500);
  // reversal on the manual page with the water on
  holdMenu(); CHECK(currentScreen==SCREEN_HEIGHT,"manual page");
  waterControl(true, SOURCE_REMOTE); run(1500); CHECK(outv[1]==1,"water on (pump relay on)");
  pinv[7]=0; run(2500); CHECK(state.Motor_Running && state.Motor_Direction_Up,"driving up");
  pinv[34]=1;   // off the bottom switch now
  long offAt=-1,onAt=-1; bool pumpBlip=false, overlap=false;
  pinv[7]=1; run(20); pinv[6]=0;
  for(int i=0;i<3000;i++){ g_millis++; loop();
    if(offAt<0 && outv[2]==0) offAt=g_millis;
    if(offAt>0 && onAt<0 && outv[2]==1) onAt=g_millis;
    if(outv[1]==1) pumpBlip=true; if(outv[1]&&outv[2]) overlap=true; }
  printf("      reversal: motor off for %ld ms before running down\n", onAt-offAt);
  CHECK(state.Motor_Running && !state.Motor_Direction_Up,"reverses to down");
  CHECK(onAt-offAt>=1000,"at least 1 s between directions");
  CHECK(!pumpBlip && !overlap,"pump stays off through the whole reversal");
  pinv[6]=1; run(2000); CHECK(outv[1]==1 && outv[2]==0,"pump back on after stopping");
#endif
  printf("%s (%d failures)\n",fails?"SOME FAILED":"ALL PASSED",fails); return fails;
}
