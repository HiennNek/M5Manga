#include "state.h"
#include <M5Unified.h>

AppState appState = STATE_MENU;
AlarmConfig alarmConfig = {0, 0, 1, 1, 2026, false};
std::vector<String> mangaFolders;
std::vector<int> mangaPageCounts;
std::vector<String> bookFiles;
int menuSelecteds[3] = {0, 0, 0};
int menuScrolls[3] = {0, 0, 0};
int bookmarkScroll = 0;

MenuTab currentMenuTab = TAB_COMIC;

String currentMangaPath;
String currentBookPath = "";
int totalPages = 0;
int currentPage = 0;
int currentTextPage = 0;
std::vector<uint32_t> textPageOffsets;
uint32_t textFileSize = 0;
int estimatedTotalPages = 1;
bool needRedraw = true;
bool controlMenuOpen = false;
bool bookConfigOpen = false;
int bookConfigPendingPage = 0;
DitherMode ditherMode = DITHER_OFF;
ContrastPreset contrastPreset = CONTRAST_NORMAL;
FitMode fitMode = FIT_SMART;
bool isMagnifierActive = false;
int magnifierX = 0, magnifierY = 0;
String selectedBookmarkFolder = "";
epd_mode_t currentEpdMode = epd_mode_t::epd_fast;
OrientationMode orientationMode = ORIENT_PORTRAIT;
int currentStrip = 0;
int stripsPerPage = 3;
int stripOverlapPx = 20;

Preferences prefs;
LGFX_Sprite gSprite(&M5.Display);
LGFX_Sprite nextPageSprite(&M5.Display);
LGFX_Sprite menuCacheSprites[3] = {LGFX_Sprite(&M5.Display), LGFX_Sprite(&M5.Display), LGFX_Sprite(&M5.Display)};
int lastDrawnMenuScrolls[3] = {-1, -1, -1};
bool menuCacheValids[3] = {false, false, false};
String lastMangaPath = "";
int lastPage = 0;
String lastMangaName = "";
String lastBookPath = "";
int lastTextPage = 0;
bool isLastReadManga = true;

int getActiveRotation() {
  if (orientationMode == ORIENT_PORTRAIT) return 0;
  if (orientationMode == ORIENT_LANDSCAPE) return 1;
  
  static int lastAutoRot = 0;
  float ax_raw, ay_raw, az_raw;
  if (M5.Imu.getAccel(&ax_raw, &ay_raw, &az_raw)) {
    // Sensor is rotated 90 degrees clockwise relative to the display
    float ax = -ay_raw;
    float ay = ax_raw;

    // Determine orientation based on gravity vector
    if (abs(ay) > abs(ax) + 0.2f) {
      if (ay > 0.5f) lastAutoRot = 0;  // Portrait
      else if (ay < -0.5f) lastAutoRot = 2; // Reverse Portrait
    }
    else if (abs(ax) > abs(ay) + 0.2f) {
      if (ax < -0.5f) lastAutoRot = 1; // Landscape
      else if (ax > 0.5f) lastAutoRot = 3; // Reverse Landscape
    }
  }
  return lastAutoRot;
}

String preloadedMangaPath = "";
int preloadedPage = -1;
int preloadedStrip = 0;
bool isNextPageReady = false;

std::vector<Bookmark> bookmarks;
