#pragma once
#include "Arduino.h"
struct RadioEvents_t{ void(*TxDone)(); void(*TxTimeout)(); void(*RxDone)(uint8_t*,uint16_t,int16_t,int8_t); void(*RxTimeout)(); void(*RxError)(); };
#define MODEM_LORA 1
struct RadioT{ int sent=0; uint8_t last[64]; int lastLen=0; void Init(RadioEvents_t*){} void SetChannel(long){} void SetTxConfig(...){} void Rx(int){} void IrqProcess(){} void Send(uint8_t*p,int n){memcpy(last,p,n);lastLen=n;sent++;} }; extern RadioT Radio;
