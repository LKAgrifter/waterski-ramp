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
void comboGames(){ pinv[7]=0; pinv[6]=0; run(2200); pinv[7]=1; pinv[6]=1; run(100); }
const int lv[4]={2980,2610,1960,0}; const float lh[4]={4.0f,4.5f,5.0f,5.5f};
bool pressAll(){ // press every height button; true if anything tried to change the height
  bool changed=false;
  for(int k=0;k<4;k++){ float d0=state.Desired_Height; adc=lv[k];
    for(int i=0;i<150;i++){ g_millis++; loop();
#ifdef REMOTE
      if(requestedHeightIndex!=-1) changed=true;
#else
      if(state.Motor_Running) changed=true;
#endif
      if(state.Desired_Height!=d0) changed=true; }
    adc=3200; run(150); }
  return changed;
}
const char* nm(ScreenState s){ switch(s){case SCREEN_HOME:return "HOME";case SCREEN_SET_HEIGHT:return "SET HEIGHT";case SCREEN_HEIGHT:return "MANUAL";case SCREEN_WATER:return "WATER";case SCREEN_NETWORK:return "NETWORK";case SCREEN_DIAGNOSTICS:return "DIAGNOSTICS";case SCREEN_GAME:return "GAMES";default:return "?";} }
int main(){
  for(int i=0;i<64;i++) pinv[i]=1;
#ifndef REMOTE
  pinv[34]=0;
#endif
  setup(); run(6000);
  CHECK(currentScreen==SCREEN_HOME,"boots to HOME");
  { bool w0=state.Water_Relay; pinv[39]=0; run(100); pinv[39]=1; run(100);
    CHECK((long)(waterHintUntil-millis())>0 && state.Water_Relay==w0,"HOME: BACK/WATER tap shows the water reminder and changes nothing");
    adc=2610; run(100); adc=3200; run(50);
    CHECK((long)(heightHintUntil-millis())>0 && (long)(waterHintUntil-millis())<=0,"a height press then replaces it with the height reminder");
    run(3000); CHECK((long)(waterHintUntil-millis())<=0 && (long)(heightHintUntil-millis())<=0,"reminders clear"); }
  for(int p=0;p<6;p++){
    if(currentScreen!=SCREEN_SET_HEIGHT){ char m[80]; snprintf(m,80,"%s page: height buttons do nothing",nm(currentScreen)); CHECK(!pressAll(),m); }
    if(currentScreen==SCREEN_HOME){ adc=2610; run(100); adc=3200; run(50); CHECK((long)(heightHintUntil-millis())>0,"HOME: stray height press shows the SET HEIGHT reminder"); run(3000); CHECK((long)(heightHintUntil-millis())<=0,"reminder clears after ~2.5 s"); }
    holdMenu();
  }
  CHECK(currentScreen==SCREEN_HOME,"6 holds cycle back to HOME (games hidden)");
  comboGames(); CHECK(currentScreen==SCREEN_GAME,"UP + DOWN 2 s on HOME opens GAMES"); { char m[80]; CHECK(!pressAll(),"GAMES page: height buttons do nothing"); (void)m; } holdMenu(); CHECK(currentScreen==SCREEN_HOME,"hold MENU in games -> HOME");
  pinv[39]=0; run(1500); pinv[39]=1; run(100); CHECK(currentScreen==SCREEN_HOME,"holding BACK alone on HOME does nothing");
  pinv[38]=0; pinv[39]=0; run(2500); pinv[38]=1; pinv[39]=1; run(100); CHECK(currentScreen!=SCREEN_GAME,"the old MENU + BACK hold no longer opens games");
  for(int i=0;i<7 && currentScreen!=SCREEN_HOME;i++) holdMenu();
  pinv[7]=0; pinv[6]=0; run(1500); pinv[7]=1; pinv[6]=1; run(100); CHECK(currentScreen==SCREEN_HOME,"UP + DOWN for 1.5 s does not open games");
  pinv[7]=0; run(300); pinv[7]=1; pinv[6]=0; run(2500); pinv[6]=1; run(100); CHECK(currentScreen==SCREEN_HOME,"UP then DOWN one after the other (not together) does not open games");
  pinv[38]=0; run(800); pinv[38]=1; run(60); CHECK(currentScreen==SCREEN_HOME,"MENU held 0.8 s does not change page (the hold is 1 s)");
  holdMenu(); CHECK(currentScreen==SCREEN_WATER,"on WATER");
  comboGames(); CHECK(currentScreen!=SCREEN_GAME,"UP + DOWN on another page does NOT open games");
  for(int i=0;i<7 && currentScreen!=SCREEN_HOME;i++) holdMenu();
  pinv[6]=0; run(300); pinv[7]=0; run(2100); pinv[7]=1; pinv[6]=1; run(100); CHECK(currentScreen==SCREEN_GAME,"DOWN first then UP (held together) also opens games");
  holdMenu(); CHECK(currentScreen==SCREEN_HOME,"(back to HOME)");
  // sleeping screen: press wakes only
  run(61000); CHECK(state.oledSleeping,"screen asleep"); CHECK(!pressAll(),"asleep: height press only wakes the screen");
  CHECK(currentScreen==SCREEN_HOME,"still on HOME after waking");
  holdMenu(); holdMenu(); CHECK(currentScreen==SCREEN_SET_HEIGHT,"two holds from HOME -> SET HEIGHT");
  for(int k=3;k>=0;k--){ adc=lv[k]; run(150); adc=3200; run(150); char m[64]; snprintf(m,64,"SET HEIGHT: button %d selects %.1f ft",lv[k],lh[k]); CHECK(state.Desired_Height==lh[k],m); run(4000); }
  CHECK((long)(heightHintUntil-millis())<=0,"no reminder from presses on SET HEIGHT");
  printf("%s (%d failures)\n",fails?"SOME FAILED":"ALL PASSED",fails); return fails;
}
