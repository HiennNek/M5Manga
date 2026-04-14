#include <M5Unified.h>
#include "state.h"

AppState appState = STATE_MENU;
std::vector<String> mangaFolders;
std::vector<int> mangaPageCounts;
int menuSelected = 0;
int menuScroll = 0;

String currentMangaPath;
int totalPages = 0;
int currentPage = 0;
bool needRedraw = true;
bool controlMenuOpen = false;
bool bookConfigOpen = false;
int readerContrastBias = 0;
int bookConfigPendingPage = 0;
int bookConfigPendingContrastBias = 0;
String selectedBookmarkFolder = "";
epd_mode_t currentEpdMode = epd_mode_t::epd_fast;

Preferences prefs;
LGFX_Sprite gSprite(&M5.Display);
String lastMangaPath = "";
int lastPage = 0;
String lastMangaName = "";

std::vector<Bookmark> bookmarks;
