#pragma once

#include <Arduino.h>
#include <SD.h>
#include <M5GFX.h>

// SD SPI pins (M5PaperS3)
#define SD_CS_PIN   47
#define SD_SCK_PIN  39
#define SD_MOSI_PIN 38
#define SD_MISO_PIN 40

// Display geometry
#define DISPLAY_W    540
#define DISPLAY_H    960
#define LEFT_ZONE_W  (DISPLAY_W / 2)
#define SWIPE_UP_MIN  80

// Manga root
#define MANGA_ROOT "/manga"

// Filename pattern
#define IMG_PREFIX  "m5_"
#define IMG_SUFFIX  ".jpg"
#define IMG_DIGITS  4

// Menu layout
#define GRID_COLS      2
#define GRID_GUTTER   30
#define GRID_Y_TOP    110
#define THUMB_W       ((DISPLAY_W - (GRID_COLS + 1) * GRID_GUTTER) / GRID_COLS)
#define THUMB_H       (int(THUMB_W * 1.41))
#define GRID_ROW_H    (THUMB_H + 60)
#define MENU_VISIBLE  (GRID_COLS * 2)
#define UI_RADIUS     10

enum AppState { STATE_MENU, STATE_READER, STATE_BOOKMARKS };
