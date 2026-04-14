#pragma once

#include <M5Unified.h>
#include "config.h"
#include "state.h"
#include "navigation.h"

void handleTouch();
void handleMenuTouch(const m5::touch_detail_t& t);
void handleReaderTouch(const m5::touch_detail_t& t);
void handleControlTouch(const m5::touch_detail_t& t);
void handleBookmarksTouch(const m5::touch_detail_t& t);
void handleBookConfigTouch(const m5::touch_detail_t& t);
