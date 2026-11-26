// The remote's four height LEDs follow the ramp's telemetry like the ramp's own: the LED for its height when stopped,
// a strobe in the direction of travel while it moves, none while the height is unknown or the link is down.
#include "Arduino.h"
#include "LoRaWan_APP.h"
static int pinv[64]; static int outv[64]; static int adc=3200;
RadioT Radio;
void pinMode(uint8_t,uint8_t){}
void digitalWrite(uint8_t p,uint8_t v){ outv[p]=v; }
int digitalRead(uint8_t p){ return pinv[p]; }
int analogRead(uint8_t){return adc;}
#include SRC
#include "radio_test.h"
static int fails=0;
#define CHECK(c,msg) do{ if(!(c)){printf("FAIL: %s\n",msg);fails++;} else printf("ok:   %s\n",msg);}while(0)
static const int PINS[4]={40,19,20,26};   // LED for 4.0, 4.5, 5.0, 5.5 ft
static bool rampOn=true; static TelemetryPacket tel;
// Which LED is lit: 0..3, -1 none, -2 more than one
static int lit(){ int n=-1; for(int i=0;i<4;i++) if(outv[PINS[i]]){ if(n!=-1) return -2; n=i; } return n; }
static std::string seq; static int lastLit=-9;
void run(long ms){ for(long i=0;i<ms;i++){ g_millis++; if(rampOn && g_millis%250==0) rxTelemetry(tel); loop();
    int l=lit(); if(l!=lastLit){ seq += (l<0 ? (l==-1?'-':'X') : (char)('0'+l)); lastLit=l; } } }
void watch(long ms){ seq.clear(); lastLit=-9; run(ms); }

int main(){
  for(int i=0;i<64;i++) pinv[i]=1;
  memset(&tel,0,sizeof tel); tel.packetId=0xA1;
  tel.rampHeightIndex=2;   // 5.0 ft
  setup(); run(6000);
  CHECK(state.Remote_Connected,"(ramp online)");
  watch(2000); CHECK(seq=="2","ramp stopped at 5.0 ft: only the 5.0 ft LED, steady");
  tel.rampHeightIndex=0; run(600); watch(1000); CHECK(seq=="0","at 4.0 ft: the 4.0 ft LED");

  tel.motorRunning=1; tel.motorDirectionUp=1; run(600);
  watch(3000); printf("      moving up:   %s\n",seq.c_str());
  CHECK(seq.find("0123012301")!=std::string::npos && seq.find('X')==std::string::npos && seq.find('-')==std::string::npos,
        "moving up: one LED at a time, sweeping 4.0 -> 5.5 over and over");
  int n=0; for(char c: seq) if(c=='0') n++;
  CHECK(n>=3 && n<=5,"... one sweep every 750 ms");
  tel.motorDirectionUp=0; run(600);
  watch(3000); printf("      moving down: %s\n",seq.c_str());
  CHECK(seq.find("3210321032")!=std::string::npos && seq.find('X')==std::string::npos,"moving down: sweeping 5.5 -> 4.0");

  tel.motorRunning=0; tel.rampHeightIndex=1; run(600); watch(1000); CHECK(seq=="1","stopped again at 4.5 ft: the 4.5 ft LED");
  tel.powerLoss=1; run(600); watch(1000); CHECK(seq=="-","height not known (CAL REQUIRED): no height LED");
  tel.powerLoss=0; run(600);
  rampOn=false; run(3000); watch(1000); CHECK(!state.Remote_Connected && seq=="-","link lost: all height LEDs off");
  rampOn=true; run(1000); watch(1000); CHECK(seq=="1","link back: the 4.5 ft LED again");
  printf("%s (%d failures)\n",fails?"SOME FAILED":"ALL PASSED",fails); return fails;
}
