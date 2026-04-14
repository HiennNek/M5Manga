/**
 * ============================================================
 *  M5PaperS3 Manga Reader  –  Portrait Mode (540 × 960)
 * ============================================================
 *  Expected filename format inside each manga folder:
 *    m5_0000.jpg
 *    m5_0001.jpg
 *    m5_0002.jpg  ...
 *
 *  Because filenames are predictable, NO directory scanning is
 *  needed at all:
 *    • Page path  = sprintf("/manga/<folder>/m5_%04d.jpg", page)
 *    • Page count = binary-search for the last existing file
 *      (finds the count of a 2000-image folder in ~11 probes)
 *
 *  Controls (touch):
 *    - Tap RIGHT half  → next image
 *    - Tap LEFT  half  → previous image
 *    - Swipe UP        → return to main menu
 * ============================================================
 */

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <M5Unified.h>
#include <M5GFX.h>
#include <vector>
#include <algorithm>
#include <Preferences.h>

// ── SD SPI pins (M5PaperS3) ──────────────────────────────────
#define SD_CS_PIN   47
#define SD_SCK_PIN  39
#define SD_MOSI_PIN 38
#define SD_MISO_PIN 40

// ── Display geometry ─────────────────────────────────────────
#define DISPLAY_W    540
#define DISPLAY_H    960
#define LEFT_ZONE_W  (DISPLAY_W / 2)
#define SWIPE_UP_MIN  80

// ── Manga root ───────────────────────────────────────────────
#define MANGA_ROOT "/manga"

// ── Filename pattern ─────────────────────────────────────────
// Produces:  /manga/Title/m5_0000.jpg
#define IMG_PREFIX  "m5_"
#define IMG_SUFFIX  ".jpg"
#define IMG_DIGITS  4               // zero-pad width

// ── App state ────────────────────────────────────────────────
enum AppState { STATE_MENU, STATE_READER, STATE_BOOKMARKS };
AppState appState = STATE_MENU;

// ── Menu state ───────────────────────────────────────────────
std::vector<String> mangaFolders;
int menuSelected = 0;
int menuScroll   = 0;

// ── Reader state ─────────────────────────────────────────────
String currentMangaPath;    // e.g. "/manga/MyManga1"
int    totalPages  = 0;     // found via binary search, cached
int    currentPage = 0;
bool   needRedraw  = true;
bool   controlMenuOpen = false;
bool   bookConfigOpen = false;
int    readerContrastBias = 0;  // 0 = Normal, negative = darker, positive = lighter
String selectedBookmarkFolder = ""; // For hierarchical bookmarks
epd_mode_t currentEpdMode = epd_mode_t::epd_fast;

// ── Persistence ──────────────────────────────────────────────
Preferences prefs;
String lastMangaPath = "";
int    lastPage      = 0;
String lastMangaName = "";

// ── Bookmarks ────────────────────────────────────────────────
struct Bookmark {
    String folder; // e.g. "MyManga"
    int    page;
};
std::vector<Bookmark> bookmarks;
void loadBookmarks();
void saveBookmarks();
void addBookmark(String folder, int page);
void deleteBookmark(int idx);

// ── Forward declarations ──────────────────────────────────────
void   sdInit();
void   scanMangaFolders();
String makePagePath(const String& folder, int n);
bool   pageExists(const String& folder, int n);
int    findTotalPages(const String& folder);
void   drawMenu();
void   drawPage();
void   drawError(const char* msg);
void   handleTouch();
void   handleMenuTouch(const m5::touch_detail_t& t);
void   handleReaderTouch(const m5::touch_detail_t& t);
void   handleControlTouch(const m5::touch_detail_t& t);
void   handleBookmarksTouch(const m5::touch_detail_t& t);
void   openManga(int idx);
void   openMangaPath(const String& path, int page);
void   saveProgress();
void   loadProgress();
void   updateLastMangaName();
void   drawControlCenter();
void   drawBookConfig();
void   drawBookmarks();
void   systemShutdown();
void   handleBookConfigTouch(const m5::touch_detail_t& t);

// ─────────────────────────────────────────────────────────────
//  Path helper  →  /manga/Title/m5_0042.jpg
// ─────────────────────────────────────────────────────────────
String makePagePath(const String& folder, int n) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%s%0*d%s", IMG_PREFIX, IMG_DIGITS, n, IMG_SUFFIX);
    return folder + "/" + buf;
}

// ─────────────────────────────────────────────────────────────
//  Check existence of page n without reading its content.
//  SD.exists() is the fastest probe available.
// ─────────────────────────────────────────────────────────────
bool pageExists(const String& folder, int n) {
    return SD.exists(makePagePath(folder, n).c_str());
}

// ─────────────────────────────────────────────────────────────
//  Binary search for total page count.
//
//  Instead of scanning every file, we find the boundary between
//  existing and non-existing indices:
//    • Exponential probe  – doubles the guess until we overshoot
//    • Binary search      – narrows down to the exact last index
//
//  For 2000 images this takes ≈ 11 + 11 = 22 SD.exists() calls,
//  compared to 2000 calls for a linear scan. Typically < 100 ms.
// ─────────────────────────────────────────────────────────────
int findTotalPages(const String& folder) {
    // Must have at least page 0
    if (!pageExists(folder, 0)) return 0;

    // ── Phase 1: exponential probe to find an upper bound ────
    int hi = 1;
    while (pageExists(folder, hi)) {
        hi *= 2;
        if (hi > 100000) { hi = 100000; break; }  // safety cap
    }

    // ── Phase 2: binary search between hi/2 and hi ───────────
    int lo = hi / 2;
    while (lo + 1 < hi) {
        int mid = lo + (hi - lo) / 2;
        if (pageExists(folder, mid)) lo = mid;
        else                         hi = mid;
    }

    return lo + 1;  // lo is the last valid 0-based index → count = lo+1
}

// ─────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);

    auto cfg = M5.config();
    M5.begin(cfg);

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

// ─────────────────────────────────────────────────────────────
void loop() {
    M5.update();

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
    delay(20);
}

// ─────────────────────────────────────────────────────────────
//  SD Initialisation
// ─────────────────────────────────────────────────────────────
void sdInit() {
    SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
    int retry = 0;
    while (!SD.begin(SD_CS_PIN, SPI, 25000000)) {
        M5.Display.printf("SD init failed (attempt %d)...\n", ++retry);
        M5.Display.display();
        delay(1000);
        if (retry >= 5) {
            drawError("SD card not found!\nInsert SD and reset.");
            while (1) delay(1000);
        }
    }
    Serial.println("SD OK");
}

// ─────────────────────────────────────────────────────────────
//  Scan /manga for sub-folders (runs once / on menu refresh)
// ─────────────────────────────────────────────────────────────
void scanMangaFolders() {
    mangaFolders.clear();

    File root = SD.open(MANGA_ROOT);
    if (!root || !root.isDirectory()) {
        Serial.println("No /manga directory");
        return;
    }

    File entry;
    while ((entry = root.openNextFile())) {
        if (entry.isDirectory()) {
            String name = entry.name();
            int slash = name.lastIndexOf('/');
            if (slash >= 0) name = name.substring(slash + 1);
            if (name.length() > 0 && !name.startsWith(".")) {
                mangaFolders.push_back(name);
                Serial.printf("  Folder: %s\n", name.c_str());
            }
        }
        entry.close();
    }
    root.close();

    std::sort(mangaFolders.begin(), mangaFolders.end());
    Serial.printf("Found %d manga folders\n", (int)mangaFolders.size());
}

// ─────────────────────────────────────────────────────────────
//  Persistence
// ─────────────────────────────────────────────────────────────
void saveProgress() {
    prefs.begin("manga", false);
    prefs.putString("lastPath", currentMangaPath);
    prefs.putInt("lastPage", currentPage);
    prefs.end();
    
    lastMangaPath = currentMangaPath;
    lastPage = currentPage;
    updateLastMangaName();
}

void loadProgress() {
    prefs.begin("manga", true);
    lastMangaPath = prefs.getString("lastPath", "");
    lastPage = prefs.getInt("lastPage", 0);
    prefs.end();
    updateLastMangaName();
}

void updateLastMangaName() {
    if (lastMangaPath.length() == 0) {
        lastMangaName = "";
        return;
    }
    int slash = lastMangaPath.lastIndexOf('/');
    if (slash >= 0) lastMangaName = lastMangaPath.substring(slash + 1);
    else            lastMangaName = lastMangaPath;
}

// ─────────────────────────────────────────────────────────────
//  Bookmarks Engine
// ─────────────────────────────────────────────────────────────
void loadBookmarks() {
    bookmarks.clear();
    File f = SD.open("/manga/bookmarks.csv", FILE_READ);
    if (!f) return;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;
        int comma = line.indexOf(',');
        if (comma > 0) {
            Bookmark b;
            b.folder = line.substring(0, comma);
            b.page = line.substring(comma + 1).toInt();
            bookmarks.push_back(b);
        }
    }
    f.close();
}

void saveBookmarks() {
    File f = SD.open("/manga/bookmarks.csv", FILE_WRITE);
    if (!f) return;
    for (const auto& b : bookmarks) {
        f.printf("%s,%d\n", b.folder.c_str(), b.page);
    }
    f.close();
}

void addBookmark(String folder, int page) {
    if (folder.length() == 0) return;
    // Check for duplicate
    for (const auto& b : bookmarks) {
        if (b.folder == folder && b.page == page) return;
    }
    bookmarks.push_back({folder, page});
    saveBookmarks();
}

void deleteBookmark(int idx) {
    if (idx < 0 || idx >= (int)bookmarks.size()) return;
    bookmarks.erase(bookmarks.begin() + idx);
    saveBookmarks();
}

// ── Drawing: Main Menu
// ─────────────────────────────────────────────────────────────
#define GRID_COLS      2
#define GRID_GUTTER   30
#define GRID_Y_TOP    110
#define THUMB_W       ((DISPLAY_W - (GRID_COLS + 1) * GRID_GUTTER) / GRID_COLS)
#define THUMB_H       (int(THUMB_W * 1.41))      // Standard A4-ish aspect
#define GRID_ROW_H    (THUMB_H + 60)
#define MENU_VISIBLE  (GRID_COLS * 2)             // 2 rows visible
#define UI_RADIUS     10

void drawMenu() {
    static LGFX_Sprite spr(&M5.Display);
    spr.setPsram(true);
    spr.setColorDepth(8);
    if (!spr.createSprite(DISPLAY_W, DISPLAY_H)) {
        M5.Display.fillScreen(TFT_WHITE);
        return;
    }
    spr.fillScreen(TFT_WHITE);

    // Title bar
    spr.fillRect(0, 0, DISPLAY_W, 80, TFT_BLACK);
    spr.setFont(&fonts::DejaVu24);
    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.setCursor(GRID_GUTTER, 24);
    spr.print("Library");

    spr.setFont(&fonts::DejaVu12);
    spr.setCursor(GRID_GUTTER, 54);
    spr.printf("%d titles available", (int)mangaFolders.size());

    // Total items including Bookmarks button
    int totalItems = (int)mangaFolders.size() + 1;

    // Continue Reading Block (if exists) at the Bottom
    if (lastMangaName.length() > 0) {
        int barW = DISPLAY_W - (GRID_GUTTER * 2);
        int barH = 50;
        int barX = GRID_GUTTER;
        int barY = DISPLAY_H - 82; // Positioned above navigation hints
        
        spr.fillRoundRect(barX, barY, barW, barH, UI_RADIUS, 0x3333); // Dark grey
        spr.drawRoundRect(barX, barY, barW, barH, UI_RADIUS, TFT_WHITE);
        
        spr.setTextColor(TFT_WHITE, 0x3333);
        spr.setFont(&fonts::DejaVu12);
        spr.setCursor(barX + 10, barY + 10);
        spr.print("CONTINUE: ");
        
        spr.setFont(&fonts::DejaVu18);
        String shortName = lastMangaName;
        if (shortName.length() > 20) shortName = shortName.substring(0, 18) + "..";
        spr.print(shortName);
        
        spr.setFont(&fonts::DejaVu12);
        spr.setCursor(barX + 10, barY + 30);
        spr.printf("Page %d", lastPage + 1);
        
        spr.setCursor(barX + barW - 80, barY + 30);
        spr.print("RESUME >");
    }

    if (mangaFolders.empty()) {
        spr.setFont(&fonts::DejaVu18);
        spr.setTextColor(TFT_BLACK, TFT_WHITE);
        spr.setCursor(GRID_GUTTER, 120);
        spr.println("No manga found in /manga/");
        M5.Display.startWrite();
        spr.pushSprite(0, 0);
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

        // Selection highlight
        if (i == menuSelected) {
            spr.drawRoundRect(x - 5, y - 5, THUMB_W + 10, THUMB_H + 10, UI_RADIUS + 2, TFT_BLACK);
            spr.drawRoundRect(x - 4, y - 4, THUMB_W + 8, THUMB_H + 8, UI_RADIUS + 1, TFT_BLACK);
        }

        // Draw cover frame
        spr.drawRoundRect(x, y, THUMB_W, THUMB_H, UI_RADIUS, 0x8888); // Mid-grey border

        if (i == 0) {
            // ── Bookmarks icon (First Frame) ──────────────────
            spr.fillRoundRect(x + 10, y + 10, THUMB_W - 20, THUMB_H - 20, UI_RADIUS, 0xEEEE);
            spr.setTextColor(TFT_BLACK, 0xEEEE);
            spr.setFont(&fonts::DejaVu24);
            spr.setCursor(x + 25, y + THUMB_H/2 - 20);
            spr.print("   ★");
            spr.setFont(&fonts::DejaVu12);
            spr.setCursor(x + 25, y + THUMB_H/2 + 20);
            spr.print("Bookmarks");
            
            // Title below frame
            spr.setFont(&fonts::DejaVu12);
            spr.setTextColor(TFT_BLACK, TFT_WHITE);
            spr.setCursor(x + THUMB_W/2 - 35, y + THUMB_H + 10);
            spr.print("MY SAVES");
        } else {
            // ── Manga cover (m5_0000.jpg) ─────────────────────
            int fIdx = i - 1;
            String coverPath = makePagePath(String(MANGA_ROOT) + "/" + mangaFolders[fIdx], 0);
            if (SD.exists(coverPath.c_str())) {
                float imgAspect = 540.0f / 960.0f;
                int scaledW = (int)((THUMB_H - 4) * imgAspect);
                int xOffset = (THUMB_W - 4 - scaledW) / 2;
                if (xOffset < 0) xOffset = 0;

                spr.drawJpgFile(SD, coverPath.c_str(), 
                                x + 2 + xOffset, y + 2, 
                                THUMB_W - 4, THUMB_H - 4, 
                                0, 0, 0.0f, 0.0f);
            } else {
                spr.setTextColor(0x8888, TFT_WHITE);
                spr.setFont(&fonts::DejaVu12);
                spr.setCursor(x + 10, y + THUMB_H / 2);
                spr.print("NO COVER");
            }

            // Title
            spr.setFont(&fonts::DejaVu12);
            spr.setTextColor(TFT_BLACK, TFT_WHITE);
            String title = mangaFolders[fIdx];
            if (title.length() > 18) title = title.substring(0, 16) + "..";
            int textX = x + (THUMB_W - (title.length() * 7)) / 2;
            spr.setCursor(std::max(x, textX), y + THUMB_H + 10);
            spr.print(title);
        }
        
        if (i == menuSelected) {
            spr.fillRect(x + (THUMB_W/2) - 15, y + THUMB_H + 30, 30, 3, TFT_BLACK);
        }
    }

    // Navigation hints
    spr.setFont(&fonts::DejaVu12);
    spr.setTextColor(0x8888, TFT_WHITE);
    spr.setCursor(GRID_GUTTER, DISPLAY_H - 24);
    spr.print("Swipe UP: Refresh  |  Swipe DOWN: Next Page");

    M5.Display.startWrite();
    spr.pushSprite(0, 0);
    M5.Display.display();
    M5.Display.endWrite();
    
    spr.deleteSprite();
}

// ─────────────────────────────────────────────────────────────
//  Control Center UI (Floating Modal)
// ─────────────────────────────────────────────────────────────
void drawControlCenter() {
    static LGFX_Sprite spr(&M5.Display);
    spr.setPsram(true);
    spr.setColorDepth(8);
    if (!spr.createSprite(DISPLAY_W, DISPLAY_H)) return;

    // 1. Draw Background (dimmed)
    if (appState == STATE_MENU) {
        // We reuse the logic from drawMenu but without the spr.deleteSprite()
        // For simplicity, let's just draw a light grey screen or capture?
        // Actually, just drawing the menu again into this sprite:
        spr.fillScreen(TFT_WHITE);
        spr.fillRect(0, 0, DISPLAY_W, 80, TFT_BLACK);
        // ... (this is repetitive, let's just use a shortcut: 
        // draw the backdrop first, then a grey veil)
    }
    
    // To keep it simple and clean: 
    // Fill background with grey then draw white modal
    spr.fillScreen(0x8888); // Dim background

    // 2. Draw White Modal Box
    int modW = 400;
    int modH = 460;
    int modX = (DISPLAY_W - modW) / 2;
    int modY = (DISPLAY_H - modH) / 2;
    
    spr.fillRoundRect(modX, modY, modW, modH, UI_RADIUS * 2, TFT_WHITE);
    spr.drawRoundRect(modX, modY, modW, modH, UI_RADIUS * 2, TFT_BLACK);

    // Title
    spr.setTextColor(TFT_BLACK, TFT_WHITE);
    spr.setFont(&fonts::DejaVu24);
    spr.setCursor(modX + 30, modY + 30);
    spr.print("System");

    // Battery
    int bat = M5.Power.getBatteryLevel();
    spr.setFont(&fonts::DejaVu18);
    spr.setCursor(modX + 30, modY + 80);
    spr.printf("Battery: %d%%", bat);
    
    // Battery Visual
    int pW = 340;
    int pX = modX + 30;
    int pY = modY + 110;
    spr.drawRoundRect(pX, pY, pW, 40, 5, TFT_BLACK);
    spr.fillRoundRect(pX + 4, pY + 4, (pW - 8) * bat / 100, 32, 3, 0xBBBB);

    // Shutdown Button (Large)
    int btnW = 340;
    int btnH = 120;
    int btnX = modX + 30;
    int btnY = modY + 200;
    
    spr.fillRoundRect(btnX, btnY, btnW, btnH, UI_RADIUS, TFT_BLACK);
    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.setFont(&fonts::DejaVu24);
    spr.setCursor(btnX + 65, btnY + 45);
    spr.print("SHUTDOWN");

    // Info Hint
    spr.setTextColor(0x8888, TFT_WHITE);
    spr.setFont(&fonts::DejaVu12);
    spr.setCursor(modX + 30, modY + 380);
    spr.print("Swipe UP or tap outside to close");

    M5.Display.startWrite();
    spr.pushSprite(0, 0);
    M5.Display.display();
    M5.Display.endWrite();
    
    spr.deleteSprite();
}

void systemShutdown() {
    M5.Display.setEpdMode(epd_mode_t::epd_quality);
    M5.Display.fillScreen(TFT_WHITE);    
    M5.Display.display();
    delay(2000); // Give EPD plenty of time to finish the physical refresh
    M5.Power.powerOff();
}

// ─────────────────────────────────────────────────────────────
//  Bookmarks View
// ─────────────────────────────────────────────────────────────
//  Reader Config Menu
// ─────────────────────────────────────────────────────────────
void drawBookConfig() {
    static LGFX_Sprite spr(&M5.Display);
    spr.setPsram(true);
    spr.setColorDepth(8);
    if (!spr.createSprite(DISPLAY_W, DISPLAY_H)) return;

    spr.fillScreen(0x8888); // Dim background

    int modW = 480;
    int modH = 400;
    int modX = (DISPLAY_W - modW) / 2;
    int modY = (DISPLAY_H - modH) / 2;
    
    spr.fillRoundRect(modX, modY, modW, modH, UI_RADIUS * 2, TFT_WHITE);
    spr.drawRoundRect(modX, modY, modW, modH, UI_RADIUS * 2, TFT_BLACK);

    spr.setTextColor(TFT_BLACK, TFT_WHITE);
    spr.setFont(&fonts::DejaVu24);
    spr.setCursor(modX + 30, modY + 30);
    spr.print("Book Menu");

    // 1. Pagination Bar
    int barY = modY + 90;
    spr.drawRoundRect(modX + 20, barY, modW - 40, 80, UI_RADIUS, 0x8888);
    
    spr.setFont(&fonts::DejaVu24);
    spr.setCursor(modX + 45, barY + 25);  spr.print("<<");
    spr.setCursor(modX + 115, barY + 25); spr.print("<");
    
    String pg = String(currentPage + 1) + " / " + String(totalPages);
    int pgW = pg.length() * 14;
    spr.setCursor(modX + (modW - pgW)/2, barY + 25);
    spr.print(pg);
    
    spr.setCursor(modX + modW - 135, barY + 25); spr.print(">");
    spr.setCursor(modX + modW - 85, barY + 25);  spr.print(">>");

    // 2. Contrast Row
    int conY = modY + 180;
    spr.drawRoundRect(modX + 20, conY, modW - 40, 60, UI_RADIUS, 0x8888);
    spr.setFont(&fonts::DejaVu18);
    spr.setCursor(modX + 45, conY + 20); spr.print("[-]");
    
    String conText = "Contrast: ";
    if (readerContrastBias == 0) conText += "Normal";
    else if (readerContrastBias < 0) conText += "Darker (" + String(abs(readerContrastBias)) + ")";
    else conText += "Lighter (" + String(readerContrastBias) + ")";
    
    int conTextW = conText.length() * 10;
    spr.setCursor(modX + (modW - conTextW)/2, conY + 20);
    spr.print(conText);
    
    spr.setCursor(modX + modW - 75, conY + 20); spr.print("[+]");

    // 3. Bookmark Button
    int btnX = modX + 20;
    int btnW = modW - 40;
    int btnH = 60;
    int btnY1 = modY + 255;
    spr.drawRoundRect(btnX, btnY1, btnW, btnH, UI_RADIUS, TFT_BLACK);
    spr.setFont(&fonts::DejaVu18);
    spr.setCursor(btnX + 100, btnY1 + 20);
    spr.print("BOOKMARK PAGE");

    // 4. Exit Button
    int btnY2 = modY + 325;
    spr.fillRoundRect(btnX, btnY2, btnW, btnH, UI_RADIUS, TFT_BLACK);
    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.setCursor(btnX + 85, btnY2 + 25);
    spr.print("RETURN TO LIBRARY");

    M5.Display.startWrite();
    spr.pushSprite(0, 0);
    M5.Display.display();
    M5.Display.endWrite();
    spr.deleteSprite();
}

void handleBookConfigTouch(const m5::touch_detail_t& t) {
    if (!t.wasReleased()) return;

    int tapX = t.x;
    int tapY = t.y;

    int modW = 480;
    int modH = 400;
    int modX = (DISPLAY_W - modW) / 2;
    int modY = (DISPLAY_H - modH) / 2;

    // Out of bounds -> Close
    if (tapX < modX || tapX > modX + modW || tapY < modY || tapY > modY + modH) {
        bookConfigOpen = false;
        needRedraw = true;
        return;
    }

    // Pagination Row
    int barY = modY + 90;
    if (tapY >= barY && tapY <= barY + 80) {
        if (tapX < modX + 100) { // <<
            currentPage = std::max(0, currentPage - 10);
            needRedraw = true;
        } else if (tapX < modX + 180) { // <
            currentPage = std::max(0, currentPage - 1);
            needRedraw = true;
        } else if (tapX > modX + modW - 100) { // >>
            currentPage = std::min(totalPages - 1, currentPage + 10);
            needRedraw = true;
        } else if (tapX > modX + modW - 180) { // >
            currentPage = std::min(totalPages - 1, currentPage + 1);
            needRedraw = true;
        }
        if (needRedraw) {
            saveProgress();
            readerContrastBias = 0; // Reset contrast on page change
            currentEpdMode = epd_mode_t::epd_quality;
            // bookConfigOpen = false; // STAYS OPEN as requested
        }
    }

    // Contrast Row
    int conY = modY + 180;
    if (tapY >= conY && tapY <= conY + 60) {
        if (tapX < modX + 100) { // [-]
            readerContrastBias -= 20;
            if (readerContrastBias < -100) readerContrastBias = -100;
            needRedraw = true;
        } else if (tapX > modX + modW - 100) { // [+]
            readerContrastBias += 20;
            if (readerContrastBias > 100) readerContrastBias = 100;
            needRedraw = true;
        } else if (tapX > modX + 180 && tapX < modX + modW - 180) { // Reset (center tap)
            readerContrastBias = 0;
            needRedraw = true;
        }
        if (needRedraw) currentEpdMode = epd_mode_t::epd_quality;
    }

    // Bookmark
    if (tapX >= modX + 20 && tapX <= modX + modW - 20 &&
        tapY >= modY + 255 && tapY <= modY + 315) {
        int lastSlash = currentMangaPath.lastIndexOf('/');
        String folder = currentMangaPath.substring(lastSlash + 1);
        addBookmark(folder, currentPage);
        // bookConfigOpen = false; // STAYS OPEN
        needRedraw = true;
    }

    // Exit
    if (tapX >= modX + 20 && tapX <= modX + modW - 20 &&
        tapY >= modY + 325 && tapY <= modY + 385) {
        appState = STATE_MENU;
        bookConfigOpen = false;
        currentEpdMode = epd_mode_t::epd_quality;
        needRedraw = true;
    }
}
void drawBookmarks() {
    static LGFX_Sprite spr(&M5.Display);
    spr.setPsram(true);
    spr.setColorDepth(8);
    if (!spr.createSprite(DISPLAY_W, DISPLAY_H)) return;

    spr.fillScreen(TFT_WHITE);

    // Header
    spr.fillRect(0, 0, DISPLAY_W, 80, TFT_BLACK);
    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.setFont(&fonts::DejaVu24);
    spr.setCursor(20, 24);
    
    if (selectedBookmarkFolder == "") {
        spr.print("Bookmark Library");
    } else {
        String t = selectedBookmarkFolder;
        if (t.length() > 15) t = t.substring(0, 13) + "..";
        spr.printf("< %s", t.c_str());
    }

    if (bookmarks.empty()) {
        spr.setTextColor(TFT_BLACK, TFT_WHITE);
        spr.setFont(&fonts::DejaVu18);
        spr.setCursor(40, 150);
        spr.print("No bookmarks yet.");
    } else {
        int yOff = 100;
        int itemH = 80;

        if (selectedBookmarkFolder == "") {
            // ─── Level 1: List Unique Books ──────────────────
            std::vector<String> uniqueFolders;
            for (const auto& b : bookmarks) {
                if (std::find(uniqueFolders.begin(), uniqueFolders.end(), b.folder) == uniqueFolders.end()) {
                    uniqueFolders.push_back(b.folder);
                }
            }
            std::sort(uniqueFolders.begin(), uniqueFolders.end());

            for (const auto& folder : uniqueFolders) {
                if (yOff + itemH > DISPLAY_H - 100) break;
                spr.drawRoundRect(10, yOff, DISPLAY_W - 20, itemH, 10, 0x8888);
                spr.setTextColor(TFT_BLACK, TFT_WHITE);
                spr.setFont(&fonts::DejaVu18);
                spr.setCursor(30, yOff + 28);
                String t = folder;
                if (t.length() > 22) t = t.substring(0, 20) + "..";
                spr.print(t);
                
                spr.setFont(&fonts::DejaVu12);
                spr.setTextColor(0x8888, TFT_WHITE);
                spr.setCursor(DISPLAY_W - 80, yOff + 33);
                spr.print("OPEN >");
                
                yOff += itemH + 10;
            }
        } else {
            // ─── Level 2: List Pages for Selected Book ───────
            for (int i = 0; i < (int)bookmarks.size(); i++) {
                if (bookmarks[i].folder != selectedBookmarkFolder) continue;
                if (yOff + itemH > DISPLAY_H - 100) break;

                spr.drawRoundRect(10, yOff, DISPLAY_W - 20, itemH, 10, 0x8888);
                spr.setTextColor(TFT_BLACK, TFT_WHITE);
                spr.setFont(&fonts::DejaVu18);
                spr.setCursor(30, yOff + 28);
                spr.printf("Page %d", bookmarks[i].page + 1);

                // Delete button
                spr.fillRect(DISPLAY_W - 100, yOff + 20, 70, 40, 0xEEEE);
                spr.drawRoundRect(DISPLAY_W - 100, yOff + 20, 70, 40, 5, TFT_BLACK);
                spr.setTextColor(TFT_BLACK, 0xEEEE);
                spr.setFont(&fonts::DejaVu12);
                spr.setCursor(DISPLAY_W - 85, yOff + 33);
                spr.print("DEL");

                yOff += itemH + 10;
            }
        }
    }

    // Footnote
    spr.setTextColor(0x8888, TFT_WHITE);
    spr.setFont(&fonts::DejaVu12);
    spr.setCursor(20, DISPLAY_H - 30);
    if (selectedBookmarkFolder == "") spr.print("Swipe UP: Library");
    else                             spr.print("Swipe UP: Back to Titles");

    M5.Display.startWrite();
    spr.pushSprite(0, 0);
    M5.Display.display();
    M5.Display.endWrite();
    spr.deleteSprite();
}

// ─────────────────────────────────────────────────────────────
//  Drawing: Reader page
// ─────────────────────────────────────────────────────────────
void drawPage() {
    if (totalPages == 0) {
        drawError("No images in this manga.");
        return;
    }

    // Path is computed directly — no scanning needed
    String path = makePagePath(currentMangaPath, currentPage);
    Serial.printf("Drawing [%d/%d]: %s\n", currentPage + 1, totalPages, path.c_str());

    // ── Load JPEG into PSRAM ──────────────────────────────────
    File f = SD.open(path.c_str(), FILE_READ);
    if (!f) {
        drawError("Cannot open image file.");
        return;
    }

    size_t fileSize = f.size();
    Serial.printf("JPEG size: %u bytes\n", fileSize);

    uint8_t* jpgBuf = (uint8_t*)heap_caps_malloc(fileSize,
                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!jpgBuf) jpgBuf = (uint8_t*)malloc(fileSize);
    if (!jpgBuf) {
        f.close();
        drawError("Not enough RAM for image.");
        return;
    }

    size_t bytesRead = f.read(jpgBuf, fileSize);
    f.close();

    if (bytesRead != fileSize) {
        heap_caps_free(jpgBuf);
        drawError("SD read error.");
        return;
    }

    // ── Offscreen Sprite → single EPD refresh ────────────────
    static LGFX_Sprite sprite(&M5.Display);
    sprite.setPsram(true);
    sprite.setColorDepth(8);

    if (!sprite.createSprite(DISPLAY_W, DISPLAY_H)) {
        heap_caps_free(jpgBuf);
        drawError("Sprite alloc failed.");
        return;
    }

    sprite.fillScreen(TFT_WHITE);
    sprite.drawJpg(jpgBuf, fileSize,
                   0, 0, DISPLAY_W, DISPLAY_H,
                   0, 0, 0.0f, 0.0f,
                   datum_t::top_left);

    heap_caps_free(jpgBuf);

    // ── Apply Software Contrast ──────────────────────────────
    if (readerContrastBias != 0) {
        uint8_t* pix = (uint8_t*)sprite.getBuffer();
        int count = DISPLAY_W * DISPLAY_H;
        for (int i = 0; i < count; i++) {
            int v = pix[i] + readerContrastBias;
            if (v < 0)   v = 0;
            if (v > 255) v = 255;
            pix[i] = (uint8_t)v;
        }
    }

    // Top bar removed as requested for full-screen reading.
    // Swiping up will still return you to the menu.

    M5.Display.startWrite();
    sprite.pushSprite(0, 0);
    M5.Display.display();
    M5.Display.endWrite();

    sprite.deleteSprite();
}

// ─────────────────────────────────────────────────────────────
void drawError(const char* msg) {
    M5.Display.fillScreen(TFT_WHITE);
    M5.Display.setFont(&fonts::DejaVu18);
    M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
    M5.Display.setCursor(20, 40);
    M5.Display.println(msg);
    M5.Display.display();
}

// ─────────────────────────────────────────────────────────────
//  Touch
// ─────────────────────────────────────────────────────────────
void handleTouch() {
    if (M5.Touch.getCount() == 0) return;
    auto& t = M5.Touch.getDetail(0);

    // Global: Pull down from top edge to open Control Center
    if (!controlMenuOpen && t.wasReleased() && t.base_y < 100 && t.distanceY() > SWIPE_UP_MIN) {
        controlMenuOpen = true;
        needRedraw = true;
        return;
    }

    if (controlMenuOpen)              handleControlTouch(t);
    else if (bookConfigOpen)          handleBookConfigTouch(t);
    else if (appState == STATE_MENU)      handleMenuTouch(t);
    else if (appState == STATE_BOOKMARKS) handleBookmarksTouch(t);
    else {
        // Reader logic: Swipe UP from bottom for menu, or standard tap navigation
        if (t.wasReleased() && t.base_y > DISPLAY_H - 150 && t.distanceY() < -SWIPE_UP_MIN) {
            bookConfigOpen = true;
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
            currentEpdMode = epd_mode_t::epd_quality;
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
        // ─── Level 1: Select Book ────────────────────────────
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
        // ─── Level 2: Select Page / Delete ───────────────────
        for (int i = 0; i < (int)bookmarks.size(); i++) {
            if (bookmarks[i].folder != selectedBookmarkFolder) continue;
            if (tapY >= yOff && tapY <= yOff + itemH) {
                // Check if delete button tapped
                if (tapX > DISPLAY_W - 120) {
                    deleteBookmark(i);
                    // If no more bookmarks for this folder, go back
                    bool remains = false;
                    for (const auto& b : bookmarks) { if (b.folder == selectedBookmarkFolder) remains = true; }
                    if (!remains) selectedBookmarkFolder = "";
                    
                    currentEpdMode = epd_mode_t::epd_fast;
                    needRedraw = true;
                    return;
                } else {
                    // Open bookmark
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

    // Swipe up to close
    if (t.distanceY() < -SWIPE_UP_MIN) {
        controlMenuOpen = false;
        currentEpdMode = epd_mode_t::epd_fast;
        needRedraw = true;
        return;
    }

    int tapX = t.x;
    int tapY = t.y;

    int modW = 400;
    int modH = 460;
    int modX = (DISPLAY_W - modW) / 2;
    int modY = (DISPLAY_H - modH) / 2;

    // Bookmark button logic removed (moved to Book Config)

    // Shutdown button area (centered in modal)
    if (tapX > modX + 30 && tapX < modX + 370 &&
        tapY > modY + 200 && tapY < modY + 320) {
        systemShutdown();
        return;
    }

    // Tap outside modal to close
    if (tapX < modX || tapX > modX + modW || tapY < modY || tapY > modY + modH) {
        controlMenuOpen = false;
        currentEpdMode = epd_mode_t::epd_fast;
        needRedraw = true;
    }
}

void handleMenuTouch(const m5::touch_detail_t& t) {
    if (!t.wasReleased()) return;
    int dy = t.distanceY();

    // Swipe up → Refresh
    if (dy < -SWIPE_UP_MIN) {
        scanMangaFolders();
        loadProgress(); 
        menuSelected = menuScroll = 0;
        currentEpdMode = epd_mode_t::epd_fast; 
        needRedraw = true;
        return;
    }

    // Swipe down → Scroll next page (4 items)
    if (dy > SWIPE_UP_MIN) {
        int totalItems = (int)mangaFolders.size() + 1;
        if (menuScroll + MENU_VISIBLE < totalItems) {
            menuScroll += MENU_VISIBLE;
            menuSelected = menuScroll;
            currentEpdMode = epd_mode_t::epd_fast;
            needRedraw = true;
        } else {
            // Optional: wrap to top
            menuScroll = 0;
            menuSelected = 0;
            currentEpdMode = epd_mode_t::epd_fast;
            needRedraw = true;
        }
        return;
    }

    // Grid tap detection
    int tapX = t.x;
    int tapY = t.y;

    // Check Continue Reading Block Tap (now at bottom)
    if (lastMangaName.length() > 0 && tapX > GRID_GUTTER && tapX < DISPLAY_W - GRID_GUTTER &&
        tapY > (DISPLAY_H - 82) && tapY < (DISPLAY_H - 32)) {
        openMangaPath(lastMangaPath, lastPage);
        return;
    }

    if (tapY < GRID_Y_TOP) return;

    int col = (tapX - (GRID_GUTTER / 2)) / (THUMB_W + GRID_GUTTER);
    int row = (tapY - GRID_Y_TOP) / GRID_ROW_H;
    
    if (col < 0) col = 0; if (col >= GRID_COLS) col = GRID_COLS - 1;
    if (row < 0) row = 0; if (row >= 2) row = 1; // 2 rows visible

    int idx = menuScroll + (row * GRID_COLS + col);
    int totalItems = (int)mangaFolders.size() + 1;
    if (idx >= totalItems) return;

    if (idx == menuSelected) {
        if (idx == 0) {
            appState = STATE_BOOKMARKS;
            selectedBookmarkFolder = ""; // Reset to level 1
            currentEpdMode = epd_mode_t::epd_fast; 
            needRedraw = true;
        } else {
            // Quality mode is set inside openMangaPath
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

    // Reset contrast on natural page turn
    readerContrastBias = 0;

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

// ─────────────────────────────────────────────────────────────
//  Open manga — binary-search count, then instant page access
// ─────────────────────────────────────────────────────────────
void openManga(int idx) {
    if (idx < 0 || idx >= (int)mangaFolders.size()) return;
    String path = String(MANGA_ROOT) + "/" + mangaFolders[idx];
    openMangaPath(path, 0);
}

void openMangaPath(const String& path, int page) {
    currentMangaPath = path;
    currentPage      = page;

    // Show brief status while binary search runs
    M5.Display.fillScreen(TFT_WHITE);
    M5.Display.setFont(&fonts::DejaVu18);
    M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
    M5.Display.setCursor(20, DISPLAY_H / 2 - 20);
    M5.Display.print("Opening...");
    M5.Display.display();

    unsigned long t0 = millis();
    totalPages = findTotalPages(currentMangaPath);
    Serial.printf("Binary search found %d pages in %lu ms\n",
                  totalPages, millis() - t0);

    if (totalPages == 0) {
        drawError("No images found.\nExpected: m5_0000.jpg ...");
        delay(2500);
        currentEpdMode = epd_mode_t::epd_fast;
        needRedraw = true;
        return;
    }

    // Initial save of the new manga path
    saveProgress();

    currentEpdMode = epd_mode_t::epd_quality;
    appState   = STATE_READER;
    needRedraw = true;
}