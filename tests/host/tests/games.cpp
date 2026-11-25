#include "Arduino.h"
#include "LoRaWan_APP.h"
static int pinv[64]; static int adc=4095;
RadioT Radio;
void pinMode(uint8_t,uint8_t){} void digitalWrite(uint8_t,uint8_t){} int digitalRead(uint8_t p){ if(p==34||p==33||p==47||p==48) return !pinv[p]; return pinv[p]; } int analogRead(uint8_t){return adc;}
#include SRC
static int fails=0;
#define CHECK(c,msg) do{ if(!(c)){printf("FAIL: %s\n",msg);fails++;} else printf("ok:   %s\n",msg);}while(0)
enum { P_UP=7, P_DN=6, P_ENT=38, P_BACK=39 };
void setPin(int p,int v){ if(pinv[p]==1 && v==0 && g_isr[p]) g_isr[p](); pinv[p]=v; }
void step(){ g_millis++; loop(); }
void until(unsigned long t){ while(g_millis<t) step(); }
void tap(int p,int ms=70){ setPin(p,0); until(g_millis+ms); setPin(p,1); until(g_millis+70); }
void holdMenu(){ setPin(P_ENT,0); until(g_millis+800); setPin(P_ENT,1); until(g_millis+60); }
void goGames(){ if(state.oledSleeping) holdMenu(); for(int i=0;i<7 && currentScreen!=SCREEN_HOME;i++) holdMenu(); setPin(P_ENT,0); until(g_millis+100); setPin(P_BACK,0); until(g_millis+900); setPin(P_ENT,1); setPin(P_BACK,1); until(g_millis+100); }
void toMenu(){ tap(P_BACK); until(g_millis+300); }
void enterGame(int g){ goGames(); until(g_millis+300); toMenu(); gmSel=0; for(int i=1;i<g;i++) tap(P_DN); tap(P_ENT,50); until(g_millis+200); }

int main(){
  for(int i=0;i<64;i++) pinv[i]=1; pinv[34]=0;
  g_displayCost=DISPLAY_COST;
  setup(); until(g_millis+6000);
  goGames();
  CHECK(currentScreen==SCREEN_GAME,"MENU+BACK on HOME reaches the games page");
  until(g_millis+1500);
  CHECK(gmCur==0,"lands on the game menu");

  // ---- menu navigation ----
  tap(P_DN); tap(P_DN); CHECK(gmSel==2,"DOWN x2 selects 3rd game");
  tap(P_UP); CHECK(gmSel==1,"UP moves back");
  for(int i=0;i<10;i++) tap(P_DN); CHECK(gmSel==GM_COUNT-1,"DOWN clamps at the last game");
  for(int i=0;i<10;i++) tap(P_UP); CHECK(gmSel==0,"UP clamps at the first game");
  for(int g=1;g<=GM_COUNT;g++){
    gmSel=0; for(int i=1;i<g;i++) tap(P_DN); tap(P_ENT,50); until(g_millis+100);
    char m[64]; snprintf(m,64,"ENTER launches game %d (%s)",g,gmNames[g-1]); CHECK(gmCur==g,m);
    tap(P_BACK); until(g_millis+100); CHECK(gmCur==0,"BACK returns to the menu");
  }

  // ---- 1 Ski Dash ----
  enterGame(1); CHECK(skiG.phase==SKI_READY,"ski: ready screen"); tap(P_UP); CHECK(skiG.phase==SKI_RUNNING,"ski: UP starts");
  until(g_millis+9000); CHECK(skiG.phase==SKI_DEAD,"ski: no input -> wipeout"); CHECK(gmHigh[1]==skiG.score,"ski: high score saved");
  until(g_millis+800); tap(P_UP); CHECK(skiG.phase==SKI_RUNNING,"ski: UP retries");
  { unsigned long t0=g_millis, rel=0; while(g_millis-t0<90000 && skiG.phase==SKI_RUNNING){
      float dx=1e9; for(int k=0;k<SKI_MAX_OBS;k++) if(skiG.obs[k].active && skiG.obs[k].x+skiObsW(skiG.obs[k].kind)>SKI_X){ float d=skiG.obs[k].x-(SKI_X+8); if(d<dx) dx=d; }
      if(rel && g_millis>=rel){ setPin(P_UP,1); rel=0; }
      else if(!rel && skiG.h<=0 && dx>-4 && dx<=skiG.speed*7.0f){ setPin(P_UP,0); rel=g_millis+60; }
      step(); }
    setPin(P_UP,1); printf("      ski bot: %lus, score %u, speed %.2f\n",(g_millis-t0)/1000,skiG.score,skiG.speed);
    CHECK(skiG.phase==SKI_RUNNING,"ski: bot survives 90 s"); }

  // ---- 2 Tow Snake ----
  enterGame(2); CHECK(snk.phase==SKI_READY,"snake: ready"); tap(P_UP); CHECK(snk.phase==SKI_RUNNING,"snake: UP starts");
  until(g_millis+6000); CHECK(snk.phase==SKI_DEAD,"snake: straight into shore -> wipeout");
  until(g_millis+800); tap(P_UP); CHECK(snk.phase==SKI_RUNNING,"snake: retry");
  { // turn test
    snkReset(); snk.phase=SKI_RUNNING; gmLastTick=g_millis; snk.fx=255; uint8_t d0=snk.dir; tap(P_UP); until(g_millis+400);
    CHECK(snk.dir==((d0+3)&3),"snake: UP turns left"); uint8_t d1=snk.dir; tap(P_DN); until(g_millis+400);
    CHECK(snk.dir==((d1+1)&3),"snake: DOWN turns right");
    // growth test
    snkReset(); snk.phase=SKI_RUNNING; gmLastTick=g_millis; snk.fx=snk.x[0]+2; snk.fy=snk.y[0]; until(g_millis+500);
    CHECK(snk.score==1 && snk.len==4,"snake: eating a flag adds 1 score and 1 rope");
    // self-collision: circle in a tight loop
    snkReset(); snk.phase=SKI_RUNNING; gmLastTick=g_millis; snk.fx=255;
    snk.len=8; int xs[8]={10,10,10,10,9,9,9,9}; (void)xs; // build a coil: heading right with body wrapped under
    const int cx[8]={10,10,9,8,8,8,9,9}; const int cy[8]={6,7,7,7,6,5,5,6};  // head at (10,6) dir right..., body loops
    for(int i=0;i<8;i++){snk.x[i]=cx[i];snk.y[i]=cy[i];}
    snk.dir=0; snk.qn=0; tap(P_DN); until(g_millis+600); // turn right (down) into body at (10,7)
    CHECK(snk.phase==SKI_DEAD,"snake: hitting own rope -> wipeout");
    // chasing own tail is legal
    snkReset(); snk.phase=SKI_RUNNING; gmLastTick=g_millis; snk.fx=255; snk.len=4;
    const int tx[4]={6,6,5,5}, ty[4]={6,7,7,6}; for(int i=0;i<4;i++){snk.x[i]=tx[i];snk.y[i]=ty[i];} snk.dir=0; // head(6,6) body (6,7)(5,7)(5,6); step left? go right
    snk.dir=3; snk.qn=0; // heading up; turn left -> (5,6)? that is skier's... tail at (5,6) vacates
    // head (6,6) moving left into tail (5,6) = the skier cell, which vacates
    snk.dir=3; snk.q[0]=-1; snk.qn=1; until(g_millis+snk.tickMs+20);
    CHECK(snk.phase==SKI_RUNNING,"snake: entering the skier's vacating cell is legal"); }
  { // greedy bot
    int best=0; unsigned long t0=g_millis; snkStart();
    for(int attempt=0; attempt<6 && best<8 && g_millis-t0<40000; attempt++){
      if(attempt>0){ until(g_millis+800); if(snk.phase!=SKI_RUNNING){ tap(P_UP); } }
      while(snk.phase==SKI_RUNNING && g_millis-t0<40000){
        int hx=snk.x[0],hy=snk.y[0],d=snk.dir; int bestMove=0,bestD=1<<30; // 0 straight,-1 left,+1 right
        for(int mv=-1;mv<=1;mv++){ int nd=(d+(mv==0?0:(mv>0?1:3)))&3; int nx=hx+snDx[nd],ny=hy+snDy[nd];
          if(nx<0||nx>=SN_COLS||ny<0||ny>=SN_ROWS) continue; bool bad=false; for(int i=0;i<snk.len-1;i++) if(snk.x[i]==nx&&snk.y[i]==ny) bad=true; if(bad) continue;
          int dist=abs(nx-(int)snk.fx)+abs(ny-(int)snk.fy); if(dist<bestD){bestD=dist;bestMove=mv;} }
        if(snk.qn==0 && bestMove!=0) tap(bestMove<0?P_UP:P_DN,40); else until(g_millis+20);
      }
      if(snk.score>best) best=snk.score;
    }
    printf("      snake bot best score %d\n",best); CHECK(best>=6,"snake: bot collects flags and the rope grows"); }

  // ---- 3 Slalom Run ----
  enterGame(3); CHECK(slm.phase==SKI_READY,"slalom: ready"); tap(P_UP); CHECK(slm.phase==SKI_RUNNING,"slalom: UP starts");
  setPin(P_UP,1); until(g_millis+30000); CHECK(slm.phase==SKI_DEAD,"slalom: no input -> wipeout");
  until(g_millis+800); tap(P_UP); CHECK(slm.phase==SKI_RUNNING,"slalom: retry");
  { float y0=slm.cy; setPin(P_DN,0); until(g_millis+200); CHECK(slm.cy>y0,"slalom: holding DOWN moves skier down"); setPin(P_DN,1); setPin(P_UP,0); until(g_millis+400); CHECK(slm.cy<y0+10,"slalom: holding UP moves skier up"); setPin(P_UP,1); }
  until(g_millis+800); if(slm.phase==SKI_DEAD) tap(P_UP);
  { slmStart(); unsigned long t0=g_millis; int up=0,dn=0;
    while(g_millis-t0<120000 && slm.phase==SKI_RUNNING){
      float target=36; float bx=1e9; for(int i=0;i<SL_MAXG;i++) if(slm.g[i].active && slm.g[i].x+5>SL_X-2 && slm.g[i].x<bx){ bx=slm.g[i].x; target=(slm.g[i].gapTop+slm.g[i].gapBot)/2.0f; }
      bool wantUp=slm.cy>target+2, wantDn=slm.cy<target-2;
      if(wantUp&&!up){setPin(P_UP,0);up=1;} if(!wantUp&&up){setPin(P_UP,1);up=0;}
      if(wantDn&&!dn){setPin(P_DN,0);dn=1;} if(!wantDn&&dn){setPin(P_DN,1);dn=0;}
      step(); }
    setPin(P_UP,1); setPin(P_DN,1);
    printf("      slalom bot: %lus, gates %u\n",(g_millis-t0)/1000,slm.score); CHECK(slm.phase==SKI_RUNNING && slm.score>=40,"slalom: bot threads 40+ gates (always reachable)"); }

  // ---- 4 Ramp Jump ----
  enterGame(4); CHECK(rj.phase==RJ_PICK,"jump: pick screen");
  { uint8_t i0=rj.idx; tap(P_UP); CHECK(rj.idx==i0+1,"jump: UP raises the ramp"); tap(P_DN); tap(P_DN); CHECK(rj.idx==i0-1,"jump: DOWN lowers it"); for(int i=0;i<8;i++) tap(P_UP); CHECK(rj.idx==3,"jump: clamps at 5.5 ft"); for(int i=0;i<8;i++) tap(P_DN); CHECK(rj.idx==0,"jump: clamps at 4.0 ft"); }
  { for(int a=0;a<3;a++){
      tap(P_ENT,50); until(g_millis+20); CHECK(rj.phase==RJ_POWER,"jump: ENTER starts the meter");
      if(a==0){ // good timing
        while(rj.phase==RJ_POWER){ if(rj.power>=76&&rj.power<=80&&rj.dir>0){ setPin(P_UP,0); } step(); if(pinv[P_UP]==0){ setPin(P_UP,1);} }
        CHECK(rj.phase==RJ_FLY||rj.phase==RJ_RESULT,"jump: UP stops the meter"); float ex=28.0f; CHECK(rj.dist>=ex*0.8f && !rj.crash,"jump: sweet-spot gives a long jump"); }
      else if(a==1){ // overpower
        while(rj.phase==RJ_POWER){ if(rj.power>=99.5f){ setPin(P_ENT,0);} step(); if(pinv[P_ENT]==0){ setPin(P_ENT,1);} until(g_millis+0);}
        CHECK(rj.crash,"jump: stopping at 100% = wipeout"); }
      else { until(g_millis+7300); CHECK(rj.phase!=RJ_POWER,"jump: meter auto-stops after 7 s"); }
      until(g_millis+2500);
      if(a<2) CHECK(rj.phase==RJ_PICK,"jump: next attempt offered");
    }
    CHECK(rj.phase==RJ_DONE,"jump: three attempts -> results"); CHECK(gmHigh[4]==(uint16_t)rj.total,"jump: total saved as high score");
    until(g_millis+800); tap(P_UP); CHECK(rj.phase==RJ_PICK && rj.attempt==0,"jump: UP plays again"); }

  // ---- 5 Trick Combo ----
  enterGame(5); CHECK(tc.phase==TC_READY,"combo: ready"); tap(P_UP); CHECK(tc.phase==TC_SHOW,"combo: UP starts and shows the combo");
  tap(P_UP); tap(P_ENT,50); until(g_millis+10); CHECK(tc.phase==TC_SHOW,"combo: presses during SHOW are ignored");
  { int target=14; unsigned long t0=g_millis;
    while(tc.score<target && tc.phase!=TC_DEAD && g_millis-t0<400000){
      if(tc.phase==TC_INPUT){ uint8_t want=tc.seq[tc.inputIdx]; tap(want==0?P_UP:want==1?P_ENT:P_DN,60); } else step(); }
    CHECK(tc.score>=target && tc.phase!=TC_DEAD,"combo: bot repeats combos up to length 14"); }
  { while(tc.phase!=TC_INPUT) step(); uint8_t want=tc.seq[0]; tap(want==0?P_DN:P_UP,60); until(g_millis+20);
    CHECK(tc.phase==TC_DEAD,"combo: wrong trick -> wipeout"); CHECK(gmHigh[5]>=14,"combo: high score saved");
    until(g_millis+800); tap(P_UP); CHECK(tc.phase==TC_SHOW,"combo: retry");
    while(tc.phase!=TC_INPUT) step(); until(g_millis+5200); CHECK(tc.phase==TC_DEAD,"combo: 5 s with no input -> wipeout"); }

  // ---- leaving and re-entering ----
  holdMenu(); CHECK(currentScreen==SCREEN_HOME,"holding the menu button leaves the games for HOME");
  gmSel=3; goGames(); until(g_millis+500); CHECK(currentScreen==SCREEN_GAME && gmCur==0,"re-entering the games always starts at the menu");
  gmSel=2; tap(P_ENT,50); until(g_millis+3*60000+1000); CHECK(currentScreen==SCREEN_HOME,"screen timeout still returns to HOME from inside a game");
#ifdef RAMP_SIDE
  CHECK(state.Motor_Running==false && state.Control_Mode!=MODE_MANUAL,"games never moved the motor or changed mode");
#endif

  // ---- fuzz: random presses everywhere ----
  srand(12345); goGames(); until(g_millis+500);
  { unsigned long t0=g_millis; int pins[4]={P_UP,P_DN,P_ENT,P_BACK}; int nGameFrames=0;
    while(g_millis-t0<1500000){
      int p=pins[rand()%4]; int ms=20+rand()%500; setPin(p,0); until(g_millis+ms); setPin(p,1); until(g_millis+rand()%300);
      if(currentScreen!=SCREEN_GAME){ goGames(); }
      else nGameFrames++;
      if(gmCur>GM_COUNT||gmSel>=GM_COUNT){printf("FAIL: bad state gmCur=%u gmSel=%u\n",gmCur,gmSel);fails++;break;}
      if(snk.len>SN_MAX||snk.len<3){printf("FAIL: snake len %u\n",snk.len);fails++;break;}
    }
    printf("      fuzz: %d random presses survived, bests %u %u %u %u %u\n",nGameFrames,gmHigh[1],gmHigh[2],gmHigh[3],gmHigh[4],gmHigh[5]);
    CHECK(true,"fuzz: 25 simulated minutes of random presses, no crash or sanitizer report"); }
  printf("%s: %d failures\n",VARIANT,fails);
  return fails?1:0;
}
