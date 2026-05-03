#include "input.h"
#include "bookmarks.h"
#include "navigation.h"
#include "state.h"
#include "storage.h"
#include "ui.h"
#include "wifi_server.h"
#include "icon.h"
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
  menuScrolls[currentMenuTab] = newScroll;
  menuSelecteds[currentMenuTab] = menuScrolls[currentMenuTab];
  menuCacheValids[currentMenuTab] = false;
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
      appState == STATE_TEXT_READER || appState == STATE_ALARM)
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
  else if (appState == STATE_ALARM)
    handleAlarmTouch(t);
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
  bool isDocument = (currentMenuTab == TAB_DOCUMENT);
  auto uniqueFolders = getUniqueBookmarkFolders(isDocument);
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
          flashButton(M5.Display.width() - 100, yOff + 20, 70, 40, UI_RADIUS, UI_BG);
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
    // Dont clear cache here as we return to library
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
  int modW = M5.Display.width() - 40;
  int modH = 220;
  int modX = 20;
  int modY = 40;

  int btnR = 40;
  int btnX1 = modX + 160;
  int btnX2 = modX + 340;
  int btnY = modY + 140;

  // Check if tap is inside Shutdown circle
  int dx1 = tapX - btnX1;
  int dy1 = tapY - btnY;
  if (dx1 * dx1 + dy1 * dy1 <= btnR * btnR)
  {
    M5.Display.startWrite();
    M5.Display.fillCircle(btnX1, btnY, btnR, UI_FG);
    M5.Display.drawCircle(btnX1, btnY, btnR, UI_BORDER);
    M5.Display.drawBitmap(btnX1 - 28, btnY - 28, power_material_icon, 56, 56, UI_BG);
    M5.Display.display();
    M5.Display.endWrite();
    delay(50);

    systemShutdown();
    return;
  }

  // Check if tap is inside Settings/Files circle
  int dx2 = tapX - btnX2;
  int dy2 = tapY - btnY;
  if (dx2 * dx2 + dy2 * dy2 <= btnR * btnR)
  {
    M5.Display.startWrite();
    M5.Display.fillCircle(btnX2, btnY, btnR, UI_FG);
    M5.Display.drawCircle(btnX2, btnY, btnR, UI_BORDER);
    M5.Display.drawBitmap(btnX2 - 28, btnY - 28, settings_material_icon, 56, 56, UI_BG);
    M5.Display.display();
    M5.Display.endWrite();
    delay(50);

    controlMenuOpen = false;
    appState = STATE_WIFI;
    requestRedraw();
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
    totalItems = (int)mangaFolders.size() + 1;
  else if (currentMenuTab == TAB_DOCUMENT)
    totalItems = (int)bookFiles.size() + 1;
  else // TAB_APP
    totalItems = 1;

  // Tab switching (top 80px)
  if (t.y < 80)
  {
    MenuTab oldTab = currentMenuTab;
    if (t.x < DISPLAY_W / 3)
      currentMenuTab = TAB_COMIC;
    else if (t.x < (DISPLAY_W * 2) / 3)
      currentMenuTab = TAB_DOCUMENT;
    else
      currentMenuTab = TAB_APP;

    if (oldTab != currentMenuTab)
    {
      // Dont reset scroll/selection/cache here to preserve preview
      requestRedraw();
    }
    return;
  }

  // Horizontal Swipe Right or Vertical Swipe Down → Next Page
  if (dx > SWIPE_HORIZ_MIN || dy > SWIPE_UP_MIN)
  {
    if (menuScrolls[currentMenuTab] + MENU_VISIBLE < totalItems)
    {
      menuNavigate(menuScrolls[currentMenuTab] + MENU_VISIBLE);
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
    if (menuScrolls[currentMenuTab] > 0)
    {
      menuNavigate(std::max(0, menuScrolls[currentMenuTab] - MENU_VISIBLE));
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

  bool isDocTab = (currentMenuTab == TAB_DOCUMENT);
  String resumeName = isDocTab ? currentBookPath : lastMangaName;
  int resumePage = isDocTab ? currentTextPage : lastPage;

  if (isDocTab && resumeName.length() > 0) {
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

    if (!isDocTab)
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

  int idx = menuScrolls[currentMenuTab] + (row * GRID_COLS + col);
  if (idx >= totalItems)
    return;

  if (idx == menuSelecteds[currentMenuTab])
  {
    if (idx == 0 && currentMenuTab != TAB_APP)
    {
      appState = STATE_BOOKMARKS;
      selectedBookmarkFolder = "";
      bookmarkScroll = 0;
      requestRedraw();
    }
    else
    {
      if (currentMenuTab == TAB_COMIC)
        openManga(idx - 1);
      else if (currentMenuTab == TAB_DOCUMENT)
        openBook(idx - 1);
      else // TAB_APP
      {
        if (idx == 0) { // Alarm
           auto dt = M5.Rtc.getDateTime();
           alarmConfig.hour = dt.time.hours;
           alarmConfig.minute = dt.time.minutes;
           alarmConfig.day = dt.date.date;
           alarmConfig.month = dt.date.month;
           alarmConfig.year = dt.date.year;
           appState = STATE_ALARM;
           requestRedraw();
        }
      }
    }
  }
  else
  {
    menuSelecteds[currentMenuTab] = idx;
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
      if (getActiveRotation() % 2 == 1)
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

      // 2. Tap or Flick to turn page
      int dx = t.distanceX();
      bool isForward = false;
      bool isBackward = false;

      if (dx < -SWIPE_HORIZ_MIN) isForward = true;
      else if (dx > SWIPE_HORIZ_MIN) isBackward = true;
      else if (millis() - pressStart < LONG_PRESS_MS)
      {
        if (t.x >= M5.Display.width() / 2) isForward = true;
        else isBackward = true;
      }

      if (isForward || isBackward)
      {
        if (stripsPerPage > 1)
        {
          if (isForward)
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
                currentStrip = stripsPerPage - 1;
              }
            }
            else
            {
              requestRedraw(epd_mode_t::epd_quality);
              saveProgress();
            }
          }
          else // isBackward
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
                currentStrip = 0;
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
          if (isForward)
          {
            if (currentPage < totalPages - 1)
            {
              currentPage++;
              currentStrip = 0;
              requestRedraw(epd_mode_t::epd_quality);
              saveProgress();
            }
          }
          else // isBackward
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
  int modH = (appState == STATE_TEXT_READER) ? 420 : 710;
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

  // Page stepper region (two rows)
  int barY = modY + 90;
  if (tapY >= barY && tapY <= barY + 150)
  {
    if (tapY <= barY + 75)
    {
      // Row 1: << < > >>
      if (tapX < modX + 100)
        adjustPendingPage(-10);
      else if (tapX < modX + 180)
        adjustPendingPage(-1);
      else if (tapX > modX + modW - 100)
        adjustPendingPage(+10);
      else if (tapX > modX + modW - 180)
        adjustPendingPage(+1);
    }
    else
    {
      // Row 2: F FIRST / L LAST
      if (tapX < modX + modW / 2)
      {
        bookConfigPendingPage = 0;
      }
      else
      {
        int total;
        if (appState == STATE_TEXT_READER)
          total = std::max((int)textPageOffsets.size(), estimatedTotalPages);
        else
          total = totalPages;
        bookConfigPendingPage = std::max(0, total - 1);
      }
      requestRedraw();
    }
    return;
  }

  int btnW = modW - 60;
  int btnX = modX + 30;
  int btnH = 60;

  if (appState != STATE_TEXT_READER)
  {
    // 1. Dithering Toggle
    int btnY0 = modY + 260;
    if (tapX >= btnX && tapX <= btnX + btnW && tapY >= btnY0 &&
        tapY <= btnY0 + btnH)
    {
      ditherMode = (DitherMode)((ditherMode + 1) % DITHER_COUNT);
      isNextPageReady = false;
      requestRedraw();
      return;
    }

    // 2. Contrast Preset
    int btnY1 = modY + 330;
    if (tapX >= btnX && tapX <= btnX + btnW && tapY >= btnY1 &&
        tapY <= btnY1 + btnH)
    {
      contrastPreset = (ContrastPreset)((contrastPreset + 1) % CONTRAST_COUNT);
      isNextPageReady = false;
      requestRedraw();
      return;
    }

    // 2.5 Mode Toggle
    int btnYMode = modY + 400;
    if (tapX >= btnX && tapX <= btnX + btnW && tapY >= btnYMode &&
        tapY <= btnYMode + btnH)
    {
      orientationMode = (OrientationMode)((orientationMode + 1) % ORIENT_COUNT);
      isNextPageReady = false;
      requestRedraw();
      return;
    }

    // 3. FIT Mode Toggle
    int btnYFit = modY + 470;
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
    btnYBookmark = modY + 260;
    btnYReturn = modY + 330;
  }
  else
  {
    btnYBookmark = modY + 540;
    btnYReturn = modY + 610;
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
    menuScrolls[currentMenuTab] = 0;
    requestRedraw();
    return;
  }

  // 3. Taps and Flicks for page turns
  bool isForward = false;
  bool isBackward = false;

  if (dx < -SWIPE_HORIZ_MIN) isForward = true;
  else if (dx > SWIPE_HORIZ_MIN) isBackward = true;
  else 
  {
    if (tapX > screenW / 2) isForward = true;
    else isBackward = true;
  }

  if (isForward)
  {
    // Next Page
    if (currentTextPage + 1 < (int)textPageOffsets.size())
    {
      currentTextPage++;
      requestRedraw(epd_mode_t::epd_text);
      saveProgress();
    }
  }
  else if (isBackward)
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

void handleAlarmTouch(const m5::touch_detail_t &t)
{
  if (!t.wasReleased()) return;
  
  int tx = t.x;
  int ty = t.y;
  int dy = t.distanceY();
  
  // Swipe up to go back
  if (dy < -SWIPE_UP_MIN) {
      appState = STATE_MENU;
      menuCacheValids[currentMenuTab] = false;
      requestRedraw();
      return;
  }
  
  // Back button (header region)
  if (ty < 100) {
      appState = STATE_MENU;
      menuCacheValids[currentMenuTab] = false;
      requestRedraw();
      return;
  }
  
  int xDD = 100;
  int xHH = 270;
  int xMM = 440;
  int ySet = 270 + 240; // cardY + 240 = 510
  
  // DD +/-
  if (tx > xDD - 50 && tx < xDD + 50) {
      if (ty < ySet - 60) { 
          alarmConfig.day++; 
          if (alarmConfig.day > 31) alarmConfig.day = 1; 
          requestRedraw(); 
      }
      else if (ty > ySet + 60 && ty < ySet + 200) { 
          alarmConfig.day--; 
          if (alarmConfig.day < 1) alarmConfig.day = 31; 
          requestRedraw(); 
      }
  }

  // HH +/-
  if (tx > xHH - 50 && tx < xHH + 50) {
      if (ty < ySet - 60) { alarmConfig.hour = (alarmConfig.hour + 1) % 24; requestRedraw(); }
      else if (ty > ySet + 60 && ty < ySet + 200) { alarmConfig.hour = (alarmConfig.hour + 23) % 24; requestRedraw(); }
  }
  
  // MM +/-
  if (tx > xMM - 50 && tx < xMM + 50) {
      if (ty < ySet - 60) { alarmConfig.minute = (alarmConfig.minute + 1) % 60; requestRedraw(); }
      else if (ty > ySet + 60 && ty < ySet + 200) { alarmConfig.minute = (alarmConfig.minute + 59) % 60; requestRedraw(); }
  }
  
  // Set Alarm Button
  int btnW = 420;
  int btnH = 90;
  int btnX = (DISPLAY_W - btnW) / 2;
  int btnY = 810;
  
  if (tx >= btnX && tx <= btnX + btnW && ty >= btnY && ty <= btnY + btnH) {
      // Validate time before setting
      auto now = M5.Rtc.getDateTime();
      struct tm alarmTm = {0};
      alarmTm.tm_year = alarmConfig.year - 1900;
      alarmTm.tm_mon = alarmConfig.month - 1;
      alarmTm.tm_mday = alarmConfig.day;
      alarmTm.tm_hour = alarmConfig.hour;
      alarmTm.tm_min = alarmConfig.minute;
      alarmTm.tm_sec = 0;
      time_t alarmTime = mktime(&alarmTm);

      struct tm nowTm = {0};
      nowTm.tm_year = now.date.year - 1900;
      nowTm.tm_mon = now.date.month - 1;
      nowTm.tm_mday = now.date.date;
      nowTm.tm_hour = now.time.hours;
      nowTm.tm_min = now.time.minutes;
      nowTm.tm_sec = 0;
      time_t nowTime = mktime(&nowTm);

      if (alarmTime <= nowTime) {
          // Visual feedback for invalid action: thick border
          for (int i = 0; i < 5; i++) {
              M5.Display.drawRoundRect(btnX + i, btnY + i, btnW - i * 2, btnH - i * 2, UI_RADIUS, UI_FG);
          }
          delay(150);
          requestRedraw();
          return;
      }

      flashButton(btnX, btnY, btnW, btnH, UI_RADIUS, UI_BG);
      
      m5::rtc_datetime_t alarmDt;
      alarmDt.date.year = alarmConfig.year;
      alarmDt.date.month = alarmConfig.month;
      alarmDt.date.date = alarmConfig.day;
      alarmDt.time.hours = alarmConfig.hour;
      alarmDt.time.minutes = alarmConfig.minute;
      alarmDt.time.seconds = 0;
      
      M5.Rtc.setAlarmIRQ(alarmDt.date, alarmDt.time);
      
      // Shutdown/Sleep with screen saver
      systemShutdown(); 
  }
}

