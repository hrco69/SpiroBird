#include "GameRenderer.h"

// 565 colors (sprite converts to 8-bit automatically)
static const uint16_t COL_SKY        = 0x0926;   // dark blue night sky
static const uint16_t COL_SKY_DANGER = 0x4000;   // dark red tint
static const uint16_t COL_BAND       = 0x0260;   // dark green target band
static const uint16_t COL_LINE_LOW   = 0x8410;   // grey 600 line
static const uint16_t COL_LINE_MIN   = TFT_GREEN;
static const uint16_t COL_LINE_MAX   = TFT_RED;
static const uint16_t COL_PIPE       = 0x2E0B;
static const uint16_t COL_PIPE_RIM   = 0x05E0;
static const uint16_t COL_BIRD       = TFT_YELLOW;
static const uint16_t COL_BIRD_WING  = 0xFD20;   // orange
static const uint16_t COL_HUD_BG     = 0x10A2;

void GameRenderer::begin() {
  _tft.init();
  _tft.setRotation(DISPLAY_ROTATION);   // landscape 320x240
  _tft.fillScreen(TFT_BLACK);

  // Full-screen double buffer at 8-bit depth: 320*240 = 76.8 KB in SRAM.
  _spr.setColorDepth(8);
  _useSprite = _spr.createSprite(SCREEN_W, SCREEN_H) != nullptr;
  if (!_useSprite) {
    DBG("[gfx] WARNING: sprite allocation failed -> direct drawing (flicker)\n");
  } else {
    DBG("[gfx] sprite double buffer ready (%dx%d @8bpp)\n", SCREEN_W, SCREEN_H);
  }
}

int GameRenderer::flowToY(float flowMlS) {
  if (flowMlS < 0) flowMlS = 0;
  if (flowMlS > FLOW_MAX_ML_S) flowMlS = FLOW_MAX_ML_S;
  float t = flowMlS / FLOW_MAX_ML_S;   // 0..1
  return GAME_BOTTOM - (int)(t * (GAME_BOTTOM - GAME_TOP));
}

void GameRenderer::resetGame() {
  _birdY = (float)flowToY(0.0f);
  _birdVel = 0.0f;
  _pipeScroll = 0.0f;
}

void GameRenderer::clear(uint16_t color565) {
  if (_useSprite) _spr.fillSprite(color565);
  else            _tft.fillScreen(color565);
}

void GameRenderer::present() {
  if (_useSprite) _spr.pushSprite(0, 0);
}

// ----------------------------------------------------------------------------

void GameRenderer::drawGame(const SpiroPacket &p, uint32_t nowMs) {
  drawBackground(p);
  drawZones(p);
  drawPipes();
  drawBird(p, nowMs);
  drawStabilityBar(p);
  drawHud(p);
}

void GameRenderer::drawBackground(const SpiroPacket &p) {
  TFT_eSPI *g = gfx();
  clear(TFT_BLACK);
  // Sky: red-ish when in danger, dark blue otherwise.
  g->fillRect(0, GAME_TOP, SCREEN_W, GAME_BOTTOM - GAME_TOP,
              p.dangerZone ? COL_SKY_DANGER : COL_SKY);
  // A few stars for depth.
  for (int i = 0; i < 12; i++) {
    int x = (i * 53 + 17) % SCREEN_W;
    int y = GAME_TOP + ((i * 37 + 11) % (GAME_BOTTOM - GAME_TOP));
    g->drawPixel(x, y, 0x6B4D);
  }
}

void GameRenderer::drawZones(const SpiroPacket &p) {
  TFT_eSPI *g = gfx();
  const int yLow = flowToY(FLOW_LINE_LOW_ML_S);
  const int yMin = flowToY(FLOW_TARGET_MIN_ML_S);
  const int yMax = flowToY(FLOW_TARGET_MAX_ML_S);

  // Target corridor 900-1200 (yMax is ABOVE yMin on screen).
  g->fillRect(0, yMax, SCREEN_W, yMin - yMax, p.targetZone ? 0x0320 : COL_BAND);

  // Guide lines with labels.
  for (int x = 0; x < SCREEN_W; x += 8) {           // dashed 600 line
    g->drawFastHLine(x, yLow, 4, COL_LINE_LOW);
  }
  g->drawFastHLine(0, yMin, SCREEN_W, COL_LINE_MIN);
  g->drawFastHLine(0, yMax, SCREEN_W, COL_LINE_MAX);

  g->setTextFont(1);
  g->setTextDatum(BL_DATUM);
  g->setTextColor(COL_LINE_LOW);  g->drawString("600",  SCREEN_W - 24, yLow - 2);
  g->setTextColor(COL_LINE_MIN);  g->drawString("900",  SCREEN_W - 24, yMin - 2);
  g->setTextColor(COL_LINE_MAX);  g->drawString("1200", SCREEN_W - 28, yMax - 2);
}

void GameRenderer::drawPipes() {
  TFT_eSPI *g = gfx();
  _pipeScroll += PIPE_SPEED_PX;
  if (_pipeScroll >= PIPE_SPACING_PX) _pipeScroll -= PIPE_SPACING_PX;

  const int gapTop    = flowToY(FLOW_TARGET_MAX_ML_S) - PIPE_GAP_MARGIN_PX;
  const int gapBottom = flowToY(FLOW_TARGET_MIN_ML_S) + PIPE_GAP_MARGIN_PX;

  // Pipes flow right-to-left; the gap matches the target corridor so the
  // "level design" itself teaches the correct breath flow.
  for (int x = SCREEN_W - (int)_pipeScroll; x > -PIPE_WIDTH_PX; x -= PIPE_SPACING_PX) {
    if (x <= BIRD_X + 14 && x + PIPE_WIDTH_PX >= BIRD_X - 14) continue; // keep bird visible
    g->fillRect(x, GAME_TOP, PIPE_WIDTH_PX, gapTop - GAME_TOP, COL_PIPE);
    g->fillRect(x, gapBottom, PIPE_WIDTH_PX, GAME_BOTTOM - gapBottom, COL_PIPE);
    g->drawRect(x, GAME_TOP, PIPE_WIDTH_PX, gapTop - GAME_TOP, COL_PIPE_RIM);
    g->drawRect(x, gapBottom, PIPE_WIDTH_PX, GAME_BOTTOM - gapBottom, COL_PIPE_RIM);
  }
}

void GameRenderer::drawBird(const SpiroPacket &p, uint32_t nowMs) {
  TFT_eSPI *g = gfx();

  // Inertia: the bird chases the Y that corresponds to the filtered flow.
  const float targetY = (float)flowToY(p.filteredFlowMlS);
  _birdVel += (targetY - _birdY) * BIRD_SPRING;
  _birdVel *= BIRD_DAMPING;
  _birdY   += _birdVel;
  if (_birdY < GAME_TOP + 8)    _birdY = GAME_TOP + 8;
  if (_birdY > GAME_BOTTOM - 8) _birdY = GAME_BOTTOM - 8;

  const int x = BIRD_X;
  const int y = (int)_birdY;
  const bool flapUp = (nowMs / 120) % 2 == 0;   // wing flap every 120 ms

  // Simple geometric pixel-art bird (no external assets).
  g->fillCircle(x, y, 8, COL_BIRD);                          // body
  if (flapUp) g->fillTriangle(x - 2, y, x - 10, y - 9, x - 12, y - 1, COL_BIRD_WING);
  else        g->fillTriangle(x - 2, y, x - 10, y + 9, x - 12, y + 1, COL_BIRD_WING);
  g->fillTriangle(x + 7, y - 2, x + 14, y, x + 7, y + 3, COL_BIRD_WING);  // beak
  g->fillCircle(x + 3, y - 3, 2, TFT_WHITE);                 // eye
  g->drawPixel(x + 4, y - 3, TFT_BLACK);
}

void GameRenderer::drawStabilityBar(const SpiroPacket &p) {
  TFT_eSPI *g = gfx();
  // Right-edge coach bar: fills bottom-up with the 0-5 s stable timer.
  const int barX = SCREEN_W - 10, barY = GAME_TOP + 4;
  const int barH = GAME_BOTTOM - GAME_TOP - 8, barW = 7;

  g->drawRect(barX, barY, barW, barH, TFT_WHITE);
  float t = (float)p.stableTimeMs / (float)STABLE_SUCCESS_MS;
  if (t > 1.0f) t = 1.0f;
  int fillH = (int)(t * (barH - 2));
  uint16_t col = p.targetZone ? TFT_GREEN : TFT_ORANGE;
  if (fillH > 0) g->fillRect(barX + 1, barY + 1 + (barH - 2 - fillH), barW - 2, fillH, col);
}

void GameRenderer::drawHud(const SpiroPacket &p) {
  TFT_eSPI *g = gfx();

  // ---- top bar: big flow + volume ----
  g->fillRect(0, 0, SCREEN_W, GAME_TOP, COL_HUD_BG);
  g->setTextFont(4);
  g->setTextDatum(TL_DATUM);
  g->setTextColor(p.dangerZone ? TFT_RED : (p.targetZone ? TFT_GREEN : TFT_WHITE),
                  COL_HUD_BG);
  char buf[32];
  snprintf(buf, sizeof(buf), "%4.0f ml/s", p.filteredFlowMlS);
  g->drawString(buf, 4, 2);

  g->setTextFont(2);
  g->setTextDatum(TR_DATUM);
  g->setTextColor(TFT_CYAN, COL_HUD_BG);
  snprintf(buf, sizeof(buf), "VOL %.2f L", p.volumeMl / 1000.0f);
  g->drawString(buf, SCREEN_W - 4, 1);

  g->setTextColor(TFT_WHITE, COL_HUD_BG);
  snprintf(buf, sizeof(buf), "STABLE %.1f / 5.0 s", p.stableTimeMs / 1000.0f);
  g->drawString(buf, SCREEN_W - 4, 13);
}
