#pragma once

#include "config.h"
#include <Arduino.h>
#include <M5GFX.h>
#include <Preferences.h>
#include <vector>

struct AlarmConfig
{
  int hour;
  int minute;
  int day;
  int month;
  int year;
  bool active;
};
extern AlarmConfig alarmConfig;

struct Bookmark
{
  String folder;
  int page;
};

enum MenuTab { TAB_COMIC, TAB_DOCUMENT, TAB_APP };
extern MenuTab currentMenuTab;

extern AppState appState;
extern std::vector<String> mangaFolders;
extern std::vector<int> mangaPageCounts;
extern std::vector<String> bookFiles;
extern int menuSelecteds[3];
extern int menuScrolls[3];
extern int bookmarkScroll;

extern String currentMangaPath;
extern String currentBookPath;
extern int totalPages;
extern int currentPage;
extern int currentTextPage;
extern std::vector<uint32_t> textPageOffsets;
extern uint32_t textFileSize;
extern int estimatedTotalPages;
extern bool needRedraw;
extern bool controlMenuOpen;
extern bool bookConfigOpen;
extern int bookConfigPendingPage;
extern DitherMode ditherMode;
extern ContrastPreset contrastPreset;
extern FitMode fitMode;
extern bool isMagnifierActive;
extern int magnifierX, magnifierY;
extern String selectedBookmarkFolder;
extern epd_mode_t currentEpdMode;

extern OrientationMode orientationMode;
extern int currentStrip;

int getActiveRotation();
extern int stripsPerPage;
extern int stripOverlapPx;

extern Preferences prefs;

extern LGFX_Sprite gSprite;
extern LGFX_Sprite nextPageSprite;
extern LGFX_Sprite menuCacheSprites[3];
extern int lastDrawnMenuScrolls[3];
extern bool menuCacheValids[3];
extern String lastMangaPath;
extern int lastPage;
extern String lastMangaName;
extern String lastBookPath;
extern int lastTextPage;
extern bool isLastReadManga;

extern String preloadedMangaPath;
extern int preloadedPage;
extern int preloadedStrip;
extern bool isNextPageReady;

extern std::vector<Bookmark> bookmarks;

inline void requestRedraw(epd_mode_t mode = epd_mode_t::epd_fast)
{
  currentEpdMode = mode;
  needRedraw = true;
}
