// Radio encryption (Radio_Security.ino): the crypto against independent reference values (RFC 8439 test vector and
// Python's `cryptography` - see tools/gen_radio_vectors.py), the bind phrase from the web flasher's block, frame
// counters that never repeat across reboots, and the ramp / remote refusing packets with the wrong key, played back
// or from old firmware. Built for the ramp and (with -DREMOTE) for the remote.
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
#include "radio_test.h"
#include "radio_vectors.h"
#include <vector>
static int fails=0;
#define CHECK(c,msg) do{ if(!(c)){printf("FAIL: %s\n",msg);fails++;} else printf("ok:   %s\n",msg);}while(0)
static int txSeen=0;
void run(long ms){ for(long i=0;i<ms;i++){ g_millis++; loop(); if(Radio.sent!=txSeen){ txSeen=Radio.sent; OnTxDone(); } } }

void cryptoChecks(){
  uint8_t h[32];
  rsSha256(nullptr,0,nullptr,0,h); bool ok=!memcmp(h,SHA_EMPTY,32);
  rsSha256((const uint8_t*)"abc",3,nullptr,0,h); ok = ok && !memcmp(h,SHA_ABC,32);
  uint8_t longMsg[200]; for(int i=0;i<200;i++) longMsg[i]=(uint8_t)((i*7+3)&0xff);
  rsSha256(longMsg,77,longMsg+77,123,h); ok = ok && !memcmp(h,SHA_LONG,32);
  CHECK(ok,"SHA-256 matches hashlib (empty, \"abc\", 200 bytes in two pieces)");

  // RFC 8439 2.8.2 with the primitives (the message is longer than one packet, so it's assembled here)
  size_t n=strlen(RFC_PT); std::vector<uint8_t> ct(n), mac;
  for(size_t blk=0; blk*64<n; blk++){ uint8_t ks[64]; rsChachaBlock(RFC_KEY,(uint32_t)(1+blk),RFC_NONCE,ks); for(size_t i=blk*64;i<n && i<blk*64+64;i++) ct[i]=(uint8_t)RFC_PT[i]^ks[i-blk*64]; }
  uint8_t otk[64]; rsChachaBlock(RFC_KEY,0,RFC_NONCE,otk);
  mac.assign(RFC_AAD,RFC_AAD+12); while(mac.size()%16) mac.push_back(0);
  mac.insert(mac.end(),ct.begin(),ct.end()); while(mac.size()%16) mac.push_back(0);
  for(int i=0;i<8;i++) mac.push_back(i==0?12:0); for(int i=0;i<8;i++) mac.push_back(i==0?(uint8_t)n:(i==1?(uint8_t)(n>>8):0));
  uint8_t tag[16]; rsPoly1305(otk,mac.data(),mac.size(),tag);
  CHECK(!memcmp(ct.data(),RFC_CT_TAG,n),"ChaCha20 ciphertext matches the RFC 8439 AEAD test vector");
  CHECK(!memcmp(tag,RFC_CT_TAG+n,16),"Poly1305 tag matches the RFC 8439 AEAD test vector (1ae10b59...)");

  // Key and code from a phrase
  rsSetPhrase("correct horse battery staple");
  CHECK(!memcmp(rsKey,KEY_PHRASE,32) && !strcmp(rsKeyCode,CODE_PHRASE),"key and 4-character code from a phrase match the reference");
  rsSetPhrase("");
  CHECK(!memcmp(rsKey,KEY_UNBOUND,32) && !strcmp(rsKeyCode,CODE_UNBOUND),"unbound key matches the reference");

  // Whole packets, every length 0..40, with and without the echo
  int bad=0, accepted=0, tamperOk=0;
  for(const SealVec &v : SEAL_VECS){
    uint8_t plain[40], out[64], back[40];
    for(int i=0;i<v.len;i++) plain[i]=(uint8_t)((i*31+v.len)&0xff);
    uint8_t L=rsSeal(v.sender,v.fcnt,v.hasEcho,v.echo,plain,v.len,out);
    if(L!=5+v.len+4 || memcmp(out,v.pkt,L)) bad++;
    if(rsOpen(v.pkt,L,v.hasEcho,v.echo,back) && !memcmp(back,plain,v.len)) accepted++;
    for(int i=0;i<L;i++){ uint8_t t[64]; memcpy(t,v.pkt,L); t[i]^=0x01; if(rsOpen(t,L,v.hasEcho,v.echo,back)) tamperOk++; }
    if(v.hasEcho && rsOpen(v.pkt,L,true,v.echo+1,back)) tamperOk++;
  }
  int nv=(int)(sizeof(SEAL_VECS)/sizeof(SEAL_VECS[0]));
  CHECK(bad==0,"rsSeal builds exactly the packets Python's ChaCha20-Poly1305 does (82 packets, 0-40 bytes)");
  CHECK(accepted==nv,"rsOpen accepts and decrypts all of them");
  CHECK(tamperOk==0,"any single changed bit, or a different echo, is refused");
}

void bindChecks(){
  static uint8_t part[8192];
  memset(part,0xFF,sizeof part); memcpy(part+8192-4096,BIND_BLOCK,sizeof BIND_BLOCK);
  g_testPartition.size=8192; g_testPartitionData=part;
  radioSecurityBegin();
  CHECK(rsBindSource==1 && !strcmp(rsKeyCode,CODE_PHRASE) && !memcmp(rsKey,KEY_PHRASE,32),"phrase written by the web flasher is found and used");
  char want[24]; snprintf(want,sizeof want,"Bind: %s",CODE_PHRASE);
  CHECK(!strcmp(bindText(),want),"NETWORK page shows its code");
  CHECK(rsFlashRemoteId==3 && rsFlashWaterMode==1 && rsFlashId==0x12345678u,"the remote ID / type / flash number in the block are read too");
#ifdef REMOTE
  // Remote settings picked in the flasher: saved on the first boot after that flash, then the menus can change them
  applyFlashedSettings();
  CHECK(prefs.getUChar("RemoteId",0)==3 && prefs.getUChar("WaterMode",9)==WATER_MODE_TIMER,"first boot after the web flash: remote ID 3 and TIMER mode saved");
  prefs.putUChar("RemoteId",1); prefs.putUChar("WaterMode",WATER_MODE_BOAT);    // changed on the NETWORK / WATER pages
  radioSecurityBegin(); applyFlashedSettings();
  CHECK(prefs.getUChar("RemoteId",0)==1 && prefs.getUChar("WaterMode",9)==WATER_MODE_BOAT,"next boot: the menu changes are kept (the flash's settings only apply once)");
  { uint8_t keep[sizeof BIND_BLOCK_RAMP]; memcpy(keep,BIND_BLOCK_RAMP,sizeof keep); memcpy(part+8192-4096,keep,sizeof keep); }
  radioSecurityBegin(); applyFlashedSettings();
  CHECK(prefs.getUChar("RemoteId",0)==1 && prefs.getUChar("WaterMode",9)==WATER_MODE_BOAT,"a new flash with no remote settings (the ramp's kind) leaves them alone");
  memcpy(part+8192-4096,BIND_BLOCK,sizeof BIND_BLOCK);
  radioSecurityBegin(); applyFlashedSettings();
  CHECK(prefs.getUChar("RemoteId",0)==3 && prefs.getUChar("WaterMode",9)==WATER_MODE_TIMER,"flashed again with settings: applied again");
#endif
  part[8192-4096+12]^=0x20; radioSecurityBegin();
  CHECK(rsBindSource==0 && !strcmp(bindText(),"Bind: NONE (open)") && !memcmp(rsKey,KEY_UNBOUND,32) && rsFlashId==0,"damaged block (CRC wrong): unbound, no remote settings");
  g_testPartitionData=nullptr; radioSecurityBegin();
  CHECK(rsBindSource==0,"no block: unbound");

  // Frame counters never repeat, also across reboots
  uint32_t last=0; bool first=true, repeat=false;
  for(int boot=0; boot<3; boot++){
    radioSecurityBegin();
    for(int i=0;i<(boot==1?5000:20);i++){ uint32_t f=rsNextFcnt(); if(!first && (int32_t)(f-last)<=0) repeat=true; last=f; first=false; }
  }
  CHECK(!repeat,"frame counters keep going up across 3 reboots (one boot sends 5000, more than a reserved block)");
}

int main(){
  for(int i=0;i<64;i++) pinv[i]=1;
#ifndef REMOTE
  pinv[34]=0;
#endif
  setup(); run(3000);
  cryptoChecks();
  bindChecks();
  radioSecurityBegin();   // back to unbound, like the test helpers
#ifndef REMOTE
  // ---- RAMP ----
  run(600);
  TelemetryPacket t; CHECK(sentTelemetry(t) && Radio.lastLen==18,"the ramp's status packet is 18 bytes and decrypts");
  CHECK(t.rampHeightIndex==0 && t.desiredHeightIndex==0 && t.limitMask==1 && !t.waterRelay,"...and says: at 4 ft, 4 ft sensor on, water off");

  CommandPacket c; c.remoteId=1; c.seq=7; c.targetHeightIndex=-1; c.waterCommand=1; c.remoteControlMode=0; c.bypassCommand=-1; c.txCounter=0;
  rxCommand(c); run(20);
  CHECK(state.Water_Relay && sentTelemetry(t) && t.ackRemoteId==1 && t.ackSeq==7 && t.waterRelay,"a correctly signed command is obeyed and ACKed");
  uint8_t rec[32]; int recLen=0;
  { uint8_t p[5]; testPackCommand(c,p); recLen=rsSeal(RS_SENDER_REMOTE+1,g_testRemoteFcnt[1]-1,true,lastTelemetryFcnt-1,p,5,rec); }

  waterControl(false, SOURCE_LOCAL); run(20);
  uint32_t replay0=loraStats.rxReplay; OnRxDone(rec,recLen,-60,7); run(20);
  CHECK(!state.Water_Relay && loraStats.rxReplay==replay0+1,"the same packet played back: refused (counted as Replayed)");

  uint32_t bad0=loraStats.rxBadKey;
  { uint8_t p[5]; c.seq=8; testPackCommand(c,p); uint8_t air[16]; uint8_t n=rsSeal(RS_SENDER_REMOTE+1,g_testRemoteFcnt[1]++,true,lastTelemetryFcnt-COMMAND_ECHO_WINDOW,p,5,air); OnRxDone(air,n,-60,7); run(20); }
  CHECK(!state.Water_Relay && loraStats.rxBadKey==bad0+1,"signed with a ramp counter from more than 16 status packets ago: refused");
  { rsSetPhrase("someone else's phrase"); uint8_t p[5]; c.seq=9; testPackCommand(c,p); uint8_t air[16]; uint8_t n=rsSeal(RS_SENDER_REMOTE+1,g_testRemoteFcnt[1]++,true,lastTelemetryFcnt,p,5,air); rsSetPhrase(""); OnRxDone(air,n,-60,7); run(20); }
  CHECK(!state.Water_Relay && loraStats.rxBadKey==bad0+2,"a remote with a different bind phrase: refused (Bad key)");
  { uint8_t old[8]={0xB2,1,10,0xFF,1,0,0xFF,0}; OnRxDone(old,8,-60,7); run(20); }
  CHECK(!state.Water_Relay && loraStats.rxOldFw==1,"an unencrypted packet from old firmware: refused, counted as old firmware");

  // A command recorded before the ramp rebooted is refused afterwards (its echo is no longer recent)
  { uint8_t p[5]; c.seq=11; testPackCommand(c,p); recLen=rsSeal(RS_SENDER_REMOTE+2,g_testRemoteFcnt[2]++,true,lastTelemetryFcnt,p,5,rec); }
  radioSecurityBegin(); remoteHaveFcnt[1]=false; telemetrySent=false; run(600);   // "reboot": new counter block, RAM forgotten
  bad0=loraStats.rxBadKey; OnRxDone(rec,recLen,-60,7); run(20);
  CHECK(!state.Water_Relay && loraStats.rxBadKey==bad0+1,"a command recorded before a ramp reboot, played back after it: refused");
  c.remoteId=2; c.seq=12; rxCommand(c); run(20);
  CHECK(state.Water_Relay,"(a fresh command from that remote still works)");
  buildNetworkLines(); bool hasBind=false; for(int i=0;i<listLineCount;i++) if(strstr(listLines[i],"Bind: NONE")) hasBind=true;
  CHECK(hasBind && listLineCount<=LIST_MAX_LINES,"NETWORK page shows the bind status");
#else
  // ---- REMOTE ----
  TelemetryPacket t; memset(&t,0,sizeof t); t.packetId=0xA1; t.rampHeightIndex=2; t.desiredHeightIndex=3; t.waterRelay=1; t.waterRemainMin=95; t.flags=0x10; t.limitMask=4; t.faultCount=3;
  for(int i=0;i<8;i++){ rxTelemetry(t); run(250); }
  CHECK(state.Remote_Connected && state.Ramp_Height==5.0f && state.Desired_Height==5.5f && state.Water_Relay && state.Water_RemainMin==95 &&
        state.Remote_Manual_Off && diag.faultCount==3 && diag.limitMask==4,"a status packet from the ramp decrypts and every field comes through");
  for(int i=0;i<8 && currentScreen!=SCREEN_SET_HEIGHT;i++){ pinv[38]=0; run(700); pinv[38]=1; run(60); for(int k=0;k<3;k++){ rxTelemetry(t); run(250);} }
  int before=Radio.sent; adc=2610; run(150); adc=3200;
  CommandPacket c; bool got=false;
  for(int i=0;i<12 && !got;i++){ rxTelemetry(t); run(250); if(Radio.sent!=before) got=sentCommand(c); }
  CHECK(got && c.remoteId==REMOTE_ID && c.targetHeightIndex==1 && Radio.lastLen==14,"a height pick goes out as a 14-byte packet signed with the last status packet, and decrypts");

  // Played back / wrong key / old firmware
  uint8_t air[32]; uint8_t p[9]; testPackTelemetry(t,p); uint8_t n=rsSeal(RS_SENDER_RAMP,g_testRampFcnt-3,false,0,p,9,air);
  uint32_t r0=loraStats.rxReplay; OnRxDone(air,n,-60,7); CHECK(loraStats.rxReplay==r0+1,"an older status packet played back: refused");
  rsSetPhrase("someone else's phrase"); n=rsSeal(RS_SENDER_RAMP,g_testRampFcnt++,false,0,p,9,air); rsSetPhrase("");
  uint32_t b0=loraStats.rxBadKey; OnRxDone(air,n,-60,7); CHECK(loraStats.rxBadKey==b0+1,"a ramp with a different bind phrase: refused (Bad key)");
  uint8_t old[19]={0xA1}; OnRxDone(old,19,-60,7); CHECK(loraStats.rxOldFw==1,"an unencrypted packet from old firmware: counted as old firmware");

  // A ramp that was erased and reflashed starts its counter again: taken once the link has dropped
  run(3000); CHECK(!state.Remote_Connected,"(link dropped)");
  g_testRampFcnt=10; rxTelemetry(t); run(300);
  CHECK(state.Remote_Connected,"after the link drops, a ramp whose counter started again (reflashed) is heard");
  buildNetworkLines(); bool hasBind=false; for(int i=0;i<listLineCount;i++) if(strstr(listLines[i],"Bind: NONE")) hasBind=true;
  CHECK(hasBind && listLineCount<=LIST_MAX_LINES,"NETWORK page shows the bind status");
#endif
  printf("%s (%d failures)\n",fails?"SOME FAILED":"ALL PASSED",fails); return fails;
}
