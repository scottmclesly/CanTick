#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>

#include <SPI.h>
#include "driver/spi_master.h"
#include "driver/gpio.h"

#include "mcp2515.h"
#include <Adafruit_NeoPixel.h>

// --------------------- USER CONFIG ---------------------
// Set false to run the display and CAN without the WiFi radio. Use this to
// confirm the panel is stable on its own; a failing WiFi connect can reset
// the board while the LEDs are driven. Turn back on once WiFi is confirmed.
constexpr bool ENABLE_WIFI = false;

const char* WIFI_SSID     = "iSENSYS";
const char* WIFI_PASSWORD = "unmanned";

// NOTE: this string looks malformed. IPAddress::fromString will reject the
// "=39" and leave the peer at 0.0.0.0, which silently breaks UDP transmit.
// It should almost certainly read "192.168.0.5". Left as you had it.
const char* PEER_IP  = "192.168.0.5=39";
const uint16_t PEER_PORT  = 54701;
const uint16_t LOCAL_PORT = 54700;

const CAN_SPEED  CAN_BITRATE = CAN_125KBPS;
const CAN_CLOCK  MCP_CLK     = MCP_8MHZ;

#define PIN_MOSI  GPIO_NUM_6
#define PIN_MISO  GPIO_NUM_5
#define PIN_SCK   GPIO_NUM_4
#define PIN_CS    GPIO_NUM_7
#define PIN_INT   GPIO_NUM_3

// --------------------- MATRIX CONFIG ---------------------
// Feedback panel on D0 (GPIO2), clear of the SPI and INT pins above.
constexpr uint8_t MATRIX_PIN    = D0;
constexpr int     MATRIX_WIDTH  = 10;
constexpr int     MATRIX_HEIGHT = 6;

// Rotation stays 0: render through preset 11 alone, the exact path that drew
// HELLO WORLD upright. Non-zero values mirror because preset 11 already
// transposes, so leave this at 0.
constexpr int CANVAS_ROTATE = 0;
constexpr int CANVAS_W = (CANVAS_ROTATE == 90 || CANVAS_ROTATE == 270) ? MATRIX_HEIGHT : MATRIX_WIDTH;
constexpr int CANVAS_H = (CANVAS_ROTATE == 90 || CANVAS_ROTATE == 270) ? MATRIX_WIDTH  : MATRIX_HEIGHT;
constexpr int     LED_COUNT     = 60;
constexpr uint8_t BRIGHTNESS    = 16;

// Orientation map for the mounted panel: preset 10 = flipY, columnMajor.
constexpr uint8_t LOCKED_PRESET = 10;
constexpr bool    CALIBRATE     = false;

// Scroll one step every STEP_MS. SCROLL_DIR: +1 travels left to right,
// -1 travels right to left (the HELLO WORLD green direction). You said green
// revealed backwards, so this defaults to +1. Flip if it still reads wrong.
constexpr uint16_t STEP_MS   = 45;
constexpr int      SCROLL_DIR = +1;

// Re-scroll the current status every few seconds while idle, so the panel
// visibly lives on the bench. Set false for a quiet field unit.
constexpr bool     IDLE_REANNOUNCE = true;
constexpr uint32_t REANNOUNCE_MS   = 8000;

// Set 0 if mcp2515.getErrorFlags() is not in your library. Activity based bus
// liveness still works without it; only bus-off and warn detection is lost.
#define USE_MCP_ERROR_FLAGS 1

static const size_t PKT_SIZE = 13;

WiFiUDP udp;
IPAddress peer;

spi_device_handle_t spi_handle = nullptr;
MCP2515 mcp2515(&spi_handle);

Adafruit_NeoPixel pixels(LED_COUNT, MATRIX_PIN, NEO_GRB + NEO_KHZ800);

// --------------------- feedback state ---------------------
enum BusState { BUS_SILENT, BUS_OK, BUS_WARN, BUS_OFF };

struct Msg { const char* text; uint32_t color; };

volatile uint32_t lastRx = 0;
volatile uint32_t lastTx = 0;
bool     wifiUp   = false;
bool     canReady = false;
BusState busState = BUS_SILENT;

uint32_t fb[MATRIX_HEIGHT][MATRIX_WIDTH];
uint32_t shown[MATRIX_HEIGHT][MATRIX_WIDTH];
bool     shownInit = false;

inline uint32_t C(uint8_t r, uint8_t g, uint8_t b) { return pixels.Color(r, g, b); }

// --------------------- your mapping, unchanged ---------------------
int xyToIndex(int x, int y, uint8_t preset)
{
  if (x < 0 || x >= MATRIX_WIDTH || y < 0 || y >= MATRIX_HEIGHT) return -1;

  const bool flipX = preset & 0x01;
  const bool flipY = preset & 0x02;
  const bool zigzag = preset & 0x04;
  const bool columnMajor = preset & 0x08;

  if (flipX) x = MATRIX_WIDTH - 1 - x;
  if (flipY) y = MATRIX_HEIGHT - 1 - y;

  if (!columnMajor)
  {
    if (zigzag && (y & 1)) x = MATRIX_WIDTH - 1 - x;
    return y * MATRIX_WIDTH + x;
  }
  if (zigzag && (x & 1)) y = MATRIX_HEIGHT - 1 - y;
  return x * MATRIX_HEIGHT + y;
}

void setPixFB(int x, int y, uint32_t c)
{
  if (x < 0 || x >= MATRIX_WIDTH || y < 0 || y >= MATRIX_HEIGHT) return;
  fb[y][x] = c;
}
// Draw in reading-orientation canvas coords; this rotates onto the panel.
void setCanvas(int cx, int cy, uint32_t c)
{
  int fx, fy;
  switch (CANVAS_ROTATE)
  {
    case 90:  fx = cy;                    fy = CANVAS_W - 1 - cx;      break;
    case 180: fx = MATRIX_WIDTH - 1 - cx; fy = MATRIX_HEIGHT - 1 - cy; break;
    case 270: fx = MATRIX_WIDTH - 1 - cy; fy = cx;                     break;
    default:  fx = cx;                    fy = cy;                     break;
  }
  setPixFB(fx, fy, c);
}
void clearFB()
{
  for (int y = 0; y < MATRIX_HEIGHT; y++)
    for (int x = 0; x < MATRIX_WIDTH; x++) fb[y][x] = 0;
}
void pushMatrix()
{
  bool changed = !shownInit;
  for (int y = 0; y < MATRIX_HEIGHT; y++)
    for (int x = 0; x < MATRIX_WIDTH; x++)
      if (fb[y][x] != shown[y][x]) changed = true;
  if (!changed) return;                       // keep the interrupt-off window rare
  for (int y = 0; y < MATRIX_HEIGHT; y++)
    for (int x = 0; x < MATRIX_WIDTH; x++)
    {
      int i = xyToIndex(x, y, LOCKED_PRESET);
      if (i >= 0 && i < LED_COUNT) pixels.setPixelColor(i, fb[y][x]);
      shown[y][x] = fb[y][x];
    }
  pixels.show();
  shownInit = true;
}

// --------------------- 3x5 font, column encoded, bit 0 top ---------------------
const uint8_t FONT_DIGITS[10][3] = {
  {31,17,31},{18,31,16},{25,21,18},{17,21,31},{7,4,31},
  {23,21,9},{31,21,29},{1,29,3},{31,21,31},{23,21,31}
};
const uint8_t FONT_ALPHA[26][3] = {
  {30,5,30},{31,21,10},{14,17,17},{31,17,14},{31,21,17},{31,5,1},{14,17,29},{31,4,31},
  {17,31,17},{8,16,15},{31,10,17},{31,16,16},{31,2,31},{23,10,29},{14,17,14},{31,5,2},
  {14,17,30},{31,5,26},{18,21,9},{1,31,1},{15,16,15},{7,24,7},{31,8,31},{27,4,27},
  {3,28,3},{25,21,19}
};
void glyph(char ch, uint8_t o[3])
{
  if (ch >= 'a' && ch <= 'z') ch -= 32;
  if (ch >= '0' && ch <= '9')      { o[0]=FONT_DIGITS[ch-'0'][0]; o[1]=FONT_DIGITS[ch-'0'][1]; o[2]=FONT_DIGITS[ch-'0'][2]; }
  else if (ch >= 'A' && ch <= 'Z') { o[0]=FONT_ALPHA[ch-'A'][0]; o[1]=FONT_ALPHA[ch-'A'][1]; o[2]=FONT_ALPHA[ch-'A'][2]; }
  else { o[0]=o[1]=o[2]=0; }
}
void plotGlyph(char ch, int ox, int oy, uint32_t color)
{
  uint8_t g[3];
  glyph(ch, g);
  for (int gx = 0; gx < 3; gx++)
    for (int gy = 0; gy < 5; gy++)
      if (g[gx] & (1 << gy)) setCanvas(ox + gx, oy + gy, color);
}
void drawText(const char* s, int x0, uint32_t c)
{
  int x = x0;
  while (*s) { plotGlyph(*s, x, 0, c); x += 4; s++; }   // rows 0..4, strip owns row 5
}
int textLen(const char* s) { int n = strlen(s); return n <= 0 ? 0 : n * 4 - 1; }

// --------------------- message queue ---------------------
Msg mq[8];
int mqHead = 0, mqTail = 0;
void enqueue(const char* t, uint32_t c)
{
  int n = (mqTail + 1) % 8;
  if (n == mqHead) return;                     // full, drop the newest
  mq[mqTail].text = t; mq[mqTail].color = c;
  mqTail = n;
  Serial.printf("feedback: %s\n", t);
}
bool dequeue(Msg& m)
{
  if (mqHead == mqTail) return false;
  m = mq[mqHead]; mqHead = (mqHead + 1) % 8; return true;
}

// --------------------- scroll state ---------------------
bool     scrolling = false;
Msg      cur;
int      curLen = 0;
int      scrollOff = 0;

void startMessage(const Msg& m)
{
  cur = m; curLen = textLen(m.text); scrolling = true;
  scrollOff = (SCROLL_DIR > 0) ? -curLen : CANVAS_W;
}

const char* bitrateLabel(CAN_SPEED s)
{
  switch (s)
  {
    case CAN_125KBPS:  return "125K";
    case CAN_250KBPS:  return "250K";
    case CAN_500KBPS:  return "500K";
    case CAN_1000KBPS: return "1M";
    default:           return "CAN";
  }
}

// --------------------- status strip on row 5 ---------------------
void drawStrip()
{
  bool bl = (millis() / 300) % 2;
  int row = CANVAS_H - 1;
  setCanvas(0, row, wifiUp ? C(0, 200, 0) : (bl ? C(150, 0, 0) : 0));   // wifi link

  uint32_t busc;
  switch (busState)
  {
    case BUS_OK:   busc = C(0, 200, 0);   break;
    case BUS_WARN: busc = C(200, 130, 0); break;
    case BUS_OFF:  busc = bl ? C(220, 0, 0) : C(60, 0, 0); break;
    default:       busc = C(90, 90, 90);  break;                     // idle, dim white
  }
  setCanvas(1, row, busc);

  if (millis() - lastRx < 120) setCanvas(3, row, C(0, 220, 0));   // receive pulse
  if (millis() - lastTx < 120) setCanvas(4, row, C(0, 0, 220));   // transmit pulse
  setCanvas(CANVAS_W - 1, row, bl ? C(120, 120, 120) : 0);        // alive heartbeat
}

void renderFrame()
{
  clearFB();
  if (scrolling) drawText(cur.text, scrollOff, cur.color);
  drawStrip();
}

void matrixTick()
{
  static uint32_t tRender = 0;
  uint32_t now = millis();
  if (now - tRender < STEP_MS) return;
  tRender = now;

  if (!scrolling) { Msg m; if (dequeue(m)) startMessage(m); }
  renderFrame();
  pushMatrix();

  if (scrolling)
  {
    scrollOff += SCROLL_DIR;
    bool done = (SCROLL_DIR > 0) ? (scrollOff > CANVAS_W) : (scrollOff < -curLen);
    if (done) scrolling = false;
  }
}

// --------------------- event detection ---------------------
void checkWifi()
{
  if (!ENABLE_WIFI) return;
  bool up = (WiFi.status() == WL_CONNECTED);
  if (up != wifiUp)
  {
    wifiUp = up;
    if (up) { udp.begin(LOCAL_PORT); peer.fromString(PEER_IP); }   // was in wifi_connect
    enqueue(up ? "LINK UP" : "LINK DOWN", up ? C(0, 180, 0) : C(180, 90, 0));
  }
}
void reannounce()
{
  switch (busState)
  {
    case BUS_OFF:  enqueue("BUS OFF",  C(200, 0, 0));   break;
    case BUS_WARN: enqueue("BUS WARN", C(200, 130, 0)); break;
    case BUS_OK:   enqueue("BUS OK",   C(0, 180, 0));   break;
    default:
      if (!ENABLE_WIFI) enqueue(canReady ? bitrateLabel(CAN_BITRATE) : "NO CAN",
                                canReady ? C(0, 180, 0) : C(200, 0, 0));
      else              enqueue(wifiUp ? "LINK UP" : "NO LINK",
                                wifiUp ? C(0, 180, 0) : C(180, 90, 0));
      break;
  }
}
BusState computeBus()
{
  if (!canReady) return BUS_SILENT;
  bool alive = (millis() - lastRx) < 1500;
#if USE_MCP_ERROR_FLAGS
  uint8_t ef = mcp2515.getErrorFlags();
  if (ef & MCP2515::EFLG_TXBO) return BUS_OFF;
  if (ef & (MCP2515::EFLG_TXEP | MCP2515::EFLG_RXEP | MCP2515::EFLG_EWARN)) return BUS_WARN;
#endif
  return alive ? BUS_OK : BUS_SILENT;
}
void checkBus()
{
  static uint32_t tBus = 0;
  if (millis() - tBus < 250) return;
  tBus = millis();
  BusState s = computeBus();
  if (s == busState) return;
  busState = s;
  switch (s)
  {
    case BUS_OFF:  enqueue("BUS OFF",  C(200, 0, 0));   break;
    case BUS_WARN: enqueue("BUS WARN", C(180, 120, 0)); break;
    case BUS_OK:   enqueue("BUS OK",   C(0, 180, 0));   break;
    default:       break;                               // silent needs no message
  }
}

// --------------------- your bridge, unchanged ---------------------
bool init_spi_for_mcp2515()
{
  spi_bus_config_t buscfg = {};
  buscfg.mosi_io_num = PIN_MOSI;
  buscfg.miso_io_num = PIN_MISO;
  buscfg.sclk_io_num = PIN_SCK;
  buscfg.quadwp_io_num = -1;
  buscfg.quadhd_io_num = -1;
  buscfg.max_transfer_sz = 64;

  esp_err_t err = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return false;

  spi_device_interface_config_t devcfg = {};
  devcfg.clock_speed_hz = 8 * 1000 * 1000;
  devcfg.mode = 0;
  devcfg.spics_io_num = PIN_CS;
  devcfg.queue_size = 5;

  err = spi_bus_add_device(SPI2_HOST, &devcfg, &spi_handle);
  return (err == ESP_OK);
}

void wifi_start()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);   // non-blocking; checkWifi finishes on connect
}

void bootFlash()
{
  clearFB();
  plotGlyph('F', 0, 0, C(0, 150, 0));   // asymmetric letter: reveals rotation and mirror at a glance
  pushMatrix();
  delay(2000);
  clearFB();
  pushMatrix();
}

// Draw an upright logical F straight through the given preset. No canvas layer.
void showFwithPreset(uint8_t p)
{
  pixels.clear();
  uint8_t g[3];
  glyph('F', g);
  for (int gx = 0; gx < 3; gx++)
    for (int gy = 0; gy < 5; gy++)
      if (g[gx] & (1 << gy))
      {
        int i = xyToIndex(gx, gy, p);
        if (i >= 0 && i < LED_COUNT) pixels.setPixelColor(i, C(0, 150, 0));
      }
  pixels.show();
}
void calibrateCycle()
{
  static const uint8_t ps[] = {0, 1, 2, 3, 8, 9, 10, 11};   // the 8 orientations, zigzag off
  static int k = 0;
  static uint32_t t = 0;
  if (millis() - t < 2500) return;
  t = millis();
  Serial.printf("CAL preset %u  (upright F here means LOCKED_PRESET = %u)\n", ps[k], ps[k]);
  showFwithPreset(ps[k]);
  k = (k + 1) % 8;
}

void sendFrameUDP(const struct can_frame &fr)
{
  uint8_t pkt[PKT_SIZE] = {0};
  uint32_t id = (fr.can_id & CAN_EFF_MASK);
  if (fr.can_id & CAN_EFF_FLAG) id |= 0x80000000UL;
  if (fr.can_id & CAN_RTR_FLAG) id |= 0x40000000UL;
  pkt[0] = (id >> 24) & 0xFF;
  pkt[1] = (id >> 16) & 0xFF;
  pkt[2] = (id >> 8)  & 0xFF;
  pkt[3] = (id)       & 0xFF;
  pkt[4] = fr.can_dlc & 0x0F;
  memcpy(&pkt[5], fr.data, min(8, (int)fr.can_dlc));
  udp.beginPacket(peer, PEER_PORT);
  udp.write(pkt, PKT_SIZE);
  udp.endPacket();
}

bool recvFrameUDP(struct can_frame &fr)
{
  int psize = udp.parsePacket();
  if (psize < (int)PKT_SIZE) return false;
  uint8_t pkt[64];
  int n = udp.read(pkt, sizeof(pkt));
  if (n < (int)PKT_SIZE) return false;
  uint32_t id = (uint32_t(pkt[0])<<24) | (uint32_t(pkt[1])<<16) |
                (uint32_t(pkt[2])<<8)  | (uint32_t(pkt[3]));
  bool ext = id & 0x80000000UL;
  bool rtr = id & 0x40000000UL;
  id &= 0x1FFFFFFFUL;
  memset(&fr, 0, sizeof(fr));
  fr.can_dlc = pkt[4] & 0x0F;
  fr.can_id = id | (ext ? CAN_EFF_FLAG : 0) | (rtr ? CAN_RTR_FLAG : 0);
  memcpy(fr.data, &pkt[5], min(8, (int)fr.can_dlc));
  return true;
}

void setup()
{
  Serial.begin(115200);
  delay(200);
  Serial.printf("boot: reset_reason=%d  (4=panic 6=task_wdt 9=brownout 1=poweron 3=sw)\n",
                (int)esp_reset_reason());

  pixels.begin();
  pixels.setBrightness(BRIGHTNESS);
  pixels.clear();
  pixels.show();

  if (CALIBRATE) return;   // calibration cycle runs in loop; skip CAN and WiFi

  bootFlash();

  pinMode(PIN_INT, INPUT_PULLUP);

  bool ok = init_spi_for_mcp2515();
  if (ok)
  {
    mcp2515.reset();
    ok = (mcp2515.setBitrate(CAN_BITRATE, MCP_CLK) == MCP2515::ERROR_OK) &&
         (mcp2515.setNormalMode() == MCP2515::ERROR_OK);
  }
  canReady = ok;

  if (canReady) enqueue(bitrateLabel(CAN_BITRATE), C(0, 180, 0));   // scrolls once at boot
  else          enqueue("NO CAN", C(200, 0, 0));                    // controller absent, display still runs

  if (ENABLE_WIFI) wifi_start();                       // does not block; link is announced when it connects
}

void loop()
{
  if (CALIBRATE) { calibrateCycle(); return; }

  if (canReady && digitalRead(PIN_INT) == LOW)
  {
    struct can_frame rx;
    while (mcp2515.readMessage(&rx) == MCP2515::ERROR_OK)
    {
      if (wifiUp) sendFrameUDP(rx);
      lastRx = millis();
    }
  }

  if (canReady && wifiUp)
  {
    struct can_frame tx;
    if (recvFrameUDP(tx))
    {
      mcp2515.sendMessage(&tx);
      lastTx = millis();
    }
  }

  checkWifi();
  checkBus();

  static uint32_t tReann = 0;
  if (IDLE_REANNOUNCE && !scrolling && mqHead == mqTail && millis() - tReann > REANNOUNCE_MS)
  {
    tReann = millis();
    reannounce();
  }

  matrixTick();
}
