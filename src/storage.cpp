#include "storage.h"
#include "ui.h"
#include <M5Unified.h>
#include <Preferences.h>
#include <SD.h>
#include <SPI.h>
#include <algorithm>

void sdInit()
{
  SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  int retry = 0;
  // Boost SD SPI to 80MHz for maximum performance with UHS-I cards
  while (!SD.begin(SD_CS_PIN, SPI, 80000000))
  {
    if (retry == 0)
    {
      M5.Display.fillScreen(TFT_WHITE);
      M5.Display.setCursor(20, 20);
    }
    M5.Display.printf("SD init failed (attempt %d)...\n", ++retry);
    M5.Display.display();
    delay(1000);
    if (retry >= 5)
    {
      drawError("SD card not found!\nInsert SD and reset.");
      while (1)
        delay(1000);
    }
  }
  Serial.println("SD OK");
}

void scanMangaFolders()
{
  setCpuFrequencyMhz(240);
  mangaFolders.clear();
  mangaPageCounts.clear();

  File root = SD.open(MANGA_ROOT);
  if (!root || !root.isDirectory())
  {
    Serial.println("No /manga directory");
    return;
  }

  File entry;
  while ((entry = root.openNextFile()))
  {
    if (entry.isDirectory())
    {
      String name = entry.name();
      int slash = name.lastIndexOf('/');
      if (slash >= 0)
        name = name.substring(slash + 1);
      if (name.length() > 0 && !name.startsWith("."))
      {
        mangaFolders.push_back(name);
        Serial.printf("  Folder: %s\n", name.c_str());
      }
    }
    entry.close();
  }
  root.close();

  std::sort(mangaFolders.begin(), mangaFolders.end());
  mangaPageCounts.assign(mangaFolders.size(), -1);
  Serial.printf("Found %d manga folders\n", (int)mangaFolders.size());
  setCpuFrequencyMhz(80);
}
 
void scanBookFiles()
{
  setCpuFrequencyMhz(240);
  bookFiles.clear();
 
  File root = SD.open(BOOK_ROOT);
  if (!root || !root.isDirectory())
  {
    SD.mkdir(BOOK_ROOT);
    Serial.println("Created /book directory");
    return;
  }
 
  File entry;
  while ((entry = root.openNextFile()))
  {
    if (!entry.isDirectory())
    {
      String name = entry.name();
      if (name.endsWith(".txt") || name.endsWith(".TXT"))
      {
        int slash = name.lastIndexOf('/');
        if (slash >= 0)
          name = name.substring(slash + 1);
        bookFiles.push_back(name);
      }
    }
    entry.close();
  }
  root.close();
 
  std::sort(bookFiles.begin(), bookFiles.end());
  Serial.printf("Found %d book files\n", (int)bookFiles.size());
  setCpuFrequencyMhz(80);
}

static int findCachedPageCount(const String &folder)
{
  int slash = folder.lastIndexOf('/');
  String name = (slash >= 0) ? folder.substring(slash + 1) : folder;
  for (size_t i = 0; i < mangaFolders.size(); ++i)
  {
    if (mangaFolders[i] == name)
    {
      return mangaPageCounts[i];
    }
  }
  return -1;
}

static void storeCachedPageCount(const String &folder, int count)
{
  int slash = folder.lastIndexOf('/');
  String name = (slash >= 0) ? folder.substring(slash + 1) : folder;
  for (size_t i = 0; i < mangaFolders.size(); ++i)
  {
    if (mangaFolders[i] == name)
    {
      mangaPageCounts[i] = count;
      return;
    }
  }
}

String makePagePath(const String &folder, int n)
{
  char buf[128];
  snprintf(buf, sizeof(buf), "%s/%s%0*d%s", folder.c_str(), IMG_PREFIX,
           IMG_DIGITS, n, IMG_SUFFIX);
  return String(buf);
}

static bool pageExistsFast(char *buf, int prefixLen, int n)
{
  sprintf(buf + prefixLen, "/%s%0*d%s", IMG_PREFIX, IMG_DIGITS, n, IMG_SUFFIX);
  return SD.exists(buf);
}

static int readCachedPageCountSD(const String &folder)
{
  String cachePath = folder + "/.pagecount";
  File f = SD.open(cachePath.c_str(), FILE_READ);
  if (!f)
    return -1;
  String val = f.readStringUntil('\n');
  f.close();
  val.trim();
  int count = val.toInt();
  if (count <= 0 && val != "0")
    return -1; // Invalid content
  return count;
}

static void writeCachedPageCountSD(const String &folder, int count)
{
  String cachePath = folder + "/.pagecount";
  File f = SD.open(cachePath.c_str(), FILE_WRITE);
  if (f)
  {
    f.println(count);
    f.close();
    Serial.printf("Wrote page count cache: %s = %d\n", cachePath.c_str(), count);
  }
}

void invalidatePageCountCache(const String &folder)
{
  String cachePath = folder + "/.pagecount";
  if (SD.exists(cachePath.c_str()))
  {
    SD.remove(cachePath.c_str());
    Serial.printf("Invalidated page count cache: %s\n", cachePath.c_str());
  }
  // Also clear the in-memory cache
  int slash = folder.lastIndexOf('/');
  String name = (slash >= 0) ? folder.substring(slash + 1) : folder;
  for (size_t i = 0; i < mangaFolders.size(); ++i)
  {
    if (mangaFolders[i] == name)
    {
      mangaPageCounts[i] = -1;
      break;
    }
  }
}

int findTotalPages(const String &folder)
{
  static String cachedFolder = "";
  static int cachedCount = 0;
  if (folder == cachedFolder)
    return cachedCount;

  // Check in-memory cache first
  int count = findCachedPageCount(folder);
  if (count >= 0)
  {
    cachedFolder = folder;
    cachedCount = count;
    return count;
  }

  // Check persistent SD cache file
  count = readCachedPageCountSD(folder);
  if (count >= 0)
  {
    Serial.printf("Cache hit from SD: %d pages\n", count);
    cachedFolder = folder;
    cachedCount = count;
    storeCachedPageCount(folder, count);
    return count;
  }

  // No cache — run binary search
  char pathBuf[256];
  strncpy(pathBuf, folder.c_str(), sizeof(pathBuf) - 64);
  pathBuf[sizeof(pathBuf) - 65] = '\0';
  int prefixLen = strlen(pathBuf);

  if (!pageExistsFast(pathBuf, prefixLen, 0))
  {
    cachedFolder = folder;
    cachedCount = 0;
    storeCachedPageCount(folder, 0);
    writeCachedPageCountSD(folder, 0);
    return 0;
  }

  int hi = 1;
  while (pageExistsFast(pathBuf, prefixLen, hi))
  {
    hi *= 2;
    if (hi > 100000)
    {
      hi = 100000;
      break;
    }
  }

  int lo = hi / 2;
  while (lo + 1 < hi)
  {
    int mid = lo + (hi - lo) / 2;
    if (pageExistsFast(pathBuf, prefixLen, mid))
      lo = mid;
    else
      hi = mid;
  }

  cachedFolder = folder;
  cachedCount = lo + 1;
  storeCachedPageCount(folder, cachedCount);
  writeCachedPageCountSD(folder, cachedCount);
  return cachedCount;
}

void saveProgress()
{
  static unsigned long lastSaveMs = 0;
  unsigned long now = millis();
  
  if (appState == STATE_READER) {
    lastMangaPath = currentMangaPath;
    lastPage = currentPage;
    updateLastMangaName();
    isLastReadManga = true;
  } else if (appState == STATE_TEXT_READER) {
    isLastReadManga = false;
  }
  
  if (now - lastSaveMs < 2000)
    return;
  lastSaveMs = now;
  prefs.begin("manga", false);
  prefs.putString("lastPath", lastMangaPath);
  prefs.putInt("lastPage", lastPage);
  prefs.putInt("lastStrip", currentStrip);
  prefs.putInt("orientMode", (int)orientationMode);
  prefs.putInt("fitMode", (int)fitMode);
  prefs.putString("lastBook", currentBookPath);
  prefs.putInt("lastTextPg", currentTextPage);
  prefs.putBool("lastIsManga", isLastReadManga);
  prefs.end();

  // Save per-book progress
  if (appState == STATE_READER && currentMangaPath.length() > 0)
    saveBookProgress(currentMangaPath, currentPage, currentStrip);
  else if (appState == STATE_TEXT_READER && currentBookPath.length() > 0)
    saveBookProgress(currentBookPath, currentTextPage);
}

void loadConfig()
{
  File f = SD.open("/manga/config.txt");
  if (f)
  {
    while (f.available())
    {
      String line = f.readStringUntil('\n');
      line.trim();
      if (line.startsWith("strips_per_page="))
      {
        stripsPerPage = line.substring(16).toInt();
        if (stripsPerPage < 1)
          stripsPerPage = 1;
      }
      else if (line.startsWith("strip_overlap_px="))
      {
        stripOverlapPx = line.substring(17).toInt();
      }
    }
    f.close();
    Serial.printf("Config loaded: strips=%d, overlap=%d\n", stripsPerPage, stripOverlapPx);
  }
}

void loadProgress()
{
  prefs.begin("manga", true);
  lastMangaPath = prefs.getString("lastPath", "");
  lastPage = prefs.getInt("lastPage", 0);
  currentStrip = prefs.getInt("lastStrip", 0);
  if (prefs.isKey("orientMode")) {
    orientationMode = (OrientationMode)prefs.getInt("orientMode", (int)ORIENT_PORTRAIT);
  } else {
    // Migration fallback
    orientationMode = prefs.getBool("horizMode", false) ? ORIENT_LANDSCAPE : ORIENT_PORTRAIT;
  }
  fitMode = (FitMode)prefs.getInt("fitMode", (int)FIT_SMART);
  currentBookPath = prefs.getString("lastBook", "");
  currentTextPage = prefs.getInt("lastTextPg", 0);
  isLastReadManga = prefs.getBool("lastIsManga", true);
  prefs.end();
  updateLastMangaName();
}

void updateLastMangaName()
{
  if (lastMangaPath.length() == 0)
  {
    lastMangaName = "";
    return;
  }
  int slash = lastMangaPath.lastIndexOf('/');
  if (slash >= 0)
    lastMangaName = lastMangaPath.substring(slash + 1);
  else
    lastMangaName = lastMangaPath;
}

// ── Per-book progress persistence ───────────────────────────────────
// Manga: /manga/<folder>/.progress (contains "page\nstrip")
// Books: /book/<filename>.progress (contains "page")

static String getProgressPath(const String &path)
{
  if (path.startsWith(MANGA_ROOT))
  {
    // Manga folder: /manga/<folder>/.progress
    return path + "/.progress";
  }
  else
  {
    // Text book: /book/<file>.progress
    return path + ".progress";
  }
}

int loadBookProgress(const String &path)
{
  String progPath = getProgressPath(path);
  File f = SD.open(progPath.c_str(), FILE_READ);
  if (!f)
    return 0;
  String val = f.readStringUntil('\n');
  f.close();
  val.trim();
  int page = val.toInt();
  if (page < 0) page = 0;
  Serial.printf("Loaded per-book progress: %s → page %d\n", progPath.c_str(), page);
  return page;
}

int loadBookProgressStrip(const String &path)
{
  String progPath = getProgressPath(path);
  File f = SD.open(progPath.c_str(), FILE_READ);
  if (!f)
    return 0;
  f.readStringUntil('\n'); // skip page line
  String val = f.readStringUntil('\n');
  f.close();
  val.trim();
  int strip = val.toInt();
  if (strip < 0) strip = 0;
  return strip;
}

void saveBookProgress(const String &path, int page, int strip)
{
  String progPath = getProgressPath(path);
  File f = SD.open(progPath.c_str(), FILE_WRITE);
  if (f)
  {
    f.println(page);
    f.println(strip);
    f.close();
  }
}
