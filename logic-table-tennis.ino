#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "config.h"

static const int SCREEN_W = 128;
static const int SCREEN_H = 64;
static const int BOARD_W = 6;
static const int BOARD_H = 6;
static const int HAND_SIZE = 6;
static const uint32_t TURN_LIMIT_MS = 30000;
static const uint32_t TITLE_MIN_SHOW_MS = 800;
static const uint32_t BALL_STEP_ANIM_MS = 120;
static const int TOP_INFO_Y = 14;
static const int TOP_LABEL_Y = 3;

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
  PHASE_2P_WAIT,
  PHASE_GAME_INTRO,
  PHASE_SERVE_POS,
  PHASE_SERVE_CARD,
  PHASE_PLAYER_CARD,
  PHASE_CPU_CARD,
  PHASE_GAME_OVER,
  PHASE_MATCH_OVER
};

enum GameMode
{
  MODE_CPU,
  MODE_2P
};

struct TwoPMessage;

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
  int8_t ballX = 2;
  int8_t ballY = 5;
  bool ballPlaced = false;
  uint32_t phaseStartedAt = 0;
  uint32_t cpuActionDelayMs = 0;
  uint8_t playerGames = 0;
  uint8_t cpuGames = 0;
  uint16_t turnCount = 0;
  GameMode gameMode = MODE_CPU;
  uint8_t titleCursor = 0;
  uint8_t localSeat = 0;
  uint8_t serverSeat = 0;
  uint8_t turnSeat = 0;
  bool twoPNetReady = false;
  bool twoPConnected = false;
  bool twoPSessionActive = false;
  bool twoPLeader = false;
  uint32_t twoPSessionSeed = 0;
  uint32_t twoPLocalNonce = 0;
  uint32_t twoPLastHelloAt = 0;
  uint32_t twoPLastStartAt = 0;
  uint8_t twoPPeerMac[6] = {0, 0, 0, 0, 0, 0};
  uint32_t twoPPeerNonce = 0;
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
uint32_t randomCpuDelayMs();

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
#define cpuActionDelayMs g.cpuActionDelayMs
#define playerGames g.playerGames
#define cpuGames g.cpuGames
#define turnCount g.turnCount
#define gameMode g.gameMode
#define titleCursor g.titleCursor
#define localSeat g.localSeat
#define serverSeat g.serverSeat
#define turnSeat g.turnSeat
#define twoPNetReady g.twoPNetReady
#define twoPConnected g.twoPConnected
#define twoPSessionActive g.twoPSessionActive
#define twoPLeader g.twoPLeader
#define twoPSessionSeed g.twoPSessionSeed
#define twoPLocalNonce g.twoPLocalNonce
#define twoPLastHelloAt g.twoPLastHelloAt
#define twoPLastStartAt g.twoPLastStartAt
#define twoPPeerMac g.twoPPeerMac
#define twoPPeerNonce g.twoPPeerNonce
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
  drawDirectionGlyphColor(dir, x, y, SSD1306_WHITE);
}

void drawDirectionGlyphColor(CardDir dir, int x, int y, uint16_t color)
{
  // x,y is the glyph center inside a card cell.
  if (dir == STRAIGHT)
  {
    display.drawLine(x, y + 3, x, y - 2, color);
    display.drawLine(x, y - 2, x - 2, y, color);
    display.drawLine(x, y - 2, x + 2, y, color);
  }
  else if (dir == DIAG_R)
  {
    display.drawLine(x - 2, y + 2, x + 2, y - 2, color);
    display.drawLine(x + 2, y - 2, x + 2, y + 1, color);
    display.drawLine(x + 2, y - 2, x - 1, y - 2, color);
  }
  else
  {
    display.drawLine(x + 2, y + 2, x - 2, y - 2, color);
    display.drawLine(x - 2, y - 2, x - 2, y + 1, color);
    display.drawLine(x - 2, y - 2, x + 1, y - 2, color);
  }
}

void drawCardNumberGlyph(uint8_t value, int x, int y)
{
  drawCardNumberGlyphColor(value, x, y, SSD1306_WHITE);
}

void drawCardNumberGlyphColor(uint8_t value, int x, int y, uint16_t color)
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
    display.drawLine(x, y, x + 2, y, color);
  if (b)
    display.drawLine(x + 2, y, x + 2, y + 2, color);
  if (c)
    display.drawLine(x + 2, y + 2, x + 2, y + 4, color);
  if (d)
    display.drawLine(x, y + 4, x + 2, y + 4, color);
  if (e)
    display.drawLine(x, y + 2, x, y + 4, color);
  if (f)
    display.drawLine(x, y, x, y + 2, color);
  if (g)
    display.drawLine(x, y + 2, x + 2, y + 2, color);
}

static bool useMirroredTwoPView()
{
  return gameMode == MODE_2P && localSeat == 1;
}

static int viewBoardX(int x)
{
  return useMirroredTwoPView() ? (BOARD_W - 1 - x) : x;
}

static int viewBoardY(int y)
{
  return useMirroredTwoPView() ? (BOARD_H - 1 - y) : y;
}

static CardDir viewCardDir(CardDir dir)
{
  if (!useMirroredTwoPView())
    return dir;
  if (dir == DIAG_R)
    return DIAG_L;
  if (dir == DIAG_L)
    return DIAG_R;
  return dir;
}

static const int BOARD_OX = 3;
static const int BOARD_OY = 6;
static const int BOARD_CELL_W = 8;
static const int BOARD_CELL_H = 9;

static void drawPreviewDot(int cx, int cy)
{
  // Keep the hint smaller than the actual ball.
  display.fillCircle(cx, cy, 1, SSD1306_WHITE);
}

static void drawCardTargetPreview(bool actorIsPlayer, const Card &c)
{
  if (!ballPlaced || c.used)
    return;

  int signY = actorIsPlayer ? -1 : 1;
  int dx = 0;
  if (c.dir == DIAG_R)
    dx = actorIsPlayer ? 1 : -1;
  if (c.dir == DIAG_L)
    dx = actorIsPlayer ? -1 : 1;

  int nx = ballX + dx * c.value;
  int ny = ballY + signY * c.value;

  int cx = BOARD_OX + viewBoardX(nx) * BOARD_CELL_W + (BOARD_CELL_W / 2);
  int cy = BOARD_OY + viewBoardY(ny) * BOARD_CELL_H + (BOARD_CELL_H / 2);

  // Keep out-of-board preview visible at the nearest edge.
  if (nx < 0)
    cx = useMirroredTwoPView() ? BOARD_OX + BOARD_CELL_W * BOARD_W + 2 : BOARD_OX - 2;
  else if (nx >= BOARD_W)
    cx = useMirroredTwoPView() ? BOARD_OX - 2 : BOARD_OX + BOARD_CELL_W * BOARD_W + 2;
  if (ny < 0)
    cy = useMirroredTwoPView() ? BOARD_OY + BOARD_CELL_H * BOARD_H + 2 : BOARD_OY - 2;
  else if (ny >= BOARD_H)
    cy = useMirroredTwoPView() ? BOARD_OY - 2 : BOARD_OY + BOARD_CELL_H * BOARD_H + 2;

  drawPreviewDot(cx, cy);
}

void drawLastPlayedCard(const Card &c, int x, int y, bool invert)
{
  if (c.used)
    return;
  uint16_t fg = invert ? SSD1306_BLACK : SSD1306_WHITE;
  if (invert)
    display.fillRect(x, y, 14, 10, SSD1306_WHITE);
  display.drawRect(x, y, 14, 10, SSD1306_WHITE);
  drawDirectionGlyphColor(viewCardDir(c.dir), x + 4, y + 4, fg);
  drawCardNumberGlyphColor(c.value, x + 9, y + 3, fg);
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
void sfxAttack()
{
  toneMs(1800, 25);
  delay(5);
  toneMs(2200, 35);
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
  if (e == SFX_WIN_EVT)
    return 4;
  if (e == SFX_CONFIRM_EVT)
    return 3;
  if (e == SFX_MISS_EVT)
    return 2;
  if (e == SFX_CLICK_EVT)
    return 1;
  return 0;
}

void emitSound(FrameEffects &fx, SoundEvent e)
{
  if (soundPriority(e) > soundPriority(fx.sound))
    fx.sound = e;
}

void emitNeo(FrameEffects &fx, NeoEvent e)
{
  if (e != NEO_NONE)
    fx.neo = e;
}

void applyEffects(const FrameEffects &fx)
{
  if (fx.neo == NEO_NORMAL_EVT)
    setNeoNormal();
  else if (fx.neo == NEO_WIN_EVT)
    setNeoWin();
  else if (fx.neo == NEO_LOSE_EVT)
    setNeoLose();

  if (fx.sound == SFX_CLICK_EVT)
    sfxClick();
  else if (fx.sound == SFX_CONFIRM_EVT)
    sfxConfirm();
  else if (fx.sound == SFX_WIN_EVT)
    sfxWin();
  else if (fx.sound == SFX_MISS_EVT)
    sfxMiss();
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

static const uint8_t kBroadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

enum TwoPMsgType : uint8_t
{
  TWO_P_HELLO = 1,
  TWO_P_START = 2,
  TWO_P_ACK = 3,
  TWO_P_SERVE_POS = 4,
  TWO_P_CARD = 5
};

struct __attribute__((packed)) TwoPMessage
{
  uint8_t type;
  uint8_t seatId;
  uint8_t serverId;
  uint8_t turnId;
  uint16_t turnIndex;
  uint32_t seed;
  uint32_t nonce;
  uint8_t servePos;
  uint8_t dir;
  uint8_t value;
  uint8_t reserved;
};

static volatile bool gTwoPPacketReady = false;
static TwoPMessage gTwoPPacket;
static uint8_t gTwoPPacketMac[6] = {0, 0, 0, 0, 0, 0};

bool sameMac(const uint8_t *a, const uint8_t *b)
{
  for (int i = 0; i < 6; ++i)
  {
    if (a[i] != b[i])
      return false;
  }
  return true;
}

bool macLess(const uint8_t *a, const uint8_t *b)
{
  for (int i = 0; i < 6; ++i)
  {
    if (a[i] != b[i])
      return a[i] < b[i];
  }
  return false;
}

void syncTwoPViewState()
{
  if (gameMode != MODE_2P)
    return;
  playerServe = (localSeat == serverSeat);
  playerTurn = (localSeat == turnSeat);
}

int findMatchingCardIndex(Card hand[], const Card &c)
{
  for (int i = 0; i < HAND_SIZE; ++i)
  {
    if (!hand[i].used && hand[i].dir == c.dir && hand[i].value == c.value)
      return i;
  }
  return -1;
}

bool applyCard(bool actorIsPlayer, const Card &c, bool animate = true);
void awardRound(bool playerWon, uint32_t nowMs, FrameEffects &fx);

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

bool hasServeValue(Card hand[])
{
  for (int i = 0; i < HAND_SIZE; ++i)
  {
    if (hand[i].value >= 3)
      return true;
  }
  return false;
}

void dealHands()
{
  while (true)
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
      Card seat0 = kDeck[order[i]];
      Card seat1 = kDeck[order[i + HAND_SIZE]];
      if (gameMode == MODE_2P && localSeat == 1)
      {
        playerHand[i] = seat1;
        cpuHand[i] = seat0;
      }
      else
      {
        playerHand[i] = seat0;
        cpuHand[i] = seat1;
      }
      playerHand[i].used = false;
      cpuHand[i].used = false;
    }

    Card *serveHand = playerServe ? playerHand : cpuHand;
    if (hasServeValue(serveHand))
      return;
  }
}

void twoPSendMessage(const uint8_t *mac, const TwoPMessage &msg)
{
  if (!twoPNetReady)
    return;
  esp_now_send(mac, reinterpret_cast<const uint8_t *>(&msg), sizeof(msg));
}

void twoPEnsurePeer(const uint8_t *mac)
{
  if (esp_now_is_peer_exist(mac))
    return;

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, mac, 6);
  peerInfo.channel = 1;
  peerInfo.encrypt = false;
  peerInfo.ifidx = WIFI_IF_STA;
  esp_now_add_peer(&peerInfo);
}

void twoPStartSession(uint32_t nowMs, FrameEffects &fx)
{
  if (twoPSessionActive)
    return;
  twoPSessionActive = true;
  gameMode = MODE_2P;
  syncTwoPViewState();
  randomSeed(twoPSessionSeed);
  resetGame(true, nowMs, fx);
}

void twoPHandlePacket(const uint8_t *mac, const TwoPMessage &msg, uint32_t nowMs, FrameEffects &fx)
{
  if (msg.type == TWO_P_HELLO)
  {
    memcpy(twoPPeerMac, mac, 6);
    twoPPeerNonce = msg.nonce;
    twoPConnected = true;
    twoPEnsurePeer(mac);
    uint8_t localMac[6];
    WiFi.macAddress(localMac);
    localSeat = macLess(localMac, twoPPeerMac) ? 0 : 1;
    twoPLeader = macLess(localMac, twoPPeerMac);
    syncTwoPViewState();
    return;
  }

  if (msg.type == TWO_P_START)
  {
    serverSeat = msg.serverId;
    turnSeat = msg.turnId;
    twoPSessionSeed = msg.seed;
    twoPConnected = true;
    twoPEnsurePeer(mac);
    syncTwoPViewState();
    twoPStartSession(nowMs, fx);
    TwoPMessage ackMsg = {};
    ackMsg.type = TWO_P_ACK;
    ackMsg.seed = twoPSessionSeed;
    twoPSendMessage(twoPPeerMac, ackMsg);
    return;
  }

  if (msg.type == TWO_P_ACK)
  {
    twoPConnected = true;
    twoPStartSession(nowMs, fx);
    return;
  }

  if (!twoPSessionActive || !twoPConnected)
    return;

  if (msg.type == TWO_P_SERVE_POS)
  {
    if (msg.turnIndex != turnCount)
      return;
    serveX = msg.servePos;
    ballX = serveX;
    ballY = playerServe ? 5 : 0;
    ballPlaced = true;
    phase = PHASE_CPU_CARD;
    phaseStartedAt = nowMs;
    return;
  }

  if (msg.type == TWO_P_CARD)
  {
    if (msg.turnIndex != turnCount)
      return;
    Card c{(CardDir)msg.dir, msg.value, false};
    int idx = findMatchingCardIndex(cpuHand, c);
    if (idx < 0)
      idx = firstUsableIndex(cpuHand);
    if (idx < 0)
      return;
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
    turnSeat ^= 1;
    syncTwoPViewState();
    playerTurn = true;
    phase = PHASE_PLAYER_CARD;
    phaseStartedAt = nowMs;
  }
}

void twoPProcessNetwork(uint32_t nowMs, FrameEffects &fx)
{
  if (!twoPNetReady)
    return;

  while (gTwoPPacketReady)
  {
    noInterrupts();
    TwoPMessage msg = gTwoPPacket;
    uint8_t mac[6];
    memcpy(mac, gTwoPPacketMac, 6);
    gTwoPPacketReady = false;
    interrupts();
    twoPHandlePacket(mac, msg, nowMs, fx);
  }

  if (phase != PHASE_2P_WAIT && !twoPSessionActive)
    return;

  if (nowMs - twoPLastHelloAt >= 400)
  {
    TwoPMessage hello = {};
    hello.type = TWO_P_HELLO;
    hello.nonce = twoPLocalNonce;
    hello.seatId = localSeat;
    twoPSendMessage(kBroadcastMac, hello);
    twoPLastHelloAt = nowMs;
  }

  if (twoPConnected && !twoPSessionActive && twoPLeader && nowMs - twoPLastStartAt >= 600)
  {
    if (twoPSessionSeed == 0)
    {
      twoPSessionSeed = esp_random();
      serverSeat = (twoPSessionSeed & 1) ? 1 : 0;
      turnSeat = serverSeat;
      syncTwoPViewState();
    }
    TwoPMessage startMsg = {};
    startMsg.type = TWO_P_START;
    startMsg.seatId = localSeat;
    startMsg.serverId = serverSeat;
    startMsg.turnId = turnSeat;
    startMsg.seed = twoPSessionSeed;
    twoPSendMessage(twoPPeerMac, startMsg);
    twoPLastStartAt = nowMs;
  }
}

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
static void twoPOnReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len)
#else
static void twoPOnReceive(const uint8_t *mac, const uint8_t *data, int len)
#endif
{
  if (len != (int)sizeof(TwoPMessage))
    return;
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  memcpy(gTwoPPacketMac, info->src_addr, 6);
#else
  memcpy(gTwoPPacketMac, mac, 6);
#endif
  memcpy((void *)&gTwoPPacket, data, sizeof(TwoPMessage));
  gTwoPPacketReady = true;
}

void twoPInitNetwork(uint32_t nowMs)
{
  if (twoPNetReady)
    return;

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  WiFi.setSleep(false);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK)
    return;

  esp_now_register_recv_cb(twoPOnReceive);

  esp_now_peer_info_t broadcastPeer = {};
  memcpy(broadcastPeer.peer_addr, kBroadcastMac, 6);
  broadcastPeer.channel = 1;
  broadcastPeer.encrypt = false;
  broadcastPeer.ifidx = WIFI_IF_STA;
  esp_now_add_peer(&broadcastPeer);

  twoPLocalNonce = esp_random();
  twoPLastHelloAt = nowMs;
  twoPLastStartAt = 0;
  localSeat = 0;
  serverSeat = 0;
  turnSeat = 0;
  twoPConnected = false;
  twoPSessionActive = false;
  twoPLeader = false;
  twoPSessionSeed = 0;
  twoPPeerNonce = 0;
  memset(twoPPeerMac, 0, sizeof(twoPPeerMac));
  gTwoPPacketReady = false;
  twoPNetReady = true;
}

void twoPShutdownNetwork()
{
  if (!twoPNetReady)
    return;
  esp_now_deinit();
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  twoPNetReady = false;
  twoPConnected = false;
  twoPSessionActive = false;
  twoPLeader = false;
}

void twoPSendServePos(uint8_t sx)
{
  if (!twoPNetReady || !twoPConnected || !twoPSessionActive)
    return;
  TwoPMessage msg = {};
  msg.type = TWO_P_SERVE_POS;
  msg.turnIndex = turnCount;
  msg.servePos = sx;
  twoPSendMessage(twoPPeerMac, msg);
}

void twoPSendCard(const Card &c)
{
  if (!twoPNetReady || !twoPConnected || !twoPSessionActive)
    return;
  TwoPMessage msg = {};
  msg.type = TWO_P_CARD;
  msg.turnIndex = turnCount;
  msg.dir = (uint8_t)c.dir;
  msg.value = c.value;
  twoPSendMessage(twoPPeerMac, msg);
}

void resetGame(bool keepServer, uint32_t nowMs, FrameEffects &fx)
{
  if (gameMode == MODE_2P)
  {
    syncTwoPViewState();
  }
  else if (!keepServer)
    playerServe = random(2) == 0;
  dealHands();
  turnCount = 0;
  lastPlayerCard.used = true;
  lastCpuCard.used = true;
  ballPlaced = false;
  serveX = 2;
  cardCursor = 0;
  cpuGhostCursor = 0;
  if (gameMode == MODE_2P)
  {
    turnSeat = serverSeat;
    syncTwoPViewState();
  }
  else
  {
    playerTurn = playerServe;
  }
  phase = PHASE_GAME_INTRO;
  phaseStartedAt = nowMs;
  emitNeo(fx, NEO_NORMAL_EVT);
}

bool applyCard(bool actorIsPlayer, const Card &c, bool animate)
{
  int signY = actorIsPlayer ? -1 : 1;
  int dx = 0;
  if (c.dir == DIAG_R)
    dx = actorIsPlayer ? 1 : -1;
  if (c.dir == DIAG_L)
    dx = actorIsPlayer ? -1 : 1;
  bool out = false;
  bool sideFail = false;

  if (animate)
  {
    for (uint8_t step = 0; step < c.value; ++step)
    {
      int nextX = ballX + dx;
      int nextY = ballY + signY;
      bool nextOut = nextX < 0 || nextX >= BOARD_W || nextY < 0 || nextY >= BOARD_H;
      ballX = (int8_t)nextX;
      ballY = (int8_t)nextY;
      renderGame(g, display);
      delay(BALL_STEP_ANIM_MS);
      if (nextOut)
      {
        out = true;
        break; // Show up to one cell outside the board.
      }
    }
  }
  else
  {
    int nx = ballX + dx * c.value;
    int ny = ballY + signY * c.value;
    ballX = (int8_t)nx;
    ballY = (int8_t)ny;
    out = nx < 0 || nx >= BOARD_W || ny < 0 || ny >= BOARD_H;
    if (out)
    {
      if (ballX < 0)
        ballX = -1;
      if (ballX >= BOARD_W)
        ballX = BOARD_W;
      if (ballY < 0)
        ballY = -1;
      if (ballY >= BOARD_H)
        ballY = BOARD_H;
    }
  }

  // Must reach opponent side in one shot.
  if (!out)
  {
    if (actorIsPlayer && ballY > 2)
      sideFail = true;
    if (!actorIsPlayer && ballY < 3)
      sideFail = true;
  }

  return !out && !sideFail;
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
  if (gameMode == MODE_2P)
  {
    serverSeat ^= 1;
    syncTwoPViewState();
  }
  else
  {
    playerServe = !playerServe;
  }
  resetGame(true, nowMs, fx);
}

void drawBoard()
{
  const int ox = BOARD_OX, oy = BOARD_OY, cellW = BOARD_CELL_W, cellH = BOARD_CELL_H;
  display.drawRect(ox, oy, cellW * BOARD_W + 1, cellH * BOARD_H + 1, SSD1306_WHITE);
  for (int i = 1; i < BOARD_W; ++i)
    display.drawLine(ox + i * cellW, oy, ox + i * cellW, oy + cellH * BOARD_H, SSD1306_WHITE);
  for (int i = 1; i < BOARD_H; ++i)
    display.drawLine(ox, oy + i * cellH, ox + cellW * BOARD_W, oy + i * cellH, SSD1306_WHITE);

  // Center horizontal line (net): thicker and slightly overhangs outside frame.
  int yMid = oy + cellH * 3;
  display.drawLine(ox - 3, yMid - 1, ox + cellW * BOARD_W + 3, yMid - 1, SSD1306_WHITE);
  display.drawLine(ox - 3, yMid, ox + cellW * BOARD_W + 3, yMid, SSD1306_WHITE);
  display.drawLine(ox - 3, yMid + 1, ox + cellW * BOARD_W + 3, yMid + 1, SSD1306_WHITE);

  // Center vertical line: bias thickness to the left side so visual cell widths stay even.
  int xMid = ox + cellW * 3;
  display.drawLine(xMid - 1, oy, xMid - 1, oy + cellH * BOARD_H, SSD1306_WHITE);
  display.drawLine(xMid, oy, xMid, oy + cellH * BOARD_H, SSD1306_WHITE);

  if (ballPlaced)
  {
    int cx = ox + viewBoardX(ballX) * cellW + (cellW / 2);
    int cy = oy + viewBoardY(ballY) * cellH + (cellH / 2);
    // Keep out-of-board ball visible at the nearest edge.
    if (ballX < 0)
      cx = useMirroredTwoPView() ? ox + cellW * BOARD_W + 2 : ox - 2;
    else if (ballX >= BOARD_W)
      cx = useMirroredTwoPView() ? ox - 2 : ox + cellW * BOARD_W + 2;
    if (ballY < 0)
      cy = useMirroredTwoPView() ? oy + cellH * BOARD_H + 2 : oy - 2;
    else if (ballY >= BOARD_H)
      cy = useMirroredTwoPView() ? oy - 2 : oy + cellH * BOARD_H + 2;
    display.fillCircle(cx, cy, 2, SSD1306_WHITE);
  }

  if (phase == PHASE_SERVE_CARD || phase == PHASE_PLAYER_CARD)
  {
    drawCardTargetPreview(true, playerHand[cardCursor]);
  }

  if (phase == PHASE_SERVE_POS)
  {
    int sx = ox + viewBoardX(serveX) * cellW;
    int sy = oy + viewBoardY(5) * cellH;
    // Serve selection: fill selected cell white, then cut out the ball in black.
    display.fillRect(sx + 1, sy + 1, cellW - 1, cellH - 1, SSD1306_WHITE);
    int pcx = sx + (cellW / 2);
    int pcy = sy + (cellH / 2);
    display.fillCircle(pcx, pcy, 2, SSD1306_BLACK);
  }
}

void drawHand(Card hand[], bool showCursor)
{
  int x = 57;
  int y = 40;
  const int cardW = 11;
  const int cardH = 18;
  const int gapX = 1;
  display.setTextSize(1);
  for (int i = 0; i < HAND_SIZE; ++i)
  {
    int cx = x + i * (cardW + gapX);
    int cy = y;
    if (hand[i].used)
      continue;
    display.drawRect(cx, cy, cardW, cardH, SSD1306_WHITE);
    if (showCursor && i == cardCursor)
      display.drawRect(cx - 1, cy - 1, cardW + 2, cardH + 2, SSD1306_WHITE);
    // Vertical layout: direction on top, value below.
    drawDirectionGlyph(viewCardDir(hand[i].dir), cx + 5, cy + 5);
    drawCardNumberGlyph(hand[i].value, cx + 4, cy + 12);
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
    display.setCursor(106, TOP_INFO_Y);
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
    display.setCursor(31, 38);
    display.print(titleCursor == 0 ? ">CPU" : " CPU");
    display.setCursor(73, 38);
    display.print(titleCursor == 1 ? ">2P" : " 2P");
  }
  else if (phase == PHASE_2P_WAIT)
  {
    display.setTextSize(1);
    display.setCursor(18, 18);
    display.print("2P CONNECTING");
    display.setCursor(18, 30);
    display.print(twoPConnected ? "PEER FOUND" : "SEARCHING...");
    display.setCursor(18, 44);
    display.print(twoPSessionActive ? "READY" : "WAITING");
  }
  else if (phase == PHASE_GAME_INTRO)
  {
    display.setTextSize(1);
    display.setCursor(44, 20);
    display.print("Game");
    display.print((int)(playerGames + cpuGames + 1));
    display.setCursor(24, 32);
    display.print("1P ");
    display.print(playerGames);
    display.print(" - ");
    display.print(cpuGames);
    display.print(gameMode == MODE_CPU ? " CPU" : " 2P");
    display.setCursor(46, 44);
    display.print(playerServe ? "Serve" : "Receive");
  }
  else if (phase == PHASE_MATCH_OVER)
  {
    display.setTextSize(1);
    display.setCursor(20, 20);
    if (playerGames >= 3)
      display.print("YOU WIN MATCH");
    else
      display.print(gameMode == MODE_CPU ? "CPU WIN MATCH" : "2P WIN MATCH");
    display.setCursor(24, 30);
    display.print("1P ");
    display.print(playerGames);
    display.print(" - ");
    display.print(cpuGames);
    display.print(gameMode == MODE_CPU ? " CPU" : " 2P");
    display.setCursor(20, 42);
    display.print("UP: RETRY");
  }
  else
  {
    drawHUD();
    drawBoard();
    if (phase != PHASE_GAME_OVER)
    {
      drawLastPlayedCard(lastPlayerCard, 56, TOP_INFO_Y + 2, false);
      drawLastPlayedCard(lastCpuCard, 72, TOP_INFO_Y + 2, true);
    }
    if (phase == PHASE_SERVE_POS)
    {
      display.setCursor(96, TOP_LABEL_Y);
      display.print("Serve");
    }
    else if (phase == PHASE_SERVE_CARD)
    {
      display.setCursor(96, TOP_LABEL_Y);
      display.print("Serve");
    }
    else if (phase == PHASE_CPU_CARD)
    {
      display.setCursor(106, TOP_LABEL_Y);
      display.print(gameMode == MODE_CPU ? "CPU" : "2P");
    }
    else if (phase == PHASE_PLAYER_CARD)
    {
      display.setCursor(104, TOP_LABEL_Y);
      display.print("1P");
    }
    if (phase == PHASE_GAME_OVER)
    {
      display.setCursor(54, 10);
      display.print("ROUND END");
    }
    // Keep the local hand visible, but only show the cursor on the local turn.
    drawHand(playerHand, phase == PHASE_SERVE_CARD || phase == PHASE_PLAYER_CARD);
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
  twoPSendCard(c);
  sfxAttack();
  // Clear the preview before the real ball motion starts.
  renderGame(g, display);
  display.display();
  bool ok = applyCard(true, c);
  if (!ok)
  {
    emitSound(fx, SFX_MISS_EVT);
    awardRound(false, nowMs, fx);
    return;
  }
  emitSound(fx, SFX_CONFIRM_EVT);
  turnCount++;
  if (gameMode == MODE_2P)
  {
    turnSeat ^= 1;
    syncTwoPViewState();
  }
  else
  {
    playerTurn = false;
  }
  phase = PHASE_CPU_CARD;
  phaseStartedAt = nowMs;
  cpuActionDelayMs = randomCpuDelayMs();
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
      int8_t bx = ballX, by = ballY;
      if (applyCard(false, test, false))
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
  if (gameMode == MODE_2P)
  {
    turnSeat ^= 1;
    syncTwoPViewState();
  }
  else
  {
    playerTurn = true;
  }
  phase = PHASE_PLAYER_CARD;
  phaseStartedAt = nowMs;
}

uint32_t randomCpuDelayMs()
{
  return (uint32_t)random(10000, 20001);
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
  if (gameMode == MODE_2P)
    twoPProcessNetwork(nowMs, fx);
  if (phase == PHASE_TITLE)
  {
    if (in.leftPressed)
    {
      if (titleCursor != 0)
        emitSound(fx, SFX_CLICK_EVT);
      titleCursor = 0;
    }
    if (in.rightPressed)
    {
      if (titleCursor != 1)
        emitSound(fx, SFX_CLICK_EVT);
      titleCursor = 1;
    }
    if (in.upPressed && (nowMs - bootAtMs >= TITLE_MIN_SHOW_MS))
    {
      playerGames = 0;
      cpuGames = 0;
      gameMode = titleCursor == 0 ? MODE_CPU : MODE_2P;
      if (gameMode == MODE_CPU)
      {
        resetGame(false, nowMs, fx);
        emitSound(fx, SFX_CONFIRM_EVT);
      }
      else
      {
        twoPInitNetwork(nowMs);
        phase = PHASE_2P_WAIT;
        phaseStartedAt = nowMs;
        emitSound(fx, SFX_CONFIRM_EVT);
      }
    }
  }
  else if (phase == PHASE_GAME_INTRO)
  {
    if (nowMs - phaseStartedAt >= 1000)
    {
      phase = playerTurn ? PHASE_SERVE_POS : PHASE_CPU_CARD;
      phaseStartedAt = nowMs;
      if (phase == PHASE_CPU_CARD)
      {
        // CPU serves first in this game: place the serve ball before card play.
        serveX = (uint8_t)random(0, BOARD_W);
        ballX = serveX;
        ballY = 0;
        ballPlaced = true;
        cpuActionDelayMs = randomCpuDelayMs();
      }
    }
  }
  else if (phase == PHASE_MATCH_OVER)
  {
    if (in.upPressed)
    {
      if (gameMode == MODE_2P)
        twoPShutdownNetwork();
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
      twoPSendServePos(serveX);
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
    if (gameMode == MODE_2P)
    {
      // Opponent turn: keep the local view passive.
      return fx;
    }
    // CPU mode only: let the local player watch the CPU selection.
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
    if (nowMs - phaseStartedAt >= cpuActionDelayMs)
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
