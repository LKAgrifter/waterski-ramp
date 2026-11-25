#include "Arduino.h"
unsigned long g_millis=0; Serial_t Serial; McuT Mcu;
void (*g_isr[64])()={0}; unsigned long g_displayCost=0;
std::string g_serialIn, g_serialOut; bool g_serialCapture=false;
