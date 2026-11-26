#pragma once
#include "Arduino.h"
#define GEOMETRY_128_64 0
#define ArialMT_Plain_10 0
#define ArialMT_Plain_16 0
#define ArialMT_Plain_24 0
enum TEXT_ALIGNMENT{TEXT_ALIGN_LEFT,TEXT_ALIGN_RIGHT,TEXT_ALIGN_CENTER};
#include <string>
extern std::string g_displayText;   // tests: the text of the last frame shown (strings joined with '|')
struct SSD1306Wire{ std::string cur; SSD1306Wire(int,int,int,int,int,int){} void init(){} void clear(){ cur.clear(); } void display(){ g_displayText = cur; g_millis += g_displayCost; } void displayOn(){} void displayOff(){} void setFont(int){} void setTextAlignment(int){} void drawString(int,int,const String&t){ cur += t.s; cur += '|'; } void drawLine(int,int,int,int){} void drawVerticalLine(int,int,int){} void fillRect(int,int,int,int){} };
