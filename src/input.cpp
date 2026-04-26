#include "input.h"
#include "bookmarks.h"
#include "navigation.h"
#include "state.h"
#include "storage.h"
#include "ui.h"
#include "wifi_server.h"
#include <algorithm>

// ── File-local helpers ──────────────────────────────────────────────

// Brief visual flash on a button region to acknowledge a tap
static void flashButton(int x, int y, int w, int h, int radius = UI_RADIUS,
                        uint16_t color = UI_FG)
{
  M5.Display.startWrite();
  M5.Display.fillRoundRect(x, y, w, h, radius, color);
  M5.Display.display();
  M5.Display.endWrite();
  delay(50);
}

// Navigate the main menu to a new scroll position
static void menuNavigate(int newScroll)
{
  menuScroll = newScroll;
  menuSelected = menuScroll;
  menuCacheValid = false;
  requestRedraw();
}

// Adjust the pending page in book-config by a delta, clamped to valid range
static void adjustPendingPage(int delta)
{
  int maxPg;
  if (appState == STATE_TEXT_READER)
    maxPg = std::max((int)textPageOffsets.size(), estimatedTotalPages);
  else
    maxPg = totalPages;
    
  bookConfigPendingPage =
      std::max(0, std::min(maxPg - 1, (int)bookConfigPendingPage + delta));
  requestRedraw();
}

// ── Main touch dispatcher ───────────────────────────────────────────

void handleTouch()
{
  if (M5.Touch.getCount() == 0)
  {
    if (isMagnifierActive)
    {
      isMagnifierActive = false;
      resetMagnifierTracking();
      needRedraw = true; // Full redraw to clear magnifier overlay
    }
    return;
  }

  auto &t = M5.Touch.getDetail(0);

  // If a menu is open, we MUST ensure the touch coordinate system matches the menu drawing (Portrait/0)
  if (controlMenuOpen || bookConfigOpen || appState == STATE_MENU || 
      appState == STATE_BOOKMARKS || appState == STATE_WIFI || 
      appState == STATE_TEXT_READER)
  {
    M5.Display.setRotation(0);
  }

  // Skip control menu gesture if magnifying
  if (!isMagnifierActive && !controlMenuOpen && t.wasReleased() &&
      t.base_y < HEADER_H && t.distanceY() > SWIPE_UP_MIN)
  {
    controlMenuOpen = true;
    requestRedraw();
    return;
  }

  if (controlMenuOpen)
    handleControlTouch(t);
  else if (bookConfigOpen)
    handleBookConfigTouch(t);
  else if (appState == STATE_MENU)
    handleMenuTouch(t);
  else if (appState == STATE_BOOKMARKS)
    handleBookmarksTouch(t);
  else if (appState == STATE_WIFI)
    handleWifiTouch(t);
  else if (appState == STATE_TEXT_READER)
    handleTextTouch(t);
  else
  {
    // We handle Reader state (including its gestures) in handleReaderTouch
    handleReaderTouch(t);
  }
}

// ── Bookmarks ───────────────────────────────────────────────────────

void handleBookmarksTouch(const m5::touch_detail_t &t)
{
  if (!t.wasReleased())
    return;

  int dy = t.distanceY();
  int dx = t.distanceX();

  // Swipe up → go back (to library or to folder list)
  if (dy < -SWIPE_UP_MIN)
  {
    if (selectedBookmarkFolder == "")
    {
      appState = STATE_MENU;
    }
    else
    {
      selectedBookmarkFolder = "";
      bookmarkScroll = 0;
    }
    requestRedraw();
    return;
  }

  int itemsPerPage = (M5.Display.height() - 200) / 90;

  // Count total items for pagination
  auto uniqueFolders = getUniqueBookmarkFolders();
  int totalItems;
  if (selectedBookmarkFolder == "")
  {
    totalItems = uniqueFolders.size();
  }
  else
  {
    totalItems = 0;
    for (const auto &b : bookmarks)
    {
      if (b.folder == selectedBookmarkFolder)
        totalItems++;
    }
  }

  // Swipe down or right → next page
  if (dy > SWIPE_UP_MIN || dx > SWIPE_HORIZ_MIN)
  {
    if (bookmarkScroll + itemsPerPage < totalItems)
    {
      bookmarkScroll += itemsPerPage;
    }
    else
    {
      bookmarkScroll = 0;
    }
    requestRedraw();
    return;
  }

  // Swipe left → previous page
  if (dx < -SWIPE_HORIZ_MIN)
  {
    if (bookmarkScroll > 0)
    {
      bookmarkScroll = std::max(0, bookmarkScroll - itemsPerPage);
    }
    else
    {
      bookmarkScroll = ((totalItems - 1) / itemsPerPage) * itemsPerPage;
    }
    requestRedraw();
    return;
  }

  // ── Tap handling ──
  int tapY = t.y;
  if (tapY < HEADER_H)
    return;

  int yOff = 100;
  int itemH = 80;

  if (selectedBookmarkFolder == "")
  {
    // Tap on a folder row
    int count = 0;
    for (int i = 0; i < (int)uniqueFolders.size(); i++)
    {
      if (i < bookmarkScroll)
        continue;
      if (count >= itemsPerPage)
        break;

      if (tapY >= yOff && tapY <= yOff + itemH)
      {
        flashButton(10, yOff, M5.Display.width() - 20, itemH);
        selectedBookmarkFolder = uniqueFolders[i];
        bookmarkScroll = 0;
        requestRedraw();
        return;
      }
      yOff += itemH + 10;
      count++;
    }
  }
  else
  {
    // Tap on a bookmark row (with delete button region)
    int tapX = t.x;
    int count = 0;
    int folderItemIdx = 0;
    for (int i = 0; i < (int)bookmarks.size(); i++)
    {
      if (bookmarks[i].folder != selectedBookmarkFolder)
        continue;

      if (folderItemIdx < bookmarkScroll)
      {
        folderItemIdx++;
        continue;
      }
      if (count >= itemsPerPage)
        break;

      if (tapY >= yOff && tapY <= yOff + itemH)
      {
        if (tapX > M5.Display.width() - 120)
        {
          // Delete button
          flashButton(M5.Display.width() - 100, yOff + 20, 70, 40);
          deleteBookmark(i);
          bool remains = false;
          for (const auto &b : bookmarks)
          {
            if (b.folder == selectedBookmarkFolder)
            {
              remains = true;
              break;
            }
          }
          if (!remains)
          {
            selectedBookmarkFolder = "";
            bookmarkScroll = 0;
          }
          requestRedraw();
          return;
        }
        else
        {
          // Open bookmark
          flashButton(10, yOff, M5.Display.width() - 20, itemH);
          if (bookmarks[i].folder.endsWith(".txt") || bookmarks[i].folder.endsWith(".TXT"))
          {
            String path = String(BOOK_ROOT) + "/" + bookmarks[i].folder;
            openBookPath(path, bookmarks[i].page);
          }
          else
          {
            String path = String(MANGA_ROOT) + "/" + bookmarks[i].folder;
            openMangaPath(path, bookmarks[i].page);
          }
          return;
        }
      }
      yOff += itemH + 10;
      count++;
      folderItemIdx++;
    }
  }
}

// ── WiFi ────────────────────────────────────────────────────────────

void handleWifiTouch(const m5::touch_detail_t &t)
{
  if (!t.wasReleased())
    return;

  int tapX = t.x;
  int tapY = t.y;

  int btnW = 300;
  int btnH = 60;
  int btnX = (M5.Display.width() - btnW) / 2;
  int btnY = M5.Display.height() - 150;

  if (tapX >= btnX && tapX <= btnX + btnW && tapY >= btnY &&
      tapY <= btnY + btnH)
  {
    M5.Display.startWrite();
    M5.Display.fillRoundRect(btnX, btnY, btnW, btnH, UI_RADIUS, UI_BG);
    M5.Display.drawRoundRect(btnX, btnY, btnW, btnH, UI_RADIUS, UI_BORDER);
    M5.Display.setTextColor(UI_FG, UI_BG);
    M5.Display.setFont(&fonts::DejaVu18);
    M5.Display.setTextDatum(middle_center);
    M5.Display.drawString("STOP SERVER", btnX + btnW / 2, btnY + btnH / 2);
    M5.Display.setTextDatum(top_left);
    M5.Display.display();
    M5.Display.endWrite();
    delay(50);

    stopWifiServer();
    appState = STATE_MENU;
    menuCacheValid = false;
    requestRedraw();
  }
}

// ── Control Center ──────────────────────────────────────────────────

void handleControlTouch(const m5::touch_detail_t &t)
{
  if (!t.wasReleased())
    return;
  if (t.distanceY() < -SWIPE_UP_MIN)
  {
    controlMenuOpen = false;
    requestRedraw((appState == STATE_READER) ? epd_mode_t::epd_quality
                                             : epd_mode_t::epd_fast);
    return;
  }

  int tapX = t.x;
  int tapY = t.y;
  int modW = 500;
  int modH = 300;
  int modX = (M5.Display.width() - modW) / 2;
  int modY = (M5.Display.height() - modH) / 2;

  int btnW = modW - 60;
  int btnH = 65;
  int btnX = modX + 30;
  int btnY = modY + 210;

  if (tapX > btnX && tapX < btnX + btnW && tapY > btnY && tapY < btnY + btnH)
  {
    M5.Display.startWrite();
    M5.Display.fillRoundRect(btnX, btnY, btnW, btnH, UI_RADIUS, UI_BG);
    M5.Display.drawRoundRect(btnX, btnY, btnW, btnH, UI_RADIUS, UI_BORDER);
    M5.Display.setTextColor(UI_FG, UI_BG);
    M5.Display.setFont(&fonts::DejaVu18);
    M5.Display.setTextDatum(middle_center);
    M5.Display.drawString("SHUTDOWN", btnX + btnW / 2, btnY + btnH / 2);
    M5.Display.setTextDatum(top_left);
    M5.Display.display();
    M5.Display.endWrite();
    delay(50);

    systemShutdown();
    return;
  }

  if (tapX < modX || tapX > modX + modW || tapY < modY || tapY > modY + modH)
  {
    controlMenuOpen = false;
    requestRedraw((appState == STATE_READER) ? epd_mode_t::epd_quality
                                             : epd_mode_t::epd_fast);
  }
}

// ── Menu ────────────────────────────────────────────────────────────

void handleMenuTouch(const m5::touch_detail_t &t)
{
  if (!t.wasReleased())
    return;
  int dy = t.distanceY();
  int dx = t.distanceX();

  int totalItems;
  if (currentMenuTab == TAB_COMIC)
    totalItems = (int)mangaFolders.size() + 2;
  else
    totalItems = (int)bookFiles.size() + 2;

  // Tab switching (top 80px)
  if (t.y < 80)
  {
    MenuTab oldTab = currentMenuTab;
    if (t.x < DISPLAY_W / 2)
      currentMenuTab = TAB_COMIC;
    else
      currentMenuTab = TAB_DOCUMENT;

    if (oldTab != currentMenuTab)
    {
      menuScroll = 0;
      menuSelected = -1;
      menuCacheValid = false;
      requestRedraw();
    }
    return;
  }

  // Horizontal Swipe Right or Vertical Swipe Down → Next Page
  if (dx > SWIPE_HORIZ_MIN || dy > SWIPE_UP_MIN)
  {
    if (menuScroll + MENU_VISIBLE < totalItems)
    {
      menuNavigate(menuScroll + MENU_VISIBLE);
    }
    else if (dx > SWIPE_HORIZ_MIN)
    {
      menuNavigate(0); // Wrap around
    }
    return;
  }

  // Horizontal Swipe Left → Previous Page
  if (dx < -SWIPE_HORIZ_MIN)
  {
    if (menuScroll > 0)
    {
      menuNavigate(std::max(0, menuScroll - MENU_VISIBLE));
    }
    else
    {
      menuNavigate(((totalItems - 1) / MENU_VISIBLE) *
                   MENU_VISIBLE); // Wrap around
    }
    return;
  }

  int tapX = t.x;
  int tapY = t.y;
  int barW = M5.Display.width() - 40;
  int barH = 60;
  int barX = 20;
  int barY = M5.Display.height() - 85;

  String resumeName = isLastReadManga ? lastMangaName : currentBookPath;
  int resumePage = isLastReadManga ? lastPage : currentTextPage;

  if (!isLastReadManga && resumeName.length() > 0) {
      int lastSlash = resumeName.lastIndexOf('/');
      if (lastSlash >= 0) resumeName = resumeName.substring(lastSlash + 1);
  }

  if (resumeName.length() > 0 && tapX >= barX && tapX <= barX + barW &&
      tapY >= barY && tapY <= barY + barH)
  {

    M5.Display.startWrite();
    M5.Display.fillRoundRect(barX, barY, barW, barH, UI_RADIUS, UI_FG);
    M5.Display.setTextColor(UI_BG, UI_FG);
    M5.Display.setFont(&fonts::DejaVu12);
    M5.Display.setCursor(barX + 15, barY + 12);
    M5.Display.print("CONTINUE: ");
    M5.Display.setFont(&fonts::DejaVu18);
    String shortName = resumeName;
    if (shortName.length() > 19)
      shortName = shortName.substring(0, 17) + "...";
    M5.Display.print(shortName);
    M5.Display.setFont(&fonts::DejaVu12);
    M5.Display.setCursor(barX + 15, barY + 36);
    M5.Display.printf("Page %d", resumePage + 1);
    M5.Display.display();
    M5.Display.endWrite();
    delay(50);

    if (isLastReadManga)
    {
      openMangaPath(lastMangaPath, lastPage);
    }
    else
    {
      openBookPath(currentBookPath, currentTextPage);
    }
    return;
  }
  if (tapY < GRID_Y_TOP)
    return;

  int col = (tapX - GRID_GUTTER) / (THUMB_W + GRID_GUTTER);
  int row = (tapY - GRID_Y_TOP) / GRID_ROW_H;
  if (col < 0)
    col = 0;
  if (col >= GRID_COLS)
    col = GRID_COLS - 1;
  if (row < 0)
    row = 0;
  if (row >= GRID_ROWS)
    row = GRID_ROWS - 1;

  int idx = menuScroll + (row * GRID_COLS + col);
  if (idx >= totalItems)
    return;

  if (idx == menuSelected)
  {
    if (idx == 0)
    {
      appState = STATE_BOOKMARKS;
      selectedBookmarkFolder = "";
      bookmarkScroll = 0;
      requestRedraw();
    }
    else if (idx == 1)
    {
      appState = STATE_WIFI;
      requestRedraw();
    }
    else
    {
      if (currentMenuTab == TAB_COMIC)
        openManga(idx - 2);
      else
        openBook(idx - 2);
    }
  }
  else
  {
    menuSelected = idx;
    requestRedraw();
  }
}

// ── Reader ──────────────────────────────────────────────────────────

void handleReaderTouch(const m5::touch_detail_t &t)
{
  static unsigned long pressStart = 0;
  static unsigned long lastMoveTime = 0;
  static bool potentialLongPress = false;
  static bool qualityApplied = false;
  static bool wasMagnifying = false;

  if (t.wasPressed())
  {
    pressStart = millis();
    lastMoveTime = millis();
    potentialLongPress = true;
    qualityApplied = false;
    wasMagnifying = false;
  }

  if (t.isPressed())
  {
    if (potentialLongPress && !isMagnifierActive &&
        (millis() - pressStart > LONG_PRESS_MS))
    {
      if (horizontalMode)
      {
        potentialLongPress = false;
      }
      else
      {
        isMagnifierActive = true;
        wasMagnifying = true;
        potentialLongPress = false;
        lastMoveTime = millis();
      }
    }

    if (isMagnifierActive)
    {
      static int lastMagX = -1, lastMagY = -1;
      if (abs(t.x - lastMagX) > 4 || abs(t.y - lastMagY) > 4)
      {
        drawMagnifier(t.x, t.y, false);
        lastMagX = t.x;
        lastMagY = t.y;
        lastMoveTime = millis();
        qualityApplied = false;
      }
      else if (!qualityApplied && (millis() - lastMoveTime > 300))
      {
        drawMagnifier(t.x, t.y, true);
        qualityApplied = true;
      }
      return;
    }
  }

  if (t.wasReleased())
  {
    potentialLongPress = false;
    if (isMagnifierActive)
    {
      isMagnifierActive = false;
      resetMagnifierTracking();
      needRedraw = true;
      return;
    }

    // Only handle other gestures if we DID NOT use the magnifier during this
    // touch
    if (!wasMagnifying)
    {
      // 1. Check for Book Menu gesture (Swipe UP from bottom)
      if (t.base_y > M5.Display.height() - 150 && t.distanceY() < -SWIPE_UP_MIN)
      {
        bookConfigOpen = true;
        bookConfigPendingPage = currentPage;
        requestRedraw();
        return;
      }

      // 2. Tap to turn page (if not a long press)
      if (millis() - pressStart < LONG_PRESS_MS)
      {
        if (stripsPerPage > 1)
        {
          int zoneW = M5.Display.width() / 2;
          if (t.x >= zoneW)
          {
            currentStrip++;
            if (currentStrip >= stripsPerPage)
            {
              if (currentPage < totalPages - 1)
              {
                currentPage++;
                currentStrip = 0;
                requestRedraw(epd_mode_t::epd_quality);
                saveProgress();
              }
              else
              {
                currentStrip = stripsPerPage - 1; // Stay at last strip
              }
            }
            else
            {
              requestRedraw(epd_mode_t::epd_quality);
              saveProgress();
            }
          }
          else
          {
            currentStrip--;
            if (currentStrip < 0)
            {
              if (currentPage > 0)
              {
                currentPage--;
                currentStrip = -1; // Special value, drawPage will calculate stripsPerPage
                requestRedraw(epd_mode_t::epd_quality);
                saveProgress();
              }
              else
              {
                currentStrip = 0; // Stay at first strip
              }
            }
            else
            {
              requestRedraw(epd_mode_t::epd_quality);
              saveProgress();
            }
          }
        }
        else
        {
          int zoneW = M5.Display.width() / 2;
          if (t.x >= zoneW)
          {
            if (currentPage < totalPages - 1)
            {
              currentPage++;
              currentStrip = 0;
              requestRedraw(epd_mode_t::epd_quality);
              saveProgress();
            }
          }
          else
          {
            if (currentPage > 0)
            {
              currentPage--;
              currentStrip = 0;
              requestRedraw(epd_mode_t::epd_quality);
              saveProgress();
            }
          }
        }
      }
    }
  }
}

// ── Book Config ─────────────────────────────────────────────────────

void handleBookConfigTouch(const m5::touch_detail_t &t)
{
  if (!t.wasReleased())
    return;

  int tapX = t.x;
  int tapY = t.y;
  int modW = 460;
  int modH = (appState == STATE_TEXT_READER) ? 350 : 640;
  int modX = (M5.Display.width() - modW) / 2;
  int modY = (M5.Display.height() - modH) / 2;

  // Tap outside → close and apply pending page
  if (tapX < modX || tapX > modX + modW || tapY < modY || tapY > modY + modH)
  {
    bookConfigOpen = false;
    if (appState == STATE_TEXT_READER)
    {
      if (currentTextPage != bookConfigPendingPage)
      {
        currentTextPage = bookConfigPendingPage;
        requestRedraw(epd_mode_t::epd_text);
        saveProgress();
      }
      else
        requestRedraw(epd_mode_t::epd_text);
    }
    else
    {
      if (currentPage != bookConfigPendingPage)
      {
        currentPage = bookConfigPendingPage;
        currentStrip = 0; // Reset strip on page jump
      }
      requestRedraw(epd_mode_t::epd_quality);
    }
    return;
  }

  // Page stepper region
  int barY = modY + 90;
  if (tapY >= barY && tapY <= barY + 80)
  {
    if (tapX < modX + 100)
      adjustPendingPage(-10);
    else if (tapX < modX + 180)
      adjustPendingPage(-1);
    else if (tapX > modX + modW - 100)
      adjustPendingPage(+10);
    else if (tapX > modX + modW - 180)
      adjustPendingPage(+1);
    return;
  }

  int btnW = modW - 60;
  int btnX = modX + 30;
  int btnH = 60;

  if (appState != STATE_TEXT_READER)
  {
    // 1. Dithering Toggle
    int btnY0 = modY + 190;
    if (tapX >= btnX && tapX <= btnX + btnW && tapY >= btnY0 &&
        tapY <= btnY0 + btnH)
    {
      ditherMode = (DitherMode)((ditherMode + 1) % DITHER_COUNT);
      isNextPageReady = false;
      requestRedraw();
      return;
    }

    // 2. Contrast Preset
    int btnY1 = modY + 260;
    if (tapX >= btnX && tapX <= btnX + btnW && tapY >= btnY1 &&
        tapY <= btnY1 + btnH)
    {
      contrastPreset = (ContrastPreset)((contrastPreset + 1) % CONTRAST_COUNT);
      isNextPageReady = false;
      requestRedraw();
      return;
    }

    // 2.5 Mode Toggle
    int btnYMode = modY + 330;
    if (tapX >= btnX && tapX <= btnX + btnW && tapY >= btnYMode &&
        tapY <= btnYMode + btnH)
    {
      horizontalMode = !horizontalMode;
      isNextPageReady = false;
      requestRedraw();
      return;
    }

    // 3. FIT Mode Toggle
    int btnYFit = modY + 400;
    if (tapX >= btnX && tapX <= btnX + btnW && tapY >= btnYFit &&
        tapY <= btnYFit + btnH)
    {
      fitMode = (FitMode)((fitMode + 1) % FIT_COUNT);
      isNextPageReady = false;
      requestRedraw();
      return;
    }
  }

  int btnYBookmark, btnYReturn;
  if (appState == STATE_TEXT_READER)
  {
    btnYBookmark = modY + 190;
    btnYReturn = modY + 260;
  }
  else
  {
    btnYBookmark = modY + 470;
    btnYReturn = modY + 540;
  }

  // 4. Bookmark Page
  if (tapX >= btnX && tapX <= btnX + btnW && tapY >= btnYBookmark &&
      tapY <= btnYBookmark + btnH)
  {
    M5.Display.startWrite();
    M5.Display.fillRoundRect(btnX, btnYBookmark, btnW, btnH, UI_RADIUS, UI_FG);
    M5.Display.setTextColor(UI_BG, UI_FG);
    M5.Display.setFont(&fonts::DejaVu18);
    M5.Display.setTextDatum(middle_center);
    M5.Display.drawString("BOOKMARK SAVED!", btnX + btnW / 2, btnYBookmark + btnH / 2);
    M5.Display.setTextDatum(top_left);
    M5.Display.display();
    M5.Display.endWrite();
    delay(50);

    int lastSlash;
    String folder;
    if (appState == STATE_TEXT_READER)
    {
        lastSlash = currentBookPath.lastIndexOf('/');
        folder = currentBookPath.substring(lastSlash + 1);
        addBookmark(folder, currentTextPage);
    }
    else
    {
        lastSlash = currentMangaPath.lastIndexOf('/');
        folder = currentMangaPath.substring(lastSlash + 1);
        addBookmark(folder, currentPage);
    }
    needRedraw = true;
    return;
  }

  // 5. Return to Library
  if (tapX >= btnX && tapX <= btnX + btnW && tapY >= btnYReturn &&
      tapY <= btnYReturn + btnH)
  {
    M5.Display.startWrite();
    M5.Display.fillRoundRect(btnX, btnYReturn, btnW, btnH, UI_RADIUS, UI_BG);
    M5.Display.drawRoundRect(btnX, btnYReturn, btnW, btnH, UI_RADIUS, UI_BORDER);
    M5.Display.setTextColor(UI_FG, UI_BG);
    M5.Display.setFont(&fonts::DejaVu18);
    M5.Display.setTextDatum(middle_center);
    M5.Display.drawString("RETURN TO LIBRARY", btnX + btnW / 2,
                          btnYReturn + btnH / 2);
    M5.Display.setTextDatum(top_left);
    M5.Display.display();
    M5.Display.endWrite();
    delay(50);

    appState = STATE_MENU;
    bookConfigOpen = false;
    requestRedraw();
    return;
  }
}



void handleTextTouch(const m5::touch_detail_t &t)
{
  if (!t.wasReleased())
    return;

  int dy = t.distanceY();
  int dx = t.distanceX();
  int tapX = t.x;
  int tapY = t.y;
  int screenW = M5.Display.width();
  int screenH = M5.Display.height();

  // 1. Swipe UP from bottom → Open Book Menu
  if (t.base_y > screenH - 150 && dy < -SWIPE_UP_MIN)
  {
    bookConfigOpen = true;
    bookConfigPendingPage = currentTextPage;
    requestRedraw();
    return;
  }

  // 2. Swipe UP from middle/top → Return to library
  if (dy < -SWIPE_UP_MIN)
  {
    appState = STATE_MENU;
    currentMenuTab = TAB_DOCUMENT;
    menuScroll = 0;
    requestRedraw();
    return;
  }

  // 3. Taps for page turns
  int zoneW = screenW / 2;
  if (tapX > zoneW || dx > SWIPE_HORIZ_MIN)
  {
    // Next Page
    if (currentTextPage + 1 < (int)textPageOffsets.size())
    {
      currentTextPage++;
      requestRedraw(epd_mode_t::epd_text);
      saveProgress();
    }
  }
  else if (tapX <= zoneW || dx < -SWIPE_HORIZ_MIN)
  {
    // Prev Page
    if (currentTextPage > 0)
    {
      currentTextPage--;
      requestRedraw(epd_mode_t::epd_text);
      saveProgress();
    }
  }
}

