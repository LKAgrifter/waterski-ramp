#pragma once
#include "Arduino.h"
#define GEOMETRY_128_64 0
#define ArialMT_Plain_10 0
#define ArialMT_Plain_16 0
#define ArialMT_Plain_24 0
enum TEXT_ALIGNMENT{TEXT_ALIGN_LEFT,TEXT_ALIGN_RIGHT,TEXT_ALIGN_CENTER};
#include <string>
extern std::string g_displayText;   // tests: the text of the last frame shown (strings joined with '|')
extern std::string g_displayShapes; // tests: the lines and filled rectangles of the last frame ("L x0 y0 x1 y1;R x y w h;")
struct SSD1306Wire{ std::string cur, shp; SSD1306Wire(int,int,int,int,int,int){} void init(){} void clear(){ cur.clear(); shp.clear(); } void display(){ g_displayText = cur; g_displayShapes = shp; g_millis += g_displayCost; } void displayOn(){} void displayOff(){} void setFont(int){} void setTextAlignment(int){} void drawString(int,int,const String&t){ cur += t.s; cur += '|'; }
  void drawLine(int x0,int y0,int x1,int y1){ shp += "L " + std::to_string(x0) + " " + std::to_string(y0) + " " + std::to_string(x1) + " " + std::to_string(y1) + ";"; }
  void drawVerticalLine(int x,int y,int h){ drawLine(x, y, x, y + h - 1); }
  void fillRect(int x,int y,int w,int h){ shp += "R " + std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(w) + " " + std::to_string(h) + ";"; } };
