#include "ui.h"
#include "storage.h"
#include "wifi_server.h"
#include "icon.h"
#include "state.h"
#include <M5Unified.h>
#include <algorithm>
#include <SD.h>
#include <esp_heap_caps.h>

static bool forceFullMenuRedraw = true;

static uint8_t* jpgBuffer = nullptr;
static size_t jpgBufferSize = 0;

void ensureJpgBuffer(size_t size) {
    if (jpgBufferSize < size) {
        if (jpgBuffer) heap_caps_free(jpgBuffer);
        jpgBuffer = (uint8_t*)heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
        if (jpgBuffer) jpgBufferSize = size;
        else jpgBufferSize = 0;
    }
}

void prepareSprite(LGFX_Sprite& sprite, int w, int h, int depth, bool usePsram) {
    if (sprite.width() == w && sprite.height() == h && sprite.getColorDepth() == depth) {
        return; 
    }
    sprite.deleteSprite();
    sprite.setPsram(usePsram);
    sprite.setColorDepth(depth);
    sprite.createSprite(w, h);
}

void applyFloydSteinberg(LGFX_Sprite& sprite) {
    int w = sprite.width();
    int h = sprite.height();
    uint16_t* buf = (uint16_t*)sprite.getBuffer();
    if (!buf) return;

    // Error buffers for current and next row (initialized to 0)
    int16_t* error_buf = (int16_t*)heap_caps_calloc(w * 2, sizeof(int16_t), MALLOC_CAP_INTERNAL);
    if (!error_buf) return;

    int16_t* curr_err = error_buf;
    int16_t* next_err = error_buf + w;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            // LGFX RGB565 is Big-Endian on ESP32
            uint16_t raw_pixel = buf[y * w + x];
            uint16_t pixel = (raw_pixel >> 8) | (raw_pixel << 8);
            
            // Extract RGB565 components
            int32_t r = (pixel >> 11) << 3;
            int32_t g = ((pixel >> 5) & 0x3F) << 2;
            int32_t b = (pixel & 0x1F) << 3;
            
            // Simple grayscale conversion (accurate enough for manga)
            int32_t gray = (r * 306 + g * 601 + b * 117) >> 10;
            
            // Add error from previous pixels
            int32_t gray_with_err = gray + curr_err[x];
            if (gray_with_err < 0) gray_with_err = 0;
            else if (gray_with_err > 255) gray_with_err = 255;

            // Quantize to 16 levels (0, 17, 34 ... 255)
            // Each level is exactly 17 apart: floor((v / 17) + 0.5) * 17
            int32_t quantized = ((gray_with_err + 8) / 17) * 17;
            if (quantized > 255) quantized = 255;

            int32_t err = gray_with_err - quantized;

            // Pack quantized gray back to RGB565 (Big-Endian)
            uint16_t q = (uint16_t)quantized;
            uint16_t out_pixel = ((q >> 3) << 11) | ((q >> 2) << 5) | (q >> 3);
            buf[y * w + x] = (out_pixel >> 8) | (out_pixel << 8);

            // Floyd-Steinberg error distribution
            //   X   7/16
            // 3/16 5/16 1/16
            if (x + 1 < w) curr_err[x + 1] += (err * 7) >> 4;
            if (y + 1 < h) {
                if (x > 0)     next_err[x - 1] += (err * 3) >> 4;
                next_err[x]   += (err * 5) >> 4;
                if (x + 1 < w) next_err[x + 1] += (err * 1) >> 4;
            }
        }
        // Swap error buffers for next row
        int16_t* tmp = curr_err;
        curr_err = next_err;
        next_err = tmp;
        memset(next_err, 0, w * sizeof(int16_t));
    }

    heap_caps_free(error_buf);
}

static int lastOutX = -1, lastOutY = -1;

void drawMagnifier(int x, int y, bool qualityMode) {
    static LGFX_Sprite magSprite(&M5.Display);
    prepareSprite(magSprite, MAG_SIZE, MAG_SIZE, 16, true);
    if (!magSprite.getBuffer()) return;

    // 1. Calculate new position
    int outX = x - MAG_SIZE / 2;
    int outY = y - MAG_SIZE - 60;
    if (outX < 10) outX = 10;
    if (outX + MAG_SIZE > DISPLAY_W - 10) outX = DISPLAY_W - MAG_SIZE - 10;
    if (outY < 10) outY = 10;

    // 2. Prepare the zoomed image
    int srcSize = MAG_SIZE / MAG_SCALE;
    int srcX = x, srcY = y;
    if (srcX < srcSize/2) srcX = srcSize/2;
    if (srcY < srcSize/2) srcY = srcSize/2;
    if (srcX > DISPLAY_W - srcSize/2) srcX = DISPLAY_W - srcSize/2;
    if (srcY > DISPLAY_H - srcSize/2) srcY = DISPLAY_H - srcSize/2;

    magSprite.fillScreen(TFT_WHITE);
    gSprite.setPivot(srcX, srcY);
    gSprite.pushRotateZoom(&magSprite, MAG_SIZE / 2, MAG_SIZE / 2, 0, MAG_SCALE, MAG_SCALE);
    
    magSprite.drawRect(0, 0, MAG_SIZE, MAG_SIZE, TFT_BLACK);
    magSprite.drawRect(1, 1, MAG_SIZE-2, MAG_SIZE-2, TFT_WHITE);
    magSprite.drawRect(2, 2, MAG_SIZE-4, MAG_SIZE-4, TFT_BLACK);

    // 3. CLEANUP: If we moved, erase the old magnifier position using gSprite
    M5.Display.startWrite();
    if (lastOutX != -1 && (lastOutX != outX || lastOutY != outY)) {
        // Restore background from gSprite to the OLD magnifier location
        M5.Display.setEpdMode(epd_mode_t::epd_fast);
        M5.Display.setClipRect(lastOutX, lastOutY, MAG_SIZE, MAG_SIZE);
        gSprite.pushSprite(0, 0);
        M5.Display.clearClipRect();
    }

    // 4. DRAW: Draw the new magnifier
    M5.Display.setEpdMode(qualityMode ? epd_mode_t::epd_quality : epd_mode_t::epd_fast);
    magSprite.pushSprite(outX, outY);
    
    M5.Display.display(); 
    M5.Display.endWrite();

    lastOutX = outX;
    lastOutY = outY;
}

// Reset tracking when magnifier closes
void resetMagnifierTracking() {
    lastOutX = -1;
    lastOutY = -1;
}

void preloadPage(int page) {
    if (page < 0 || page >= totalPages) {
        isNextPageReady = false;
        return;
    }
    if (isNextPageReady && preloadedPage == page && preloadedMangaPath == currentMangaPath) {
        return;
    }

    setCpuFrequencyMhz(240);
    String path = makePagePath(currentMangaPath, page);
    
    prepareSprite(nextPageSprite, DISPLAY_W, DISPLAY_H, 16, true);
    if (!nextPageSprite.getBuffer()) {
        isNextPageReady = false;
        return;
    }

    File pageFile = SD.open(path.c_str());
    if (!pageFile) {
        isNextPageReady = false;
        return;
    }

    size_t fileSize = pageFile.size();
    ensureJpgBuffer(fileSize + 1024);
    
    bool success = false;
    if (jpgBuffer) {
        pageFile.read(jpgBuffer, fileSize);
        pageFile.close();
        success = nextPageSprite.drawJpg(jpgBuffer, fileSize, 0, 0, DISPLAY_W, DISPLAY_H, 0, 0, 0.0f, 0.0f, datum_t::top_left);
    } else {
        success = nextPageSprite.drawJpg(&pageFile, 0, 0, DISPLAY_W, DISPLAY_H, 0, 0, 0.0f, 0.0f, datum_t::top_left);
        pageFile.close();
    }

    if (success) {
        if (isDitheringEnabled) {
            applyFloydSteinberg(nextPageSprite);
        }
        preloadedPage = page;
        preloadedMangaPath = currentMangaPath;
        isNextPageReady = true;
    } else {
        isNextPageReady = false;
    }
    setCpuFrequencyMhz(80);
}

void drawMenu() {
    int totalItems = (int)mangaFolders.size() + 2; 
    int end = std::min(totalItems, menuScroll + MENU_VISIBLE);

    if (!menuCacheValid || lastDrawnMenuScroll != menuScroll) {
        prepareSprite(menuCacheSprite, DISPLAY_W, DISPLAY_H, 8, true);
        if (menuCacheSprite.getBuffer()) {
            menuCacheSprite.fillScreen(TFT_WHITE);
            menuCacheSprite.fillRect(0, 0, DISPLAY_W, 80, TFT_BLACK);
            menuCacheSprite.setFont(&fonts::DejaVu24);
            menuCacheSprite.setTextColor(TFT_WHITE, TFT_BLACK);
            menuCacheSprite.setCursor(GRID_GUTTER, 24);
            menuCacheSprite.print("Library");

            menuCacheSprite.setFont(&fonts::DejaVu12);
            menuCacheSprite.setCursor(GRID_GUTTER, 54);
            menuCacheSprite.printf("%d titles available", (int)mangaFolders.size());

            if (totalItems > 0) {
                int curPg = (menuScroll / MENU_VISIBLE) + 1;
                int maxPg = (totalItems + MENU_VISIBLE - 1) / MENU_VISIBLE;
                menuCacheSprite.setCursor(DISPLAY_W - 100, 24);
                menuCacheSprite.printf("Pg %d/%d", curPg, maxPg);
            }

            if (mangaFolders.empty()) {
                menuCacheSprite.setFont(&fonts::DejaVu18);
                menuCacheSprite.setTextColor(TFT_BLACK, TFT_WHITE);
                menuCacheSprite.setCursor(GRID_GUTTER, 120);
                menuCacheSprite.println("No manga found in /manga/");
            } else {
                for (int i = menuScroll; i < end; i++) {
                    int relIdx = i - menuScroll;
                    int row    = relIdx / GRID_COLS;
                    int col    = relIdx % GRID_COLS;
                    int x = GRID_GUTTER + col * (THUMB_W + GRID_GUTTER);
                    int y = GRID_Y_TOP + row * GRID_ROW_H;

                    menuCacheSprite.drawRoundRect(x, y, THUMB_W, THUMB_H, UI_RADIUS, 0x8888);

                    if (i == 0) {
                        menuCacheSprite.fillRoundRect(x + 10, y + 10, THUMB_W - 20, THUMB_H - 20, UI_RADIUS, 0xEEEE);
                        int iconX = x + (THUMB_W - 96) / 2;
                        int iconY = y + (THUMB_H - 96) / 2;
                        menuCacheSprite.drawBitmap(iconX, iconY, image_book_70dp_1F1F1F_bits, 96, 96, 0x3333);
                        menuCacheSprite.setFont(&fonts::DejaVu12);
                        menuCacheSprite.setTextColor(TFT_BLACK, TFT_WHITE);
                        menuCacheSprite.setTextDatum(top_center);
                        menuCacheSprite.drawString("BOOKMARKS", x + THUMB_W / 2, y + THUMB_H + 10);
                        menuCacheSprite.setTextDatum(top_left);
                    } else if (i == 1) {
                        menuCacheSprite.fillRoundRect(x + 10, y + 10, THUMB_W - 20, THUMB_H - 20, UI_RADIUS, 0xEEEE);
                        int iconX = x + (THUMB_W - 96) / 2;
                        int iconY = y + (THUMB_H - 96) / 2;
                        menuCacheSprite.drawBitmap(iconX, iconY, image_drive_folder_upload_70dp_1F1F1F_bits, 96, 96, 0x3333);
                        menuCacheSprite.setFont(&fonts::DejaVu12);
                        menuCacheSprite.setTextColor(TFT_BLACK, TFT_WHITE);
                        menuCacheSprite.setTextDatum(top_center);
                        menuCacheSprite.drawString("FILES", x + THUMB_W / 2, y + THUMB_H + 10);
                        menuCacheSprite.setTextDatum(top_left);
                    } else {
                        int fIdx = i - 2;
                        String coverPath = makePagePath(String(MANGA_ROOT) + "/" + mangaFolders[fIdx], 0);
                        File coverFile = SD.open(coverPath.c_str());
                        if (coverFile) {
                            float imgAspect = 540.0f / 960.0f;
                            int scaledW = (int)((THUMB_H - 4) * imgAspect);
                            int xOffset = (THUMB_W - 4 - scaledW) / 2;
                            if (xOffset < 0) xOffset = 0;
                            menuCacheSprite.drawJpg(&coverFile, x + 2 + xOffset, y + 2, THUMB_W - 4, THUMB_H - 4, 0, 0, 0.0f, 0.0f);
                            coverFile.close();
                        } else {
                            menuCacheSprite.setTextColor(0x8888, TFT_WHITE);
                            menuCacheSprite.setFont(&fonts::DejaVu12);
                            menuCacheSprite.setCursor(x + 10, y + THUMB_H / 2);
                            menuCacheSprite.print("NO COVER");
                        }
                        menuCacheSprite.setFont(&fonts::DejaVu12);
                        menuCacheSprite.setTextColor(TFT_BLACK, TFT_WHITE);
                        String title = mangaFolders[fIdx];
                        if (title.length() > 18) title = title.substring(0, 16) + "..";
                        int textX = x + (THUMB_W - (title.length() * 7)) / 2;
                        menuCacheSprite.setCursor(std::max(x, textX), y + THUMB_H + 10);
                        menuCacheSprite.print(title);
                    }
                }
            }
            menuCacheSprite.setFont(&fonts::DejaVu12);
            menuCacheSprite.setTextColor(0x8888, TFT_WHITE);
            menuCacheSprite.setCursor(GRID_GUTTER, DISPLAY_H - 24);
            menuCacheSprite.print("Swipe UP: Refresh  |  Swipe DOWN: Next Page");
            menuCacheValid = true;
            lastDrawnMenuScroll = menuScroll;
            forceFullMenuRedraw = true;
        }
    }

    static String drawnLastMangaName = "";
    static int drawnLastPage = -1;
    static int drawnLastMenuScroll = -1;

    if (menuCacheValid && (drawnLastMangaName != lastMangaName || drawnLastPage != lastPage || drawnLastMenuScroll != menuScroll)) {
        int barW = DISPLAY_W - (GRID_GUTTER * 2);
        int barH = 50;
        int barX = GRID_GUTTER;
        int barY = DISPLAY_H - 82;

        if (lastMangaName.length() == 0) {
            menuCacheSprite.fillRect(barX, barY, barW, barH, TFT_WHITE);
        } else {
            menuCacheSprite.fillRoundRect(barX, barY, barW, barH, UI_RADIUS, 0x3333);
            menuCacheSprite.drawRoundRect(barX, barY, barW, barH, UI_RADIUS, TFT_WHITE);
            menuCacheSprite.setTextColor(TFT_WHITE, 0x3333);
            menuCacheSprite.setFont(&fonts::DejaVu12);
            menuCacheSprite.setCursor(barX + 10, barY + 10);
            menuCacheSprite.print("CONTINUE: ");
            menuCacheSprite.setFont(&fonts::DejaVu18);
            String shortName = lastMangaName;
            if (shortName.length() > 20) shortName = shortName.substring(0, 18) + "..";
            menuCacheSprite.print(shortName);
            menuCacheSprite.setFont(&fonts::DejaVu12);
            menuCacheSprite.setCursor(barX + 10, barY + 30);
            menuCacheSprite.printf("Page %d", lastPage + 1);
            menuCacheSprite.setCursor(barX + barW - 80, barY + 30);
            menuCacheSprite.print("RESUME >");
        }
        drawnLastMangaName = lastMangaName;
        drawnLastPage = lastPage;
        drawnLastMenuScroll = menuScroll;
        forceFullMenuRedraw = true;
    }

    static int prevMenuSelected = -1;
    static int lastMenuScrollForDirty = -1;
    bool isFullRedraw = forceFullMenuRedraw;
    if (lastMenuScrollForDirty != menuScroll) isFullRedraw = true;
    lastMenuScrollForDirty = menuScroll;
    forceFullMenuRedraw = false;

    M5.Display.startWrite();
    if (isFullRedraw) {
        if (menuCacheValid) menuCacheSprite.pushSprite(0, 0);
        else M5.Display.fillScreen(TFT_WHITE);
        for (int i = menuScroll; i < end; i++) {
            if (i == menuSelected) {
                int relIdx = i - menuScroll;
                int row = relIdx / GRID_COLS;
                int col = relIdx % GRID_COLS;
                int x = GRID_GUTTER + col * (THUMB_W + GRID_GUTTER);
                int y = GRID_Y_TOP + row * GRID_ROW_H;
                M5.Display.drawRoundRect(x - 5, y - 5, THUMB_W + 10, THUMB_H + 10, UI_RADIUS + 2, TFT_BLACK);
                M5.Display.drawRoundRect(x - 4, y - 4, THUMB_W + 8, THUMB_H + 8, UI_RADIUS + 1, TFT_BLACK);
                M5.Display.fillRect(x + (THUMB_W / 2) - 15, y + THUMB_H + 30, 30, 3, TFT_BLACK);
            }
        }
    } else {
        if (prevMenuSelected >= menuScroll && prevMenuSelected < end) {
            int relIdx = prevMenuSelected - menuScroll;
            int row = relIdx / GRID_COLS;
            int col = relIdx % GRID_COLS;
            int x = GRID_GUTTER + col * (THUMB_W + GRID_GUTTER);
            int y = GRID_Y_TOP + row * GRID_ROW_H;
            M5.Display.setClipRect(x - 6, y - 6, THUMB_W + 12, THUMB_H + 40);
            menuCacheSprite.pushSprite(0, 0);
            M5.Display.clearClipRect();
        }
        if (menuSelected >= menuScroll && menuSelected < end) {
            int relIdx = menuSelected - menuScroll;
            int row = relIdx / GRID_COLS;
            int col = relIdx % GRID_COLS;
            int x = GRID_GUTTER + col * (THUMB_W + GRID_GUTTER);
            int y = GRID_Y_TOP + row * GRID_ROW_H;
            M5.Display.setClipRect(x - 6, y - 6, THUMB_W + 12, THUMB_H + 40);
            menuCacheSprite.pushSprite(0, 0);
            M5.Display.clearClipRect();
            M5.Display.drawRoundRect(x - 5, y - 5, THUMB_W + 10, THUMB_H + 10, UI_RADIUS + 2, TFT_BLACK);
            M5.Display.drawRoundRect(x - 4, y - 4, THUMB_W + 8, THUMB_H + 8, UI_RADIUS + 1, TFT_BLACK);
            M5.Display.fillRect(x + (THUMB_W / 2) - 15, y + THUMB_H + 30, 30, 3, TFT_BLACK);
        }
    }
    prevMenuSelected = menuSelected;
    M5.Display.display();
    M5.Display.endWrite();
}

void drawControlCenter() {
    forceFullMenuRedraw = true;
    prepareSprite(gSprite, DISPLAY_W, DISPLAY_H, 8, true);
    if (!gSprite.getBuffer()) return;
    gSprite.fillScreen(0x0000);
    int modW = 480;
    int modH = 380;
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
    gSprite.setCursor(modX + 30, modY + 85);
    gSprite.printf("Battery: %d%%", bat);
    int pW = modW - 60;
    int pX = modX + 30;
    int pY = modY + 120;
    gSprite.drawRoundRect(pX, pY, pW, 40, 5, TFT_BLACK);
    gSprite.fillRoundRect(pX + 4, pY + 4, (pW - 8) * bat / 100, 32, 3, 0xBBBB);
    int btnW = modW - 60;
    int btnH = 65;
    int btnX = modX + 30;
    int btnY = modY + 210;
    gSprite.fillRoundRect(btnX, btnY, btnW, btnH, UI_RADIUS, TFT_BLACK);
    gSprite.setTextColor(TFT_WHITE, TFT_BLACK);
    gSprite.setFont(&fonts::DejaVu24);
    gSprite.setTextDatum(middle_center);
    gSprite.drawString("SHUTDOWN", btnX + btnW / 2, btnY + btnH / 2);
    gSprite.setTextDatum(top_left);
    gSprite.setTextColor(0x8888, TFT_WHITE);
    gSprite.setFont(&fonts::DejaVu12);
    gSprite.setCursor(modX + 30, modY + modH - 40);
    gSprite.print("Swipe UP or tap outside to close");
    M5.Display.startWrite();
    gSprite.pushSprite(0, 0);
    M5.Display.display();
    M5.Display.endWrite();
}

void systemShutdown() {
    setCpuFrequencyMhz(240);
    M5.Display.setEpdMode(epd_mode_t::epd_quality);
    std::vector<String> pics;
    File root = SD.open(PIC_ROOT);
    if (root && root.isDirectory()) {
        File entry;
        while ((entry = root.openNextFile())) {
            if (!entry.isDirectory()) {
                String name = entry.name();
                if (name.endsWith(".jpg") || name.endsWith(".jpeg") || name.endsWith(".JPG") || name.endsWith(".JPEG")) {
                    pics.push_back(name);
                }
            }
            entry.close();
        }
        root.close();
    }
    bool drawn = false;
    if (!pics.empty()) {
        int r = random(pics.size());
        String path = pics[r];
        if (!path.startsWith("/")) path = String(PIC_ROOT) + "/" + path;
        prepareSprite(gSprite, DISPLAY_W, DISPLAY_H, 16, true);
        if (gSprite.getBuffer()) {
            File f = SD.open(path.c_str());
            if (f) {
                if (gSprite.drawJpg(&f, 0, 0, DISPLAY_W, DISPLAY_H, 0, 0, 0.0f, 0.0f, datum_t::top_left)) {
                    M5.Display.startWrite();
                    gSprite.pushSprite(0, 0);
                    M5.Display.display();
                    M5.Display.endWrite();
                    drawn = true;
                }
                f.close();
            }
        }
    }
    if (!drawn) {
        M5.Display.fillScreen(TFT_WHITE);
        M5.Display.display();
    }
    delay(2000);
    M5.Power.powerOff();
}

void drawBookConfig() {
    forceFullMenuRedraw = true;
    prepareSprite(gSprite, DISPLAY_W, DISPLAY_H, 8, true);
    if (!gSprite.getBuffer()) return;
    gSprite.fillScreen(0x0000);
    int modW = 480;
    int modH = 500; // Increased to fit dithering toggle
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

    int btnW = modW - 60;
    int btnX = modX + 30;
    int btnH = 60;
    gSprite.setTextDatum(middle_center);

    // Dithering Toggle Button
    int btnY0 = modY + 190;
    gSprite.drawRoundRect(btnX, btnY0, btnW, btnH, UI_RADIUS, 0x8888);
    gSprite.setTextColor(TFT_BLACK, TFT_WHITE);
    gSprite.setFont(&fonts::DejaVu18);
    String ditherMsg = "DITHERING: " + String(isDitheringEnabled ? "ON" : "OFF");
    gSprite.drawString(ditherMsg, btnX + btnW / 2, btnY0 + btnH / 2);

    // Bookmark Page
    int btnY1 = modY + 265;
    gSprite.drawRoundRect(btnX, btnY1, btnW, btnH, UI_RADIUS, TFT_BLACK);
    gSprite.setTextColor(TFT_BLACK, TFT_WHITE);
    gSprite.setFont(&fonts::DejaVu18);
    gSprite.drawString("BOOKMARK PAGE", btnX + btnW / 2, btnY1 + btnH / 2);

    // Return to Library
    int btnY2 = modY + 340;
    gSprite.fillRoundRect(btnX, btnY2, btnW, btnH, UI_RADIUS, TFT_BLACK);
    gSprite.setTextColor(TFT_WHITE, TFT_BLACK);
    gSprite.setFont(&fonts::DejaVu18);
    gSprite.drawString("RETURN TO LIBRARY", btnX + btnW / 2, btnY2 + btnH / 2);

    gSprite.setTextDatum(top_left);
    gSprite.setTextColor(0x8888, TFT_WHITE);
    gSprite.setFont(&fonts::DejaVu12);
    gSprite.setCursor(modX + 30, modY + modH - 40);
    gSprite.print("Swipe DOWN or tap outside to close");
    M5.Display.startWrite();
    gSprite.pushSprite(0, 0);
    M5.Display.display();
    M5.Display.endWrite();
}

void drawBookmarks() {
    forceFullMenuRedraw = true;
    prepareSprite(gSprite, DISPLAY_W, DISPLAY_H, 8, true);
    if (!gSprite.getBuffer()) return;
    gSprite.fillScreen(TFT_WHITE);
    gSprite.fillRect(0, 0, DISPLAY_W, 80, TFT_BLACK);
    gSprite.setTextColor(TFT_WHITE, TFT_BLACK);
    gSprite.setFont(&fonts::DejaVu24);
    gSprite.setCursor(20, 24);
    if (selectedBookmarkFolder == "") gSprite.print("Bookmark Library");
    else {
        String t = selectedBookmarkFolder;
        if (t.length() > 15) t = t.substring(0, 13) + "..";
        gSprite.printf("< %s", t.c_str());
    }
    int itemsPerPage = (DISPLAY_H - 200) / 90;
    int totalItems = 0;
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
            totalItems = uniqueFolders.size();
            int count = 0;
            for (int i = 0; i < (int)uniqueFolders.size(); i++) {
                if (i < bookmarkScroll) continue;
                if (count >= itemsPerPage) break;
                gSprite.drawRoundRect(10, yOff, DISPLAY_W - 20, itemH, 10, 0x8888);
                gSprite.setTextColor(TFT_BLACK, TFT_WHITE);
                gSprite.setFont(&fonts::DejaVu18);
                gSprite.setCursor(30, yOff + 28);
                String t = uniqueFolders[i];
                if (t.length() > 22) t = t.substring(0, 20) + "..";
                gSprite.print(t);
                gSprite.setFont(&fonts::DejaVu12);
                gSprite.setTextColor(0x8888, TFT_WHITE);
                gSprite.setCursor(DISPLAY_W - 80, yOff + 33);
                gSprite.print("OPEN >");
                yOff += itemH + 10;
                count++;
            }
        } else {
            int count = 0;
            int folderItemIdx = 0;
            for (int i = 0; i < (int)bookmarks.size(); i++) {
                if (bookmarks[i].folder != selectedBookmarkFolder) continue;
                totalItems++;
                if (folderItemIdx < bookmarkScroll) { folderItemIdx++; continue; }
                if (count >= itemsPerPage) { folderItemIdx++; continue; }
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
                count++;
                folderItemIdx++;
            }
        }
    }
    if (totalItems > 0) {
        int curPg = (bookmarkScroll / itemsPerPage) + 1;
        int maxPg = (totalItems + itemsPerPage - 1) / itemsPerPage;
        gSprite.setTextColor(TFT_WHITE, TFT_BLACK);
        gSprite.setFont(&fonts::DejaVu12);
        gSprite.setCursor(DISPLAY_W - 100, 24);
        gSprite.printf("Pg %d/%d", curPg, maxPg);
    }
    gSprite.setTextColor(0x8888, TFT_WHITE);
    gSprite.setFont(&fonts::DejaVu12);
    gSprite.setCursor(20, DISPLAY_H - 45);
    if (selectedBookmarkFolder == "") gSprite.print("Swipe UP: Library");
    else                             gSprite.print("Swipe UP: Back to Titles");
    gSprite.setCursor(20, DISPLAY_H - 25);
    gSprite.print("Swipe L/R/D: Next/Prev Page");
    M5.Display.startWrite();
    gSprite.pushSprite(0, 0);
    M5.Display.display();
    M5.Display.endWrite();
}

void drawWifiServer() {
    forceFullMenuRedraw = true;
    if (!isWifiServerRunning()) startWifiServer();
    prepareSprite(gSprite, DISPLAY_W, DISPLAY_H, 8, true);
    if (!gSprite.getBuffer()) return;
    gSprite.fillScreen(TFT_WHITE);
    gSprite.fillRect(0, 0, DISPLAY_W, 80, TFT_BLACK);
    gSprite.setTextColor(TFT_WHITE, TFT_BLACK);
    gSprite.setFont(&fonts::DejaVu24);
    gSprite.setCursor(20, 24);
    gSprite.print("WiFi File Browser");
    gSprite.setTextColor(TFT_BLACK, TFT_WHITE);
    gSprite.setFont(&fonts::DejaVu18);
    int y = 150;
    gSprite.setCursor(40, y);
    gSprite.print("Access Point Started");
    y += 60;
    gSprite.setFont(&fonts::DejaVu24);
    gSprite.setCursor(40, y);
    gSprite.print("Connect to:");
    y += 40;
    gSprite.setTextColor(0x007BFF, TFT_WHITE);
    gSprite.setCursor(60, y);
    gSprite.print(getWifiSSID());
    y += 80;
    gSprite.setTextColor(TFT_BLACK, TFT_WHITE);
    gSprite.setFont(&fonts::DejaVu24);
    gSprite.setCursor(40, y);
    gSprite.print("Open in Browser:");
    y += 40;
    gSprite.setTextColor(0x28A745, TFT_WHITE);
    gSprite.setCursor(60, y);
    gSprite.printf("http://%s", getWifiIP().c_str());
    y += 120;
    gSprite.setTextColor(TFT_BLACK, TFT_WHITE);
    gSprite.setFont(&fonts::DejaVu12);
    gSprite.setCursor(40, y);
    gSprite.println("Features:");
    gSprite.println("  - Upload Files & Folders");
    gSprite.println("  - Delete & Rename items");
    gSprite.println("  - Mass selection support");
    int btnW = 300;
    int btnH = 80;
    int btnX = (DISPLAY_W - btnW) / 2;
    int btnY = DISPLAY_H - 150;
    gSprite.fillRoundRect(btnX, btnY, btnW, btnH, 10, TFT_BLACK);
    gSprite.setTextColor(TFT_WHITE, TFT_BLACK);
    gSprite.setFont(&fonts::DejaVu18);
    gSprite.setCursor(btnX + 65, btnY + 28);
    gSprite.print("STOP SERVER");
    M5.Display.startWrite();
    gSprite.pushSprite(0, 0);
    M5.Display.display();
    M5.Display.endWrite();
}

void drawPage() {
    forceFullMenuRedraw = true;
    if (totalPages == 0) {
        drawError("No images in this manga.");
        return;
    }

    setCpuFrequencyMhz(240);
    
    // 1. Instant turn logic: Check if requested page is preloaded
    if (isNextPageReady && preloadedPage == currentPage && preloadedMangaPath == currentMangaPath) {
        Serial.printf("Instant turn for page %d\n", currentPage + 1);
        
        // Swap buffers/sprites or just draw nextPageSprite to gSprite
        // The most robust way in LGFX without direct pointer hacking is pushSprite
        prepareSprite(gSprite, DISPLAY_W, DISPLAY_H, 16, true);
        nextPageSprite.pushSprite(&gSprite, 0, 0);
        
        isNextPageReady = false; // Used it up
    } else {
        String path = makePagePath(currentMangaPath, currentPage);
        Serial.printf("Drawing [%d/%d]: %s\n", currentPage + 1, totalPages, path.c_str());

        prepareSprite(gSprite, DISPLAY_W, DISPLAY_H, 16, true);
        if (!gSprite.getBuffer()) {
            setCpuFrequencyMhz(80);
            drawError("Sprite alloc failed.");
            return;
        }

        File pageFile = SD.open(path.c_str());
        if (!pageFile) {
            setCpuFrequencyMhz(80);
            drawError("Cannot open image.");
            return;
        }

        size_t fileSize = pageFile.size();
        ensureJpgBuffer(fileSize + 1024);
        bool decodeSuccess = false;

        if (jpgBuffer) {
            pageFile.read(jpgBuffer, fileSize);
            pageFile.close();
            decodeSuccess = gSprite.drawJpg(jpgBuffer, fileSize, 0, 0, DISPLAY_W, DISPLAY_H, 0, 0, 0.0f, 0.0f, datum_t::top_left);
        } else {
            decodeSuccess = gSprite.drawJpg(&pageFile, 0, 0, DISPLAY_W, DISPLAY_H, 0, 0, 0.0f, 0.0f, datum_t::top_left);
            pageFile.close();
        }

        if (!decodeSuccess) {
            setCpuFrequencyMhz(80);
            drawError("Cannot decode image.");
            return;
        }
        if (isDitheringEnabled) {
            applyFloydSteinberg(gSprite);
        }
    }

    M5.Display.startWrite();
    gSprite.pushSprite(0, 0);
    M5.Display.display();
    M5.Display.endWrite();

    // 2. Preload NEXT page in background (after rendering current)
    if (currentPage < totalPages - 1) {
        preloadPage(currentPage + 1);
    }

    setCpuFrequencyMhz(80);
}

void drawError(const char* msg) {
    forceFullMenuRedraw = true;
    M5.Display.fillScreen(TFT_WHITE);
    M5.Display.setFont(&fonts::DejaVu18);
    M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
    M5.Display.setCursor(20, 40);
    M5.Display.println(msg);
    M5.Display.display();
}
