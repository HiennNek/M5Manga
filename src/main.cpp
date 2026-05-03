#include "bookmarks.h"
#include "config.h"
#include "input.h"
#include "navigation.h"
#include "state.h"
#include "storage.h"
#include "ui.h"
#include "wifi_server.h"
#include <Arduino.h>
#include <M5Unified.h>

static unsigned long lastInteractionMs = 0;

void setup()
{
  auto cfg = M5.config();
  cfg.internal_spk = false;
  cfg.internal_mic = false;
  cfg.internal_imu = true;
  cfg.clear_display = false; // Prevent automatic screen clear on boot
  cfg.disable_rtc_irq = false; // Preserve RTC IRQ status so we can check it
  M5.begin(cfg);

  // Boost CPU for fast initialization
  setCpuFrequencyMhz(240);

  Serial.begin(115200);

  randomSeed(micros());

  currentEpdMode = epd_mode_t::epd_fast;
  
  sdInit();
  loadConfig();
  loadProgress();
  
  M5.Display.setRotation(getActiveRotation());
  M5.Display.setColorDepth(8);
  M5.Display.setEpdMode(epd_mode_t::epd_fast);

  // Skip the redundant "Initialising..." screen refresh to save ~1s of E-ink
  // update time. The first real frame will be drawn in loop().

  scanMangaFolders();
  scanBookFiles();
  loadBookmarks();

  if (M5.Rtc.getIRQstatus())
  {
    Serial.println("Woke up from RTC Alarm!");
    pinMode(BUZZER_PIN, OUTPUT);
    
    // Draw Alarm Trigger Screen
    M5.Display.fillScreen(TFT_WHITE);
    M5.Display.setTextColor(TFT_BLACK);
    M5.Display.setTextDatum(middle_center);
    M5.Display.setFont(&fonts::DejaVu40);
    M5.Display.drawString("ALARM ACTIVE", DISPLAY_W / 2, DISPLAY_H / 2 - 40);
    M5.Display.setFont(&fonts::DejaVu24);
    M5.Display.drawString("Touch Screen to Stop", DISPLAY_W / 2, DISPLAY_H / 2 + 40);
    M5.Display.display();

    ledcAttach(BUZZER_PIN, 2700, 8);

    bool stopAlarm = false;
    while (!stopAlarm)
    {
      // 4 Fast Beeps
      for (int i = 0; i < 4; i++)
      {
        ledcWrite(BUZZER_PIN, 128);
        // Check touch during 100ms beep
        uint32_t start = millis();
        while (millis() - start < 50) {
          M5.update();
          if (M5.Touch.getCount() > 0) { stopAlarm = true; break; }
          delay(5);
        }
        if (stopAlarm) break;

        ledcWrite(BUZZER_PIN, 0);
        // Check touch during 50ms silence
        start = millis();
        while (millis() - start < 50) {
          M5.update();
          if (M5.Touch.getCount() > 0) { stopAlarm = true; break; }
          delay(5);
        }
        if (stopAlarm) break;
      }

      if (stopAlarm) break;

      // 4 "Rests" (400ms silence)
      uint32_t start = millis();
      while (millis() - start < 400) {
        M5.update();
        if (M5.Touch.getCount() > 0) { stopAlarm = true; break; }
        delay(10);
      }
    }
    
    ledcWrite(BUZZER_PIN, 0);
    ledcDetach(BUZZER_PIN);
    M5.Rtc.clearIRQ();
    
    // Refresh to main menu after stopping
    requestRedraw();
  }

  // Auto-resume last read book (Kindle-like behavior)
  if (isLastReadManga && lastMangaPath.length() > 0)
  {
    openMangaPath(lastMangaPath, lastPage);
  }
  else if (!isLastReadManga && currentBookPath.length() > 0)
  {
    openBookPath(currentBookPath, currentTextPage);
  }
  else
  {
    needRedraw = true;
  }
}

void loop()
{
  M5.update();
  if (M5.Touch.getCount() > 0)
  {
    lastInteractionMs = millis();
  }

  // Handle auto-rotation in manga reader
  if (appState == STATE_READER && orientationMode == ORIENT_AUTO && !controlMenuOpen && !bookConfigOpen)
  {
    static int lastRot = -1;
    int currentRot = getActiveRotation();
    if (lastRot != -1 && currentRot != lastRot)
    {
      needRedraw = true;
      isNextPageReady = false; // Invalidate cache for new orientation
    }
    lastRot = currentRot;
  }

  if (appState == STATE_WIFI)
  {
    updateWifiServer();
  }

  if (needRedraw)
  {
    setCpuFrequencyMhz(240);
    M5.Display.setEpdMode(currentEpdMode);
    if (appState == STATE_ALARM)
      drawAlarm();
    else if (controlMenuOpen)
      drawControlCenter();
    else if (bookConfigOpen)
      drawBookConfig();
    else if (appState == STATE_MENU)
      drawMenu();
    else if (appState == STATE_BOOKMARKS)
      drawBookmarks();
    else if (appState == STATE_WIFI)
      drawWifiServer();
    else if (appState == STATE_TEXT_READER)
      drawTextPage();
    else
      drawPage();
    needRedraw = false;
  }

  handleTouch();

  bool readerIdle =
      appState == STATE_READER && !controlMenuOpen && !bookConfigOpen;
  bool wifiActive = appState == STATE_WIFI;

  if (wifiActive)
  {
    setCpuFrequencyMhz(240);
    delay(1);
  }
  else if (readerIdle)
  {
    if (millis() - lastInteractionMs >= 5000)
    {
      setCpuFrequencyMhz(80);
      M5.delay(50); // M5.delay handles background tasks better
    }
    else
    {
      setCpuFrequencyMhz(240);
      M5.delay(10);
    }
  }
  else
  {
    setCpuFrequencyMhz(240);
    M5.delay(10);
  }
}

void openManga(int idx)
{
  if (idx < 0 || idx >= (int)mangaFolders.size())
    return;
  String path = String(MANGA_ROOT) + "/" + mangaFolders[idx];
  // Load per-book saved progress
  int savedPage = loadBookProgress(path);
  int savedStrip = loadBookProgressStrip(path);
  openMangaPath(path, savedPage);
  currentStrip = savedStrip;
}

void openMangaPath(const String &path, int page)
{
  currentMangaPath = path;

  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.setFont(&fonts::DejaVu18);
  M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
  M5.Display.setCursor(20, DISPLAY_H / 2 - 20);
  M5.Display.print("Opening...");
  M5.Display.display();

  setCpuFrequencyMhz(240);
  unsigned long t0 = millis();
  totalPages = findTotalPages(currentMangaPath);
  Serial.printf("Found %d pages in %lu ms\n", totalPages, millis() - t0);

  if (totalPages == 0)
  {
    drawError("No images found.\nExpected: m5_0000.jpg ...");
    delay(2500);
    appState = STATE_MENU;
    currentEpdMode = epd_mode_t::epd_fast;
    needRedraw = true;
    return;
  }

  // Clamp page to valid range
  if (page >= totalPages) page = totalPages - 1;
  if (page < 0) page = 0;
  currentPage = page;

  saveProgress();
  setCpuFrequencyMhz(80);

  currentEpdMode = epd_mode_t::epd_quality;
  appState = STATE_READER;
  needRedraw = true;
}
