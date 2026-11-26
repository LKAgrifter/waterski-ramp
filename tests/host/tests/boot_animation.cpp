// The boot screen's ramp-jump animation (Boot_Animation.ino): the skier moves, really jumps (the head rises well
// above where it rides on the water), stays clear of the two text lines, and the boot screen still hands over to
// HOME as soon as the skier has gone off the right edge (one jump). Ramp and (-DREMOTE) remote.
#include "Arduino.h"
#include "LoRaWan_APP.h"
#include <set>
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

// The skier's head is the only 3x3 filled rectangle: its top y in this frame, or -1000 if it isn't drawn
static int headY(const std::string &shapes){
  size_t at=0;
  while((at=shapes.find("R ",at))!=std::string::npos){
    int x,y,w,h;
    if(sscanf(shapes.c_str()+at,"R %d %d %d %d",&x,&y,&w,&h)==4 && w==3 && h==3 && x>=0 && x<126) return y;
    at+=2;
  }
  return -1000;
}

int main(){
  for(int i=0;i<64;i++) pinv[i]=1;
#ifndef REMOTE
  pinv[34]=0;
#endif
  setup();
  std::set<std::string> frames; std::string bootText;
  int lowest=-1000, highest=1000; std::string after, lastBoot; unsigned long bootFrom=0, homeAt=0;
  while(g_millis<7000){
    g_millis++; loop();
    bool boot=g_displayText.find("Initializing")!=std::string::npos;
    if(boot){
      if(!bootFrom) bootFrom=g_millis;
      frames.insert(g_displayShapes); lastBoot=g_displayShapes;
      if(bootText.empty()) bootText=g_displayText;
      int y=headY(g_displayShapes);
      if(y>-1000){ if(y>lowest) lowest=y; if(y<highest) highest=y; }
    } else if(bootFrom && !homeAt && !g_displayText.empty()) homeAt=g_millis;
  }
  after=g_displayText;
  printf("      boot screen from %lu ms to %lu ms\n",bootFrom,homeAt);
  printf("      boot text: %s   %zu different frames, head from y=%d up to y=%d\n",bootText.c_str(),frames.size(),lowest,highest);
#ifdef REMOTE
  CHECK(bootText=="Ramp-O-Matic Initializing|Remote Controller|","the remote's boot screen keeps its two text lines");
#else
  CHECK(bootText=="Ramp-O-Matic Initializing|Ramp Side Controller|","the ramp's boot screen keeps its two text lines");
#endif
  CHECK(frames.size()>=20,"the boot screen is animated (the frames keep changing)");
  CHECK(lowest-highest>=12,"the skier jumps: the head rises well above where it rides on the water");
  CHECK(highest>=25,"the skier stays below the text lines (y 0 to 24)");
  CHECK(lowest<=63-12,"the skier is drawn on the screen, above the water");
  CHECK(headY(lastBoot)==-1000,"the boot screen's last frame has the skier gone off the right edge");
  CHECK(homeAt>bootFrom+2000 && homeAt<bootFrom+2700,"the boot screen lasts one jump (~2.4 s), then hands over to HOME");
  CHECK(after.find("Initializing")==std::string::npos && !after.empty(),"HOME stays up afterwards");
  printf("%s (%d failures)\n",fails?"SOME FAILED":"ALL PASSED",fails); return fails;
}
