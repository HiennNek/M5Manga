#include "ui.h"
#include "storage.h"
#include <M5Unified.h>
#include <algorithm>
#include <SD.h>

void drawMenu() {
    gSprite.deleteSprite();
    gSprite.setPsram(true);
    gSprite.setColorDepth(8);
    if (!gSprite.createSprite(DISPLAY_W, DISPLAY_H)) {
        M5.Display.fillScreen(TFT_WHITE);
        M5.Display.display();
        return;
    }
    gSprite.fillScreen(TFT_WHITE);

    gSprite.fillRect(0, 0, DISPLAY_W, 80, TFT_BLACK);
    gSprite.setFont(&fonts::DejaVu24);
    gSprite.setTextColor(TFT_WHITE, TFT_BLACK);
    gSprite.setCursor(GRID_GUTTER, 24);
    gSprite.print("Library");

    gSprite.setFont(&fonts::DejaVu12);
    gSprite.setCursor(GRID_GUTTER, 54);
    gSprite.printf("%d titles available", (int)mangaFolders.size());

    int totalItems = (int)mangaFolders.size() + 1;

    if (lastMangaName.length() > 0) {
        int barW = DISPLAY_W - (GRID_GUTTER * 2);
        int barH = 50;
        int barX = GRID_GUTTER;
        int barY = DISPLAY_H - 82;

        gSprite.fillRoundRect(barX, barY, barW, barH, UI_RADIUS, 0x3333);
        gSprite.drawRoundRect(barX, barY, barW, barH, UI_RADIUS, TFT_WHITE);

        gSprite.setTextColor(TFT_WHITE, 0x3333);
        gSprite.setFont(&fonts::DejaVu12);
        gSprite.setCursor(barX + 10, barY + 10);
        gSprite.print("CONTINUE: ");

        gSprite.setFont(&fonts::DejaVu18);
        String shortName = lastMangaName;
        if (shortName.length() > 20) shortName = shortName.substring(0, 18) + "..";
        gSprite.print(shortName);

        gSprite.setFont(&fonts::DejaVu12);
        gSprite.setCursor(barX + 10, barY + 30);
        gSprite.printf("Page %d", lastPage + 1);

        gSprite.setCursor(barX + barW - 80, barY + 30);
        gSprite.print("RESUME >");
    }

    if (mangaFolders.empty()) {
        gSprite.setFont(&fonts::DejaVu18);
        gSprite.setTextColor(TFT_BLACK, TFT_WHITE);
        gSprite.setCursor(GRID_GUTTER, 120);
        gSprite.println("No manga found in /manga/");
        M5.Display.startWrite();
        gSprite.pushSprite(0, 0);
        M5.Display.display();
        M5.Display.endWrite();
        return;
    }

    int end = std::min(totalItems, menuScroll + MENU_VISIBLE);

    for (int i = menuScroll; i < end; i++) {
        int relIdx = i - menuScroll;
        int row    = relIdx / GRID_COLS;
        int col    = relIdx % GRID_COLS;

        int x = GRID_GUTTER + col * (THUMB_W + GRID_GUTTER);
        int y = GRID_Y_TOP + row * GRID_ROW_H;

        if (i == menuSelected) {
            gSprite.drawRoundRect(x - 5, y - 5, THUMB_W + 10, THUMB_H + 10, UI_RADIUS + 2, TFT_BLACK);
            gSprite.drawRoundRect(x - 4, y - 4, THUMB_W + 8, THUMB_H + 8, UI_RADIUS + 1, TFT_BLACK);
        }

        gSprite.drawRoundRect(x, y, THUMB_W, THUMB_H, UI_RADIUS, 0x8888);

        if (i == 0) {
            gSprite.fillRoundRect(x + 10, y + 10, THUMB_W - 20, THUMB_H - 20, UI_RADIUS, 0xEEEE);
            gSprite.setTextColor(TFT_BLACK, 0xEEEE);
            gSprite.setFont(&fonts::DejaVu24);
            gSprite.setCursor(x + 25, y + THUMB_H / 2 - 20);
            gSprite.print("   ★");
            gSprite.setFont(&fonts::DejaVu12);
            gSprite.setCursor(x + 25, y + THUMB_H / 2 + 20);
            gSprite.print("Bookmarks");

            gSprite.setFont(&fonts::DejaVu12);
            gSprite.setTextColor(TFT_BLACK, TFT_WHITE);
            gSprite.setCursor(x + THUMB_W / 2 - 35, y + THUMB_H + 10);
            gSprite.print("MY SAVES");
        } else {
            int fIdx = i - 1;
            String coverPath = makePagePath(String(MANGA_ROOT) + "/" + mangaFolders[fIdx], 0);
            File coverFile = SD.open(coverPath.c_str());
            if (coverFile) {
                float imgAspect = 540.0f / 960.0f;
                int scaledW = (int)((THUMB_H - 4) * imgAspect);
                int xOffset = (THUMB_W - 4 - scaledW) / 2;
                if (xOffset < 0) xOffset = 0;

                gSprite.drawJpg(&coverFile, x + 2 + xOffset, y + 2, THUMB_W - 4, THUMB_H - 4, 0, 0, 0.0f, 0.0f);
                coverFile.close();
            } else {
                gSprite.setTextColor(0x8888, TFT_WHITE);
                gSprite.setFont(&fonts::DejaVu12);
                gSprite.setCursor(x + 10, y + THUMB_H / 2);
                gSprite.print("NO COVER");
            }

            gSprite.setFont(&fonts::DejaVu12);
            gSprite.setTextColor(TFT_BLACK, TFT_WHITE);
            String title = mangaFolders[fIdx];
            if (title.length() > 18) title = title.substring(0, 16) + "..";
            int textX = x + (THUMB_W - (title.length() * 7)) / 2;
            gSprite.setCursor(std::max(x, textX), y + THUMB_H + 10);
            gSprite.print(title);
        }

        if (i == menuSelected) {
            gSprite.fillRect(x + (THUMB_W / 2) - 15, y + THUMB_H + 30, 30, 3, TFT_BLACK);
        }
    }

    gSprite.setFont(&fonts::DejaVu12);
    gSprite.setTextColor(0x8888, TFT_WHITE);
    gSprite.setCursor(GRID_GUTTER, DISPLAY_H - 24);
    gSprite.print("Swipe UP: Refresh  |  Swipe DOWN: Next Page");

    M5.Display.startWrite();
    gSprite.pushSprite(0, 0);
    M5.Display.display();
    M5.Display.endWrite();

    gSprite.deleteSprite();
}

void drawControlCenter() {
    gSprite.deleteSprite();
    gSprite.setPsram(true);
    gSprite.setColorDepth(8);
    if (!gSprite.createSprite(DISPLAY_W, DISPLAY_H)) return;

    gSprite.fillScreen(0x8888);

    int modW = 400;
    int modH = 460;
    int modX = (DISPLAY_W - modW) / 2;
    int modY = (DISPLAY_H - modH) / 2;

    gSprite.fillRoundRect(modX, modY, modW, modH, UI_RADIUS * 2, TFT_WHITE);
    gSprite.drawRoundRect(modX, modY, modW, modH, UI_RADIUS * 2, TFT_BLACK);

    gSprite.setTextColor(TFT_BLACK, TFT_WHITE);
    gSprite.setFont(&fonts::DejaVu24);
    gSprite.setCursor(modX + 30, modY + 30);
    gSprite.print("System");

    int bat = M5.Power.getBatteryLevel();
    gSprite.setFont(&fonts::DejaVu18);
    gSprite.setCursor(modX + 30, modY + 80);
    gSprite.printf("Battery: %d%%", bat);

    int pW = 340;
    int pX = modX + 30;
    int pY = modY + 110;
    gSprite.drawRoundRect(pX, pY, pW, 40, 5, TFT_BLACK);
    gSprite.fillRoundRect(pX + 4, pY + 4, (pW - 8) * bat / 100, 32, 3, 0xBBBB);

    int btnW = 340;
    int btnH = 120;
    int btnX = modX + 30;
    int btnY = modY + 200;

    gSprite.fillRoundRect(btnX, btnY, btnW, btnH, UI_RADIUS, TFT_BLACK);
    gSprite.setTextColor(TFT_WHITE, TFT_BLACK);
    gSprite.setFont(&fonts::DejaVu24);
    gSprite.setCursor(btnX + 65, btnY + 45);
    gSprite.print("SHUTDOWN");

    gSprite.setTextColor(0x8888, TFT_WHITE);
    gSprite.setFont(&fonts::DejaVu12);
    gSprite.setCursor(modX + 30, modY + 380);
    gSprite.print("Swipe UP or tap outside to close");

    M5.Display.startWrite();
    gSprite.pushSprite(0, 0);
    M5.Display.display();
    M5.Display.endWrite();

    gSprite.deleteSprite();
}

void systemShutdown() {
    M5.Display.setEpdMode(epd_mode_t::epd_quality);
    M5.Display.fillScreen(TFT_WHITE);
    M5.Display.display();
    delay(2000);
    M5.Power.powerOff();
}

void drawBookConfig() {
    gSprite.deleteSprite();
    gSprite.setPsram(true);
    gSprite.setColorDepth(8);
    if (!gSprite.createSprite(DISPLAY_W, DISPLAY_H)) return;

    gSprite.fillScreen(0x8888);

    int modW = 480;
    int modH = 400;
    int modX = (DISPLAY_W - modW) / 2;
    int modY = (DISPLAY_H - modH) / 2;

    gSprite.fillRoundRect(modX, modY, modW, modH, UI_RADIUS * 2, TFT_WHITE);
    gSprite.drawRoundRect(modX, modY, modW, modH, UI_RADIUS * 2, TFT_BLACK);

    gSprite.setTextColor(TFT_BLACK, TFT_WHITE);
    gSprite.setFont(&fonts::DejaVu24);
    gSprite.setCursor(modX + 30, modY + 30);
    gSprite.print("Book Menu");

    int barY = modY + 90;
    gSprite.drawRoundRect(modX + 20, barY, modW - 40, 80, UI_RADIUS, 0x8888);
    gSprite.setFont(&fonts::DejaVu24);
    gSprite.setCursor(modX + 45, barY + 25);
    gSprite.print("<<");
    gSprite.setCursor(modX + 115, barY + 25);
    gSprite.print("<");

    String pg = String(bookConfigPendingPage + 1) + " / " + String(totalPages);
    int pgW = pg.length() * 14;
    gSprite.setCursor(modX + (modW - pgW) / 2, barY + 25);
    gSprite.print(pg);

    gSprite.setCursor(modX + modW - 135, barY + 25);
    gSprite.print(">");
    gSprite.setCursor(modX + modW - 85, barY + 25);
    gSprite.print(">>");

    int conY = modY + 180;
    gSprite.drawRoundRect(modX + 20, conY, modW - 40, 60, UI_RADIUS, 0x8888);
    gSprite.setFont(&fonts::DejaVu18);
    gSprite.setCursor(modX + 45, conY + 20);
    gSprite.print("[-]");

    String conText = "Contrast: ";
    if (bookConfigPendingContrastBias == 0) conText += "Normal";
    else if (bookConfigPendingContrastBias < 0) conText += "Darker (" + String(abs(bookConfigPendingContrastBias)) + ")";
    else conText += "Lighter (" + String(bookConfigPendingContrastBias) + ")";

    int conTextW = conText.length() * 10;
    gSprite.setCursor(modX + (modW - conTextW) / 2, conY + 20);
    gSprite.print(conText);
    gSprite.setCursor(modX + modW - 75, conY + 20);
    gSprite.print("[+]");

    int btnX = modX + 20;
    int btnW = modW - 40;
    int btnH = 60;
    int btnY1 = modY + 255;
    gSprite.drawRoundRect(btnX, btnY1, btnW, btnH, UI_RADIUS, TFT_BLACK);
    gSprite.setFont(&fonts::DejaVu18);
    gSprite.setCursor(btnX + 100, btnY1 + 20);
    gSprite.print("BOOKMARK PAGE");

    int btnY2 = modY + 325;
    gSprite.fillRoundRect(btnX, btnY2, btnW, btnH, UI_RADIUS, TFT_BLACK);
    gSprite.setTextColor(TFT_WHITE, TFT_BLACK);
    gSprite.setCursor(btnX + 85, btnY2 + 25);
    gSprite.print("RETURN TO LIBRARY");

    M5.Display.startWrite();
    gSprite.pushSprite(0, 0);
    M5.Display.display();
    M5.Display.endWrite();
    gSprite.deleteSprite();
}

void drawBookmarks() {
    gSprite.deleteSprite();
    gSprite.setPsram(true);
    gSprite.setColorDepth(8);
    if (!gSprite.createSprite(DISPLAY_W, DISPLAY_H)) return;

    gSprite.fillScreen(TFT_WHITE);

    gSprite.fillRect(0, 0, DISPLAY_W, 80, TFT_BLACK);
    gSprite.setTextColor(TFT_WHITE, TFT_BLACK);
    gSprite.setFont(&fonts::DejaVu24);
    gSprite.setCursor(20, 24);
    if (selectedBookmarkFolder == "") {
        gSprite.print("Bookmark Library");
    } else {
        String t = selectedBookmarkFolder;
        if (t.length() > 15) t = t.substring(0, 13) + "..";
        gSprite.printf("< %s", t.c_str());
    }

    if (bookmarks.empty()) {
        gSprite.setTextColor(TFT_BLACK, TFT_WHITE);
        gSprite.setFont(&fonts::DejaVu18);
        gSprite.setCursor(40, 150);
        gSprite.print("No bookmarks yet.");
    } else {
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
                if (yOff + itemH > DISPLAY_H - 100) break;
                gSprite.drawRoundRect(10, yOff, DISPLAY_W - 20, itemH, 10, 0x8888);
                gSprite.setTextColor(TFT_BLACK, TFT_WHITE);
                gSprite.setFont(&fonts::DejaVu18);
                gSprite.setCursor(30, yOff + 28);
                String t = folder;
                if (t.length() > 22) t = t.substring(0, 20) + "..";
                gSprite.print(t);
                gSprite.setFont(&fonts::DejaVu12);
                gSprite.setTextColor(0x8888, TFT_WHITE);
                gSprite.setCursor(DISPLAY_W - 80, yOff + 33);
                gSprite.print("OPEN >");
                yOff += itemH + 10;
            }
        } else {
            for (int i = 0; i < (int)bookmarks.size(); i++) {
                if (bookmarks[i].folder != selectedBookmarkFolder) continue;
                if (yOff + itemH > DISPLAY_H - 100) break;
                gSprite.drawRoundRect(10, yOff, DISPLAY_W - 20, itemH, 10, 0x8888);
                gSprite.setTextColor(TFT_BLACK, TFT_WHITE);
                gSprite.setFont(&fonts::DejaVu18);
                gSprite.setCursor(30, yOff + 28);
                gSprite.printf("Page %d", bookmarks[i].page + 1);
                gSprite.fillRect(DISPLAY_W - 100, yOff + 20, 70, 40, 0xEEEE);
                gSprite.drawRoundRect(DISPLAY_W - 100, yOff + 20, 70, 40, 5, TFT_BLACK);
                gSprite.setTextColor(TFT_BLACK, 0xEEEE);
                gSprite.setFont(&fonts::DejaVu12);
                gSprite.setCursor(DISPLAY_W - 85, yOff + 33);
                gSprite.print("DEL");
                yOff += itemH + 10;
            }
        }
    }

    gSprite.setTextColor(0x8888, TFT_WHITE);
    gSprite.setFont(&fonts::DejaVu12);
    gSprite.setCursor(20, DISPLAY_H - 30);
    if (selectedBookmarkFolder == "") gSprite.print("Swipe UP: Library");
    else                             gSprite.print("Swipe UP: Back to Titles");

    M5.Display.startWrite();
    gSprite.pushSprite(0, 0);
    M5.Display.display();
    M5.Display.endWrite();
    gSprite.deleteSprite();
}

void drawPage() {
    if (totalPages == 0) {
        drawError("No images in this manga.");
        return;
    }

    setCpuFrequencyMhz(240);
    String path = makePagePath(currentMangaPath, currentPage);
    Serial.printf("Drawing [%d/%d]: %s\n", currentPage + 1, totalPages, path.c_str());

    gSprite.deleteSprite();
    gSprite.setPsram(true);
    gSprite.setColorDepth(8);
    if (!gSprite.createSprite(DISPLAY_W, DISPLAY_H)) {
        setCpuFrequencyMhz(80);
        drawError("Sprite alloc failed.");
        return;
    }

    File pageFile = SD.open(path.c_str());
    if (!pageFile) {
        gSprite.deleteSprite();
        setCpuFrequencyMhz(80);
        drawError("Cannot open or decode image.");
        return;
    }
    if (!gSprite.drawJpg(&pageFile, 0, 0, DISPLAY_W, DISPLAY_H, 0, 0, 0.0f, 0.0f, datum_t::top_left)) {
        pageFile.close();
        gSprite.deleteSprite();
        setCpuFrequencyMhz(80);
        drawError("Cannot open or decode image.");
        return;
    }
    pageFile.close();

    if (readerContrastBias != 0) {
        uint8_t* pix = (uint8_t*)gSprite.getBuffer();
        if (pix) {
            int count = DISPLAY_W * DISPLAY_H;
            for (int i = 0; i < count; i++) {
                int v = (int)pix[i] + readerContrastBias;
                pix[i] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
            }
        }
    }

    M5.Display.startWrite();
    gSprite.pushSprite(0, 0);
    M5.Display.display();
    M5.Display.endWrite();

    gSprite.deleteSprite();
    setCpuFrequencyMhz(80);
}

void drawError(const char* msg) {
    M5.Display.fillScreen(TFT_WHITE);
    M5.Display.setFont(&fonts::DejaVu18);
    M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
    M5.Display.setCursor(20, 40);
    M5.Display.println(msg);
    M5.Display.display();
}
