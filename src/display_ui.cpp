#include "display_ui.h"

#include <math.h>
#include <Fonts/TomThumb.h>

#include "app_config.h"

static uint8_t priceToPixelY(const CandleHistory& h, int32_t priceScaled) {
  if (h.count < 2) {
    return GRAPH_Y_OFFSET + GRAPH_HEIGHT / 2;
  }

  int32_t minP = h.minCloseScaled;
  int32_t maxP = h.maxCloseScaled;
  int32_t range = maxP - minP;
  if (range < 1) range = 1;

  float normalized = ((float)(priceScaled - minP)) / (float)range;
  if (normalized < 0.0f) normalized = 0.0f;
  if (normalized > 1.0f) normalized = 1.0f;

  uint8_t pixelY = GRAPH_Y_OFFSET + GRAPH_HEIGHT - 1 - (uint8_t)(normalized * (GRAPH_HEIGHT - 1));
  if (pixelY < GRAPH_Y_OFFSET) pixelY = GRAPH_Y_OFFSET;
  if (pixelY >= GRAPH_Y_OFFSET + GRAPH_HEIGHT) pixelY = GRAPH_Y_OFFSET + GRAPH_HEIGHT - 1;
  return pixelY;
}

static void renderHeader(MatrixPanel_I2S_DMA* display, const TickerState& ts) {
  display->fillRect(0, 0, DISPLAY_WIDTH, HEADER_HEIGHT, 0);
  display->setTextSize(1);
  display->setTextWrap(false);

  const int16_t topLineY = 0;
  const int16_t pctBaselineY = 6;

  display->setFont(nullptr);
  display->setCursor(HEADER_PAD_X, topLineY + HEADER_PAD_Y);
  display->setTextColor(display->color565(255, 255, 255));
  display->print(ts.symbol);

  char changeStr[10];
  if (ts.lastPrice > 0.0f) {
    snprintf(changeStr, sizeof(changeStr), "%+.1f%%", ts.changePercent);
  } else {
    snprintf(changeStr, sizeof(changeStr), "--.-%%");
  }

  display->setFont(&TomThumb);
  int16_t x1 = 0, y1 = 0;
  uint16_t w = 0, h = 0;
  display->getTextBounds(changeStr, 0, pctBaselineY, &x1, &y1, &w, &h);
  int16_t changeX = (int16_t)DISPLAY_WIDTH - (int16_t)w - HEADER_PAD_RIGHT;
  if (changeX < 0) changeX = 0;
  display->setCursor(changeX, pctBaselineY);

  uint16_t changeColor = display->color565(180, 180, 180);
  if (ts.lastPrice > 0.0f) {
    if (fabsf(ts.changePercent) < 0.05f) {
      changeColor = display->color565(180, 180, 180);
    } else if (ts.changePercent >= 0.0f) {
      // Temporary hardware workaround: use RED for up.
      changeColor = display->color565(255, 0, 0);
    } else {
      // Temporary hardware workaround: use BLUE for down.
      changeColor = display->color565(0, 0, 255);
    }
  }
  display->setTextColor(changeColor);
  display->print(changeStr);

  display->setFont(nullptr);
  display->setCursor(HEADER_PAD_X, TEXT_LINE_HEIGHT + HEADER_PAD_Y);
  display->setTextColor(display->color565(220, 220, 220));
  char priceStr[16];
  if (ts.lastPrice > 0.0f) {
    snprintf(priceStr, sizeof(priceStr), "$%.2f", ts.lastPrice);
  } else {
    snprintf(priceStr, sizeof(priceStr), "$--.--");
  }
  display->print(priceStr);
  display->setFont(nullptr);
}

static void renderGraph(MatrixPanel_I2S_DMA* display, const TickerState& ts) {
  if (!ts.hasBackfill || ts.history.count == 0) {
    display->setCursor(10, GRAPH_Y_OFFSET + 4);
    display->setTextSize(1);
    display->setTextColor(display->color565(100, 100, 100));
    display->print("Loading");
    return;
  }

  uint16_t graphColor = display->color565(255, 255, 255);
  uint16_t fillColor = display->color565(50, 50, 50);
  if (ts.lastPrice > 0.0f) {
    if (fabsf(ts.changePercent) < 0.05f) {
      graphColor = display->color565(200, 200, 200);
      fillColor = display->color565(35, 35, 35);
    } else if (ts.changePercent > 0.0f) {
      graphColor = display->color565(255, 0, 0);
      fillColor = display->color565(80, 0, 0);
    } else {
      graphColor = display->color565(0, 0, 255);
      fillColor = display->color565(0, 0, 80);
    }
  }

  static bool xHasPoint[DISPLAY_WIDTH];
  static int32_t xCloseScaled[DISPLAY_WIDTH];
  static uint8_t xY[DISPLAY_WIDTH];

  for (uint8_t x = 0; x < DISPLAY_WIDTH; x++) {
    xHasPoint[x] = false;
    xCloseScaled[x] = 0;
    xY[x] = 0;
  }

  const float X_SCALE = (DISPLAY_WIDTH - 1) / (float)(TRADING_MINUTES - 1);
  for (uint16_t mi = 0; mi < TRADING_MINUTES; mi++) {
    if (!ts.history.isValid(mi)) continue;
    int16_t x = (int16_t)(mi * X_SCALE + 0.5f);
    if (x < 0) x = 0;
    if (x >= DISPLAY_WIDTH) x = DISPLAY_WIDTH - 1;
    xHasPoint[(uint8_t)x] = true;
    xCloseScaled[(uint8_t)x] = ts.history.closeScaled[mi];
  }

  for (uint8_t x = 0; x < DISPLAY_WIDTH; x++) {
    if (!xHasPoint[x]) continue;
    xY[x] = priceToPixelY(ts.history, xCloseScaled[x]);
  }

  const uint8_t graphBottomY = (uint8_t)(GRAPH_Y_OFFSET + GRAPH_HEIGHT - 1);
  for (uint8_t x = 0; x < DISPLAY_WIDTH; x++) {
    if (!xHasPoint[x]) continue;
    uint8_t yTop = xY[x];
    if (yTop < GRAPH_Y_OFFSET) yTop = GRAPH_Y_OFFSET;
    for (uint8_t yy = yTop; yy <= graphBottomY; yy++) {
      display->drawPixel(x, yy, fillColor);
    }
  }

  int16_t prevX = -1;
  uint8_t prevY = 0;
  for (uint8_t x = 0; x < DISPLAY_WIDTH; x++) {
    if (!xHasPoint[x]) continue;
    uint8_t y = xY[x];
    if (prevX >= 0) {
      display->drawLine((uint8_t)prevX, prevY, x, y, graphColor);
    }
    display->drawPixel(x, y, graphColor);
    prevX = x;
    prevY = y;
  }
}

void displayActive(MatrixPanel_I2S_DMA* display, const TickerState& ts) {
  display->clearScreen();
  renderHeader(display, ts);
  renderGraph(display, ts);
}

void displaySplash(MatrixPanel_I2S_DMA* display, const char* line1, const char* line2) {
  display->clearScreen();
  display->setFont(nullptr);
  display->setTextSize(1);
  display->setTextWrap(false);
  display->setTextColor(display->color565(200, 200, 200));
  display->setCursor(2, 10);
  display->print(line1 ? line1 : "");
  display->setTextColor(display->color565(140, 140, 140));
  display->setCursor(2, 20);
  display->print(line2 ? line2 : "");
}


