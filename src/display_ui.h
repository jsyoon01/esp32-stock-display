#pragma once

#include "ESP32-HUB75-MatrixPanel-I2S-DMA.h"

#include "ticker_state.h"

void displaySplash(MatrixPanel_I2S_DMA* display, const char* line1, const char* line2);
void displayActive(MatrixPanel_I2S_DMA* display, const TickerState& ts);
