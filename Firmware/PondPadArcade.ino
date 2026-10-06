#include <SPI.h>
#include <math.h>
#include <Preferences.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <BleKeyboard.h>

//Settings
#define MAC_MODE         false  
#define SCREEN_ROTATION  1       
#define LONG_PRESS_MS    600
#define CHORD_MS         800     
#define DEBOUNCE_MS      20
#define FRAME_MS         33      
#define DIM_AFTER_MS     30000
#define SLEEP_AFTER_MS   120000
#define BRIGHT_FULL      255
#define BRIGHT_DIM       40
#define BRIGHT_SLEEP     6

//Pins
#define PIN_BLK   2   
#define PIN_RST   3  
#define PIN_DC    4 
#define PIN_CS    5  
#define PIN_SW1   6  
#define PIN_SW2   7 
#define PIN_SW3   21  
#define PIN_SW5   20  
#define PIN_SCK   8   
#define PIN_SW4   9  
#define PIN_MOSI  10   

const uint8_t KEY_PINS[5] = {PIN_SW1, PIN_SW2, PIN_SW3, PIN_SW4, PIN_SW5};


//Colours
#define C_POND    0x0A29
#define C_HEADER  0x0145
#define C_LANE    0x1B6B
#define C_TEXT    0xE7F5
#define C_DIM     0xAD75
#define C_LILY    0x7F6B
#define C_GOLD    0xFE60
#define C_RED     0xF800
#define C_PINK    0xFBB5
#define TRANSP    0x0001   

const uint16_t ARC[5] = {0xF800, 0xFFE0, 0x07E0, 0x039F, 0xF81F}; 


//  Objects
Adafruit_ST7789 tft(&SPI, PIN_CS, PIN_DC, PIN_RST);
BleKeyboard bleKeyboard("Pond Pad", "Pond Labs", 100);
Preferences prefs;


//  Macro def
enum ActionType { ACT_COMBO, ACT_MEDIA, ACT_TEXT };

struct KeyDef {
  const char*    label;  
  const char*    sub;     
  ActionType     type;
  uint8_t        k1, k2, k3;
  const uint8_t* media;
  const char*    text;
};

#define COMBO(l, s, a, b, c) { l, s, ACT_COMBO, a, b, c, nullptr, nullptr }
#define MEDIA(l, s, m)       { l, s, ACT_MEDIA, 0, 0, 0, m, nullptr }
#define TEXT(l, s, t)        { l, s, ACT_TEXT,  0, 0, 0, nullptr, t }

struct Layer {
  const char* name;
  uint16_t    accent;
  KeyDef      keys[5];
};

#if MAC_MODE
  #define MOD KEY_LEFT_GUI
  #define SHOT_KEY '4'
#else
  #define MOD KEY_LEFT_CTRL
  #define SHOT_KEY 's'
#endif

const Layer LAYERS[] = {
  { "LILY PAD", 0x7F6B, {
      COMBO("COPY",  "Ctrl+C",     MOD, 'c', 0),
      COMBO("PSTE",  "Ctrl+V",     MOD, 'v', 0),
      COMBO("UNDO",  "Ctrl+Z",     MOD, 'z', 0),
      COMBO("SAVE",  "Ctrl+S",     MOD, 's', 0),
      COMBO("SNIP",  "Screenshot", KEY_LEFT_GUI, KEY_LEFT_SHIFT, SHOT_KEY),
  }},
  { "TADPOLE", 0xFD4B, {
      MEDIA("PREV",  "Track",  KEY_MEDIA_PREVIOUS_TRACK),
      MEDIA("PLAY",  "Pause",  KEY_MEDIA_PLAY_PAUSE),
      MEDIA("NEXT",  "Track",  KEY_MEDIA_NEXT_TRACK),
      MEDIA("VOL-",  "Volume", KEY_MEDIA_VOLUME_DOWN),
      MEDIA("VOL+",  "Volume", KEY_MEDIA_VOLUME_UP),
  }},
  { "FROG DEV", 0x5E9F, {
      COMBO("CMD",   "Palette",  MOD, KEY_LEFT_SHIFT, 'p'),
      COMBO("TERM",  "Terminal", MOD, '`', 0),
      COMBO("RUN",   "F5",       KEY_F5, 0, 0),
      COMBO("NOTE",  "Comment",  MOD, '/', 0),
      COMBO("FIND",  "In files", MOD, KEY_LEFT_SHIFT, 'f'),
  }},
  { "SPLASH", 0xFEA0, {
      TEXT("THX",   "Thanks!",  "Thanks!"),
      TEXT("BEST",  "Regards",  "Best regards,\n"),
      TEXT("SMLE",  "Smiley",   ":)"),
      TEXT("LGTM",  "Ship it",  "LGTM, ship it!"),
      TEXT("MAIL",  "Email",    "your@mail.com"),
  }},
};
const uint8_t NUM_LAYERS = sizeof(LAYERS) / sizeof(LAYERS[0]);


//  Global state
enum Mode : uint8_t { M_MENU, M_MACRO, M_FLY, M_WHACK, M_DODGE, M_SCORES, M_OVER };
Mode mode = M_MENU;

// layout
#define HUD_H     28
#define CATCH_Y   180
#define HIT_WIN   24
#define REMOVE_Y  206
#define FROG_Y    182
#define HINT_Y    231
#define POND_TOP  30
#define RIPPLE_Y  100
#define RIPPLE_MAX 26

inline int laneX(int i) { return 32 + i * 64; }
inline int imin(int a, int b) { return a < b ? a : b; }
inline int imax(int a, int b) { return a > b ? a : b; }

// player profile (saved)
uint32_t totalXP = 0, totalPresses = 0;
uint16_t hiScore[3] = {0, 0, 0};
uint32_t levelUpUntil = 0;

// power
uint32_t lastActivity = 0;
uint8_t  curBrightness = BRIGHT_FULL;
bool     lastConnected = false;

// input
struct KeyState {
  bool     down = false;
  bool     rawLast = false;
  uint32_t rawChangedAt = 0;
  uint32_t downSince = 0;
  bool     longFired = false;
};
KeyState ks[5];
uint32_t chordStart = 0;
bool     chordFired = false;
bool     exitRequested = false;

// macro mode
uint8_t  curLayer = 0;
uint8_t  comboCount = 0;
uint32_t lastComboAt = 0;
uint32_t lastHeaderAt = 0;

// ripples
struct Ripple { bool alive = false; int16_t x; int16_t r; uint16_t color; };
Ripple ripples[6];
uint32_t lastRippleTick = 0;

// menu
struct Firefly { int16_t x, y; };
Firefly flies[7];
uint32_t lastFireflyAt = 0, lastBlinkAt = 0;
bool blinkOn = true;

// game common
struct Obj { bool alive = false; uint8_t lane; uint8_t kind; float y; };  // kind 0 fly,1 golden,2 puffer
#define OBJ_N 14
Obj objs[OBJ_N];
int      score = 0, lives = 3, combo = 0;
bool     hudDirty = true;
uint32_t lastFrame = 0, lastSpawn = 0;

// fly catch
uint32_t tFlashUntil[5];
uint16_t tFlashCol[5];
bool     tFlashState[5];

// whack
struct Hole { uint8_t state; uint8_t kind; uint32_t until; };  // state 0 empty,1 up,2 feedback
Hole     holes[5];
char     fbText[5][6];
uint16_t fbCol[5];
uint32_t roundEnd = 0, nextPop = 0;
int      lastSecs = -1;

// dodge
uint8_t  frogLane = 2;
bool     frogDirty = true;
uint32_t frogFlashUntil = 0;
bool     frogWasFlash = false;

// game over
uint8_t  lastGame = 0;
int      lastScore = 0;
bool     lastNewHi = false, lastLevelUp = false;
uint16_t lastXpGain = 0;
uint32_t overReadyAt = 0;


void loadProfile() {
  prefs.begin("pondpad", true);
  totalXP      = prefs.getUInt("xp", 0);
  totalPresses = prefs.getUInt("presses", 0);
  hiScore[0]   = prefs.getUShort("hs0", 0);
  hiScore[1]   = prefs.getUShort("hs1", 0);
  hiScore[2]   = prefs.getUShort("hs2", 0);
  prefs.end();
}

void saveProfile() {
  prefs.begin("pondpad", false);
  prefs.putUInt("xp", totalXP);
  prefs.putUInt("presses", totalPresses);
  prefs.putUShort("hs0", hiScore[0]);
  prefs.putUShort("hs1", hiScore[1]);
  prefs.putUShort("hs2", hiScore[2]);
  prefs.end();
}

uint16_t levelFor(uint32_t xp) { return 1 + (uint16_t)sqrtf(xp / 20.0f); }

bool addXP(uint32_t n) {
  uint16_t before = levelFor(totalXP);
  totalXP += n;
  if (levelFor(totalXP) > before) {
    levelUpUntil = millis() + 2500;
    return true;
  }
  return false;
}

void setBacklight(uint8_t v) {
  curBrightness = v;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(PIN_BLK, v);
#else
  ledcWrite(0, v);
#endif
}

void backlightInit() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(PIN_BLK, 5000, 8);
#else
  ledcSetup(0, 5000, 8);
  ledcAttachPin(PIN_BLK, 0);
#endif
  setBacklight(BRIGHT_FULL);
}

void wake() {
  lastActivity = millis();
  if (curBrightness != BRIGHT_FULL) setBacklight(BRIGHT_FULL);
}

void idleCheck() {
  uint32_t idle = millis() - lastActivity;
  if (idle > SLEEP_AFTER_MS && curBrightness != BRIGHT_SLEEP) setBacklight(BRIGHT_SLEEP);
  else if (idle > DIM_AFTER_MS && idle <= SLEEP_AFTER_MS && curBrightness != BRIGHT_DIM) setBacklight(BRIGHT_DIM);
}


void drawCentered(const char* s, int cx, int y, uint8_t size, uint16_t color, uint16_t bg) {
  int16_t x1, y1; uint16_t w, h;
  tft.setTextSize(size);
  tft.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  if (bg == TRANSP) tft.setTextColor(color);
  else              tft.setTextColor(color, bg);
  tft.setCursor(cx - (int)w / 2 - x1, y);
  tft.print(s);
}

uint16_t blend565(uint16_t a, uint16_t b, uint8_t t) {
  int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
  int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
  int r = ar + ((br - ar) * t) / 255;
  int g = ag + ((bg - ag) * t) / 255;
  int bl = ab + ((bb - ab) * t) / 255;
  return (r << 11) | (g << 5) | bl;
}

void drawHeart(int x, int y, uint16_t c) {
  tft.fillCircle(x - 3, y, 3, c);
  tft.fillCircle(x + 3, y, 3, c);
  tft.fillTriangle(x - 6, y + 1, x + 6, y + 1, x, y + 8, c);
}

void drawStar(int x, int y, int r, uint16_t c) {
  tft.fillTriangle(x, y - r, x - r * 87 / 100, y + r / 2, x + r * 87 / 100, y + r / 2, c);
  tft.fillTriangle(x, y + r, x - r * 87 / 100, y - r / 2, x + r * 87 / 100, y - r / 2, c);
}

void drawFly(int x, int y, uint8_t kind, uint8_t s) {
  uint16_t body = (kind == 1) ? C_GOLD : 0xB5B6;
  uint16_t wing = (kind == 1) ? 0xFFF0 : 0x7DFF;
  tft.fillCircle(x - 6 * s, y - 5 * s, 4 * s, wing);
  tft.fillCircle(x + 6 * s, y - 5 * s, 4 * s, wing);
  tft.fillCircle(x, y, 5 * s, body);
  tft.fillCircle(x - 2 * s, y - 2 * s, s, C_RED);
  tft.fillCircle(x + 2 * s, y - 2 * s, s, C_RED);
}

void drawPuffer(int x, int y) {
  static const int8_t SPX[8] = {14, 10, 0, -10, -14, -10, 0, 10};
  static const int8_t SPY[8] = {0, 10, 14, 10, 0, -10, -14, -10};
  for (int k = 0; k < 8; k++) tft.drawLine(x, y, x + SPX[k], y + SPY[k], C_GOLD);
  tft.fillCircle(x, y, 9, 0xFD20);
  tft.fillCircle(x, y + 3, 5, 0xFFE0);
  tft.fillCircle(x - 3, y - 3, 2, 0xFFFF);
  tft.fillCircle(x + 3, y - 3, 2, 0xFFFF);
  tft.drawPixel(x - 3, y - 3, 0x0000);
  tft.drawPixel(x + 3, y - 3, 0x0000);
}

void drawFrogFace(int x, int y, int r, uint16_t col) {
  int er = r * 4 / 10;
  int ex = r * 55 / 100, ey = r * 70 / 100;
  tft.fillCircle(x - ex, y - ey, er, col);
  tft.fillCircle(x + ex, y - ey, er, col);
  tft.fillCircle(x, y, r, col);
  tft.fillCircle(x - ex, y - ey, er * 6 / 10 + 1, 0xFFFF);
  tft.fillCircle(x + ex, y - ey, er * 6 / 10 + 1, 0xFFFF);
  tft.fillCircle(x - ex, y - ey, er / 4 + 1, 0x0000);
  tft.fillCircle(x + ex, y - ey, er / 4 + 1, 0x0000);
  tft.drawFastHLine(x - r / 2, y + r / 3, r, 0x0000);
  tft.drawFastHLine(x - r / 2, y + r / 3 + 1, r, 0x0000);
}

void drawSnake(int x, int y) {
  tft.fillRoundRect(x - 15, y - 14, 30, 28, 11, 0xA280);
  tft.fillCircle(x - 7, y - 4, 4, 0xFFE0);
  tft.fillCircle(x + 7, y - 4, 4, 0xFFE0);
  tft.drawFastVLine(x - 7, y - 7, 7, 0x0000);
  tft.drawFastVLine(x + 7, y - 7, 7, 0x0000);
  tft.drawLine(x, y + 6, x, y + 14, C_RED);
  tft.drawLine(x, y + 14, x - 3, y + 18, C_RED);
  tft.drawLine(x, y + 14, x + 3, y + 18, C_RED);
}


//Header
void drawHeader() {
  uint32_t now = millis();
  tft.fillRect(0, 0, 320, HUD_H, C_HEADER);

  uint16_t lv = levelFor(totalXP);
  char b[16];
  snprintf(b, sizeof(b), "LV%02u", lv);
  tft.setTextSize(2);
  tft.setTextColor(C_GOLD, C_HEADER);
  tft.setCursor(6, 6);
  tft.print(b);

  uint32_t lo = 20UL * (lv - 1) * (lv - 1);
  uint32_t hi = 20UL * lv * lv;
  int fill = (int)(70UL * (totalXP - lo) / (hi - lo));
  tft.drawRect(58, 9, 72, 10, C_LILY);
  tft.fillRect(59, 10, fill, 8, C_LILY);

  if (levelUpUntil && now < levelUpUntil) {
    if ((now / 250) & 1) drawCentered("LEVEL UP!", 177, 10, 1, C_GOLD, C_HEADER);
  } else if (mode == M_MACRO && comboCount >= 2) {
    snprintf(b, sizeof(b), "COMBO x%u", comboCount);
    drawCentered(b, 177, 10, 1, ARC[comboCount % 5], C_HEADER);
  } else {
    drawCentered("PLAYER 1", 177, 10, 1, C_DIM, C_HEADER);
  }

  bool c = bleKeyboard.isConnected();
  uint16_t col = c ? 0x07E0 : 0xFD20;
  tft.fillCircle(232, 13, 5, col);
  tft.setTextSize(1);
  tft.setTextColor(col, C_HEADER);
  tft.setCursor(244, 10);
  tft.print(c ? "LINKED" : "PAIRING");
  tft.drawFastHLine(0, HUD_H - 1, 320, C_LILY);
  lastHeaderAt = now;
}

void headerTick(uint32_t now) {
  if (levelUpUntil) {
    if (now >= levelUpUntil) { drawHeader(); levelUpUntil = 0; }
    else if (now - lastHeaderAt > 250) drawHeader();
  }
}

void suppressHeld() {
  uint32_t now = millis();
  for (uint8_t i = 0; i < 5; i++) {
    bool raw = (digitalRead(KEY_PINS[i]) == LOW);
    ks[i].down = raw;
    ks[i].rawLast = raw;
    ks[i].rawChangedAt = now;
    ks[i].downSince = now;
    ks[i].longFired = true;
  }
}


//  Ripples
void ringDraw(int cx, int r, uint16_t color) {
  if (r > 0) tft.drawCircle(cx, RIPPLE_Y, r, color);
  if (r > 6) tft.drawCircle(cx, RIPPLE_Y, r - 6, color);
}

void spawnRipple(uint8_t keyIndex, uint16_t color) {
  for (auto& r : ripples) {
    if (!r.alive) {
      r.alive = true; r.x = laneX(keyIndex); r.r = 2; r.color = color;
      return;
    }
  }
}

void updateRipples() {
  if (millis() - lastRippleTick < 35) return;
  lastRippleTick = millis();
  for (auto& r : ripples) {
    if (!r.alive) continue;
    ringDraw(r.x, r.r, C_POND);
    r.r += 2;
    if (r.r > RIPPLE_MAX) { r.alive = false; continue; }
    uint8_t fade = (uint16_t)r.r * 255 / RIPPLE_MAX;
    ringDraw(r.x, r.r, blend565(r.color, C_POND, fade));
  }
}


//menu
const char* MENU_A[5] = {"MACRO",  "FLY",   "WHACK",  "LILY",  "HI"};
const char* MENU_B[5] = {"PAD",    "CATCH", "A FROG", "DODGE", "SCORES"};

void drawMenuTile(uint8_t i) {
  int cx = laneX(i), cy = 165;
  tft.fillRect(i * 64 + 1, 134, 62, 104, C_HEADER);
  tft.fillRoundRect(cx - 27, 138, 54, 54, 8, 0x0841);
  tft.drawRoundRect(cx - 27, 138, 54, 54, 8, ARC[i]);
  switch (i) {
    case 0:
      for (int r = 0; r < 2; r++)
        for (int k = 0; k < 5; k++)
          tft.fillRect(cx - 24 + k * 10, 151 + r * 12, 8, 8, ARC[k]);
      tft.fillRect(cx - 16, 176, 32, 6, C_TEXT);
      break;
    case 1: drawFly(cx, cy + 2, 0, 2); break;
    case 2: drawFrogFace(cx, cy + 4, 14, 0x47E0); break;
    case 3: drawPuffer(cx, cy); break;
    case 4: drawStar(cx, cy, 18, C_GOLD); break;
  }
  drawCentered(MENU_A[i], cx, 198, 1, C_TEXT, C_HEADER);
  drawCentered(MENU_B[i], cx, 210, 1, ARC[i], C_HEADER);
  char b[8]; snprintf(b, sizeof(b), "KEY %d", i + 1);
  drawCentered(b, cx, 225, 1, C_DIM, C_HEADER);
}

void enterMenu() {
  mode = M_MENU;
  suppressHeld();
  tft.fillScreen(C_POND);
  drawHeader();
  drawCentered("POND PAD", 164, 38, 4, C_HEADER, TRANSP);   // shadow
  drawCentered("POND PAD", 160, 34, 4, C_LILY, TRANSP);
  drawCentered("A R C A D E", 160, 74, 2, C_GOLD, TRANSP);
  tft.fillRect(0, 132, 320, 108, C_HEADER);
  tft.drawFastHLine(0, 132, 320, C_LILY);
  for (uint8_t i = 0; i < 5; i++) drawMenuTile(i);
  for (auto& f : flies) { f.x = random(10, 310); f.y = random(110, 128); }
  blinkOn = true;
  lastBlinkAt = millis();
  for (auto& r : ripples) r.alive = false;
}

void menuTick(uint32_t now) {
  headerTick(now);

  if (now - lastBlinkAt > 500) {
    lastBlinkAt = now;
    blinkOn = !blinkOn;
    drawCentered("- PICK YOUR GAME -", 160, 96, 1, blinkOn ? C_TEXT : C_POND, C_POND);
  }

  if (now - lastFireflyAt > 140) {
    lastFireflyAt = now;
    for (auto& f : flies) {
      tft.fillRect(f.x - 1, f.y - 1, 3, 3, C_POND);
      f.x += random(-5, 6);
      f.y += random(-2, 3);
      if (f.x < 4) f.x = 4;
      if (f.x > 315) f.x = 315;
      if (f.y < 108) f.y = 108;
      if (f.y > 127) f.y = 127;
      uint16_t c = random(3) ? C_GOLD : 0xFFF0;
      tft.fillRect(f.x - 1, f.y - 1, 3, 3, c);
    }
  }
}


//Mode
void drawMacroPond() {
  const Layer& L = LAYERS[curLayer];
  tft.fillRect(0, HUD_H, 320, 132 - HUD_H, C_POND);
  drawCentered(L.name, 160, 38, 3, L.accent, C_POND);
  int total = NUM_LAYERS * 14;
  int sx = 160 - total / 2 + 7;
  for (uint8_t i = 0; i < NUM_LAYERS; i++) {
    if (i == curLayer) tft.fillCircle(sx + i * 14, 72, 4, L.accent);
    else               tft.drawCircle(sx + i * 14, 72, 3, C_LANE);
  }
}

void drawArcadeButton(uint8_t i, bool pressed) {
  const Layer& L = LAYERS[curLayer];
  const KeyDef& k = L.keys[i];
  int cx = laneX(i), cy = 158;
  uint16_t col = ARC[i];

  tft.fillRect(i * 64 + 1, 134, 62, 104, C_HEADER);
  tft.fillCircle(cx, cy, 28, 0x2104);
  tft.drawCircle(cx, cy, 28, 0x528A);
  if (pressed) {
    tft.fillCircle(cx, cy + 2, 23, blend565(col, 0x0000, 120));
  } else {
    tft.fillCircle(cx, cy - 1, 24, col);
    tft.fillCircle(cx - 8, cy - 9, 6, blend565(col, 0xFFFF, 140));
  }
  uint8_t sz = (strlen(k.label) * 12 <= 58) ? 2 : 1;
  drawCentered(k.label, cx, 196, sz, pressed ? L.accent : C_TEXT, C_HEADER);
  drawCentered(k.sub, cx, 220, 1, C_DIM, C_HEADER);
}

void enterMacro() {
  mode = M_MACRO;
  comboCount = 0;
  suppressHeld();
  tft.fillScreen(C_POND);
  drawHeader();
  drawMacroPond();
  tft.fillRect(0, 132, 320, 108, C_HEADER);
  tft.drawFastHLine(0, 132, 320, C_LILY);
  for (uint8_t i = 0; i < 5; i++) drawArcadeButton(i, false);
  for (auto& r : ripples) r.alive = false;
}

void changeLayer(int dir) {
  curLayer = (curLayer + NUM_LAYERS + dir) % NUM_LAYERS;
  drawMacroPond();
  for (uint8_t i = 0; i < 5; i++) drawArcadeButton(i, ks[i].down);
}

void runAction(uint8_t i) {
  if (!bleKeyboard.isConnected()) return;
  const KeyDef& k = LAYERS[curLayer].keys[i];
  switch (k.type) {
    case ACT_COMBO:
      if (k.k1) bleKeyboard.press(k.k1);
      if (k.k2) bleKeyboard.press(k.k2);
      if (k.k3) bleKeyboard.press(k.k3);
      delay(25);
      bleKeyboard.releaseAll();
      break;
    case ACT_MEDIA: bleKeyboard.write(k.media); break;
    case ACT_TEXT:  bleKeyboard.print(k.text); break;
  }
}

void macroPress(uint8_t i) {
  drawArcadeButton(i, true);
  spawnRipple(i, LAYERS[curLayer].accent);
}

void macroRelease(uint8_t i) {
  drawArcadeButton(i, false);
  if (ks[i].longFired) return;

  bool sent = false;
  if (bleKeyboard.isConnected()) {
    runAction(i);
    sent = true;
  }

  if (!sent) return;

  uint32_t now = millis();
  comboCount = (now - lastComboAt < 2500 && comboCount < 250) ? comboCount + 1 : 1;
  lastComboAt = now;

  uint32_t xp = 1;
  if (comboCount % 5 == 0) {            // combo milestone!
    xp += 2;
    for (uint8_t k = 0; k < 5; k++) spawnRipple(k, ARC[k]);
  }
  addXP(xp);
  totalPresses++;
  if (totalPresses % 25 == 0) saveProfile();
  drawHeader();
}

void macroTick(uint32_t now) {
  updateRipples();
  if (comboCount && now - lastComboAt > 2500) { comboCount = 0; drawHeader(); }
  headerTick(now);
}


//game
void drawGameHud() {
  tft.fillRect(0, 0, 320, HUD_H, C_HEADER);
  char b[20];
  snprintf(b, sizeof(b), "SCORE %d", score);
  tft.setTextSize(2);
  tft.setTextColor(C_TEXT, C_HEADER);
  tft.setCursor(6, 6);
  tft.print(b);

  if (combo >= 2) {
    snprintf(b, sizeof(b), "x%d", combo);
    drawCentered(b, 205, 6, 2, C_GOLD, C_HEADER);
  }
  if (mode == M_WHACK) {
    int s = lastSecs < 0 ? 30 : lastSecs;
    snprintf(b, sizeof(b), "T%02d", s);
    tft.setTextColor(s <= 5 ? C_RED : C_TEXT, C_HEADER);
    tft.setCursor(262, 6);
    tft.print(b);
  } else {
    for (int h = 0; h < 3; h++) drawHeart(272 + h * 18, 12, h < lives ? C_RED : 0x2104);
  }
  tft.drawFastHLine(0, HUD_H - 1, 320, C_LILY);
}

void drawHints() {
  for (int i = 0; i < 5; i++) {
    tft.fillCircle(laneX(i), HINT_Y, 8, ARC[i]);
    char b[2] = {(char)('1' + i), 0};
    drawCentered(b, laneX(i), HINT_Y - 3, 1, 0x0000, TRANSP);
  }
}

void eraseObj(uint8_t k) {
  tft.fillRect(laneX(objs[k].lane) - 16, (int)objs[k].y - 16, 32, 34, C_POND);
}

void drawObj(uint8_t k) {
  int cx = laneX(objs[k].lane), y = (int)objs[k].y;
  if (objs[k].kind == 2) drawPuffer(cx, y);
  else                   drawFly(cx, y, objs[k].kind, 1);
}

void spawnObj() {
  for (int t = 0; t < 6; t++) {
    uint8_t lane = random(5);
    bool busy = false;
    for (auto& o : objs) if (o.alive && o.lane == lane && o.y < 90) busy = true;
    if (busy) continue;
    for (auto& o : objs) {
      if (!o.alive) {
        o.alive = true; o.lane = lane; o.y = HUD_H + 20;
        if (mode == M_FLY) o.kind = (random(100) < 8) ? 1 : 0;
        else {
          int pct = imin(55, 28 + score / 40);
          o.kind = (random(100) < pct) ? 2 : 0;
        }
        return;
      }
    }
    return;
  }
}

void countdown() {
  tft.fillScreen(C_POND);
  drawCentered("GET READY", 160, 60, 2, C_TEXT, C_POND);
  for (int n = 3; n >= 1; n--) {
    char b[2] = {(char)('0' + n), 0};
    tft.fillRect(100, 100, 120, 60, C_POND);
    drawCentered(b, 160, 102, 6, C_GOLD, C_POND);
    delay(420);
  }
  tft.fillRect(100, 100, 120, 60, C_POND);
  drawCentered("GO!", 160, 108, 5, 0x07E0, C_POND);
  delay(350);
}

void drawOver() {
  tft.fillRoundRect(40, 50, 240, 140, 10, C_HEADER);
  tft.drawRoundRect(40, 50, 240, 140, 10, C_GOLD);
  tft.drawRoundRect(42, 52, 236, 136, 8, C_GOLD);
  drawCentered("GAME OVER", 160, 62, 3, C_RED, C_HEADER);
  char b[28];
  snprintf(b, sizeof(b), "SCORE %d", lastScore);
  drawCentered(b, 160, 96, 2, C_TEXT, C_HEADER);
  if (lastNewHi) drawCentered("NEW HIGH SCORE!", 160, 120, 2, C_GOLD, C_HEADER);
  else {
    snprintf(b, sizeof(b), "BEST %u", hiScore[lastGame]);
    drawCentered(b, 160, 120, 2, C_DIM, C_HEADER);
  }
  snprintf(b, sizeof(b), "+%u XP%s", lastXpGain, lastLevelUp ? "  LEVEL UP!" : "");
  drawCentered(b, 160, 148, 1, lastLevelUp ? C_GOLD : C_LILY, C_HEADER);
}

void endGame(uint8_t g) {
  lastGame = g;
  lastScore = score;
  lastNewHi = score > hiScore[g];
  if (lastNewHi) hiScore[g] = score;
  lastXpGain = score / 10 + (lastNewHi ? 5 : 0);
  lastLevelUp = addXP(lastXpGain);
  saveProfile();
  mode = M_OVER;
  overReadyAt = millis() + 900;
  blinkOn = false;
  lastBlinkAt = 0;
  drawOver();
}

void overTick(uint32_t now) {
  if (now < overReadyAt) return;
  if (now - lastBlinkAt > 450) {
    lastBlinkAt = now;
    blinkOn = !blinkOn;
    drawCentered("PRESS ANY KEY", 160, 166, 1, blinkOn ? C_TEXT : C_HEADER, C_HEADER);
  }
}

void startGame(uint8_t m) {
  mode = (Mode)m;
  suppressHeld();
  score = 0; lives = 3; combo = 0; hudDirty = true; lastSecs = -1;
  for (auto& o : objs) o.alive = false;

  countdown();
  tft.fillScreen(C_POND);

  if (m != M_WHACK) {
    for (int k = 1; k < 5; k++) tft.drawFastVLine(k * 64, HUD_H, HINT_Y - 8 - HUD_H, C_LANE);
  }
  drawHints();

  if (m == M_FLY) {
    for (int i = 0; i < 5; i++) { tFlashUntil[i] = 0; tFlashState[i] = false; }
    extern void drawTarget(uint8_t);
    for (uint8_t i = 0; i < 5; i++) drawTarget(i);
  }
  if (m == M_DODGE) { frogLane = 2; frogFlashUntil = 0; frogWasFlash = false; frogDirty = true; }
  if (m == M_WHACK) {
    extern void drawHole(uint8_t);
    for (uint8_t i = 0; i < 5; i++) { holes[i].state = 0; drawHole(i); }
    roundEnd = millis() + 30000;
    nextPop = millis() + 600;
  }
  drawGameHud();
  lastSpawn = millis();
  lastFrame = 0;
}


//  GAME 1 : FLY CATCH
void drawTarget(uint8_t i) {
  int cx = laneX(i);
  bool fl = millis() < tFlashUntil[i];
  uint16_t col = fl ? tFlashCol[i] : C_LILY;
  tft.fillCircle(cx, CATCH_Y, 24, fl ? blend565(col, C_POND, 170) : C_POND);
  tft.drawCircle(cx, CATCH_Y, 24, col);
  tft.drawCircle(cx, CATCH_Y, 23, col);
  tft.drawCircle(cx, CATCH_Y, 12, blend565(col, C_POND, 150));
}

void flashTarget(uint8_t i, uint16_t col) {
  tFlashUntil[i] = millis() + 160;
  tFlashCol[i] = col;
}

void flyPress(uint8_t i) {
  int best = -1; float bd = 999;
  for (int k = 0; k < OBJ_N; k++) {
    if (!objs[k].alive || objs[k].lane != i) continue;
    float d = fabsf(objs[k].y - CATCH_Y);
    if (d <= HIT_WIN && d < bd) { bd = d; best = k; }
  }
  if (best >= 0) {
    eraseObj(best);
    int base = (objs[best].kind == 1) ? 50 : 10;
    score += base * (1 + imin(combo / 5, 4));
    combo++;
    if (objs[best].kind == 1 && lives < 3) lives++;
    objs[best].alive = false;
    flashTarget(i, 0x07E0);
  } else {
    combo = 0;
    flashTarget(i, C_RED);
  }
  hudDirty = true;
}

void flyTick(uint32_t now) {
  float speed = 2.4f + score / 350.0f;
  if (speed > 6.0f) speed = 6.0f;
  int gap = imax(320, 850 - (int)(score * 1.2f));
  if (now - lastSpawn > (uint32_t)gap) { lastSpawn = now; spawnObj(); }

  bool dirty[5] = {false, false, false, false, false};
  for (uint8_t k = 0; k < OBJ_N; k++) {
    if (!objs[k].alive) continue;
    eraseObj(k);
    objs[k].y += speed;
    if (objs[k].y > REMOVE_Y) {               // missed it
      objs[k].alive = false;
      lives--; combo = 0; hudDirty = true;
      flashTarget(objs[k].lane, C_RED);
      if (lives <= 0) { endGame(0); return; }
      continue;
    }
    if (objs[k].y > CATCH_Y - 60) dirty[objs[k].lane] = true;
  }
  for (uint8_t i = 0; i < 5; i++) {
    bool fl = now < tFlashUntil[i];
    if (dirty[i] || fl != tFlashState[i]) { drawTarget(i); tFlashState[i] = fl; }
  }
  for (uint8_t k = 0; k < OBJ_N; k++) if (objs[k].alive) drawObj(k);
  if (hudDirty) { drawGameHud(); hudDirty = false; }
}


//  GAME 2 : WHACK-A-FROG
void drawHole(uint8_t i) {
  int cx = laneX(i);
  tft.fillRect(cx - 31, 70, 62, 100, C_POND);
  tft.fillRoundRect(cx - 27, 146, 54, 18, 9, 0x4208);
  tft.fillRoundRect(cx - 22, 150, 44, 10, 5, 0x0000);
  if (holes[i].state == 1) {
    if (holes[i].kind == 0)      drawFrogFace(cx, 128, 20, 0x47E0);
    else if (holes[i].kind == 1) drawFrogFace(cx, 128, 20, C_GOLD);
    else                         drawSnake(cx, 128);
    tft.fillRoundRect(cx - 27, 150, 54, 14, 7, 0x4208);
  } else if (holes[i].state == 2) {
    drawCentered(fbText[i], cx, 120, 2, fbCol[i], C_POND);
  }
}

void whackPress(uint8_t i) {
  uint32_t now = millis();
  Hole& h = holes[i];
  if (h.state == 1) {
    if (h.kind == 2) {
      score = imax(0, score - 15);
      combo = 0;
      snprintf(fbText[i], 6, "-15");
      fbCol[i] = C_RED;
    } else {
      int pts = ((h.kind == 1) ? 30 : 10) * (1 + imin(combo / 5, 4));
      score += pts;
      combo++;
      snprintf(fbText[i], 6, "+%d", pts);
      fbCol[i] = C_GOLD;
    }
    h.state = 2;
    h.until = now + 320;
    drawHole(i);
  } else {
    combo = 0;        
  }
  hudDirty = true;
}

void whackTick(uint32_t now) {
  if ((int32_t)(roundEnd - now) <= 0) { endGame(1); return; }
  uint32_t elapsed = 30000 - (roundEnd - now);

  if (now >= nextPop) {
    uint8_t empt[5]; int n = 0;
    for (uint8_t i = 0; i < 5; i++) if (holes[i].state == 0) empt[n++] = i;
    if (n) {
      uint8_t i = empt[random(n)];
      int r = random(100);
      holes[i].kind = (r < 65) ? 0 : (r < 82 ? 1 : 2);
      uint32_t up = 1200 - elapsed / 40;
      if (holes[i].kind == 1) up = up * 7 / 10;
      holes[i].state = 1;
      holes[i].until = now + up;
      drawHole(i);
    }
    nextPop = now + (750 - elapsed / 60) + random(150);
  }

  for (uint8_t i = 0; i < 5; i++) {
    if (holes[i].state == 1 && now >= holes[i].until) {
      holes[i].state = 0;
      if (holes[i].kind != 2) { combo = 0; hudDirty = true; }
      drawHole(i);
    } else if (holes[i].state == 2 && now >= holes[i].until) {
      holes[i].state = 0;
      drawHole(i);
    }
  }

  int secs = (int)((int32_t)(roundEnd - now) + 999) / 1000;
  if (secs != lastSecs) { lastSecs = secs; hudDirty = true; }
  if (hudDirty) { drawGameHud(); hudDirty = false; }
}


//  GAME 3 : LILY DODGE
void eraseFrog() {
  tft.fillRect(laneX(frogLane) - 24, FROG_Y - 14, 48, 46, C_POND);
}

void drawLilyFrog(bool flash) {
  int cx = laneX(frogLane);
  tft.fillCircle(cx, FROG_Y + 8, 22, 0x3C86);
  tft.fillTriangle(cx, FROG_Y + 8, cx + 22, FROG_Y - 2, cx + 22, FROG_Y + 8, C_POND);
  drawFrogFace(cx, FROG_Y, 12, flash ? C_RED : 0x47E0);
}

void dodgePress(uint8_t i) {
  if (i == frogLane) return;
  eraseFrog();
  frogLane = i;
  frogDirty = true;
}

void dodgeTick(uint32_t now) {
  float speed = 2.6f + score / 400.0f;
  if (speed > 6.5f) speed = 6.5f;
  int gap = imax(330, 750 - score);
  if (now - lastSpawn > (uint32_t)gap) { lastSpawn = now; spawnObj(); }

  for (uint8_t k = 0; k < OBJ_N; k++) {
    if (!objs[k].alive) continue;
    eraseObj(k);
    objs[k].y += speed;

    if (objs[k].lane == frogLane && fabsf(objs[k].y - FROG_Y) < 20) {
      if (objs[k].kind == 2) {
        lives--; combo = 0;
        frogFlashUntil = now + 350;
        if (lives <= 0) { endGame(2); return; }
      } else {
        score += 10 * (1 + imin(combo / 5, 4));
        combo++;
      }
      objs[k].alive = false;
      frogDirty = true;
      hudDirty = true;
      continue;
    }
    if (objs[k].y > REMOVE_Y) { objs[k].alive = false; continue; }
    if (objs[k].lane == frogLane && fabsf(objs[k].y - FROG_Y) < 50) frogDirty = true;
  }

  bool fl = now < frogFlashUntil;
  if (fl != frogWasFlash) { frogWasFlash = fl; frogDirty = true; }
  if (frogDirty) { eraseFrog(); drawLilyFrog(fl); frogDirty = false; }

  for (uint8_t k = 0; k < OBJ_N; k++) if (objs[k].alive) drawObj(k);
  if (hudDirty) { drawGameHud(); hudDirty = false; }
}


//  High scores
void enterScores() {
  mode = M_SCORES;
  suppressHeld();
  tft.fillScreen(C_POND);
  drawCentered("HI-SCORES", 160, 14, 3, C_GOLD, C_POND);
  drawStar(60, 28, 12, C_GOLD);
  drawStar(260, 28, 12, C_GOLD);

  const char* names[3] = {"FLY CATCH", "WHACK-FROG", "LILY DODGE"};
  char b[32];
  tft.setTextSize(2);
  for (int g = 0; g < 3; g++) {
    snprintf(b, sizeof(b), "%-12s%5u", names[g], hiScore[g]);
    tft.setTextColor(ARC[g + 1], C_POND);
    tft.setCursor(58, 62 + g * 24);
    tft.print(b);
  }
  tft.drawFastHLine(40, 142, 240, C_LANE);

  uint16_t lv = levelFor(totalXP);
  snprintf(b, sizeof(b), "LEVEL %u", lv);
  drawCentered(b, 160, 152, 2, C_LILY, C_POND);
  snprintf(b, sizeof(b), "TOTAL XP %lu", (unsigned long)totalXP);
  drawCentered(b, 160, 176, 1, C_TEXT, C_POND);
  snprintf(b, sizeof(b), "MACROS FIRED %lu", (unsigned long)totalPresses);
  drawCentered(b, 160, 190, 1, C_TEXT, C_POND);
  blinkOn = false; lastBlinkAt = 0;
}

void scoresTick(uint32_t now) {
  if (now - lastBlinkAt > 450) {
    lastBlinkAt = now;
    blinkOn = !blinkOn;
    drawCentered("PRESS ANY KEY", 160, 216, 1, blinkOn ? C_TEXT : C_POND, C_POND);
  }
}


//Input 
void onKeyPress(uint8_t i) {
  wake();
  switch (mode) {
    case M_MENU:
      switch (i) {
        case 0: enterMacro(); break;
        case 1: startGame(M_FLY); break;
        case 2: startGame(M_WHACK); break;
        case 3: startGame(M_DODGE); break;
        case 4: enterScores(); break;
      }
      break;
    case M_MACRO:  macroPress(i); break;
    case M_FLY:    flyPress(i); break;
    case M_WHACK:  whackPress(i); break;
    case M_DODGE:  dodgePress(i); break;
    case M_SCORES: enterMenu(); break;
    case M_OVER:   if (millis() >= overReadyAt) enterMenu(); break;
  }
}

void onKeyRelease(uint8_t i) {
  if (mode == M_MACRO) macroRelease(i);
}

void scanButtons() {
  uint32_t now = millis();
  for (uint8_t i = 0; i < 5; i++) {
    bool raw = (digitalRead(KEY_PINS[i]) == LOW);
    if (raw != ks[i].rawLast) { ks[i].rawLast = raw; ks[i].rawChangedAt = now; }

    if ((now - ks[i].rawChangedAt) > DEBOUNCE_MS && raw != ks[i].down) {
      ks[i].down = raw;
      if (raw) {
        ks[i].downSince = now;
        ks[i].longFired = false;
        onKeyPress(i);
      } else {
        onKeyRelease(i);
      }
    }
  }

  if (ks[0].down && ks[4].down) {
    if (!chordStart) {
      chordStart = now;
      ks[0].longFired = true;
      ks[4].longFired = true;
    } else if (!chordFired && now - chordStart > CHORD_MS) {
      chordFired = true;
      exitRequested = true;
    }
  } else {
    chordStart = 0;
    chordFired = false;
  }


  if (mode == M_MACRO) {
    const uint8_t ends[2] = {0, 4};
    for (uint8_t e = 0; e < 2; e++) {
      uint8_t i = ends[e];
      if (ks[i].down && !ks[i].longFired && (now - ks[i].downSince) > LONG_PRESS_MS) {
        ks[i].longFired = true;
        changeLayer(i == 4 ? +1 : -1);
        spawnRipple(i, LAYERS[curLayer].accent);
      }
    }
  }
}


//  Boot splash
void splash() {
  tft.fillScreen(C_POND);
  for (int r = 4; r < 100; r += 6) {
    tft.drawCircle(160, 120, r, blend565(C_LILY, C_POND, (uint8_t)(r * 2.5f)));
    delay(18);
  }
  drawCentered("POND PAD", 164, 82, 4, C_HEADER, TRANSP);
  drawCentered("POND PAD", 160, 78, 4, C_LILY, TRANSP);
  drawCentered("A R C A D E", 160, 122, 2, C_GOLD, TRANSP);
  drawCentered("INSERT COIN", 160, 170, 1, C_TEXT, TRANSP);
  delay(1500);
}


void setup() {
  Serial.begin(115200);
  randomSeed(esp_random());

  for (uint8_t i = 0; i < 5; i++) pinMode(KEY_PINS[i], INPUT_PULLUP);

  backlightInit();
  setBacklight(0);

  SPI.begin(PIN_SCK, -1, PIN_MOSI, PIN_CS);
  tft.init(240, 320);
  tft.setSPISpeed(40000000);
  tft.setRotation(SCREEN_ROTATION);
  tft.setTextWrap(false);
  tft.fillScreen(C_POND);
  setBacklight(BRIGHT_FULL);

  loadProfile();
  splash();

  bleKeyboard.setBatteryLevel(100);
  bleKeyboard.begin();
  lastConnected = bleKeyboard.isConnected();

  lastActivity = millis();
  enterMenu();
}

void loop() {
  scanButtons();
  uint32_t now = millis();

  if (exitRequested) {
    exitRequested = false;
    if (mode != M_MENU) {
      if (mode == M_MACRO) saveProfile();
      enterMenu();
    }
  }

  switch (mode) {
    case M_MENU:   menuTick(now); break;
    case M_MACRO:  macroTick(now); break;
    case M_FLY:
    case M_WHACK:
    case M_DODGE:
      if (now - lastFrame >= FRAME_MS) {
        lastFrame = now;
        if (mode == M_FLY) flyTick(now);
        else if (mode == M_WHACK) whackTick(now);
        else dodgeTick(now);
      }
      break;
    case M_SCORES: scoresTick(now); break;
    case M_OVER:   overTick(now); break;
  }

  idleCheck();

  bool c = bleKeyboard.isConnected();
  if (c != lastConnected) {
    lastConnected = c;
    if (mode == M_MENU || mode == M_MACRO) drawHeader();
    wake();
  }

  delay(1);
}
