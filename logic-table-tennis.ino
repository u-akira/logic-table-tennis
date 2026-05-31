#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>
#include "config.h"

static const int SCREEN_W = 128;
static const int SCREEN_H = 64;
static const int BOARD_W = 6;
static const int BOARD_H = 6;
static const int HAND_SIZE = 6;
static const uint32_t TURN_LIMIT_MS = 30000;
static const uint32_t TITLE_MIN_SHOW_MS = 800;

Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire, OLED_RESET);
Adafruit_NeoPixel pixels(NUM_LEDS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

enum CardDir
{
  STRAIGHT,
  DIAG_R,
  DIAG_L
};
struct Card
{
  CardDir dir;
  uint8_t value;
  bool used;
};

enum Phase
{
  PHASE_TITLE,
  PHASE_GAME_INTRO,
  PHASE_SERVE_POS,
  PHASE_SERVE_CARD,
  PHASE_PLAYER_CARD,
  PHASE_CPU_CARD,
  PHASE_GAME_OVER,
  PHASE_MATCH_OVER
};

struct ButtonState
{
  uint8_t pin;
  bool stable;
  bool lastRead;
  uint32_t changedAt;
  bool pressedEdge;
};

ButtonState btnUp{BUTTON_UP, false, false, 0, false};
ButtonState btnDown{BUTTON_DOWN, false, false, 0, false};
ButtonState btnLeft{BUTTON_LEFT, false, false, 0, false};
ButtonState btnRight{BUTTON_RIGHT, false, false, 0, false};

struct GameContext
{
  Phase phase = PHASE_TITLE;
  uint32_t bootAtMs = 0;
  bool playerServe = true;
  bool playerTurn = true;
  uint8_t serveX = 2;
  uint8_t cardCursor = 0;
  uint8_t cpuGhostCursor = 0;
  uint8_t ballX = 2;
  uint8_t ballY = 5;
  bool ballPlaced = false;
  uint32_t phaseStartedAt = 0;
  uint8_t playerGames = 0;
  uint8_t cpuGames = 0;
  uint16_t turnCount = 0;
  Card lastPlayerCard{STRAIGHT, 0, true};
  Card lastCpuCard{STRAIGHT, 0, true};
  Card playerHand[HAND_SIZE];
  Card cpuHand[HAND_SIZE];
};

struct InputState
{
  bool upPressed = false;
  bool downPressed = false;
  bool leftPressed = false;
  bool rightPressed = false;
  bool upHeld = false;
  bool downHeld = false;
  bool leftHeld = false;
  bool rightHeld = false;
};

enum SoundEvent
{
  SFX_NONE,
  SFX_CLICK_EVT,
  SFX_CONFIRM_EVT,
  SFX_WIN_EVT,
  SFX_MISS_EVT
};

enum NeoEvent
{
  NEO_NONE,
  NEO_NORMAL_EVT,
  NEO_WIN_EVT,
  NEO_LOSE_EVT
};

struct FrameEffects
{
  SoundEvent sound = SFX_NONE;
  NeoEvent neo = NEO_NONE;
};

static GameContext g;

// Stage-1 compatibility shim: move call sites to context-backed state
// while keeping existing function bodies mostly unchanged.
#define phase g.phase
#define bootAtMs g.bootAtMs
#define playerServe g.playerServe
#define playerTurn g.playerTurn
#define serveX g.serveX
#define cardCursor g.cardCursor
#define cpuGhostCursor g.cpuGhostCursor
#define ballX g.ballX
#define ballY g.ballY
#define ballPlaced g.ballPlaced
#define phaseStartedAt g.phaseStartedAt
#define playerGames g.playerGames
#define cpuGames g.cpuGames
#define turnCount g.turnCount
#define lastPlayerCard g.lastPlayerCard
#define lastCpuCard g.lastCpuCard
#define playerHand g.playerHand
#define cpuHand g.cpuHand

const Card kDeck[15] = {
    {STRAIGHT, 1, false},
    {STRAIGHT, 2, false},
    {STRAIGHT, 3, false},
    {STRAIGHT, 4, false},
    {STRAIGHT, 5, false},
    {DIAG_R, 1, false},
    {DIAG_R, 2, false},
    {DIAG_R, 3, false},
    {DIAG_R, 4, false},
    {DIAG_R, 4, false},
    {DIAG_L, 1, false},
    {DIAG_L, 2, false},
    {DIAG_L, 3, false},
    {DIAG_L, 4, false},
    {DIAG_L, 4, false},
};

static inline const char *dirLabel(CardDir d)
{
  if (d == STRAIGHT)
    return "U";
  if (d == DIAG_R)
    return "UR";
  return "UL";
}

void drawDirectionGlyph(CardDir dir, int x, int y)
{
  // x,y is the glyph center inside a card cell.
  if (dir == STRAIGHT)
  {
    display.drawLine(x, y + 3, x, y - 2, SSD1306_WHITE);
    display.drawLine(x, y - 2, x - 2, y, SSD1306_WHITE);
    display.drawLine(x, y - 2, x + 2, y, SSD1306_WHITE);
  }
  else if (dir == DIAG_R)
  {
    display.drawLine(x - 2, y + 2, x + 2, y - 2, SSD1306_WHITE);
    display.drawLine(x + 2, y - 2, x + 2, y + 1, SSD1306_WHITE);
    display.drawLine(x + 2, y - 2, x - 1, y - 2, SSD1306_WHITE);
  }
  else
  {
    display.drawLine(x + 2, y + 2, x - 2, y - 2, SSD1306_WHITE);
    display.drawLine(x - 2, y - 2, x - 2, y + 1, SSD1306_WHITE);
    display.drawLine(x - 2, y - 2, x + 1, y - 2, SSD1306_WHITE);
  }
}

void drawCardNumberGlyph(uint8_t value, int x, int y)
{
  // 3x5 seven-segment-like glyph via line primitives.
  // Segment anchors:
  //  a
  // f b
  //  g
  // e c
  //  d
  bool a = false, b = false, c = false, d = false, e = false, f = false, g = false;
  switch (value)
  {
  case 1:
    b = c = true;
    break;
  case 2:
    a = b = g = e = d = true;
    break;
  case 3:
    a = b = g = c = d = true;
    break;
  case 4:
    f = g = b = c = true;
    break;
  case 5:
    a = f = g = c = d = true;
    break;
  default:
    a = d = e = f = g = true; // fallback "E"
    break;
  }

  if (a)
    display.drawLine(x, y, x + 2, y, SSD1306_WHITE);
  if (b)
    display.drawLine(x + 2, y, x + 2, y + 2, SSD1306_WHITE);
  if (c)
    display.drawLine(x + 2, y + 2, x + 2, y + 4, SSD1306_WHITE);
  if (d)
    display.drawLine(x, y + 4, x + 2, y + 4, SSD1306_WHITE);
  if (e)
    display.drawLine(x, y + 2, x, y + 4, SSD1306_WHITE);
  if (f)
    display.drawLine(x, y, x, y + 2, SSD1306_WHITE);
  if (g)
    display.drawLine(x, y + 2, x + 2, y + 2, SSD1306_WHITE);
}

void toneMs(int freq, int ms)
{
  ledcAttach(BUZZER_PIN, 2000, 8);
  ledcWriteTone(BUZZER_PIN, freq);
  delay(ms);
  ledcWriteTone(BUZZER_PIN, 0);
}

void sfxClick() { toneMs(1600, 35); }
void sfxConfirm()
{
  toneMs(1300, 45);
  delay(10);
  toneMs(1700, 55);
}
void sfxWin()
{
  toneMs(1200, 80);
  delay(10);
  toneMs(1600, 80);
  delay(10);
  toneMs(2000, 100);
}
void sfxMiss() { toneMs(700, 90); }

void setNeoNormal()
{
  for (int i = 0; i < NUM_LEDS; ++i)
  {
    pixels.setPixelColor(i, i < playerGames ? pixels.Color(0, 80, 0) : 0);
  }
  pixels.show();
}

void setNeoWin()
{
  for (int i = 0; i < NUM_LEDS; ++i)
    pixels.setPixelColor(i, pixels.Color(0, 0, 80));
  pixels.show();
}

void setNeoLose()
{
  for (int i = 0; i < NUM_LEDS; ++i)
    pixels.setPixelColor(i, pixels.Color(80, 0, 0));
  pixels.show();
}

int soundPriority(SoundEvent e)
{
  if (e == SFX_WIN_EVT) return 4;
  if (e == SFX_CONFIRM_EVT) return 3;
  if (e == SFX_MISS_EVT) return 2;
  if (e == SFX_CLICK_EVT) return 1;
  return 0;
}

void emitSound(FrameEffects &fx, SoundEvent e)
{
  if (soundPriority(e) > soundPriority(fx.sound)) fx.sound = e;
}

void emitNeo(FrameEffects &fx, NeoEvent e)
{
  if (e != NEO_NONE) fx.neo = e;
}

void applyEffects(const FrameEffects &fx)
{
  if (fx.neo == NEO_NORMAL_EVT) setNeoNormal();
  else if (fx.neo == NEO_WIN_EVT) setNeoWin();
  else if (fx.neo == NEO_LOSE_EVT) setNeoLose();

  if (fx.sound == SFX_CLICK_EVT) sfxClick();
  else if (fx.sound == SFX_CONFIRM_EVT) sfxConfirm();
  else if (fx.sound == SFX_WIN_EVT) sfxWin();
  else if (fx.sound == SFX_MISS_EVT) sfxMiss();
}

void updateButton(ButtonState &b)
{
  bool rawPressed = digitalRead(b.pin) == LOW;
  b.pressedEdge = false;
  if (rawPressed != b.lastRead)
  {
    b.lastRead = rawPressed;
    b.changedAt = millis();
  }
  if ((millis() - b.changedAt) > 30 && b.stable != rawPressed)
  {
    b.stable = rawPressed;
    if (b.stable)
      b.pressedEdge = true;
  }
}

void updateButtons()
{
  updateButton(btnUp);
  updateButton(btnDown);
  updateButton(btnLeft);
  updateButton(btnRight);
}

InputState readInputState()
{
  InputState in;
  in.upPressed = btnUp.pressedEdge;
  in.downPressed = btnDown.pressedEdge;
  in.leftPressed = btnLeft.pressedEdge;
  in.rightPressed = btnRight.pressedEdge;
  in.upHeld = btnUp.stable;
  in.downHeld = btnDown.stable;
  in.leftHeld = btnLeft.stable;
  in.rightHeld = btnRight.stable;
  return in;
}

bool hasUsableCard(Card hand[])
{
  for (int i = 0; i < HAND_SIZE; ++i)
  {
    if (!hand[i].used)
      return true;
  }
  return false;
}

int firstUsableIndex(Card hand[])
{
  for (int i = 0; i < HAND_SIZE; ++i)
  {
    if (!hand[i].used)
      return i;
  }
  return -1;
}

void dealHands()
{
  int order[15];
  for (int i = 0; i < 15; ++i)
    order[i] = i;
  for (int i = 14; i > 0; --i)
  {
    int j = random(i + 1);
    int t = order[i];
    order[i] = order[j];
    order[j] = t;
  }
  for (int i = 0; i < HAND_SIZE; ++i)
  {
    playerHand[i] = kDeck[order[i]];
    cpuHand[i] = kDeck[order[i + HAND_SIZE]];
    playerHand[i].used = false;
    cpuHand[i].used = false;
  }
}

void resetGame(bool keepServer, uint32_t nowMs, FrameEffects &fx)
{
  dealHands();
  turnCount = 0;
  ballPlaced = false;
  serveX = 2;
  cardCursor = 0;
  cpuGhostCursor = 0;
  if (!keepServer)
    playerServe = random(2) == 0;
  playerTurn = playerServe;
  phase = PHASE_GAME_INTRO;
  phaseStartedAt = nowMs;
  emitNeo(fx, NEO_NORMAL_EVT);
}

bool applyCard(bool actorIsPlayer, const Card &c)
{
  int signY = actorIsPlayer ? -1 : 1;
  int dx = 0;
  if (c.dir == DIAG_R)
    dx = actorIsPlayer ? 1 : -1;
  if (c.dir == DIAG_L)
    dx = actorIsPlayer ? -1 : 1;
  int nx = ballX + dx * c.value;
  int ny = ballY + signY * c.value;
  bool out = nx < 0 || nx >= BOARD_W || ny < 0 || ny >= BOARD_H;
  if (out)
    return false;
  ballX = (uint8_t)nx;
  ballY = (uint8_t)ny;

  // Must reach opponent side in one shot.
  if (actorIsPlayer && ballY > 2)
    return false;
  if (!actorIsPlayer && ballY < 3)
    return false;
  return true;
}

void awardRound(bool playerWon, uint32_t nowMs, FrameEffects &fx)
{
  if (playerWon)
  {
    playerGames++;
    emitNeo(fx, NEO_WIN_EVT);
  }
  else
  {
    cpuGames++;
    emitNeo(fx, NEO_LOSE_EVT);
  }
  emitSound(fx, SFX_WIN_EVT);
  phase = PHASE_GAME_OVER;
  phaseStartedAt = nowMs;
}

void startNextRoundOrMatch(uint32_t nowMs, FrameEffects &fx)
{
  if (playerGames >= 3 || cpuGames >= 3)
  {
    phase = PHASE_MATCH_OVER;
    return;
  }
  playerServe = !playerServe;
  resetGame(true, nowMs, fx);
}

void drawBoard()
{
  const int ox = 0, oy = 6, cellW = 8, cellH = 9;
  display.drawRect(ox, oy, cellW * BOARD_W + 1, cellH * BOARD_H + 1, SSD1306_WHITE);
  for (int i = 1; i < BOARD_W; ++i)
    display.drawLine(ox + i * cellW, oy, ox + i * cellW, oy + cellH * BOARD_H, SSD1306_WHITE);
  for (int i = 1; i < BOARD_H; ++i)
    display.drawLine(ox, oy + i * cellH, ox + cellW * BOARD_W, oy + i * cellH, SSD1306_WHITE);

  // Center horizontal line: thicker than other grid lines.
  int yMid = oy + cellH * 3;
  display.drawLine(ox, yMid - 1, ox + cellW * BOARD_W, yMid - 1, SSD1306_WHITE);
  display.drawLine(ox, yMid, ox + cellW * BOARD_W, yMid, SSD1306_WHITE);
  display.drawLine(ox, yMid + 1, ox + cellW * BOARD_W, yMid + 1, SSD1306_WHITE);

  // Center vertical line: thicker than other vertical lines, but thinner than horizontal center.
  int xMid = ox + cellW * 3;
  display.drawLine(xMid, oy, xMid, oy + cellH * BOARD_H, SSD1306_WHITE);
  display.drawLine(xMid + 1, oy, xMid + 1, oy + cellH * BOARD_H, SSD1306_WHITE);

  if (ballPlaced)
  {
    int cx = ox + ballX * cellW + (cellW / 2);
    int cy = oy + ballY * cellH + (cellH / 2);
    display.fillCircle(cx, cy, 2, SSD1306_WHITE);
  }
  if (phase == PHASE_SERVE_POS)
  {
    int sx = ox + serveX * cellW;
    int sy = oy + 5 * cellH;
    // Serve selection: fill selected cell white, then cut out the ball in black.
    display.fillRect(sx + 1, sy + 1, cellW - 1, cellH - 1, SSD1306_WHITE);
    int pcx = sx + (cellW / 2);
    int pcy = sy + (cellH / 2);
    display.fillCircle(pcx, pcy, 2, SSD1306_BLACK);
  }
}

void drawHand(Card hand[], bool showCursor)
{
  int x = 50;
  int y = 34;
  const int cardW = 24;
  const int cardH = 14;
  const int gapX = 1;
  const int gapY = 1;
  display.setTextSize(1);
  for (int i = 0; i < HAND_SIZE; ++i)
  {
    int cx = x + (i % 3) * (cardW + gapX);
    int cy = y + (i / 3) * (cardH + gapY);
    display.drawRect(cx, cy, cardW, cardH, SSD1306_WHITE);
    if (showCursor && i == cardCursor)
      display.drawRect(cx - 1, cy - 1, cardW + 2, cardH + 2, SSD1306_WHITE);
    if (hand[i].used)
    {
      display.setCursor(cx + 8, cy + 4);
      display.print("X");
    }
    else
    {
      // Direction glyph + value on the right side.
      drawDirectionGlyph(hand[i].dir, cx + 6, cy + 5);
      drawCardNumberGlyph(hand[i].value, cx + 14, cy + 4);
    }
  }
}

void drawHUD()
{
  display.setTextSize(1);
  if (phase == PHASE_PLAYER_CARD || phase == PHASE_CPU_CARD)
  {
    uint32_t remain = 0;
    if (millis() - phaseStartedAt < TURN_LIMIT_MS)
      remain = (TURN_LIMIT_MS - (millis() - phaseStartedAt)) / 1000;
    display.setCursor(88, 0);
    display.print(remain);
    display.print("s");
  }
}

void renderGame(const GameContext &, Adafruit_SSD1306 &)
{
  display.clearDisplay();
  if (phase == PHASE_TITLE)
  {
    display.setTextSize(1);
    display.setCursor(8, 20);
    display.print(" Logic Table Tennis");
    display.setCursor(28, 36);
    display.print(" press any button");
  }
  else if (phase == PHASE_GAME_INTRO)
  {
    display.setTextSize(1);
    display.setCursor(44, 28);
    display.print("Game");
    display.print((int)(playerGames + cpuGames + 1));
  }
  else if (phase == PHASE_MATCH_OVER)
  {
    display.setTextSize(1);
    display.setCursor(20, 20);
    display.print(playerGames >= 3 ? "YOU WIN MATCH" : "CPU WIN MATCH");
    display.setCursor(20, 36);
    display.print("UP: RETRY");
  }
  else
  {
    drawHUD();
    drawBoard();
    if (phase == PHASE_SERVE_POS)
    {
      display.setCursor(121, 0);
      display.print("S");
    }
    else if (phase == PHASE_SERVE_CARD)
    {
      display.setCursor(121, 0);
      display.print("S");
    }
    else
    {
      display.setCursor(52, 10);
    }
    if (phase == PHASE_PLAYER_CARD)
      display.print("YOUR CARD");
    if (phase == PHASE_CPU_CARD)
      display.print("CPU TURN");
    if (phase == PHASE_GAME_OVER)
      display.print("ROUND END");
    // Keep cards visible whenever the board is visible.
    drawHand(playerHand, phase == PHASE_SERVE_POS || phase == PHASE_SERVE_CARD || phase == PHASE_PLAYER_CARD || phase == PHASE_CPU_CARD);
  }
  display.display();
}

void moveCursorLR(int delta, Card hand[])
{
  for (int i = 0; i < HAND_SIZE; ++i)
  {
    cardCursor = (cardCursor + HAND_SIZE + delta) % HAND_SIZE;
    if (!hand[cardCursor].used)
      return;
  }
}

void pickAndApplyPlayerCard(uint32_t nowMs, FrameEffects &fx)
{
  if (playerHand[cardCursor].used)
    return;
  Card c = playerHand[cardCursor];
  playerHand[cardCursor].used = true;
  lastPlayerCard = c;
  bool ok = applyCard(true, c);
  if (!ok)
  {
    emitSound(fx, SFX_MISS_EVT);
    awardRound(false, nowMs, fx);
    return;
  }
  emitSound(fx, SFX_CONFIRM_EVT);
  turnCount++;
  playerTurn = false;
  phase = PHASE_CPU_CARD;
  phaseStartedAt = nowMs;
}

void cpuPlay(uint32_t nowMs, FrameEffects &fx)
{
  if (!hasUsableCard(cpuHand))
  {
    awardRound(true, nowMs, fx);
    return;
  }
  int idx = firstUsableIndex(cpuHand);
  for (int i = 0; i < HAND_SIZE; ++i)
  {
    if (!cpuHand[i].used)
    {
      // Pick first legal, fallback first unused.
      Card test = cpuHand[i];
      uint8_t bx = ballX, by = ballY;
      if (applyCard(false, test))
      {
        ballX = bx;
        ballY = by;
        idx = i;
        break;
      }
      ballX = bx;
      ballY = by;
    }
  }
  Card c = cpuHand[idx];
  cpuHand[idx].used = true;
  lastCpuCard = c;
  bool ok = applyCard(false, c);
  if (!ok)
  {
    emitSound(fx, SFX_MISS_EVT);
    awardRound(true, nowMs, fx);
    return;
  }
  turnCount++;
  playerTurn = true;
  phase = PHASE_PLAYER_CARD;
  phaseStartedAt = nowMs;
}

void setup()
{
  pinMode(BUTTON_UP, INPUT_PULLUP);
  pinMode(BUTTON_DOWN, INPUT_PULLUP);
  pinMode(BUTTON_LEFT, INPUT_PULLUP);
  pinMode(BUTTON_RIGHT, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  Serial.begin(SERIAL_BAUD);

  Wire.begin(OLED_SDA, OLED_SCL);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(false);
  display.clearDisplay();
  display.display();

  pixels.begin();
  pixels.clear();
  pixels.show();

  randomSeed(esp_random());
  phase = PHASE_TITLE;
  bootAtMs = millis();
  setNeoNormal();
}

FrameEffects updateGame(GameContext &, const InputState &in, uint32_t nowMs)
{
  FrameEffects fx;
  if (phase == PHASE_TITLE)
  {
    bool anyPressed = in.upPressed || in.downPressed || in.leftPressed || in.rightPressed;
    bool anyHeld = in.upHeld || in.downHeld || in.leftHeld || in.rightHeld;
    if ((anyPressed || anyHeld) && (nowMs - bootAtMs >= TITLE_MIN_SHOW_MS))
    {
      playerGames = 0;
      cpuGames = 0;
      resetGame(false, nowMs, fx);
      emitSound(fx, SFX_CONFIRM_EVT);
    }
  }
  else if (phase == PHASE_GAME_INTRO)
  {
    if (nowMs - phaseStartedAt >= 1000)
    {
      phase = playerTurn ? PHASE_SERVE_POS : PHASE_CPU_CARD;
      phaseStartedAt = nowMs;
    }
  }
  else if (phase == PHASE_MATCH_OVER)
  {
    if (in.upPressed)
    {
      phase = PHASE_TITLE;
      emitNeo(fx, NEO_NORMAL_EVT);
      emitSound(fx, SFX_CONFIRM_EVT);
    }
  }
  else if (phase == PHASE_SERVE_POS)
  {
    if (in.leftPressed && serveX > 0)
    {
      serveX--;
      emitSound(fx, SFX_CLICK_EVT);
    }
    if (in.rightPressed && serveX < 5)
    {
      serveX++;
      emitSound(fx, SFX_CLICK_EVT);
    }
    if (in.upPressed)
    {
      ballX = serveX;
      ballY = playerServe ? 5 : 0;
      ballPlaced = true;
      phase = PHASE_SERVE_CARD;
      cardCursor = firstUsableIndex(playerHand);
      if ((int)cardCursor < 0)
        cardCursor = 0;
      emitSound(fx, SFX_CONFIRM_EVT);
    }
  }
  else if (phase == PHASE_SERVE_CARD || phase == PHASE_PLAYER_CARD)
  {
    if (!hasUsableCard(playerHand))
    {
      awardRound(false, nowMs, fx);
    }
    else
    {
      if (in.leftPressed)
      {
        moveCursorLR(-1, playerHand);
        emitSound(fx, SFX_CLICK_EVT);
      }
      if (in.rightPressed)
      {
        moveCursorLR(1, playerHand);
        emitSound(fx, SFX_CLICK_EVT);
      }
      if (in.upPressed)
        pickAndApplyPlayerCard(nowMs, fx);
      if (phase == PHASE_PLAYER_CARD && nowMs - phaseStartedAt >= TURN_LIMIT_MS)
      {
        awardRound(false, nowMs, fx);
      }
    }
  }
  else if (phase == PHASE_CPU_CARD)
  {
    // During opponent selection, allow card cursor movement but disable confirm.
    if (in.leftPressed)
    {
      moveCursorLR(-1, playerHand);
      emitSound(fx, SFX_CLICK_EVT);
    }
    if (in.rightPressed)
    {
      moveCursorLR(1, playerHand);
      emitSound(fx, SFX_CLICK_EVT);
    }
    if (in.upPressed)
    {
      // Confirmation disabled by design.
      emitSound(fx, SFX_MISS_EVT);
    }
    cpuGhostCursor = (cpuGhostCursor + 1) % HAND_SIZE;
    if (nowMs - phaseStartedAt > 700)
      cpuPlay(nowMs, fx);
    if (nowMs - phaseStartedAt >= TURN_LIMIT_MS)
    {
      awardRound(true, nowMs, fx);
    }
  }
  else if (phase == PHASE_GAME_OVER)
  {
    if (nowMs - phaseStartedAt > 1200)
      startNextRoundOrMatch(nowMs, fx);
  }
  return fx;
}

void loop()
{
  updateButtons();
  InputState in = readInputState();
  FrameEffects fx = updateGame(g, in, millis());
  applyEffects(fx);
  renderGame(g, display);
  delay(16);
}
