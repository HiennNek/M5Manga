#include "input.h"
#include "ui.h"
#include "bookmarks.h"
#include "storage.h"
#include "wifi_server.h"
#include "navigation.h"
#include "state.h"
#include <algorithm>

void handleTouch() {
    if (M5.Touch.getCount() == 0) {
        if (isMagnifierActive) {
            isMagnifierActive = false;
            resetMagnifierTracking();
            needRedraw = true; // Full redraw to clear magnifier overlay
        }
        return;
    }
    
    auto& t = M5.Touch.getDetail(0);

    // Skip control menu gesture if magnifying
    if (!isMagnifierActive && !controlMenuOpen && t.wasReleased() && t.base_y < 100 && t.distanceY() > SWIPE_UP_MIN) {
        controlMenuOpen = true;
        currentEpdMode = epd_mode_t::epd_fast;
        needRedraw = true;
        return;
    }

    if (controlMenuOpen)              handleControlTouch(t);
    else if (bookConfigOpen)          handleBookConfigTouch(t);
    else if (appState == STATE_MENU)      handleMenuTouch(t);
    else if (appState == STATE_BOOKMARKS) handleBookmarksTouch(t);
    else if (appState == STATE_WIFI)      handleWifiTouch(t);
    else {
        // We handle Reader state (including its gestures) in handleReaderTouch
        handleReaderTouch(t);
    }
}

void handleBookmarksTouch(const m5::touch_detail_t& t) {
    if (!t.wasReleased()) return;

    int dy = t.distanceY();
    int dx = t.distanceX();

    if (dy < -SWIPE_UP_MIN) {
        if (selectedBookmarkFolder == "") {
            appState = STATE_MENU;
            currentEpdMode = epd_mode_t::epd_fast;
        } else {
            selectedBookmarkFolder = "";
            bookmarkScroll = 0;
            currentEpdMode = epd_mode_t::epd_fast;
        }
        needRedraw = true;
        return;
    }

    int itemsPerPage = (DISPLAY_H - 200) / 90;

    if (dy > SWIPE_UP_MIN || dx > 60 || dx < -60) {
        int totalItems = 0;
        if (selectedBookmarkFolder == "") {
            std::vector<String> uniqueFolders;
            for (const auto& b : bookmarks) {
                if (std::find(uniqueFolders.begin(), uniqueFolders.end(), b.folder) == uniqueFolders.end()) {
                    uniqueFolders.push_back(b.folder);
                }
            }
            totalItems = uniqueFolders.size();
        } else {
            for (const auto& b : bookmarks) {
                if (b.folder == selectedBookmarkFolder) totalItems++;
            }
        }

        if (dy > SWIPE_UP_MIN || dx > 60) {
            if (bookmarkScroll + itemsPerPage < totalItems) {
                bookmarkScroll += itemsPerPage;
            } else {
                bookmarkScroll = 0;
            }
            needRedraw = true;
        } else if (dx < -60) {
            if (bookmarkScroll > 0) {
                bookmarkScroll = std::max(0, bookmarkScroll - itemsPerPage);
            } else {
                bookmarkScroll = ((totalItems - 1) / itemsPerPage) * itemsPerPage;
            }
            needRedraw = true;
        }
        
        if (needRedraw) {
            currentEpdMode = epd_mode_t::epd_fast;
            return;
        }
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

        int count = 0;
        for (int i = 0; i < (int)uniqueFolders.size(); i++) {
            if (i < bookmarkScroll) continue;
            if (count >= itemsPerPage) break;

            if (tapY >= yOff && tapY <= yOff + itemH) {
                M5.Display.startWrite();
                M5.Display.fillRoundRect(10, yOff, DISPLAY_W - 20, itemH, 10, 0xDDDD);
                M5.Display.display();
                M5.Display.endWrite();
                delay(50);

                selectedBookmarkFolder = uniqueFolders[i];
                bookmarkScroll = 0;
                currentEpdMode = epd_mode_t::epd_fast;
                needRedraw = true;
                return;
            }
            yOff += itemH + 10;
            count++;
        }
    } else {
        int count = 0;
        int folderItemIdx = 0;
        for (int i = 0; i < (int)bookmarks.size(); i++) {
            if (bookmarks[i].folder != selectedBookmarkFolder) continue;
            
            if (folderItemIdx < bookmarkScroll) {
                folderItemIdx++;
                continue;
            }
            if (count >= itemsPerPage) break;

            if (tapY >= yOff && tapY <= yOff + itemH) {
                if (tapX > DISPLAY_W - 120) {
                    M5.Display.startWrite();
                    M5.Display.fillRoundRect(DISPLAY_W - 100, yOff + 20, 70, 40, 5, 0x8888);
                    M5.Display.display();
                    M5.Display.endWrite();
                    delay(50);

                    deleteBookmark(i);
                    bool remains = false;
                    for (const auto& b : bookmarks) {
                        if (b.folder == selectedBookmarkFolder) {
                            remains = true;
                            break;
                        }
                    }
                    if (!remains) {
                        selectedBookmarkFolder = "";
                        bookmarkScroll = 0;
                    }
                    currentEpdMode = epd_mode_t::epd_fast;
                    needRedraw = true;
                    return;
                } else {
                    M5.Display.startWrite();
                    M5.Display.fillRoundRect(10, yOff, DISPLAY_W - 20, itemH, 10, 0xDDDD);
                    M5.Display.display();
                    M5.Display.endWrite();
                    delay(50);

                    String path = String(MANGA_ROOT) + "/" + bookmarks[i].folder;
                    currentEpdMode = epd_mode_t::epd_quality;
                    openMangaPath(path, bookmarks[i].page);
                    return;
                }
            }
            yOff += itemH + 10;
            count++;
            folderItemIdx++;
        }
    }
}

void handleWifiTouch(const m5::touch_detail_t& t) {
    if (!t.wasReleased()) return;

    int tapX = t.x;
    int tapY = t.y;

    int btnW = 300;
    int btnH = 80;
    int btnX = (DISPLAY_W - btnW) / 2;
    int btnY = DISPLAY_H - 150;

    if (tapX >= btnX && tapX <= btnX + btnW && tapY >= btnY && tapY <= btnY + btnH) {
        M5.Display.startWrite();
        M5.Display.fillRoundRect(btnX, btnY, btnW, btnH, 10, 0x8888);
        M5.Display.display();
        M5.Display.endWrite();
        delay(50);

        stopWifiServer();
        appState = STATE_MENU;
        menuCacheValid = false;
        currentEpdMode = epd_mode_t::epd_fast;
        needRedraw = true;
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
    int modW = 480;
    int modH = 380;
    int modX = (DISPLAY_W - modW) / 2;
    int modY = (DISPLAY_H - modH) / 2;

    int btnW = modW - 60;
    int btnH = 65;
    int btnX = modX + 30;
    int btnY = modY + 210;

    if (tapX > btnX && tapX < btnX + btnW && tapY > btnY && tapY < btnY + btnH) {
        M5.Display.startWrite();
        M5.Display.fillRoundRect(btnX, btnY, btnW, btnH, 10, 0x5555);
        M5.Display.setTextColor(TFT_WHITE, 0x5555);
        M5.Display.setFont(&fonts::DejaVu24);
        M5.Display.setTextDatum(middle_center);
        M5.Display.drawString("SHUTDOWN", btnX + btnW / 2, btnY + btnH / 2);
        M5.Display.setTextDatum(top_left);
        M5.Display.display();
        M5.Display.endWrite();
        delay(50);

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
    int dx = t.distanceX();
    int totalItems = (int)mangaFolders.size() + 2;

    // Horizontal Swipe Right or Vertical Swipe Down for Next Page
    if (dx > 60 || dy > SWIPE_UP_MIN) {
        if (menuScroll + MENU_VISIBLE < totalItems) {
            menuScroll += MENU_VISIBLE;
            menuSelected = menuScroll;
            menuCacheValid = false;
            currentEpdMode = epd_mode_t::epd_fast;
            needRedraw = true;
        } else if (dx > 60) {
            menuScroll = 0;
            menuSelected = 0;
            menuCacheValid = false;
            currentEpdMode = epd_mode_t::epd_fast;
            needRedraw = true;
        }
        return;
    }

    if (dx < -60) {
        if (menuScroll > 0) {
            menuScroll = std::max(0, menuScroll - MENU_VISIBLE);
            menuSelected = menuScroll;
            menuCacheValid = false;
            currentEpdMode = epd_mode_t::epd_fast;
            needRedraw = true;
        } else {
            menuScroll = ((totalItems - 1) / MENU_VISIBLE) * MENU_VISIBLE;
            menuSelected = menuScroll;
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
        int barW = DISPLAY_W - (GRID_GUTTER * 2);
        int barX = GRID_GUTTER;
        int barY = DISPLAY_H - 82;
        
        M5.Display.startWrite();
        M5.Display.fillRoundRect(barX, barY, barW, 50, 10, 0x8888);
        M5.Display.setTextColor(TFT_WHITE, 0x8888);
        M5.Display.setFont(&fonts::DejaVu12);
        M5.Display.setCursor(barX + 10, barY + 10);
        M5.Display.print("CONTINUE: ");
        M5.Display.setFont(&fonts::DejaVu18);
        String shortName = lastMangaName;
        if (shortName.length() > 20) shortName = shortName.substring(0, 18) + "..";
        M5.Display.print(shortName);
        M5.Display.setFont(&fonts::DejaVu12);
        M5.Display.setCursor(barX + 10, barY + 30);
        M5.Display.printf("Page %d", lastPage + 1);
        M5.Display.display();
        M5.Display.endWrite();
        delay(50);

        openMangaPath(lastMangaPath, lastPage);
        return;
    }
    if (tapY < GRID_Y_TOP) return;

    int col = (tapX - GRID_GUTTER) / (THUMB_W + GRID_GUTTER);
    int row = (tapY - GRID_Y_TOP) / GRID_ROW_H;
    if (col < 0) col = 0; if (col >= GRID_COLS) col = GRID_COLS - 1;
    if (row < 0) row = 0; if (row >= 2) row = 1;

    int idx = menuScroll + (row * GRID_COLS + col);
    if (idx >= totalItems) return;

    if (idx == menuSelected) {
        if (idx == 0) {
            appState = STATE_BOOKMARKS;
            selectedBookmarkFolder = "";
            bookmarkScroll = 0;
            currentEpdMode = epd_mode_t::epd_fast;
            needRedraw = true;
        } else if (idx == 1) {
            appState = STATE_WIFI;
            currentEpdMode = epd_mode_t::epd_fast;
            needRedraw = true;
        } else {
            openManga(idx - 2);
        }
    } else {
        menuSelected = idx;
        currentEpdMode = epd_mode_t::epd_fast;
        needRedraw = true;
    }
}

void handleReaderTouch(const m5::touch_detail_t& t) {
    static unsigned long pressStart = 0;
    static unsigned long lastMoveTime = 0;
    static bool potentialLongPress = false;
    static bool qualityApplied = false;
    static bool wasMagnifying = false;

    if (t.wasPressed()) {
        pressStart = millis();
        lastMoveTime = millis();
        potentialLongPress = true;
        qualityApplied = false;
        wasMagnifying = false;
    }

    if (t.isPressed()) {
        if (potentialLongPress && !isMagnifierActive && (millis() - pressStart > 600)) {
            isMagnifierActive = true;
            wasMagnifying = true;
            potentialLongPress = false;
            lastMoveTime = millis();
        }

        if (isMagnifierActive) {
            static int lastMagX = -1, lastMagY = -1;
            if (abs(t.x - lastMagX) > 4 || abs(t.y - lastMagY) > 4) {
                drawMagnifier(t.x, t.y, false);
                lastMagX = t.x;
                lastMagY = t.y;
                lastMoveTime = millis();
                qualityApplied = false;
            } else if (!qualityApplied && (millis() - lastMoveTime > 300)) {
                drawMagnifier(t.x, t.y, true);
                qualityApplied = true;
            }
            return;
        }
    }

    if (t.wasReleased()) {
        potentialLongPress = false;
        if (isMagnifierActive) {
            isMagnifierActive = false;
            resetMagnifierTracking(); 
            needRedraw = true;
            return;
        }

        // Only handle other gestures if we DID NOT use the magnifier during this touch
        if (!wasMagnifying) {
            // 1. Check for Book Menu gesture (Swipe UP from bottom)
            if (t.base_y > DISPLAY_H - 150 && t.distanceY() < -SWIPE_UP_MIN) {
                bookConfigOpen = true;
                bookConfigPendingPage = currentPage;
                currentEpdMode = epd_mode_t::epd_fast;
                needRedraw = true;
                return;
            }

            // 2. Tap to turn page (if not a long press)
            if (millis() - pressStart < 600) {
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
        }
    }
}

void handleBookConfigTouch(const m5::touch_detail_t& t) {
    if (!t.wasReleased()) return;

    int tapX = t.x;
    int tapY = t.y;
    int modW = 480;
    int modH = 500; // Match drawBookConfig
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

    int btnW = modW - 60;
    int btnX = modX + 30;
    int btnH = 60;

    // 1. Dithering Toggle
    int btnY0 = modY + 190;
    if (tapX >= btnX && tapX <= btnX + btnW && tapY >= btnY0 && tapY <= btnY0 + btnH) {
        isDitheringEnabled = !isDitheringEnabled;
        isNextPageReady = false; // Invalidate preload if setting changed
        needRedraw = true;
        currentEpdMode = epd_mode_t::epd_fast;
        return;
    }

    // 2. Bookmark Page
    int btnY1 = modY + 265;
    if (tapX >= btnX && tapX <= btnX + btnW && tapY >= btnY1 && tapY <= btnY1 + btnH) {
        M5.Display.startWrite();
        M5.Display.fillRoundRect(btnX, btnY1, btnW, btnH, 10, 0x5555);
        M5.Display.setTextColor(TFT_WHITE, 0x5555);
        M5.Display.setFont(&fonts::DejaVu18);
        M5.Display.setTextDatum(middle_center);
        M5.Display.drawString("BOOKMARK PAGE", btnX + btnW / 2, btnY1 + btnH / 2);
        M5.Display.setTextDatum(top_left);
        M5.Display.display();
        M5.Display.endWrite();
        delay(50);

        int lastSlash = currentMangaPath.lastIndexOf('/');
        String folder = currentMangaPath.substring(lastSlash + 1);
        addBookmark(folder, currentPage);
        needRedraw = true;
        return;
    }

    // 3. Return to Library
    int btnY2 = modY + 340;
    if (tapX >= btnX && tapX <= btnX + btnW && tapY >= btnY2 && tapY <= btnY2 + btnH) {
        M5.Display.startWrite();
        M5.Display.fillRoundRect(btnX, btnY2, btnW, btnH, 10, 0x5555);
        M5.Display.setTextColor(TFT_WHITE, 0x5555);
        M5.Display.setFont(&fonts::DejaVu18);
        M5.Display.setTextDatum(middle_center);
        M5.Display.drawString("RETURN TO LIBRARY", btnX + btnW / 2, btnY2 + btnH / 2);
        M5.Display.setTextDatum(top_left);
        M5.Display.display();
        M5.Display.endWrite();
        delay(50);

        appState = STATE_MENU;
        bookConfigOpen = false;
        currentEpdMode = epd_mode_t::epd_fast;
        needRedraw = true;
        return;
    }
}
