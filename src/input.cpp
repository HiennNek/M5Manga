#include "input.h"
#include "ui.h"
#include "bookmarks.h"
#include "storage.h"
#include <algorithm>

void handleTouch() {
    if (M5.Touch.getCount() == 0) return;
    auto& t = M5.Touch.getDetail(0);

    if (!controlMenuOpen && t.wasReleased() && t.base_y < 100 && t.distanceY() > SWIPE_UP_MIN) {
        controlMenuOpen = true;
        currentEpdMode = epd_mode_t::epd_fast;
        needRedraw = true;
        return;
    }

    if (controlMenuOpen)              handleControlTouch(t);
    else if (bookConfigOpen)          handleBookConfigTouch(t);
    else if (appState == STATE_MENU)      handleMenuTouch(t);
    else if (appState == STATE_BOOKMARKS) handleBookmarksTouch(t);
    else {
        if (t.wasReleased() && t.base_y > DISPLAY_H - 150 && t.distanceY() < -SWIPE_UP_MIN) {
            bookConfigOpen = true;
            bookConfigPendingPage = currentPage;
            currentEpdMode = epd_mode_t::epd_fast;
            needRedraw = true;
            return;
        }
        handleReaderTouch(t);
    }
}

void handleBookmarksTouch(const m5::touch_detail_t& t) {
    if (!t.wasReleased()) return;

    if (t.distanceY() < -SWIPE_UP_MIN) {
        if (selectedBookmarkFolder == "") {
            appState = STATE_MENU;
            menuCacheValid = false;
            currentEpdMode = epd_mode_t::epd_fast;
        } else {
            selectedBookmarkFolder = "";
            currentEpdMode = epd_mode_t::epd_fast;
        }
        needRedraw = true;
        return;
    }

    int tapX = t.x;
    int tapY = t.y;
    if (tapY < 100) return;

    int yOff = 100;
    int itemH = 80;

    if (selectedBookmarkFolder == "") {
        std::vector<String> uniqueFolders;
        for (const auto& b : bookmarks) {
            if (std::find(uniqueFolders.begin(), uniqueFolders.end(), b.folder) == uniqueFolders.end()) {
                uniqueFolders.push_back(b.folder);
            }
        }
        std::sort(uniqueFolders.begin(), uniqueFolders.end());

        for (const auto& folder : uniqueFolders) {
            if (tapY >= yOff && tapY <= yOff + itemH) {
                selectedBookmarkFolder = folder;
                currentEpdMode = epd_mode_t::epd_fast;
                needRedraw = true;
                return;
            }
            yOff += itemH + 10;
            if (yOff > DISPLAY_H - 100) break;
        }
    } else {
        for (int i = 0; i < (int)bookmarks.size(); i++) {
            if (bookmarks[i].folder != selectedBookmarkFolder) continue;
            if (tapY >= yOff && tapY <= yOff + itemH) {
                if (tapX > DISPLAY_W - 120) {
                    deleteBookmark(i);
                    bool remains = false;
                    for (const auto& b : bookmarks) {
                        if (b.folder == selectedBookmarkFolder) {
                            remains = true;
                            break;
                        }
                    }
                    if (!remains) selectedBookmarkFolder = "";
                    currentEpdMode = epd_mode_t::epd_fast;
                    needRedraw = true;
                    return;
                } else {
                    String path = String(MANGA_ROOT) + "/" + bookmarks[i].folder;
                    currentEpdMode = epd_mode_t::epd_quality;
                    openMangaPath(path, bookmarks[i].page);
                    return;
                }
            }
            yOff += itemH + 10;
            if (yOff > DISPLAY_H - 100) break;
        }
    }
}

void handleControlTouch(const m5::touch_detail_t& t) {
    if (!t.wasReleased()) return;
    if (t.distanceY() < -SWIPE_UP_MIN) {
        controlMenuOpen = false;
        currentEpdMode = (appState == STATE_READER) ? epd_mode_t::epd_quality : epd_mode_t::epd_fast;
        needRedraw = true;
        return;
    }

    int tapX = t.x;
    int tapY = t.y;
    int modW = 400;
    int modH = 460;
    int modX = (DISPLAY_W - modW) / 2;
    int modY = (DISPLAY_H - modH) / 2;

    if (tapX > modX + 30 && tapX < modX + 370 && tapY > modY + 200 && tapY < modY + 320) {
        systemShutdown();
        return;
    }

    if (tapX < modX || tapX > modX + modW || tapY < modY || tapY > modY + modH) {
        controlMenuOpen = false;
        currentEpdMode = (appState == STATE_READER) ? epd_mode_t::epd_quality : epd_mode_t::epd_fast;
        needRedraw = true;
    }
}

void handleMenuTouch(const m5::touch_detail_t& t) {
    if (!t.wasReleased()) return;
    int dy = t.distanceY();
    if (dy < -SWIPE_UP_MIN) {
        scanMangaFolders();
        loadProgress();
        menuSelected = menuScroll = 0;
        menuCacheValid = false;
        currentEpdMode = epd_mode_t::epd_fast;
        needRedraw = true;
        return;
    }

    if (dy > SWIPE_UP_MIN) {
        int totalItems = (int)mangaFolders.size() + 1;
        if (menuScroll + MENU_VISIBLE < totalItems) {
            menuScroll += MENU_VISIBLE;
            menuSelected = menuScroll;
            menuCacheValid = false;
            currentEpdMode = epd_mode_t::epd_fast;
            needRedraw = true;
        } else {
            menuScroll = 0;
            menuSelected = 0;
            menuCacheValid = false;
            currentEpdMode = epd_mode_t::epd_fast;
            needRedraw = true;
        }
        return;
    }

    int tapX = t.x;
    int tapY = t.y;
    if (lastMangaName.length() > 0 && tapX > GRID_GUTTER && tapX < DISPLAY_W - GRID_GUTTER &&
        tapY > (DISPLAY_H - 82) && tapY < (DISPLAY_H - 32)) {
        openMangaPath(lastMangaPath, lastPage);
        return;
    }
    if (tapY < GRID_Y_TOP) return;

    int col = (tapX - GRID_GUTTER) / (THUMB_W + GRID_GUTTER);
    int row = (tapY - GRID_Y_TOP) / GRID_ROW_H;
    if (col < 0) col = 0; if (col >= GRID_COLS) col = GRID_COLS - 1;
    if (row < 0) row = 0; if (row >= 2) row = 1;

    int idx = menuScroll + (row * GRID_COLS + col);
    int totalItems = (int)mangaFolders.size() + 1;
    if (idx >= totalItems) return;

    if (idx == menuSelected) {
        if (idx == 0) {
            appState = STATE_BOOKMARKS;
            selectedBookmarkFolder = "";
            currentEpdMode = epd_mode_t::epd_fast;
            needRedraw = true;
        } else {
            openManga(idx - 1);
        }
    } else {
        menuSelected = idx;
        currentEpdMode = epd_mode_t::epd_fast;
        needRedraw = true;
    }
}

void handleReaderTouch(const m5::touch_detail_t& t) {
    if (!t.wasReleased()) return;

    if (t.x >= LEFT_ZONE_W) {
        if (currentPage < totalPages - 1) {
            currentPage++;
            currentEpdMode = epd_mode_t::epd_quality;
            needRedraw = true;
            saveProgress();
        }
    } else {
        if (currentPage > 0) {
            currentPage--;
            currentEpdMode = epd_mode_t::epd_quality;
            needRedraw = true;
            saveProgress();
        }
    }
}

void handleBookConfigTouch(const m5::touch_detail_t& t) {
    if (!t.wasReleased()) return;

    int tapX = t.x;
    int tapY = t.y;
    int modW = 480;
    int modH = 400;
    int modX = (DISPLAY_W - modW) / 2;
    int modY = (DISPLAY_H - modH) / 2;

    if (tapX < modX || tapX > modX + modW || tapY < modY || tapY > modY + modH) {
        bookConfigOpen = false;
        currentPage = bookConfigPendingPage;
        currentEpdMode = epd_mode_t::epd_quality;
        needRedraw = true;
        return;
    }

    int barY = modY + 90;
    if (tapY >= barY && tapY <= barY + 80) {
        if (tapX < modX + 100) {
            bookConfigPendingPage = std::max(0, bookConfigPendingPage - 10);
            needRedraw = true;
        } else if (tapX < modX + 180) {
            bookConfigPendingPage = std::max(0, bookConfigPendingPage - 1);
            needRedraw = true;
        } else if (tapX > modX + modW - 100) {
            bookConfigPendingPage = std::min(totalPages - 1, bookConfigPendingPage + 10);
            needRedraw = true;
        } else if (tapX > modX + modW - 180) {
            bookConfigPendingPage = std::min(totalPages - 1, bookConfigPendingPage + 1);
            needRedraw = true;
        }
        if (needRedraw) {
            currentEpdMode = epd_mode_t::epd_fast;
        }
    }

    if (tapX >= modX + 20 && tapX <= modX + modW - 20 && tapY >= modY + 255 && tapY <= modY + 315) {
        int lastSlash = currentMangaPath.lastIndexOf('/');
        String folder = currentMangaPath.substring(lastSlash + 1);
        addBookmark(folder, currentPage);
        needRedraw = true;
    }

    if (tapX >= modX + 20 && tapX <= modX + modW - 20 && tapY >= modY + 325 && tapY <= modY + 385) {
        appState = STATE_MENU;
        bookConfigOpen = false;
        menuCacheValid = false;
        currentEpdMode = epd_mode_t::epd_fast;
        needRedraw = true;
    }
}
