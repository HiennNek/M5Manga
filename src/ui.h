#pragma once

#include "config.h"
#include "state.h"

void drawMenu();
void drawControlCenter();
void drawBookConfig();
void drawBookmarks();
void drawWifiServer();
void drawPage();
void preloadPage(int page);
void drawMagnifier(int x, int y, bool qualityMode = false);
void resetMagnifierTracking();
void applyFloydSteinberg(LGFX_Sprite& sprite);
void drawError(const char* msg);
void systemShutdown();
