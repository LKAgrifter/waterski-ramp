void updateLEDs()
{
  // Adjustable strobe cycle duration in milliseconds (default is 1000ms = 1 second)
  static unsigned long strobeCycleMs = 1000; 

  // Helper lambda to turn off all LEDs
  auto allLEDsOff = []() {
    digitalWrite(LED_4, LOW);
    digitalWrite(LED_1, LOW);
    digitalWrite(LED_2, LOW);
    digitalWrite(LED_3, LOW);
  };

  if (state.Motor_Running)
  {
    // Ramp is moving: strobe through heights sequentially
    unsigned long timeInCycle = millis() % strobeCycleMs;
    uint8_t step = (timeInCycle * 4) / strobeCycleMs;
    if (step > 3) step = 3;

    allLEDsOff();

    if (state.Motor_Direction_Up)
    {
      // Moving Up: 4.0 -> 4.5 -> 5.0 -> 5.5 (LED_4 -> LED_1 -> LED_2 -> LED_3)
      const uint8_t upSequence[4] = {LED_4, LED_1, LED_2, LED_3};
      digitalWrite(upSequence[step], HIGH);
    }
    else
    {
      // Moving Down: 5.5 -> 5.0 -> 4.5 -> 4.0 (LED_3 -> LED_2 -> LED_1 -> LED_4)
      const uint8_t downSequence[4] = {LED_3, LED_2, LED_1, LED_4};
      digitalWrite(downSequence[step], HIGH);
    }
  }
  else
  {
    // Ramp is stationary: turn on the static LED for the current height
    allLEDsOff();

    if (state.Ramp_Height == 4.0f)
    {
      digitalWrite(LED_4, HIGH);
    }
    else if (state.Ramp_Height == 4.5f)
    {
      digitalWrite(LED_1, HIGH);
    }
    else if (state.Ramp_Height == 5.0f)
    {
      digitalWrite(LED_2, HIGH);
    }
    else if (state.Ramp_Height == 5.5f)
    {
      digitalWrite(LED_3, HIGH);
    }
  }
}

