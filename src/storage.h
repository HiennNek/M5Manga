#pragma once

#include "config.h"
#include "state.h"

void sdInit();
void scanMangaFolders();
void scanBookFiles();
String makePagePath(const String &folder, int n);
bool pageExists(const String &folder, int n);
int findTotalPages(const String &folder);
void invalidatePageCountCache(const String &folder);
void loadConfig();
void saveProgress();
void loadProgress();
void updateLastMangaName();
int loadBookProgress(const String &path);
int loadBookProgressStrip(const String &path);
void saveBookProgress(const String &path, int page, int strip = 0);
