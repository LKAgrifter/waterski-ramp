// Simulation mode (SIM_MODE): sensors from USB "@S" lines, relays reported as "@R" lines, real relay pins left off.
//   -DSIM            build with SIM_MODE = true (variant ramp_sim): the virtual ramp is driven ONLY by the @R lines
//   -DSIM -DNOSENSOR the PC sends nothing at boot: CAL REQUIRED, and setup() doesn't hang waiting
//   (neither)        normal build: no simulator traffic at all, relays and sensors on their pins as usual
#include "Arduino.h"
#include "LoRaWan_APP.h"
static int pinv[64]; static int outv[64]; static int adc=3200;
RadioT Radio;
static int relayPinOnWrites=0;
void pinMode(uint8_t,uint8_t){}
void digitalWrite(uint8_t p,uint8_t v){ outv[p]=v; if(p>=1&&p<=3&&v==1) relayPinOnWrites++; }
int analogRead(uint8_t){return adc;}
// ---- physical ramp model (as ramp_motion.cpp) ----
static double pos=4.0;
static const double STEP_MS=152.4/2.25*1000;
static const double ZONE=8/304.8;
static const int LP[4]={34,33,47,48}; static const double LH[4]={4.0,4.5,5.0,5.5};
static int sensorPinReads=0;
int digitalRead(uint8_t p){ for(int i=0;i<4;i++) if(p==LP[i]){ sensorPinReads++; return fabs(pos-LH[i])<=ZONE ? 1 : 0; } return pinv[p]; }
int sensorMask(){ int m=0; for(int i=0;i<4;i++) if(fabs(pos-LH[i])<=ZONE) m|=1<<i; return m; }
#include SRC
static int fails=0;
#define CHECK(c,msg) do{ if(!(c)){printf("FAIL: %s\n",msg);fails++;} else printf("ok:   %s\n",msg);}while(0)

// ---- the PC side: parse what the board prints ----
static int rPow=0,rDir=0,rPump=0, rLines=0, helloCount=0, helloVer=-1, tLines=0, otherAt=0, dirUnderPower=0;
static std::string lastT;
void pumpSerial(){
  size_t nl;
  while((nl=g_serialOut.find('\n'))!=std::string::npos){
    std::string l=g_serialOut.substr(0,nl); g_serialOut.erase(0,nl+1);
    int a,b,c;
    if(l.rfind("@R ",0)==0 && sscanf(l.c_str(),"@R %d %d %d",&a,&b,&c)==3){ if(b!=rDir && (rPow||a)) dirUnderPower++; rPow=a; rDir=b; rPump=c; rLines++; }
    else if(l.rfind("@HELLO ",0)==0){ helloCount++; helloVer=atoi(l.c_str()+7); }
    else if(l.rfind("@T ",0)==0){ tLines++; lastT=l+" "; }
    else if(!l.empty() && l[0]=='@') otherAt++;
  }
}
bool tHas(const char* kv){ return lastT.find(std::string(" ")+kv+" ")!=std::string::npos; }
static bool feeding=true;
static int coast=0, lastDir=0;
void physics(){
#ifdef SIM
  int pw=rPow, up=rDir;
#else
  int pw=outv[2], up=outv[3];
#endif
  int dir=0; if(pw){ dir=up?1:-1; coast=60; lastDir=dir; } else if(coast>0){ coast--; dir=lastDir; }
  pos += dir*0.5/STEP_MS; if(pos<3.9) pos=3.9; if(pos>5.6) pos=5.6;
}
void run(int ms){ for(int i=0;i<ms;i++){ g_millis++; if(feeding && g_millis%50==0){ g_serialIn+="@S "+std::to_string(sensorMask())+"\n"; } loop(); pumpSerial(); physics(); } }
void holdMenu(){ pinv[38]=0; run(1100); pinv[38]=1; run(60); }
void goPage(ScreenState s){ if(state.oledSleeping) holdMenu(); for(int i=0;i<8 && currentScreen!=s;i++) holdMenu(); }
void pressH(int v){ goPage(SCREEN_SET_HEIGHT); adc=v; run(120); adc=3200; run(120); }
bool runUntilStopped(int maxMs){ int quiet=0; for(int i=0;i<maxMs;i++){ run(1); if(!state.Motor_Running && !state.DirectionChangePending && !state.MotorPowerPending) quiet++; else quiet=0; if(quiet>3000) return true; } return false; }
bool onSw(double h){ return fabs(pos-h)<=ZONE; }

int main(){
  for(int i=0;i<64;i++) pinv[i]=1;
  g_serialCapture=true;
#ifdef SIM
#ifndef NOSENSOR
  g_serialIn="@S 1\n";            // the page is already sending: parked on the 4 ft sensor
#endif
  setup(); pumpSerial();
  CHECK(helloCount==1 && helloVer==SIM_PROTOCOL_VERSION,"boot: sends @HELLO with the protocol version");
  CHECK(rLines>=1 && rPow==0 && rDir==0 && rPump==0,"boot: reports all relays off");
#ifdef NOSENSOR
  CHECK(diag.powerLoss,"no @S at boot: CAL REQUIRED (setup didn't hang waiting)");
  feeding=true; run(3000);
  CHECK(relayPinOnWrites==0 && sensorPinReads==0,"relay pins never switched on, sensor pins never read");
#else
  CHECK(state.Ramp_Height==4.0f && !diag.powerLoss,"boot: finds the virtual ramp on the 4 ft sensor (no CAL REQUIRED)");
  run(6000);
  CHECK(tLines>0 && tHas("h=40") && tHas("cal=0") && tHas("pg=HOME"),"@T status lines: height 4.0, no CAL, HOME page");

  // A normal AUTO move, carried out entirely by the simulated ramp
  pressH(1960); CHECK(runUntilStopped(400000),"4 -> 5 ft comes to rest");
  printf("      ended at %.4f ft, Ramp_Height %.1f\n",pos,state.Ramp_Height);
  CHECK(onSw(5.0) && state.Ramp_Height==5.0f,"picked 5 ft: the virtual ramp is driven up and parks on the 5 ft sensor");
  run(300); printf("      %s\n",lastT.c_str()); CHECK(tHas("h=50") && tHas("t=50") && tHas("mv=0") && tHas("pg=SET_HEIGHT"),"@T reports 5.0 ft, stopped, still on SET HEIGHT (the screen stays on while the ramp moves)");
  pressH(2610); CHECK(runUntilStopped(400000) && onSw(4.5) && state.Ramp_Height==4.5f,"5 -> 4.5 ft (down) also works");

  // Real hardware never touched
  CHECK(relayPinOnWrites==0,"relay pins never switched on (a sim build can't move a real ramp)");
  CHECK(sensorPinReads==0,"sensor pins never read");
  CHECK(dirUnderPower==0,"direction relay never reported switching while motor power was on");
  CHECK(otherAt==0,"no unknown @ lines");

  // PC goes quiet: sensors read "not triggered" after ~500 ms, and come back when it resumes
  feeding=false; run(400); CHECK(limitMask()==2,"brief gap (<500 ms): still on the 4.5 ft sensor");
  run(200); CHECK(limitMask()==0 && state.Ramp_Height==4.5f && !state.Motor_Running,"no @S for 500 ms: all sensors off, nothing moves");
  feeding=true; run(100); CHECK(limitMask()==2,"@S resumes: back on the 4.5 ft sensor");

  // Bad input is ignored; good input after it still works
  feeding=false;
  g_serialIn+="@S 99\n@Sx\n@S \n@S 3x\n@S -1\nrandom text\n@S 1" + std::string(40,'0') + "\n";
  run(1); CHECK(simSensorMask==2,"malformed and over-long lines ignored");
  g_serialIn+="@S 6\r\n"; run(1); CHECK(simSensorMask==6,"a good line after them (CRLF ending) is accepted");
  g_serialIn+="@S 2\n"; feeding=true; run(100);
  int h0=helloCount; g_serialIn+="@HELLO?\n"; run(5); CHECK(helloCount==h0+1,"@HELLO? gets a fresh @HELLO");

  // Manual drive at the ramp works too (MANUAL page, hold UP)
  goPage(SCREEN_HEIGHT); pinv[7]=0; run(5000); pinv[7]=1; run(2000);
  CHECK(pos>4.5+ZONE && !state.Motor_Running && rPow==0,"manual UP on the MANUAL page moves the virtual ramp, and stops on release");
#endif
#else
  setup(); run(6000); pumpSerial();
  CHECK(state.Ramp_Height==4.0f && !diag.powerLoss,"normal build: boots on the 4 ft sensor pin");
  pressH(1960); CHECK(runUntilStopped(400000) && onSw(5.0),"normal build: 4 -> 5 ft on the real relay pins");
  CHECK(relayPinOnWrites>0 && sensorPinReads>0,"normal build: relays switched and sensors read on their pins");
  CHECK(helloCount==0 && rLines==0 && tLines==0 && otherAt==0,"normal build: no simulator traffic on Serial");
  g_serialIn+="@S 0\n@HELLO?\n"; run(1000); pumpSerial();
  CHECK(helloCount==0 && limitMask()==4,"normal build: ignores @ lines from the PC");
#endif
  printf("%s (%d failures)\n",fails?"SOME FAILED":"ALL PASSED",fails); return fails;
}
