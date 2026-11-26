// Boot screen animation: a skier on a tow rope crosses the water, rides up the jump ramp, flies, and lands with a
// splash. One jump: the boot screen ends when the skier has gone off the right edge (bootAnimationDone). Drawn under
// the boot screen's two text lines (drawBootScreen in the OLED tab).
// This file is an identical copy in both sketches (like Games.ino): edit one, copy it to the other.
// Only lines and filled rectangles are used, the shapes the simulator's screen draws too.

const int   BA_WATER_Y     = 61;      // Water line (screen y)
const int   BA_RAMP_FOOT_X = 26;      // Where the ramp leaves the water ...
const int   BA_RAMP_LIP_X  = 50;      // ... and its top edge, where the skier takes off
const int   BA_RAMP_LIP_Y  = 50;
const int   BA_LAND_X      = 98;      // Where the skier lands
const int   BA_START_X     = -14;     // The skier starts off the left edge of the screen ...
const float BA_SPEED       = 62.0f;   // ... and crosses it at this speed (pixels per second)
const float BA_POP         = 63.0f;   // Upward speed at take-off (pixels per second)
const float BA_SPLASH_S    = 0.45f;   // How long the landing splash lasts (seconds)
const int   BA_END_X       = 137;     // The skier (and the spray behind the skis) is off the right edge from here
// How long the jump takes, from coming in at the left edge to gone off the right edge (~2.4 s)
const unsigned long BA_DURATION_MS = (unsigned long)((BA_END_X - BA_START_X) * 1000.0f / BA_SPEED);

// True once the jump is over (ms after the animation started): the boot screen then hands over to HOME
bool bootAnimationDone(unsigned long ms)
{
  return ms >= BA_DURATION_MS;
}

// Draw one line of the skier: (x0, y0) to (x1, y1) in the skier's own pixels (x forward, y up from the feet),
// turned by the skis' angle (c = cos, s = sin) and placed with the feet at (fx, fy) on the screen.
void baLine(float fx, float fy, float c, float s, float x0, float y0, float x1, float y1)
{
  display.drawLine((int)lroundf(fx + x0 * c - y0 * s), (int)lroundf(fy - (x0 * s + y0 * c)),
                   (int)lroundf(fx + x1 * c - y1 * s), (int)lroundf(fy - (x1 * s + y1 * c)));
}

// The skier, crouched on the skis with the handle in both hands and the rope leading off to the boat.
// angle: radians, positive = ski tips up.
void baDrawSkier(float fx, float fy, float angle)
{
  float c = cosf(angle), s = sinf(angle);
  baLine(fx, fy, c, s, -6, 0, 5, 0);     // Skis ...
  baLine(fx, fy, c, s, 5, 0, 7, 1);      // ... and their tips
  baLine(fx, fy, c, s, 0, 1, 2, 4);      // Shin
  baLine(fx, fy, c, s, 2, 4, -1, 7);     // Thigh
  baLine(fx, fy, c, s, -1, 7, 1, 11);    // Body
  baLine(fx, fy, c, s, 1, 10, 5, 8);     // Arms, to the handle
  float hx = fx + 2 * c - 13 * s, hy = fy - (2 * s + 13 * c);   // Head
  display.fillRect((int)lroundf(hx) - 1, (int)lroundf(hy) - 1, 3, 3);

  int px = (int)lroundf(fx + 5 * c - 8 * s), py = (int)lroundf(fy - (5 * s + 8 * c));   // Handle ...
  display.drawLine(px, py - 1, px, py + 1);
  display.drawLine(px + 1, py, px + 12, py + 1);                 // ... and the start of the rope, towards the boat
}

// The ramp's surface height at x (between its foot and lip)
float baRampY(float x)
{
  return BA_WATER_Y - (x - BA_RAMP_FOOT_X) * (float)(BA_WATER_Y - BA_RAMP_LIP_Y) / (BA_RAMP_LIP_X - BA_RAMP_FOOT_X);
}

// One frame of the animation, ms after it started
void drawBootAnimation(unsigned long ms)
{
  float t = ms / 1000.0f;   // Seconds into the jump

  // Water: short ripples, drifting left past the skier
  int drift = (int)(ms / 60) % 8;
  for (int x = -drift; x < 128; x += 8)
  {
    display.drawLine(x < 0 ? 0 : x, BA_WATER_Y, x + 3, BA_WATER_Y);
    display.drawLine(x + 4, BA_WATER_Y + 1, x + 7, BA_WATER_Y + 1);
  }

  // The ramp: its sloped surface, the upright front edge and two struts underneath
  display.drawLine(BA_RAMP_FOOT_X, BA_WATER_Y, BA_RAMP_LIP_X, BA_RAMP_LIP_Y);
  display.drawLine(BA_RAMP_LIP_X, BA_RAMP_LIP_Y, BA_RAMP_LIP_X, BA_WATER_Y);
  for (int x = BA_RAMP_LIP_X - 8; x > BA_RAMP_FOOT_X + 4; x -= 8)
    display.drawLine(x, (int)lroundf(baRampY(x)) + 1, x, BA_WATER_Y);

  // The skier. Flight: up at BA_POP from the lip, with the gravity that brings them down at BA_LAND_X.
  const float rampAngle = atanf((float)(BA_WATER_Y - BA_RAMP_LIP_Y) / (BA_RAMP_LIP_X - BA_RAMP_FOOT_X));
  const float flightS = (BA_LAND_X - BA_RAMP_LIP_X) / BA_SPEED;
  const float gravity = 2.0f * ((BA_WATER_Y - BA_RAMP_LIP_Y) + BA_POP * flightS) / (flightS * flightS);

  float x = BA_START_X + BA_SPEED * t;
  if (x < BA_RAMP_FOOT_X)                        // On the water
  {
    baDrawSkier(x, BA_WATER_Y - 1, 0);
    if (((ms / 80) % 2) == 0)                    // Spray off the tails of the skis
      display.fillRect((int)x - 9, BA_WATER_Y - 2, 2, 1);
  }
  else if (x <= BA_RAMP_LIP_X)                   // Up the ramp
    baDrawSkier(x, baRampY(x) - 1, rampAngle);
  else if (x < BA_LAND_X)                        // In the air: tips go from the ramp's angle to slightly down
  {
    float f = (x - BA_RAMP_LIP_X) / (BA_LAND_X - BA_RAMP_LIP_X);
    float ft = f * flightS;
    float y = BA_RAMP_LIP_Y - BA_POP * ft + gravity * ft * ft / 2;
    baDrawSkier(x, y - 1, rampAngle + (-0.2f - rampAngle) * f);
  }
  else                                           // Landed: away to the right, through a splash
  {
    baDrawSkier(x, BA_WATER_Y - 1, 0);
    float since = (x - BA_LAND_X) / BA_SPEED;
    if (since < BA_SPLASH_S)
    {
      // Foam where they touched down, and drops thrown up in a fan (mostly backwards), falling again
      display.drawLine(BA_LAND_X - 10, BA_WATER_Y - 1, BA_LAND_X + 2, BA_WATER_Y - 1);
      for (int i = 0; i < 9; i++)
      {
        float vx = -60.0f + 11.0f * i, vy = 60.0f + 10.0f * (i % 3);
        int dx = (int)lroundf(BA_LAND_X + vx * since);
        int dy = (int)lroundf(BA_WATER_Y - 2 - vy * since + 260.0f * since * since);
        if (dy < BA_WATER_Y)
          display.fillRect(dx, dy, 1 + (i % 2), 1 + (i % 2));
      }
    }
  }
}
