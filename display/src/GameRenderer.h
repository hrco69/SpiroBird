// ============================================================================
// GameRenderer — Flappy-Bird style rendering of the live exercise
//
// Owns the TFT and a full-screen sprite (double buffer, 8-bit color depth so
// the 320x240 buffer fits comfortably in internal SRAM). All drawing goes to
// the sprite; present() pushes it in one SPI burst -> no flicker.
//
// The Display only VISUALIZES: the Controller decides success/fail. The bird
// follows filteredFlowMlS with spring/damping inertia; pipes are cosmetic.
// ============================================================================
#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "config.h"
#include "protocol.h"

class GameRenderer {
public:
  void begin();

  // Drawing surface for everyone (UiScreens included): the sprite if it was
  // allocated, otherwise the TFT directly (degraded but functional).
  TFT_eSPI *gfx() { return _useSprite ? (TFT_eSPI *)&_spr : &_tft; }

  void clear(uint16_t color565);                        // clear the frame buffer
  void drawGame(const SpiroPacket &p, uint32_t nowMs);  // STATE_ACTIVE frame
  void resetGame();                                     // new attempt starting
  void present();                                       // push sprite to panel

  // Shared coordinate helpers (UiScreens uses them for zone lines).
  static int flowToY(float flowMlS);
  static const int GAME_TOP    = 26;
  static const int GAME_BOTTOM = 214;
  static const int SCREEN_W    = 320;
  static const int SCREEN_H    = 240;

private:
  void drawBackground(const SpiroPacket &p);
  void drawZones(const SpiroPacket &p);
  void drawPipes();
  void drawBird(const SpiroPacket &p, uint32_t nowMs);
  void drawHud(const SpiroPacket &p);
  void drawStabilityBar(const SpiroPacket &p);

  TFT_eSPI    _tft;
  TFT_eSprite _spr{&_tft};
  bool _useSprite = false;

  // Bird physics
  float _birdY   = 120.0f;
  float _birdVel = 0.0f;

  // Cosmetic pipe scroll
  float _pipeScroll = 0.0f;
};
