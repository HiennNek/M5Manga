#include <Arduino.h>
#include <M5Unified.h>
#include "config.h"
#include "state.h"
#include "storage.h"
#include "bookmarks.h"
#include "ui.h"
#include "input.h"
#include "navigation.h"

static unsigned long lastInteractionMs = 0;

void setup() {
    Serial.begin(115200);

    auto cfg = M5.config();
    M5.begin(cfg);
    setCpuFrequencyMhz(80);

    currentEpdMode = epd_mode_t::epd_fast;
    M5.Display.setRotation(0);
    M5.Display.setColorDepth(8);
    M5.Display.setEpdMode(epd_mode_t::epd_fast);
    M5.Display.fillScreen(TFT_WHITE);
    M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
    M5.Display.setFont(&fonts::DejaVu18);
    M5.Display.setCursor(20, 20);
    M5.Display.println("Initialising...");
    M5.Display.display();

    sdInit();
    scanMangaFolders();
    loadProgress();
    loadBookmarks();

    needRedraw = true;
}

void loop() {
    M5.update();
    if (M5.Touch.getCount() > 0) {
        lastInteractionMs = millis();
    }

    if (needRedraw) {
        M5.Display.setEpdMode(currentEpdMode);
        if (controlMenuOpen)              drawControlCenter();
        else if (bookConfigOpen)          drawBookConfig();
        else if (appState == STATE_MENU)      drawMenu();
        else if (appState == STATE_BOOKMARKS) drawBookmarks();
        else                                  drawPage();
        needRedraw = false;
    }

    handleTouch();

    bool readerIdle = appState == STATE_READER && !controlMenuOpen && !bookConfigOpen;
    if (readerIdle) {
        if (millis() - lastInteractionMs >= 5000) {
            setCpuFrequencyMhz(80);
            delay(80);
        } else {
            setCpuFrequencyMhz(240);
            delay(20);
        }
    } else {
        setCpuFrequencyMhz(240);
        delay(20);
    }
}

void openManga(int idx) {
    if (idx < 0 || idx >= (int)mangaFolders.size()) return;
    String path = String(MANGA_ROOT) + "/" + mangaFolders[idx];
    openMangaPath(path, 0);
}

void openMangaPath(const String& path, int page) {
    currentMangaPath = path;
    currentPage      = page;

    M5.Display.fillScreen(TFT_WHITE);
    M5.Display.setFont(&fonts::DejaVu18);
    M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
    M5.Display.setCursor(20, DISPLAY_H / 2 - 20);
    M5.Display.print("Opening...");
    M5.Display.display();

    setCpuFrequencyMhz(240);
    unsigned long t0 = millis();
    totalPages = findTotalPages(currentMangaPath);
    Serial.printf("Binary search found %d pages in %lu ms\n", totalPages, millis() - t0);

    if (totalPages == 0) {
        drawError("No images found.\nExpected: m5_0000.jpg ...");
        delay(2500);
        currentEpdMode = epd_mode_t::epd_fast;
        needRedraw = true;
        return;
    }

    saveProgress();
    setCpuFrequencyMhz(80);

    currentEpdMode = epd_mode_t::epd_quality;
    appState   = STATE_READER;
    needRedraw = true;
}
