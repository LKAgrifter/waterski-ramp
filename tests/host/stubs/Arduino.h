#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <string>
#include <math.h>
#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
extern unsigned long g_millis;
inline unsigned long millis(){return g_millis;}
inline void delay(unsigned long){}
void pinMode(uint8_t,uint8_t); void digitalWrite(uint8_t,uint8_t); int digitalRead(uint8_t); int analogRead(uint8_t);
inline long random(long a,long b){return a + (b>a ? rand()%(b-a):0);} inline long random(long b){return random(0,b);}
struct String{ std::string s; String(){} String(const char*c):s(c){} String(int v):s(std::to_string(v)){} String(unsigned v):s(std::to_string(v)){} String(unsigned long v):s(std::to_string(v)){} String(float f,int d){char b[32];snprintf(b,32,"%.*f",d,f);s=b;} String operator+(const String&o)const{String r;r.s=s+o.s;return r;} const char* c_str()const{return s.c_str();} bool operator==(const char*c)const{return s==c;} };
inline String operator+(const char*a,const String&b){return String(a)+b;}
// Serial: tests can queue input in g_serialIn and, with g_serialCapture on, read everything printed from g_serialOut
extern std::string g_serialIn, g_serialOut; extern bool g_serialCapture;
class Serial_t{public: void begin(int){}
  void println(const char*t){ if(g_serialCapture){ g_serialOut+=t; g_serialOut+=(char)10; } }
  int printf(const char*f,...){ if(!g_serialCapture) return 0; char b[512]; va_list a; va_start(a,f); int n=vsnprintf(b,sizeof b,f,a); va_end(a); g_serialOut+=b; return n; }
  int available(){ return (int)g_serialIn.size(); }
  int read(){ if(g_serialIn.empty()) return -1; int c=(unsigned char)g_serialIn[0]; g_serialIn.erase(0,1); return c; } }; extern Serial_t Serial;
#define Vext 36
#define SDA_OLED 17
#define SCL_OLED 18
#define RST_OLED 21
#define HELTEC_BOARD 0
#define SLOW_CLK_TPYE 0
struct McuT{void begin(int,int){}}; extern McuT Mcu;

#define FALLING 2
#define IRAM_ATTR
extern void (*g_isr[64])();
inline void attachInterrupt(uint8_t p, void(*f)(), int){ g_isr[p]=f; }
extern unsigned long g_displayCost;
