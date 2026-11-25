// ===========================================================================
// WATER SKI ARCADE - a game menu with five waterski-themed games for the OLED.
// Works UNCHANGED on both the ramp controller and the remote (same display / button names on both).
//
//   1. Ski Dash     Chrome-dino style: jump the buoys.                       UP = jump, DOWN = fall faster
//   2. Tow Snake    Snake: you're the boat, the rope is the body, a skier    UP = turn left, DOWN = turn right
//                   is on the end. Grab slalom flags to lengthen the rope.
//   3. Slalom Run   Flappy-style: steer the skier through the buoy gates.    hold UP / DOWN to steer
//   4. Ramp Jump    Pick a ramp height (4.0 - 5.5 ft), then stop the power   UP/DOWN pick, ENTER go, UP/ENTER stop
//                   meter in the sweet spot. Three jumps, best total wins.
//   5. Trick Combo  Simon-says: watch the trick combo, then repeat it.       UP = jump, ENTER = spin, DOWN = duck
//
//   In the menu: UP / DOWN pick a game, ENTER (tap) plays it.
//   In any game: BACK (tap) returns to this menu.   HOLD ENTER (as on every other page) leaves the games entirely.
//
// Self-contained: it only reads the four AUX buttons and draws on `display`. It never touches the motor, water, radio,
// preferences or any other state, and every name it adds starts with gm / ski / snk / slm / rj / tc so nothing collides.
//
// ---------------------------------------------------------------------------
// HOOK-UP: already done. The Ramp_Control and Ramp_Remote sketches include the SCREEN_GAME page and call
// skiGameFrame() from drawScreen(). Keep this file as a tab in BOTH sketches.
// GETTING IN: the games are a hidden page - on HOME, hold UP + DOWN together for 2 s. Hold MENU (1 s) to leave.
// If you have an old Ski_Game tab, DELETE it (this file replaces it - both define the same names).
//
// NOTES
//  * The height (ladder) buttons do nothing here - they only work on the SET HEIGHT page.
//  * Drawing a full frame blocks the main loop for ~20 ms, so the games make the loop a little slower. The motor
//    failsafes and radio still run between frames, and the page times out to HOME after 1 min like any other.
//  * Button presses are caught by hardware interrupts and latched, so a tap registers even while the screen is being
//    redrawn or if the games are only being called a few times a second.
//  * Each game's best score is kept in RAM (cleared on reboot).
//  * GM_DEBUG (below) prints "c<calls/s> d<draw ms> e<presses seen>" in the menu's title bar. c should be in the hundreds
//    (about 12 would mean the games are being throttled by the screen refresh). Set GM_DEBUG to false when you are done.
// ===========================================================================

constexpr bool    GM_DEBUG = true;
constexpr uint8_t GM_COUNT = 5;

constexpr uint8_t GM_B_UP = 0;
constexpr uint8_t GM_B_DOWN = 1;
constexpr uint8_t GM_B_BACK = 2;
constexpr uint8_t GM_B_ENTER = 3;

const char * const gmNames[GM_COUNT] = { "Ski Dash", "Tow Snake", "Slalom Run", "Ramp Jump", "Trick Combo" };

uint8_t       gmCur = 0;               // 0 = the menu, 1..GM_COUNT = a game
uint8_t       gmSel = 0;               // Menu selection
uint16_t      gmHigh[GM_COUNT + 1];    // Best score per game (index = game number)
unsigned long gmLastCall = 0;          // millis() of the last gamesFrame() call (a long gap means we were on another page)
unsigned long gmLastTick = 0;
unsigned long gmSecStart = 0;
uint16_t      gmCallsThisSec = 0;
uint16_t      gmCallsLastSec = 0;
uint16_t      gmDrawMs = 0;

// ---------------------------------------------------------------------------
// Button interrupt latches. The ISRs only set flags; they never touch the display or any other state.
// ---------------------------------------------------------------------------
volatile bool     gmLatched[4];
volatile uint32_t gmLatchedAt[4];
volatile uint32_t gmLastEdge[4];
volatile uint16_t gmEdgeCount[4];
bool              gmIsrAttached = false;

bool          gmEdge[4];        // A fresh press this call (true for exactly ONE gamesFrame() call)
unsigned long gmEdgeAt[4];      // When it happened
unsigned long gmLastPress[4];

void IRAM_ATTR gmIsrUp()
{
  uint32_t t = millis();
  if ((t - gmLastEdge[0]) > 40) { gmLatched[0] = true; gmLatchedAt[0] = t; gmEdgeCount[0]++; }
  gmLastEdge[0] = t;
}

void IRAM_ATTR gmIsrDown()
{
  uint32_t t = millis();
  if ((t - gmLastEdge[1]) > 40) { gmLatched[1] = true; gmLatchedAt[1] = t; gmEdgeCount[1]++; }
  gmLastEdge[1] = t;
}

void IRAM_ATTR gmIsrBack()
{
  uint32_t t = millis();
  if ((t - gmLastEdge[2]) > 40) { gmLatched[2] = true; gmLatchedAt[2] = t; gmEdgeCount[2]++; }
  gmLastEdge[2] = t;
}

void IRAM_ATTR gmIsrEnter()
{
  uint32_t t = millis();
  if ((t - gmLastEdge[3]) > 40) { gmLatched[3] = true; gmLatchedAt[3] = t; gmEdgeCount[3]++; }
  gmLastEdge[3] = t;
}

void gmReadInput()
{
  unsigned long now = millis();
  bool polled[4] = { UpButton.pressedEvent, DownButton.pressedEvent, BackButton.pressedEvent, EnterButton.pressedEvent };

  for (uint8_t i = 0; i < 4; i++)
  {
    bool pressed = false;
    unsigned long at = now;

    if (gmLatched[i])                 // The interrupt latch (never misses)...
    {
      gmLatched[i] = false;
      pressed = true;
      at = gmLatchedAt[i];
    }
    else if (polled[i])               // ...or the polled button as a backup
    {
      pressed = true;
    }

    if (pressed)
    {
      if (gmLastPress[i] != 0 && (long)(at - gmLastPress[i]) < 120) pressed = false;   // Same tap seen by both paths
      else gmLastPress[i] = at;
    }

    gmEdge[i] = pressed;
    gmEdgeAt[i] = at;
  }
}

// How many fixed-length steps are due. If we are called slowly we catch up (up to 4), so games never run in slow motion.
uint8_t gmSteps(unsigned long tickMs)
{
  unsigned long now = millis();
  unsigned long dt = now - gmLastTick;
  if (dt < tickMs) return 0;

  unsigned long n = dt / tickMs;
  if (n > 4) n = 4;
  gmLastTick += n * tickMs;
  if ((now - gmLastTick) > 4 * tickMs) gmLastTick = now;
  return (uint8_t)n;
}

void gmBegin()
{
  display.clear();
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
}

void gmShow()
{
  unsigned long t0 = millis();
  display.display();
  gmDrawMs = (uint16_t)(millis() - t0);
}

// Title bar: text on the left, text on the right, a line underneath
void gmHeader(const char *left, const char *right)
{
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(0, 0, left);
  display.setTextAlignment(TEXT_ALIGN_RIGHT);
  display.drawString(127, 0, right);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawLine(0, 12, 127, 12);
}

// Header for the games that count points: "<label> 12"  /  "HI 40"
void gmScoreHeader(const char *label, unsigned score, unsigned high)
{
  char a[24];
  char b[16];
  snprintf(a, sizeof(a), "%s %u", label, score);
  snprintf(b, sizeof(b), "HI %u", high);
  gmHeader(a, b);
}

// The skier (feet at fy, left edge x). Shared by the games.
void gmSkier(int x, int fy, bool airborne)
{
  const int hip = airborne ? 8 : 6;               // Standing taller in the air, crouched on the water

  display.drawLine(x - 3, fy, x + 8, fy);         // Skis
  display.drawLine(x + 8, fy, x + 10, fy - 1);    // Curled tips
  display.drawLine(x + 2, fy - 1, x + 4, fy - hip);       // Legs
  display.drawLine(x + 6, fy - 1, x + 4, fy - hip);
  display.drawLine(x + 4, fy - hip, x + 5, fy - hip - 5); // Torso, leaning forward
  display.fillRect(x + 4, fy - hip - 9, 4, 4);            // Head
  display.drawLine(x + 5, fy - hip - 4, x + 9, fy - hip - 4);   // Arm
}

// Wiped-out skier lying in the water at (x, y = water line)
void gmWipeout(int x, int y)
{
  display.drawLine(x, y - 3, x + 10, y - 3);        // Body floating flat
  display.fillRect(x + 10, y - 6, 4, 4);            // Head
  display.drawLine(x - 2, y - 9, x + 2, y - 2);     // Skis sticking up, crossed
  display.drawLine(x + 2, y - 9, x - 2, y - 2);
  display.fillRect(x + 16, y - 4, 1, 1);            // Splash
  display.fillRect(x + 19, y - 7, 1, 1);
  display.fillRect(x + 22, y - 3, 1, 1);
  display.fillRect(x - 6, y - 5, 1, 1);
}

// ===========================================================================
// GAME 1: SKI DASH  (Chrome-dino style)
// ===========================================================================
constexpr int           SKI_GROUND_Y  = 56;     // The water line: the skis sit here
constexpr int           SKI_X         = 14;     // Skier's left edge
constexpr unsigned long SKI_TICK_MS   = 40;     // One physics step + redraw = 25 fps
constexpr float         SKI_JUMP_V    = 5.3f;   // Jump speed (px per step)
constexpr float         SKI_GRAVITY   = 0.664f; // px per step^2.  Air time is ~16 steps, peak height ~24 px
constexpr float         SKI_AIR_STEPS = 16.0f;  // Used to space the buoys so every one can be jumped
constexpr float         SKI_SPEED_MIN = 2.5f;   // px per step at the start (~62 px/s)
constexpr float         SKI_SPEED_MAX = 5.5f;   // top speed (~137 px/s), reached after ~80 s
constexpr uint8_t       SKI_MAX_OBS   = 3;
constexpr unsigned long SKI_JUMP_WINDOW_MS = 250;   // A tap counts as a jump for this long, so a slightly early tap still works

enum SkiPhase : uint8_t { SKI_READY, SKI_RUNNING, SKI_DEAD };

struct SkiObstacle
{
  float   x;
  uint8_t kind;    // 0 = small buoy, 1 = tall buoy, 2 = two small buoys
  bool    active;
};

struct SkiGameData
{
  SkiPhase      phase;
  unsigned long deadSince;
  unsigned long jumpQueuedAt;
  bool          jumpQueued;
  bool          newBest;
  float         h;              // height above the water
  float         vy;
  float         speed;
  float         distance;
  float         spawnIn;        // px until the next buoy appears
  uint16_t      score;
  uint32_t      frame;
  SkiObstacle   obs[SKI_MAX_OBS];
};

SkiGameData skiG;

int skiObsW(uint8_t kind) { return (kind == 2) ? 22 : 7; }
int skiObsH(uint8_t kind) { return (kind == 1) ? 15 : 9; }

void skiReset()
{
  skiG.phase = SKI_READY;
  skiG.h = 0.0f;
  skiG.vy = 0.0f;
  skiG.speed = SKI_SPEED_MIN;
  skiG.distance = 0.0f;
  skiG.score = 0;
  skiG.spawnIn = 0.0f;         // First buoy appears right away (it takes ~2 s to reach you)
  skiG.jumpQueued = false;
  skiG.newBest = false;
  skiG.frame = 0;
  for (uint8_t i = 0; i < SKI_MAX_OBS; i++) skiG.obs[i].active = false;
}

void skiStart()
{
  skiReset();
  skiG.phase = SKI_RUNNING;
  gmLastTick = millis();
}

void skiSpawn()
{
  uint8_t kind = 0;
  int r = (int)random(0, 100);
  if (skiG.speed >= 3.6f && r < 20) kind = 2;     // Double buoys only once you're fast enough to clear them comfortably
  else if (r < 45)                  kind = 1;     // Tall buoy

  for (uint8_t i = 0; i < SKI_MAX_OBS; i++)
  {
    if (!skiG.obs[i].active)
    {
      skiG.obs[i].active = true;
      skiG.obs[i].kind = kind;
      skiG.obs[i].x = 128.0f;
      break;
    }
  }

  // Gap to the next buoy: its own width + a whole jump's worth of distance + a random extra, so it is ALWAYS beatable
  skiG.spawnIn = (float)skiObsW(kind) + skiG.speed * SKI_AIR_STEPS + (float)random(22, 72);
}

void skiStep()   // One 40 ms physics step
{
  skiG.frame++;

  if (skiG.jumpQueued && (long)(millis() - skiG.jumpQueuedAt) > (long)SKI_JUMP_WINDOW_MS) skiG.jumpQueued = false;

  bool onGround = (skiG.h <= 0.0f);
  if (onGround && skiG.jumpQueued)
  {
    skiG.vy = SKI_JUMP_V;
    skiG.jumpQueued = false;
    onGround = false;
  }

  if (!onGround)
  {
    skiG.h += skiG.vy;
    skiG.vy -= (DownButton.state ? SKI_GRAVITY * 3.0f : SKI_GRAVITY);   // Hold DOWN to drop faster
    if (skiG.h <= 0.0f)
    {
      skiG.h = 0.0f;
      skiG.vy = 0.0f;
    }
  }

  if (skiG.speed < SKI_SPEED_MAX) skiG.speed += 0.0015f;
  skiG.distance += skiG.speed;
  skiG.score = (uint16_t)(skiG.distance / 10.0f);

  skiG.spawnIn -= skiG.speed;
  if (skiG.spawnIn <= 0.0f) skiSpawn();

  for (uint8_t i = 0; i < SKI_MAX_OBS; i++)
  {
    if (!skiG.obs[i].active) continue;

    skiG.obs[i].x -= skiG.speed;
    float w = (float)skiObsW(skiG.obs[i].kind);

    if (skiG.obs[i].x + w < 0.0f)
    {
      skiG.obs[i].active = false;
      continue;
    }

    // Collision. Both boxes are shrunk by 1 px so near-misses feel fair
    bool overlapX = (skiG.obs[i].x + 1.0f < (float)(SKI_X + 7)) && (skiG.obs[i].x + w - 1.0f > (float)(SKI_X + 1));
    bool belowTop = skiG.h < (float)(skiObsH(skiG.obs[i].kind) - 1);

    if (overlapX && belowTop)
    {
      skiG.phase = SKI_DEAD;
      skiG.deadSince = millis();
      skiG.h = 0.0f;
      if (skiG.score > gmHigh[1])
      {
        gmHigh[1] = skiG.score;
        skiG.newBest = true;
      }
    }
  }
}

void skiDrawBuoy(int x, bool tall)
{
  display.fillRect(x, SKI_GROUND_Y - 5, 7, 5);                      // Wide base
  if (tall)
  {
    display.fillRect(x + 1, SKI_GROUND_Y - 11, 5, 6);               // Body
    display.drawLine(x + 3, SKI_GROUND_Y - 15, x + 3, SKI_GROUND_Y - 11);   // Mast
    display.fillRect(x + 4, SKI_GROUND_Y - 15, 3, 2);               // Flag
  }
  else
  {
    display.fillRect(x + 2, SKI_GROUND_Y - 9, 3, 4);                // Top
  }
}

void skiDraw()
{
  gmBegin();
  gmScoreHeader("SCORE", skiG.score, gmHigh[1]);

  // Water: surface line plus two rows of wave dashes that scroll with your speed
  display.drawLine(0, SKI_GROUND_Y + 1, 127, SKI_GROUND_Y + 1);
  for (uint8_t row = 0; row < 2; row++)
  {
    int y = SKI_GROUND_Y + 4 + row * 3;
    int offset = (int)(skiG.distance * (row == 0 ? 1.0f : 0.6f)) % 16;
    for (int x = -offset; x < 128; x += 16) display.drawLine(x, y, x + 5, y);
  }

  for (uint8_t i = 0; i < SKI_MAX_OBS; i++)
  {
    if (!skiG.obs[i].active) continue;
    int x = (int)skiG.obs[i].x;
    uint8_t k = skiG.obs[i].kind;
    skiDrawBuoy(x, k == 1);
    if (k == 2) skiDrawBuoy(x + 15, false);
  }

  if (skiG.phase == SKI_DEAD)
  {
    gmWipeout(SKI_X, SKI_GROUND_Y);
  }
  else
  {
    int fy = SKI_GROUND_Y - (int)skiG.h;
    bool air = skiG.h > 0.0f;
    gmSkier(SKI_X, fy, air);
    if (skiG.phase == SKI_RUNNING) display.drawLine(SKI_X + 9, fy - (air ? 8 : 6) - 4, 127, 22);   // Tow rope to the boat
    if (!air)   // Spray off the skis
    {
      display.fillRect(SKI_X - 6 - (int)(skiG.frame % 3) * 2, fy - 1 - (int)(skiG.frame & 1), 1, 1);
      display.fillRect(SKI_X - 10 - (int)(skiG.frame % 4) * 2, fy - 2 + (int)(skiG.frame & 1), 1, 1);
    }
  }

  if (skiG.phase == SKI_READY)
  {
    display.drawString(24, 16, "SKI DASH");
    display.drawString(18, 28, "Press UP to start");
  }
  else if (skiG.phase == SKI_DEAD)
  {
    display.drawString(38, 15, "WIPEOUT!");
    if (skiG.newBest) display.drawString(34, 27, "NEW BEST!");
    if ((millis() - skiG.deadSince) > 700) display.drawString(30, 39, "UP = retry");
  }

  gmShow();
}

void skiFrame()
{
  if (gmEdge[GM_B_UP])
  {
    if (skiG.phase == SKI_READY)
    {
      skiStart();
    }
    else if (skiG.phase == SKI_DEAD)
    {
      if ((long)(gmEdgeAt[GM_B_UP] - skiG.deadSince) > 700) skiStart();
    }
    else
    {
      skiG.jumpQueued = true;
      skiG.jumpQueuedAt = gmEdgeAt[GM_B_UP];
    }
  }

  uint8_t n = gmSteps(SKI_TICK_MS);
  if (n == 0) return;

  for (uint8_t i = 0; i < n && skiG.phase == SKI_RUNNING; i++) skiStep();
  skiDraw();
}


// ===========================================================================
// GAME 2: TOW SNAKE  (you are the boat, the rope is the body, a skier is on the end)
// ===========================================================================
constexpr int SN_COLS = 31;
constexpr int SN_ROWS = 12;
constexpr int SN_X0   = 2;      // Pixel position of cell (0,0)
constexpr int SN_Y0   = 14;
constexpr int SN_CELL = 4;
constexpr int SN_MAX  = 160;    // Longest rope

struct SnakeData
{
  uint8_t       phase;          // SKI_READY / SKI_RUNNING / SKI_DEAD are reused
  uint8_t       len;
  uint8_t       dir;            // 0 right, 1 down, 2 left, 3 up
  uint8_t       qn;             // Queued turns
  int8_t        q[2];           // -1 = left, +1 = right
  uint8_t       fx, fy;         // Slalom flag (the pickup); fx = 255 when there is none
  uint16_t      score;
  unsigned long tickMs;
  unsigned long deadSince;
  bool          newBest;
  uint8_t       x[SN_MAX];      // Index 0 is the boat, the last one is the skier
  uint8_t       y[SN_MAX];
};

SnakeData snk;

const int8_t snDx[4] = { 1, 0, -1, 0 };
const int8_t snDy[4] = { 0, 1, 0, -1 };

bool snkOccupied(int cx, int cy)
{
  for (uint8_t i = 0; i < snk.len; i++)
  {
    if (snk.x[i] == cx && snk.y[i] == cy) return true;
  }
  return false;
}

void snkPlaceFlag()
{
  for (int tries = 0; tries < 300; tries++)
  {
    int cx = (int)random(0, SN_COLS);
    int cy = (int)random(0, SN_ROWS);
    if (!snkOccupied(cx, cy))
    {
      snk.fx = (uint8_t)cx;
      snk.fy = (uint8_t)cy;
      return;
    }
  }

  for (int cy = 0; cy < SN_ROWS; cy++)      // Crowded board: take the first free cell
  {
    for (int cx = 0; cx < SN_COLS; cx++)
    {
      if (!snkOccupied(cx, cy))
      {
        snk.fx = (uint8_t)cx;
        snk.fy = (uint8_t)cy;
        return;
      }
    }
  }

  snk.fx = 255;   // The whole lake is rope. You win.
}

void snkReset()
{
  snk.phase = SKI_READY;
  snk.len = 3;
  snk.dir = 0;
  snk.qn = 0;
  snk.score = 0;
  snk.tickMs = 150;
  snk.newBest = false;
  snk.x[0] = 10; snk.y[0] = 6;     // Boat
  snk.x[1] = 9;  snk.y[1] = 6;     // Rope
  snk.x[2] = 8;  snk.y[2] = 6;     // Skier
  snkPlaceFlag();
}

void snkStart()
{
  snkReset();
  snk.phase = SKI_RUNNING;
  gmLastTick = millis();
}

void snkDie()
{
  snk.phase = SKI_DEAD;
  snk.deadSince = millis();
  if (snk.score > gmHigh[2])
  {
    gmHigh[2] = snk.score;
    snk.newBest = true;
  }
}

void snkStep()
{
  if (snk.qn > 0)    // Apply the oldest queued turn
  {
    snk.dir = (uint8_t)((snk.dir + (snk.q[0] > 0 ? 1 : 3)) & 3);
    snk.q[0] = snk.q[1];
    snk.qn--;
  }

  int nx = snk.x[0] + snDx[snk.dir];
  int ny = snk.y[0] + snDy[snk.dir];
  bool grow = (nx == snk.fx && ny == snk.fy);

  if (nx < 0 || nx >= SN_COLS || ny < 0 || ny >= SN_ROWS)     // Hit the shore
  {
    snkDie();
    return;
  }

  // Hit your own rope. The skier's cell is about to be vacated, so it is safe to enter unless we are growing.
  uint8_t limit = grow ? snk.len : (uint8_t)(snk.len - 1);
  for (uint8_t i = 0; i < limit; i++)
  {
    if (snk.x[i] == nx && snk.y[i] == ny)
    {
      snkDie();
      return;
    }
  }

  if (grow && snk.len < SN_MAX) snk.len++;
  for (int i = snk.len - 1; i > 0; i--)
  {
    snk.x[i] = snk.x[i - 1];
    snk.y[i] = snk.y[i - 1];
  }
  snk.x[0] = (uint8_t)nx;
  snk.y[0] = (uint8_t)ny;

  if (grow)
  {
    snk.score++;
    snk.tickMs = (snk.score >= 27) ? 70 : (150 - (unsigned long)snk.score * 3);   // Faster as the rope gets longer
    snkPlaceFlag();
  }
}

void snkDraw()
{
  gmBegin();
  gmScoreHeader("FLAGS", snk.score, gmHigh[2]);

  // Shore
  display.drawLine(0, 12, 0, 63);
  display.drawLine(127, 12, 127, 63);
  display.drawLine(0, 63, 127, 63);

  // Slalom flag
  if (snk.fx != 255)
  {
    int px = SN_X0 + snk.fx * SN_CELL;
    int py = SN_Y0 + snk.fy * SN_CELL;
    display.drawLine(px + 1, py, px + 1, py + 3);
    display.fillRect(px + 2, py, 2, 2);
  }

  // Rope: one line through the middle of every cell, boat to skier
  for (uint8_t i = 0; i + 1 < snk.len; i++)
  {
    display.drawLine(SN_X0 + snk.x[i] * SN_CELL + 2, SN_Y0 + snk.y[i] * SN_CELL + 2,
                     SN_X0 + snk.x[i + 1] * SN_CELL + 2, SN_Y0 + snk.y[i + 1] * SN_CELL + 2);
  }

  // Skier on the end
  {
    int px = SN_X0 + snk.x[snk.len - 1] * SN_CELL;
    int py = SN_Y0 + snk.y[snk.len - 1] * SN_CELL;
    display.fillRect(px + 1, py, 2, 2);          // Head
    display.drawLine(px, py + 2, px + 3, py + 2);   // Arms
    display.drawLine(px, py + 3, px + 3, py + 3);   // Skis
  }

  // Boat (a solid block with a bow nub pointing the way it is heading)
  {
    int px = SN_X0 + snk.x[0] * SN_CELL;
    int py = SN_Y0 + snk.y[0] * SN_CELL;
    display.fillRect(px, py, 4, 4);
    if (snk.dir == 0) display.fillRect(px + 4, py + 1, 1, 2);
    if (snk.dir == 1) display.fillRect(px + 1, py + 4, 2, 1);
    if (snk.dir == 2) display.fillRect(px - 1, py + 1, 1, 2);
    if (snk.dir == 3) display.fillRect(px + 1, py - 1, 2, 1);
  }

  if (snk.phase == SKI_READY)
  {
    display.drawString(38, 16, "TOW SNAKE");
    display.drawString(22, 27, "UP/DOWN = turn L/R");
    display.drawString(38, 48, "UP = start");
  }
  else if (snk.phase == SKI_DEAD)
  {
    display.drawString(38, 16, "WIPEOUT!");
    if (snk.newBest) display.drawString(34, 27, "NEW BEST!");
    if ((millis() - snk.deadSince) > 700) display.drawString(34, 48, "UP = retry");
  }

  gmShow();
}

void snkFrame()
{
  if (snk.phase == SKI_READY)
  {
    if (gmEdge[GM_B_UP]) snkStart();
  }
  else if (snk.phase == SKI_DEAD)
  {
    if (gmEdge[GM_B_UP] && (long)(gmEdgeAt[GM_B_UP] - snk.deadSince) > 700) snkStart();
  }
  else
  {
    if (gmEdge[GM_B_UP]   && snk.qn < 2) snk.q[snk.qn++] = -1;   // Turn left
    if (gmEdge[GM_B_DOWN] && snk.qn < 2) snk.q[snk.qn++] = +1;   // Turn right
  }

  uint8_t n = gmSteps(snk.tickMs);
  if (n == 0) return;

  for (uint8_t i = 0; i < n && snk.phase == SKI_RUNNING; i++) snkStep();
  snkDraw();
}


// ===========================================================================
// GAME 3: SLALOM RUN  (steer through the buoy gates)
// ===========================================================================
constexpr int           SL_X       = 18;      // Skier's left edge
constexpr int           SL_MAXG    = 3;
constexpr float         SL_STEER   = 2.5f;    // px per step while UP / DOWN is held
constexpr float         SL_SPACING = 54.0f;   // px between gates

struct SlGate
{
  float   x;
  int16_t gapTop;
  int16_t gapBot;
  bool    active;
  bool    scored;
};

struct SlalomData
{
  uint8_t       phase;
  unsigned long deadSince;
  float         cy;           // Skier's vertical centre
  float         speed;
  float         distance;
  float         spawnIn;
  int           lastCenter;
  uint16_t      score;
  bool          newBest;
  SlGate        g[SL_MAXG];
};

SlalomData slm;

void slmReset()
{
  slm.phase = SKI_READY;
  slm.cy = 36.0f;
  slm.speed = 2.4f;
  slm.distance = 0.0f;
  slm.spawnIn = 40.0f;
  slm.lastCenter = 36;
  slm.score = 0;
  slm.newBest = false;
  for (uint8_t i = 0; i < SL_MAXG; i++) slm.g[i].active = false;
}

void slmStart()
{
  slmReset();
  slm.phase = SKI_RUNNING;
  gmLastTick = millis();
}

int slmGapH() { int h = 26 - (int)(slm.score / 2); return (h < 18) ? 18 : h; }   // The gap tightens as you score

void slmSpawn()
{
  int gh = slmGapH();
  int lo = 13 + gh / 2 + 2;
  int hi = 62 - gh / 2 - 2;
  // Zig-zag around the middle (8..18 px per gate, so it is always reachable) so just sitting still never works
  int dir = (slm.lastCenter > 36) ? -1 : 1;
  if (random(0, 4) == 0) dir = -dir;
  int center = slm.lastCenter + dir * (int)random(8, 19);
  if (center < lo) center = lo;
  if (center > hi) center = hi;
  slm.lastCenter = center;

  for (uint8_t i = 0; i < SL_MAXG; i++)
  {
    if (!slm.g[i].active)
    {
      slm.g[i].active = true;
      slm.g[i].scored = false;
      slm.g[i].x = 128.0f;
      slm.g[i].gapTop = (int16_t)(center - gh / 2);
      slm.g[i].gapBot = (int16_t)(center - gh / 2 + gh);
      break;
    }
  }
  slm.spawnIn = SL_SPACING;
}

void slmStep()
{
  bool up = UpButton.state;
  bool dn = DownButton.state;
  if (up && !dn) slm.cy -= SL_STEER;
  else if (dn && !up) slm.cy += SL_STEER;
  if (slm.cy < 19.0f) slm.cy = 19.0f;
  if (slm.cy > 57.0f) slm.cy = 57.0f;

  float s = 2.4f + (float)(slm.score > 40 ? 40 : slm.score) * 0.04f;   // 2.4 -> 4.0 px per step
  slm.speed = s;
  slm.distance += s;

  slm.spawnIn -= s;
  if (slm.spawnIn <= 0.0f) slmSpawn();

  for (uint8_t i = 0; i < SL_MAXG; i++)
  {
    if (!slm.g[i].active) continue;

    slm.g[i].x -= s;
    if (slm.g[i].x + 5.0f < 0.0f)
    {
      slm.g[i].active = false;
      continue;
    }

    // Collision against the two buoy lines (hitbox shrunk by 1 px)
    bool overlapX = (slm.g[i].x + 1.0f < (float)(SL_X + 8)) && (slm.g[i].x + 4.0f > (float)SL_X);
    float bodyTop = slm.cy - 3.0f;
    float bodyBot = slm.cy + 3.0f;
    if (overlapX && (bodyTop < (float)slm.g[i].gapTop || bodyBot > (float)slm.g[i].gapBot))
    {
      slm.phase = SKI_DEAD;
      slm.deadSince = millis();
      if (slm.score > gmHigh[3])
      {
        gmHigh[3] = slm.score;
        slm.newBest = true;
      }
      return;
    }

    if (!slm.g[i].scored && slm.g[i].x + 5.0f < (float)SL_X)   // Cleared it
    {
      slm.g[i].scored = true;
      slm.score++;
    }
  }
}

void slmDraw()
{
  gmBegin();
  gmScoreHeader("GATES", slm.score, gmHigh[3]);

  // Scrolling water texture
  for (uint8_t row = 0; row < 3; row++)
  {
    int y = 22 + row * 16;
    int offset = (int)(slm.distance * (0.6f + 0.2f * row)) % 24;
    for (int x = -offset; x < 128; x += 24) display.drawLine(x, y, x + 6, y);
  }

  // Gates: a dotted line of buoys above and below the gap, ending in a big buoy
  for (uint8_t i = 0; i < SL_MAXG; i++)
  {
    if (!slm.g[i].active) continue;
    int x = (int)slm.g[i].x;
    int top = slm.g[i].gapTop;
    int bot = slm.g[i].gapBot;

    for (int y = 14; y < top - 4; y += 3) display.fillRect(x + 2, y, 1, 2);
    display.fillRect(x, top - 4, 5, 4);
    for (int y = bot + 5; y < 62; y += 3) display.fillRect(x + 2, y, 1, 2);
    display.fillRect(x, bot, 5, 4);
  }

  if (slm.phase == SKI_DEAD) gmWipeout(SL_X, (int)slm.cy + 7);
  else                        gmSkier(SL_X + 1, (int)slm.cy + 7, false);

  if (slm.phase == SKI_READY)
  {
    display.drawString(34, 16, "SLALOM RUN");
    display.drawString(34, 30, "Hold UP/DOWN");
    display.drawString(34, 43, "UP = start");
  }
  else if (slm.phase == SKI_DEAD)
  {
    display.drawString(44, 16, "WIPEOUT!");
    if (slm.newBest) display.drawString(40, 28, "NEW BEST!");
    if ((millis() - slm.deadSince) > 700) display.drawString(40, 43, "UP = retry");
  }

  gmShow();
}

void slmFrame()
{
  if (slm.phase == SKI_READY)
  {
    if (gmEdge[GM_B_UP]) slmStart();
  }
  else if (slm.phase == SKI_DEAD)
  {
    if (gmEdge[GM_B_UP] && (long)(gmEdgeAt[GM_B_UP] - slm.deadSince) > 700) slmStart();
  }

  uint8_t n = gmSteps(40);
  if (n == 0) return;

  for (uint8_t i = 0; i < n && slm.phase == SKI_RUNNING; i++) slmStep();
  slmDraw();
}


// ===========================================================================
// GAME 4: RAMP JUMP  (pick a ramp height, time the power meter, three jumps)
// Higher ramp = longer possible jump, but a faster, harder meter.
// ===========================================================================
enum RjPhase : uint8_t { RJ_PICK, RJ_POWER, RJ_FLY, RJ_RESULT, RJ_DONE };

struct RampData
{
  RjPhase       phase;
  uint8_t       attempt;      // 0..2
  uint8_t       idx;          // 0..3 = 4.0, 4.5, 5.0, 5.5 ft
  int8_t        dir;          // Meter sweep direction
  bool          crash;
  bool          newBest;
  float         power;        // 0..100
  float         t;            // Flight progress 0..1
  float         dist;         // Feet
  float         total;
  float         res[3];
  unsigned long phaseStart;
  unsigned long doneSince;
};

RampData rj;

void rjReset()
{
  rj.phase = RJ_PICK;
  rj.attempt = 0;
  rj.idx = 1;
  rj.dir = 1;
  rj.crash = false;
  rj.newBest = false;
  rj.power = 0.0f;
  rj.t = 0.0f;
  rj.dist = 0.0f;
  rj.total = 0.0f;
  rj.res[0] = rj.res[1] = rj.res[2] = 0.0f;
}

// Work out how far the jump went from where the meter was stopped
void rjStopMeter()
{
  float q = 1.0f - fabsf(rj.power - 78.0f) / 78.0f;     // Sweet spot is at 78
  if (q < 0.0f) q = 0.0f;
  float maxFt = 28.0f + 9.0f * (float)rj.idx;           // 28, 37, 46, 55 ft
  rj.crash = (rj.power >= 97.0f);                       // Overpowered: wipeout
  rj.dist = rj.crash ? 0.0f : maxFt * (0.25f + 0.75f * q);
  rj.t = 0.0f;
  rj.phase = RJ_FLY;
}

void rjDrawRamp(int idx)
{
  int ph = 10 + 3 * idx;                      // Ramp height in pixels
  display.drawLine(0, 57, 127, 57);           // Water
  display.drawLine(6, 56, 36, 56 - ph);       // Slope
  display.drawLine(36, 56 - ph, 36, 56);      // Back edge
  for (int x = 9; x < 36; x += 3) display.drawLine(x, 56, x, 56 - (ph * (x - 6)) / 30);   // Hatching
}

void rjDraw()
{
  char b[28];
  int ph = 10 + 3 * rj.idx;

  gmBegin();
  snprintf(b, sizeof(b), "JUMP %u/3", (unsigned)(rj.attempt + 1 > 3 ? 3 : rj.attempt + 1));
  char h[16];
  snprintf(h, sizeof(h), "HI %u", (unsigned)gmHigh[4]);
  gmHeader(b, h);
  rjDrawRamp(rj.idx);

  if (rj.phase == RJ_PICK)
  {
    gmSkier(30, 56 - ph, false);
    snprintf(b, sizeof(b), "Ramp: %d.%d ft", 4 + (rj.idx / 2), (rj.idx % 2) * 5);
    display.drawString(46, 16, b);
    display.drawString(46, 28, "UP/DN: height");
    display.drawString(46, 40, "ENTER: go!");
  }
  else if (rj.phase == RJ_POWER)
  {
    gmSkier(30, 56 - ph, false);
    display.drawString(46, 14, "Hit the zone!");
    display.drawLine(48, 28, 118, 28);                       // Meter frame
    display.drawLine(48, 36, 118, 36);
    display.drawLine(48, 28, 48, 36);
    display.drawLine(118, 28, 118, 36);
    display.fillRect(49, 29, (int)(rj.power * 0.7f), 7);     // Fill
    display.drawLine(97, 38, 97, 42);                        // Sweet-spot bracket (70..86)
    display.drawLine(108, 38, 108, 42);
    display.drawLine(97, 42, 108, 42);
    display.drawString(46, 46, "UP / ENT = stop");
  }
  else if (rj.phase == RJ_FLY || rj.phase == RJ_RESULT)
  {
    float t = rj.t;
    float y0 = (float)(56 - ph);
    float arc = rj.crash ? 4.0f : (5.0f + rj.dist * 0.17f);
    float x = 36.0f + rj.dist * 1.6f * t;
    float y = y0 * (1.0f - t) + 56.0f * t - arc * 4.0f * t * (1.0f - t);

    if (rj.phase == RJ_RESULT && rj.crash) gmWipeout((int)x - 4, 56);
    else gmSkier((int)x - 5, (int)y, t < 1.0f);

    if (rj.phase == RJ_RESULT)
    {
      if (rj.crash)
      {
        display.drawString(46, 16, "WIPEOUT!");
        display.drawString(46, 28, "Too much power");
      }
      else
      {
        snprintf(b, sizeof(b), "%d ft!", (int)rj.dist);
        display.drawString(46, 16, b);
      }
    }
  }
  else   // RJ_DONE
  {
    display.drawString(46, 14, "ALL DONE");
    float best = rj.res[0];
    if (rj.res[1] > best) best = rj.res[1];
    if (rj.res[2] > best) best = rj.res[2];
    snprintf(b, sizeof(b), "Best %d ft", (int)best);
    display.drawString(46, 26, b);
    snprintf(b, sizeof(b), "Total %d ft", (int)rj.total);
    display.drawString(46, 38, b);
    if (rj.newBest) display.drawString(46, 50, "NEW BEST!");
    else if ((millis() - rj.doneSince) > 700) display.drawString(46, 50, "UP = again");
  }

  gmShow();
}

void rjFrame()
{
  unsigned long now = millis();
  bool stopPress = gmEdge[GM_B_UP] || gmEdge[GM_B_ENTER];

  if (rj.phase == RJ_PICK)
  {
    if (gmEdge[GM_B_UP]   && rj.idx < 3) rj.idx++;
    if (gmEdge[GM_B_DOWN] && rj.idx > 0) rj.idx--;
    if (gmEdge[GM_B_ENTER])
    {
      rj.phase = RJ_POWER;
      rj.power = 0.0f;
      rj.dir = 1;
      rj.phaseStart = now;
    }
  }
  else if (rj.phase == RJ_POWER)
  {
    if (stopPress || (now - rj.phaseStart) > 7000) rjStopMeter();   // Walk away and it just stops where it is
  }
  else if (rj.phase == RJ_DONE)
  {
    if (gmEdge[GM_B_UP] && (long)(gmEdgeAt[GM_B_UP] - rj.doneSince) > 700) rjReset();
  }

  uint8_t n = gmSteps(40);
  if (n == 0) return;

  for (uint8_t i = 0; i < n; i++)
  {
    if (rj.phase == RJ_POWER)
    {
      rj.power += (float)rj.dir * (1.8f + 0.6f * (float)rj.idx);    // Higher ramp = faster meter
      if (rj.power >= 100.0f) { rj.power = 100.0f; rj.dir = -1; }
      if (rj.power <= 0.0f)   { rj.power = 0.0f;   rj.dir = 1; }
    }
    else if (rj.phase == RJ_FLY)
    {
      rj.t += 0.04f;
      if (rj.t >= 1.0f)
      {
        rj.t = 1.0f;
        rj.res[rj.attempt] = rj.dist;
        rj.total += rj.dist;
        rj.phase = RJ_RESULT;
        rj.phaseStart = now;
      }
    }
    else if (rj.phase == RJ_RESULT)
    {
      if ((now - rj.phaseStart) > 1500)
      {
        rj.attempt++;
        if (rj.attempt >= 3)
        {
          rj.phase = RJ_DONE;
          rj.doneSince = now;
          uint16_t total = (uint16_t)rj.total;
          if (total > gmHigh[4])
          {
            gmHigh[4] = total;
            rj.newBest = true;
          }
        }
        else
        {
          rj.phase = RJ_PICK;
        }
      }
    }
  }

  rjDraw();
}


// ===========================================================================
// GAME 5: TRICK COMBO  (Simon says)
// ===========================================================================
constexpr uint8_t TC_MAX = 40;

enum TcPhase : uint8_t { TC_READY, TC_SHOW, TC_INPUT, TC_NICE, TC_DEAD };

struct TrickData
{
  TcPhase       phase;
  uint8_t       len;          // Tricks in the current combo
  uint8_t       inputIdx;     // How many you have repeated so far
  uint8_t       flash;        // Last trick you pressed (shown briefly)
  uint16_t      score;
  bool          newBest;
  unsigned long phaseStart;
  unsigned long flashAt;
  unsigned long lastInput;
  unsigned long deadSince;
  uint8_t       seq[TC_MAX];  // 0 = jump, 1 = spin, 2 = duck
};

TrickData tc;

const char * const tcNames[3] = { "JUMP", "SPIN", "DUCK" };

void tcReset()
{
  tc.phase = TC_READY;
  tc.len = 0;
  tc.score = 0;
  tc.newBest = false;
  tc.flashAt = 0;
}

void tcStartRound()
{
  tc.phase = TC_SHOW;
  tc.phaseStart = millis();
  tc.inputIdx = 0;
}

void tcStart()
{
  tcReset();
  tc.len = 1;
  tc.seq[0] = (uint8_t)random(0, 3);
  tcStartRound();
}

void tcDie()
{
  tc.phase = TC_DEAD;
  tc.deadSince = millis();
  if (tc.score > gmHigh[5])
  {
    gmHigh[5] = tc.score;
    tc.newBest = true;
  }
}

void tcPress(uint8_t trick)
{
  unsigned long now = millis();

  if (trick == tc.seq[tc.inputIdx])
  {
    tc.flash = trick;
    tc.flashAt = now;
    tc.lastInput = now;
    tc.inputIdx++;

    if (tc.inputIdx >= tc.len)      // Combo complete
    {
      tc.score = tc.len;
      tc.phase = TC_NICE;
      tc.phaseStart = now;
    }
  }
  else
  {
    tcDie();
  }
}

// Pictogram of a trick, centred around x = 64
void tcPicto(uint8_t trick)
{
  display.drawLine(30, 57, 98, 57);    // Water

  if (trick == 0)          // JUMP: skier in the air, spray below
  {
    gmSkier(57, 40, true);
    display.drawLine(52, 52, 55, 55);
    display.drawLine(70, 55, 73, 52);
  }
  else if (trick == 1)     // SPIN: skier inside a ring
  {
    gmSkier(57, 46, true);
    for (int i = 0; i < 12; i++)
    {
      float a0 = (float)i * 0.5236f;
      float a1 = (float)(i + 1) * 0.5236f;
      display.drawLine(62 + (int)(14.0f * cosf(a0)), 36 + (int)(14.0f * sinf(a0)),
                       62 + (int)(14.0f * cosf(a1)), 36 + (int)(14.0f * sinf(a1)));
    }
  }
  else                     // DUCK: skier crouched low
  {
    gmSkier(57, 55, false);
    display.drawLine(52, 30, 72, 30);
    display.drawLine(62, 30, 62, 35);
  }
}

void tcDraw()
{
  char b[28];
  unsigned long now = millis();

  gmBegin();
  gmScoreHeader("COMBO", tc.score, gmHigh[5]);

  if (tc.phase == TC_READY)
  {
    display.drawString(34, 15, "TRICK COMBO");
    display.drawString(10, 28, "Watch, then repeat it:");
    display.drawString(10, 40, "UP=JUMP ENT=SPIN DN=DUCK");
    display.drawString(34, 52, "UP = start");
  }
  else if (tc.phase == TC_SHOW)
  {
    unsigned long showMs = (tc.len >= 12) ? 350UL : (700UL - (unsigned long)tc.len * 30UL);
    unsigned long slot = showMs + 150UL;
    unsigned long el = now - tc.phaseStart;
    unsigned long idx = el / slot;

    display.drawString(40, 15, "WATCH...");
    if (idx < tc.len && (el % slot) < showMs)
    {
      tcPicto(tc.seq[idx]);
      display.drawString(52, 45, tcNames[tc.seq[idx]]);
    }
  }
  else if (tc.phase == TC_INPUT)
  {
    snprintf(b, sizeof(b), "YOUR TURN  %u/%u", (unsigned)tc.inputIdx, (unsigned)tc.len);
    display.drawString(24, 15, b);
    if ((now - tc.flashAt) < 300)
    {
      tcPicto(tc.flash);
      display.drawString(52, 45, tcNames[tc.flash]);
    }
    else
    {
      display.drawString(10, 40, "UP=JUMP ENT=SPIN DN=DUCK");
    }
  }
  else if (tc.phase == TC_NICE)
  {
    tcPicto(tc.flash);
    display.drawString(46, 15, "NICE!");
  }
  else   // TC_DEAD
  {
    display.drawString(38, 15, "WIPEOUT!");
    snprintf(b, sizeof(b), "Combo of %u", (unsigned)tc.score);
    display.drawString(34, 28, b);
    if (tc.newBest) display.drawString(34, 40, "NEW BEST!");
    if ((now - tc.deadSince) > 700) display.drawString(34, 52, "UP = retry");
  }

  gmShow();
}

void tcFrame()
{
  unsigned long now = millis();

  if (tc.phase == TC_READY)
  {
    if (gmEdge[GM_B_UP]) tcStart();
  }
  else if (tc.phase == TC_DEAD)
  {
    if (gmEdge[GM_B_UP] && (long)(gmEdgeAt[GM_B_UP] - tc.deadSince) > 700) tcStart();
  }
  else if (tc.phase == TC_INPUT)
  {
    if      (gmEdge[GM_B_UP])    tcPress(0);
    else if (gmEdge[GM_B_ENTER]) tcPress(1);
    else if (gmEdge[GM_B_DOWN])  tcPress(2);
  }

  // Timers
  if (tc.phase == TC_SHOW)
  {
    unsigned long showMs = (tc.len >= 12) ? 350UL : (700UL - (unsigned long)tc.len * 30UL);
    if ((now - tc.phaseStart) >= (unsigned long)tc.len * (showMs + 150UL) + 250UL)
    {
      tc.phase = TC_INPUT;
      tc.inputIdx = 0;
      tc.lastInput = now;
    }
  }
  else if (tc.phase == TC_INPUT)
  {
    if ((now - tc.lastInput) > 5000) tcDie();    // Too slow
  }
  else if (tc.phase == TC_NICE)
  {
    if ((now - tc.phaseStart) > 900)
    {
      if (tc.len < TC_MAX)
      {
        tc.seq[tc.len] = (uint8_t)random(0, 3);
        tc.len++;
      }
      tcStartRound();
    }
  }

  if (gmSteps(40) == 0) return;
  tcDraw();
}


// ===========================================================================
// THE MENU
// ===========================================================================
void gmStart(uint8_t g)
{
  gmCur = g;
  gmLastTick = millis();

  if      (g == 1) skiReset();
  else if (g == 2) snkReset();
  else if (g == 3) slmReset();
  else if (g == 4) rjReset();
  else if (g == 5) tcReset();
}

void gmMenuFrame()
{
  if (gmEdge[GM_B_UP]   && gmSel > 0)            gmSel--;
  if (gmEdge[GM_B_DOWN] && gmSel < GM_COUNT - 1) gmSel++;
  if (gmEdge[GM_B_ENTER])
  {
    gmStart((uint8_t)(gmSel + 1));
    return;
  }

  if (gmSteps(60) == 0) return;

  char b[28];
  char r[28];

  gmBegin();

  if (GM_DEBUG)
  {
    snprintf(r, sizeof(r), "c%u d%u e%u", (unsigned)gmCallsLastSec, (unsigned)gmDrawMs,
             (unsigned)(gmEdgeCount[0] + gmEdgeCount[1] + gmEdgeCount[2] + gmEdgeCount[3]));
  }
  else
  {
    snprintf(r, sizeof(r), "ENT = play");
  }
  gmHeader("GAMES", r);

  uint8_t top = (gmSel > 3) ? (uint8_t)(gmSel - 3) : 0;      // 4 rows are visible

  for (uint8_t row = 0; row < 4; row++)
  {
    uint8_t i = top + row;
    if (i >= GM_COUNT) break;

    int y = 15 + row * 12;

    if (i == gmSel)   // Box round the selected row
    {
      display.drawLine(0, y - 2, 119, y - 2);
      display.drawLine(0, y + 11, 119, y + 11);
      display.drawLine(0, y - 2, 0, y + 11);
      display.drawLine(119, y - 2, 119, y + 11);
    }

    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.drawString(4, y, gmNames[i]);

    if (gmHigh[i + 1] > 0)
    {
      snprintf(b, sizeof(b), "%u", (unsigned)gmHigh[i + 1]);
      display.setTextAlignment(TEXT_ALIGN_RIGHT);
      display.drawString(116, y, b);
      display.setTextAlignment(TEXT_ALIGN_LEFT);
    }
  }

  if (top > 0)                       // "More above / below" chevrons
  {
    display.drawLine(122, 18, 124, 16);
    display.drawLine(124, 16, 126, 18);
  }
  if (top + 4 < GM_COUNT)
  {
    display.drawLine(122, 57, 124, 59);
    display.drawLine(124, 59, 126, 57);
  }

  gmShow();
}


// ---------------------------------------------------------------------------
// The one function the menu calls. Call it EVERY loop() while the games page is showing: it reads its own button
// presses and does its own timing.
// ---------------------------------------------------------------------------
void gamesFrame()
{
  unsigned long now = millis();

  if (!gmIsrAttached)
  {
    attachInterrupt(UpButton.pin,    gmIsrUp,    FALLING);   // Catch taps even while the screen is being redrawn
    attachInterrupt(DownButton.pin,  gmIsrDown,  FALLING);
    attachInterrupt(BackButton.pin,  gmIsrBack,  FALLING);
    attachInterrupt(EnterButton.pin, gmIsrEnter, FALLING);
    gmIsrAttached = true;
  }

  // We weren't being called for a while (we were on another page): start over at the menu
  if ((now - gmLastCall) > 400)
  {
    gmCur = 0;
    for (uint8_t i = 0; i < 4; i++)
    {
      gmLatched[i] = false;      // Forget presses made on other pages
      gmLastPress[i] = 0;
    }
    gmSecStart = now;
    gmCallsThisSec = 0;
    gmLastTick = now;
  }
  gmLastCall = now;

  // Debug: how often are we being called?
  gmCallsThisSec++;
  if ((now - gmSecStart) >= 1000)
  {
    gmCallsLastSec = gmCallsThisSec;
    gmCallsThisSec = 0;
    gmSecStart = now;
  }

  gmReadInput();

  if (gmCur != 0 && gmEdge[GM_B_BACK])     // BACK = back to the menu from any game
  {
    gmCur = 0;
    gmLastTick = now;
    return;
  }

  switch (gmCur)
  {
    case 0: gmMenuFrame(); break;
    case 1: skiFrame();    break;
    case 2: snkFrame();    break;
    case 3: slmFrame();    break;
    case 4: rjFrame();     break;
    case 5: tcFrame();     break;
    default: gmCur = 0;    break;
  }
}

// Keeps the hook line from the single-game version working
void skiGameFrame()
{
  gamesFrame();
}
