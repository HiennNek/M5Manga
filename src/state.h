#pragma once

#include <Arduino.h>
#include <vector>
#include <M5GFX.h>
#include <Preferences.h>
#include "config.h"

struct Bookmark {
    String folder;
    int page;
};

extern AppState appState;
extern std::vector<String> mangaFolders;
extern std::vector<int> mangaPageCounts;
extern int menuSelected;
extern int menuScroll;
extern int bookmarkScroll;

extern String currentMangaPath;
extern int totalPages;
extern int currentPage;
extern bool needRedraw;
extern bool controlMenuOpen;
extern bool bookConfigOpen;
extern int bookConfigPendingPage;
extern bool isDitheringEnabled;
extern bool isMagnifierActive;
extern int magnifierX, magnifierY;
extern String selectedBookmarkFolder;
extern epd_mode_t currentEpdMode;

extern Preferences prefs;

extern LGFX_Sprite gSprite;
extern LGFX_Sprite nextPageSprite;
extern LGFX_Sprite menuCacheSprite;
extern int lastDrawnMenuScroll;
extern bool menuCacheValid;
extern String lastMangaPath;
extern int lastPage;
extern String lastMangaName;

extern String preloadedMangaPath;
extern int preloadedPage;
extern bool isNextPageReady;

extern std::vector<Bookmark> bookmarks;
