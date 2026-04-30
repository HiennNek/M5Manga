// A friendly reminder from a real human:
// If VSCode's code formatter give out compilation error
// Just place SD.h before M5Unified.h
// It took me 10min to figure this out
// Fuck arduino framework
// (This is not AI generated)

#include "ui.h"
#include "bookmarks.h"
#include "icon.h"
#include "state.h"
#include "storage.h"
#include "wifi_server.h"
#include "font.h"
#include <SD.h>
#include <M5Unified.h>
#include <algorithm>
#include <esp_heap_caps.h>
#include <JPEGDEC.h>

static JPEGDEC jpeg;

struct JpegDrawContext {
    LGFX_Sprite* spr;
    int offsetX;
    int offsetY;
    int maxWidth;
    int maxHeight;
};

static int drawMCU(JPEGDRAW *pDraw) {
    JpegDrawContext* ctx = (JpegDrawContext*)pDraw->pUser;
    LGFX_Sprite* spr = ctx->spr;
    uint16_t* pixels = (uint16_t*)pDraw->pPixels;
    uint16_t* sprBuffer = (uint16_t*)spr->getBuffer();
    
    int sprWidth = spr->width();
    int sprHeight = spr->height();
    
    int cx = pDraw->x;
    int cy = pDraw->y;
    int cw = pDraw->iWidth;
    int ch = pDraw->iHeight;
    
    if (ctx->maxWidth > 0 && cx + cw > ctx->maxWidth) cw = ctx->maxWidth - cx;
    if (ctx->maxHeight > 0 && cy + ch > ctx->maxHeight) ch = ctx->maxHeight - cy;
    if (cw <= 0 || ch <= 0) return 1;

    int outX = cx + ctx->offsetX;
    int outY = cy + ctx->offsetY;

    if (outX >= sprWidth || outY >= sprHeight) return 1;
    if (outX < 0) { cw += outX; outX = 0; }
    if (outY < 0) { ch += outY; outY = 0; }
    if (outX + cw > sprWidth) cw = sprWidth - outX;
    if (outY + ch > sprHeight) ch = sprHeight - outY;

    if (cw <= 0 || ch <= 0) return 1;

    for (int y = 0; y < ch; y++) {
        memcpy(&sprBuffer[(outY + y) * sprWidth + outX], &pixels[y * pDraw->iWidth], cw * 2);
    }
    
    return 1;
}

static bool drawJpgWithJpegDec(LGFX_Sprite& spr, uint8_t* buf, size_t size, int x, int y, int maxWidth, int maxHeight) {
    JpegDrawContext ctx = {&spr, x, y, maxWidth, maxHeight};
    if (jpeg.openRAM(buf, size, drawMCU)) {
        jpeg.setPixelType(RGB565_BIG_ENDIAN);
        jpeg.setUserPointer(&ctx);
        bool res = jpeg.decode(0, 0, 0);
        jpeg.close();
        return res;
    }
    return false;
}

static bool drawJpgWithJpegDecFile(LGFX_Sprite& spr, File& file, int x, int y, int maxWidth, int maxHeight) {
    JpegDrawContext ctx = {&spr, x, y, maxWidth, maxHeight};
    if (jpeg.open(file, drawMCU)) {
        jpeg.setPixelType(RGB565_BIG_ENDIAN);
        jpeg.setUserPointer(&ctx);
        bool res = jpeg.decode(0, 0, 0);
        jpeg.close();
        return res;
    }
    return false;
}


static bool forceFullMenuRedraw = true;

static uint8_t *jpgBuffer = nullptr;
static size_t jpgBufferSize = 0;

void ensureJpgBuffer(size_t size)
{
  if (jpgBufferSize < size)
  {
    if (jpgBuffer)
      heap_caps_free(jpgBuffer);
    jpgBuffer = (uint8_t *)heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (jpgBuffer)
      jpgBufferSize = size;
    else
      jpgBufferSize = 0;
  }
}

void prepareSprite(LGFX_Sprite &sprite, int w, int h, int depth,
                   bool usePsram)
{
  if (sprite.width() == w && sprite.height() == h &&
      sprite.getColorDepth() == depth)
  {
    return;
  }
  sprite.deleteSprite();
  sprite.setPsram(usePsram);
  sprite.setColorDepth(depth);
  sprite.createSprite(w, h);
}

void applyFloydSteinberg(LGFX_Sprite &sprite)
{
  int w = sprite.width();
  int h = sprite.height();
  uint16_t *buf = (uint16_t *)sprite.getBuffer();
  if (!buf)
    return;

  // Error buffers for current and next row (initialized to 0)
  int16_t *error_buf =
      (int16_t *)heap_caps_calloc(w * 2, sizeof(int16_t), MALLOC_CAP_INTERNAL);
  if (!error_buf)
    return;

  int16_t *curr_err = error_buf;
  int16_t *next_err = error_buf + w;

  for (int y = 0; y < h; y++)
  {
    for (int x = 0; x < w; x++)
    {
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
      if (gray_with_err < 0)
        gray_with_err = 0;
      else if (gray_with_err > 255)
        gray_with_err = 255;

      // Quantize to 16 levels (0, 17, 34 ... 255)
      // Each level is exactly 17 apart: floor((v / 17) + 0.5) * 17
      int32_t quantized = ((gray_with_err + 8) / 17) * 17;
      if (quantized > 255)
        quantized = 255;

      int32_t err = gray_with_err - quantized;

      // Pack quantized gray back to RGB565 (Big-Endian)
      uint16_t q = (uint16_t)quantized;
      uint16_t out_pixel = ((q >> 3) << 11) | ((q >> 2) << 5) | (q >> 3);
      buf[y * w + x] = (out_pixel >> 8) | (out_pixel << 8);

      // Floyd-Steinberg error distribution
      //   X   7/16
      // 3/16 5/16 1/16
      if (x + 1 < w)
        curr_err[x + 1] += (err * 7) >> 4;
      if (y + 1 < h)
      {
        if (x > 0)
          next_err[x - 1] += (err * 3) >> 4;
        next_err[x] += (err * 5) >> 4;
        if (x + 1 < w)
          next_err[x + 1] += (err * 1) >> 4;
      }
    }
    // Swap error buffers for next row
    int16_t *tmp = curr_err;
    curr_err = next_err;
    next_err = tmp;
    memset(next_err, 0, w * sizeof(int16_t));
  }

  heap_caps_free(error_buf);
}

// Atkinson dithering — distributes only 6/8 of the error (not full),
// producing higher contrast and sharper edges. Ideal for manga lineart.
void applyAtkinson(LGFX_Sprite &sprite)
{
  int w = sprite.width();
  int h = sprite.height();
  uint16_t *buf = (uint16_t *)sprite.getBuffer();
  if (!buf)
    return;

  // Atkinson looks ahead 2 columns and 2 rows, so we need 3 row buffers
  int16_t *error_buf =
      (int16_t *)heap_caps_calloc(w * 3, sizeof(int16_t), MALLOC_CAP_INTERNAL);
  if (!error_buf)
    return;

  int16_t *row0 = error_buf;         // current row
  int16_t *row1 = error_buf + w;     // next row
  int16_t *row2 = error_buf + w * 2; // row after next

  for (int y = 0; y < h; y++)
  {
    for (int x = 0; x < w; x++)
    {
      uint16_t raw_pixel = buf[y * w + x];
      uint16_t pixel = (raw_pixel >> 8) | (raw_pixel << 8);
      int32_t r = (pixel >> 11) << 3;
      int32_t g = ((pixel >> 5) & 0x3F) << 2;
      int32_t b = (pixel & 0x1F) << 3;
      int32_t gray = (r * 306 + g * 601 + b * 117) >> 10;

      int32_t gray_with_err = gray + row0[x];
      if (gray_with_err < 0)
        gray_with_err = 0;
      else if (gray_with_err > 255)
        gray_with_err = 255;

      int32_t quantized = ((gray_with_err + 8) / 17) * 17;
      if (quantized > 255)
        quantized = 255;

      // Atkinson distributes 6/8 of error (loses 2/8 → higher contrast)
      int32_t err = (gray_with_err - quantized) >> 3; // err/8 unit

      uint16_t q = (uint16_t)quantized;
      uint16_t out_pixel = ((q >> 3) << 11) | ((q >> 2) << 5) | (q >> 3);
      buf[y * w + x] = (out_pixel >> 8) | (out_pixel << 8);

      // Atkinson error distribution pattern:
      //       X   1/8  1/8
      //  1/8  1/8  1/8
      //       1/8
      if (x + 1 < w)
        row0[x + 1] += err;
      if (x + 2 < w)
        row0[x + 2] += err;
      if (y + 1 < h)
      {
        if (x > 0)
          row1[x - 1] += err;
        row1[x] += err;
        if (x + 1 < w)
          row1[x + 1] += err;
      }
      if (y + 2 < h)
      {
        row2[x] += err;
      }
    }
    // Rotate row buffers
    int16_t *tmp = row0;
    row0 = row1;
    row1 = row2;
    row2 = tmp;
    memset(row2, 0, w * sizeof(int16_t));
  }

  heap_caps_free(error_buf);
}

// Ordered (Bayer 4x4) dithering — fastest, no error propagation.
// Produces a regular pattern. Good for fast previews.
static const int8_t bayer4x4[4][4] = {
    {-8, 0, -6, 2}, {4, -4, 6, -2}, {-5, 3, -7, 1}, {7, -1, 5, -3}};

void applyOrderedBayer(LGFX_Sprite &sprite)
{
  int w = sprite.width();
  int h = sprite.height();
  uint16_t *buf = (uint16_t *)sprite.getBuffer();
  if (!buf)
    return;

  for (int y = 0; y < h; y++)
  {
    for (int x = 0; x < w; x++)
    {
      uint16_t raw_pixel = buf[y * w + x];
      uint16_t pixel = (raw_pixel >> 8) | (raw_pixel << 8);
      int32_t r = (pixel >> 11) << 3;
      int32_t g = ((pixel >> 5) & 0x3F) << 2;
      int32_t b = (pixel & 0x1F) << 3;
      int32_t gray = (r * 306 + g * 601 + b * 117) >> 10;

      // Add Bayer threshold bias (scaled to match 16-level quantization)
      gray += bayer4x4[y & 3][x & 3];
      if (gray < 0)
        gray = 0;
      else if (gray > 255)
        gray = 255;

      int32_t quantized = ((gray + 8) / 17) * 17;
      if (quantized > 255)
        quantized = 255;

      uint16_t q = (uint16_t)quantized;
      uint16_t out_pixel = ((q >> 3) << 11) | ((q >> 2) << 5) | (q >> 3);
      buf[y * w + x] = (out_pixel >> 8) | (out_pixel << 8);
    }
  }
}

// Dispatcher — applies the selected dithering algorithm
void applyDithering(LGFX_Sprite &sprite)
{
  switch (ditherMode)
  {
  case DITHER_FLOYD_STEINBERG:
    applyFloydSteinberg(sprite);
    break;
  case DITHER_ATKINSON:
    applyAtkinson(sprite);
    break;
  case DITHER_ORDERED:
    applyOrderedBayer(sprite);
    break;
  default:
    break; // DITHER_OFF — do nothing
  }
}

// Human-readable label for the current dither mode
const char *ditherModeName()
{
  switch (ditherMode)
  {
  case DITHER_OFF:
    return "OFF";
  case DITHER_FLOYD_STEINBERG:
    return "FLOYD-STEINBERG";
  case DITHER_ATKINSON:
    return "ATKINSON";
  case DITHER_ORDERED:
    return "BAYER 4x4";
  default:
    return "UNKNOWN";
  }
}

// Apply brightness/contrast adjustment using a precomputed LUT.
// This runs on the RGB565 sprite in-place, before dithering.
void applyContrast(LGFX_Sprite &sprite)
{
  if (contrastPreset == CONTRAST_NORMAL)
    return; // No-op

  int w = sprite.width();
  int h = sprite.height();
  uint16_t *buf = (uint16_t *)sprite.getBuffer();
  if (!buf)
    return;

  // Precompute a 256-entry grayscale LUT for the selected preset.
  // Using fixed-point: contrast is 8.8 fixed (256 = 1.0x)
  int32_t contrast_fp, brightness;
  switch (contrastPreset)
  {
  case CONTRAST_VIVID:
    contrast_fp = 307;
    brightness = 0;
    break; // 1.2x
  case CONTRAST_HIGH:
    contrast_fp = 358;
    brightness = 5;
    break; // 1.4x
  case CONTRAST_LIGHT:
    contrast_fp = 256;
    brightness = 30;
    break; // 1.0x + bright
  default:
    return;
  }

  uint8_t lut[256];
  for (int i = 0; i < 256; i++)
  {
    int32_t val = (((i - 128) * contrast_fp) >> 8) + 128 + brightness;
    if (val < 0)
      val = 0;
    else if (val > 255)
      val = 255;
    lut[i] = (uint8_t)val;
  }

  // Apply LUT to every pixel (convert to gray, adjust, write back)
  for (int i = 0; i < w * h; i++)
  {
    uint16_t raw = buf[i];
    uint16_t pixel = (raw >> 8) | (raw << 8);
    int32_t r = (pixel >> 11) << 3;
    int32_t g = ((pixel >> 5) & 0x3F) << 2;
    int32_t b = (pixel & 0x1F) << 3;
    int32_t gray = (r * 306 + g * 601 + b * 117) >> 10;

    uint8_t adjusted = lut[gray];
    uint16_t q = adjusted;
    uint16_t out = ((q >> 3) << 11) | ((q >> 2) << 5) | (q >> 3);
    buf[i] = (out >> 8) | (out << 8);
  }
}

const char *contrastPresetName()
{
  switch (contrastPreset)
  {
  case CONTRAST_NORMAL:
    return "NORMAL";
  case CONTRAST_VIVID:
    return "VIVID";
  case CONTRAST_HIGH:
    return "HIGH";
  case CONTRAST_LIGHT:
    return "LIGHT";
  default:
    return "UNKNOWN";
  }
}

const char *fitModeName()
{
  switch (fitMode)
  {
  case FIT_SCREEN:
    return "FIT SCREEN";
  case FIT_WIDTH:
    return "FIT WIDTH";
  case FIT_HEIGHT:
    return "FIT HEIGHT";
  case FIT_SMART:
    return "SMART";
  default:
    return "UNKNOWN";
  }
}

// Standardized modern button helper
void drawModernButton(LGFX_Sprite &sprite, int x, int y, int w, int h,
                      const char *text, bool isPrimary)
{
  if (isPrimary)
  {
    sprite.fillRoundRect(x, y, w, h, UI_RADIUS, UI_FG);
    sprite.setTextColor(UI_BG, UI_FG);
  }
  else
  {
    sprite.fillRoundRect(x, y, w, h, UI_RADIUS, UI_BG);
    sprite.drawRoundRect(x, y, w, h, UI_RADIUS, UI_BORDER);
    sprite.setTextColor(UI_FG, UI_BG);
  }
  sprite.setFont(&fonts::DejaVu18);
  sprite.setTextDatum(middle_center);
  sprite.drawString(text, x + w / 2, y + h / 2);
  sprite.setTextDatum(top_left);
}

static int lastOutX = -1, lastOutY = -1;

void drawMagnifier(int x, int y, bool qualityMode)
{
  static LGFX_Sprite magSprite(&M5.Display);
  prepareSprite(magSprite, MAG_SIZE, MAG_SIZE, 16, true);
  if (!magSprite.getBuffer())
    return;

  // 1. Calculate new position
  int outX = x - MAG_SIZE / 2;
  int outY = y - MAG_SIZE - 60;
  if (outX < 10)
    outX = 10;
  if (outX + MAG_SIZE > DISPLAY_W - 10)
    outX = DISPLAY_W - MAG_SIZE - 10;
  if (outY < 10)
    outY = 10;

  // 2. Prepare the zoomed image
  int srcSize = MAG_SIZE / MAG_SCALE;
  int srcX = x, srcY = y;
  if (srcX < srcSize / 2)
    srcX = srcSize / 2;
  if (srcY < srcSize / 2)
    srcY = srcSize / 2;
  if (srcX > DISPLAY_W - srcSize / 2)
    srcX = DISPLAY_W - srcSize / 2;
  if (srcY > DISPLAY_H - srcSize / 2)
    srcY = DISPLAY_H - srcSize / 2;

  magSprite.fillScreen(TFT_WHITE);
  gSprite.setPivot(srcX, srcY);
  gSprite.pushRotateZoom(&magSprite, MAG_SIZE / 2, MAG_SIZE / 2, 0, MAG_SCALE,
                         MAG_SCALE);

  magSprite.drawRect(0, 0, MAG_SIZE, MAG_SIZE, TFT_BLACK);
  magSprite.drawRect(1, 1, MAG_SIZE - 2, MAG_SIZE - 2, TFT_WHITE);
  magSprite.drawRect(2, 2, MAG_SIZE - 4, MAG_SIZE - 4, TFT_BLACK);

  // 3. CLEANUP: If we moved, erase the old magnifier position using gSprite
  M5.Display.startWrite();
  if (lastOutX != -1 && (lastOutX != outX || lastOutY != outY))
  {
    // Restore background from gSprite to the OLD magnifier location
    M5.Display.setEpdMode(epd_mode_t::epd_fast);
    M5.Display.setClipRect(lastOutX, lastOutY, MAG_SIZE, MAG_SIZE);
    gSprite.pushSprite(0, 0);
    M5.Display.clearClipRect();
  }

  // 4. DRAW: Draw the new magnifier
  M5.Display.setEpdMode(qualityMode ? epd_mode_t::epd_quality
                                    : epd_mode_t::epd_fast);
  magSprite.pushSprite(outX, outY);

  M5.Display.display();
  M5.Display.endWrite();

  lastOutX = outX;
  lastOutY = outY;
}

// Reset tracking when magnifier closes
void resetMagnifierTracking()
{
  lastOutX = -1;
  lastOutY = -1;
}

void preloadPage(int page, int strip)
{
  if (page < 0 || page >= totalPages)
  {
    isNextPageReady = false;
    return;
  }
  if (isNextPageReady && preloadedPage == page && preloadedStrip == strip &&
      preloadedMangaPath == currentMangaPath)
  {
    return;
  }

  setCpuFrequencyMhz(240);
  String path = makePagePath(currentMangaPath, page);

  int rot = getActiveRotation();
  int screenW = (rot % 2 == 1) ? 960 : 540;
  int screenH = (rot % 2 == 1) ? 540 : 960;

  prepareSprite(nextPageSprite, screenW, screenH, 16, true);
  if (!nextPageSprite.getBuffer())
  {
    isNextPageReady = false;
    return;
  }

  File pageFile = SD.open(path.c_str());
  if (!pageFile)
  {
    isNextPageReady = false;
    return;
  }

  size_t fileSize = pageFile.size();
  ensureJpgBuffer(fileSize + 1024);

  bool success = false;
  if (jpgBuffer)
  {
    pageFile.read(jpgBuffer, fileSize);
    pageFile.close();
    if (jpeg.openRAM(jpgBuffer, fileSize, drawMCU))
    {
      int imgW = jpeg.getWidth();
      int imgH = jpeg.getHeight();
      
      static LGFX_Sprite fullPageSprite(&M5.Display);
      prepareSprite(fullPageSprite, imgW, imgH, 16, true);
      
      JpegDrawContext ctx = {&fullPageSprite, 0, 0, imgW, imgH};
      jpeg.setPixelType(RGB565_BIG_ENDIAN);
      jpeg.setUserPointer(&ctx);
      success = jpeg.decode(0, 0, 0);
      jpeg.close();

      if (success)
      {
        nextPageSprite.fillScreen(TFT_WHITE);

        float scale = 1.0f;
        FitMode effectiveFitMode = fitMode;

        if (effectiveFitMode == FIT_SMART)
        {
          float imgAspect = (float)imgW / (float)imgH;
          float screenAspect = (float)screenW / (float)screenH;
          if (imgAspect > screenAspect * 1.1f || imgH > imgW * 1.5f)
            effectiveFitMode = FIT_WIDTH;
          else
            effectiveFitMode = FIT_SCREEN;
        }

        if (effectiveFitMode == FIT_SCREEN)
        {
          scale = std::min((float)screenW / imgW, (float)screenH / imgH);
          int outW = (int)(imgW * scale);
          int outH = (int)(imgH * scale);
          int offsetX = (screenW - outW) / 2;
          int offsetY = (screenH - outH) / 2;
          fullPageSprite.setPivot(imgW / 2, imgH / 2);
          fullPageSprite.pushRotateZoom(&nextPageSprite, offsetX + outW / 2, offsetY + outH / 2, 0, scale, scale);
        }
        else if (effectiveFitMode == FIT_WIDTH)
        {
          scale = (float)screenW / imgW;
          int visibleImgH = (int)(screenH / scale);
          int overlapImg = (int)(stripOverlapPx / scale);
          if (overlapImg >= visibleImgH)
            overlapImg = visibleImgH / 2;

          int stepImg = visibleImgH - overlapImg;
          int localStripsPerPage = (imgH - overlapImg + stepImg - 1) / stepImg;
          if (localStripsPerPage < 1)
            localStripsPerPage = 1;

          int localStrip = strip;
          if (localStrip == -1)
            localStrip = localStripsPerPage - 1;
          if (localStrip >= localStripsPerPage)
            localStrip = localStripsPerPage - 1;

          int yStart = localStrip * stepImg;
          if (yStart + visibleImgH > imgH)
            yStart = imgH - visibleImgH;
          if (yStart < 0)
            yStart = 0;

          fullPageSprite.setPivot(0, yStart);
          fullPageSprite.pushRotateZoom(&nextPageSprite, 0, 0, 0, scale, scale);
        }
        else if (effectiveFitMode == FIT_HEIGHT)
        {
          scale = (float)screenH / imgH;
          int visibleImgW = (int)(screenW / scale);
          int overlapImg = (int)(stripOverlapPx / scale);
          if (overlapImg >= visibleImgW)
            overlapImg = visibleImgW / 2;

          int stepImg = visibleImgW - overlapImg;
          int localStripsPerPage = (imgW - overlapImg + stepImg - 1) / stepImg;
          if (localStripsPerPage < 1)
            localStripsPerPage = 1;

          int localStrip = strip;
          if (localStrip == -1)
            localStrip = localStripsPerPage - 1;
          if (localStrip >= localStripsPerPage)
            localStrip = localStripsPerPage - 1;

          int xStart = localStrip * stepImg;
          if (xStart + visibleImgW > imgW)
            xStart = imgW - visibleImgW;
          if (xStart < 0)
            xStart = 0;

          fullPageSprite.setPivot(xStart, 0);
          fullPageSprite.pushRotateZoom(&nextPageSprite, 0, 0, 0, scale, scale);
        }
      }
      fullPageSprite.deleteSprite();
    }
  }
  else
  {
    success = drawJpgWithJpegDecFile(nextPageSprite, pageFile, 0, 0, screenW, screenH);
    pageFile.close();
  }

  if (success)
  {
    applyContrast(nextPageSprite);
    if (ditherMode != DITHER_OFF)
    {
      applyDithering(nextPageSprite);
    }
    preloadedPage = page;
    preloadedStrip = strip;
    preloadedMangaPath = currentMangaPath;
    isNextPageReady = true;
  }
  else
  {
    isNextPageReady = false;
  }
  setCpuFrequencyMhz(80);
}

static bool drawThumbnail(LGFX_Sprite &spr, File &f, int x, int y, int w, int h)
{
  if (jpeg.open(f, drawMCU))
  {
    int imgW = jpeg.getWidth();
    int imgH = jpeg.getHeight();

    int scaleMode = 0; // iOptions: 0=1:1, 1=1:2, 2=1:4, 3=1:8
    int div = 1;

    if (imgW >= w * 8 && imgH >= h * 8)
    {
      scaleMode = JPEG_SCALE_EIGHTH;
      div = 8;
    }
    else if (imgW >= w * 4 && imgH >= h * 4)
    {
      scaleMode = JPEG_SCALE_QUARTER;
      div = 4;
    }
    else if (imgW >= w * 2 && imgH >= h * 2)
    {
      scaleMode = JPEG_SCALE_HALF;
      div = 2;
    }

    int decW = imgW / div;
    int decH = imgH / div;

    static LGFX_Sprite tempSpr(&M5.Display);
    prepareSprite(tempSpr, decW, decH, 16, true);
    if (!tempSpr.getBuffer())
    {
      jpeg.close();
      return false;
    }

    JpegDrawContext ctx = {&tempSpr, 0, 0, decW, decH};
    jpeg.setPixelType(RGB565_BIG_ENDIAN);
    jpeg.setUserPointer(&ctx);
    bool res = jpeg.decode(0, 0, scaleMode);
    jpeg.close();

    if (res)
    {
      float imgAspect = (float)imgW / (float)imgH;
      float targetAspect = (float)w / (float)h;

      float finalScale;
      if (imgAspect > targetAspect)
      {
        finalScale = (float)w / decW;
      }
      else
      {
        finalScale = (float)h / decH;
      }

      tempSpr.setPivot(decW / 2, decH / 2);
      tempSpr.pushRotateZoom(&spr, x + w / 2, y + h / 2, 0, finalScale,
                             finalScale);
    }
    return res;
  }
  return false;
}

void drawMenu()
{
  static String drawnLastMangaName = "";
  static int drawnLastPage = -1;
  static int drawnLastMenuScroll = -1;

  M5.Display.setRotation(0);
  
  // Total items calculation depends on the active tab
  // Index 0: BOOKMARKS, Index 1+: Content
  int totalItems;
  if (currentMenuTab == TAB_COMIC)
    totalItems = (int)mangaFolders.size() + 1;
  else
    totalItems = (int)bookFiles.size() + 1;

  int end = std::min(totalItems, menuScroll + MENU_VISIBLE);

  if (!menuCacheValid || lastDrawnMenuScroll != menuScroll)
  {
    prepareSprite(menuCacheSprite, DISPLAY_W, DISPLAY_H, 8, true);
    if (menuCacheSprite.getBuffer())
    {
      menuCacheSprite.fillScreen(UI_BG);
      
      // Draw Tabs
      int tabW = DISPLAY_W / 2;
      int tabH = 80;
      
      // Comic Tab
      if (currentMenuTab == TAB_COMIC) {
        menuCacheSprite.fillRect(0, 0, tabW, tabH, UI_FG);
        menuCacheSprite.setTextColor(UI_BG, UI_FG);
      } else {
        menuCacheSprite.drawRect(0, 0, tabW, tabH, UI_BORDER);
        menuCacheSprite.setTextColor(UI_FG, UI_BG);
      }
      menuCacheSprite.setFont(&fonts::DejaVu24);
      menuCacheSprite.setTextDatum(middle_center);
      menuCacheSprite.drawString("Comic", tabW / 2, tabH / 2);

      // Document Tab
      if (currentMenuTab == TAB_DOCUMENT) {
        menuCacheSprite.fillRect(tabW, 0, tabW, tabH, UI_FG);
        menuCacheSprite.setTextColor(UI_BG, UI_FG);
      } else {
        menuCacheSprite.drawRect(tabW, 0, tabW, tabH, UI_BORDER);
        menuCacheSprite.setTextColor(UI_FG, UI_BG);
      }
      menuCacheSprite.drawString("Document", tabW + tabW / 2, tabH / 2);
      
      menuCacheSprite.setTextDatum(top_left);
      menuCacheSprite.drawLine(0, tabH, DISPLAY_W, tabH, UI_BORDER);

      if (totalItems == 0 || (currentMenuTab == TAB_COMIC && mangaFolders.empty()) || (currentMenuTab == TAB_DOCUMENT && bookFiles.empty()))
      {
        menuCacheSprite.setFont(&fonts::DejaVu18);
        menuCacheSprite.setTextColor(UI_FG, UI_BG);
        menuCacheSprite.setCursor(GRID_GUTTER, 120);
        menuCacheSprite.println(currentMenuTab == TAB_COMIC ? "No manga found in /manga/" : "No books found in /book/");
      }
      else
      {
        for (int i = menuScroll; i < end; i++)
        {
          int relIdx = i - menuScroll;
          int row = relIdx / GRID_COLS;
          int col = relIdx % GRID_COLS;
          int x = GRID_GUTTER + col * (THUMB_W + GRID_GUTTER);
          int y = GRID_Y_TOP + row * GRID_ROW_H;

          menuCacheSprite.fillRoundRect(x, y, THUMB_W, THUMB_H, UI_RADIUS, UI_BG);
          menuCacheSprite.drawRoundRect(x, y, THUMB_W, THUMB_H, UI_RADIUS, UI_BORDER);

          if (i == 0) // BOOKMARKS
          {
            menuCacheSprite.fillRoundRect(x + 10, y + 10, THUMB_W - 20, THUMB_H - 20, UI_RADIUS, UI_ACCENT);
            int iconX = x + (THUMB_W - 96) / 2;
            int iconY = y + (THUMB_H - 96) / 2;
            menuCacheSprite.drawBitmap(iconX, iconY, bookmarks_material_icon, 96, 96, UI_FG);
            menuCacheSprite.setFont(&fonts::DejaVu12);
            menuCacheSprite.setTextColor(UI_FG, UI_BG);
            menuCacheSprite.setTextDatum(top_center);
            menuCacheSprite.drawString("BOOKMARKS", x + THUMB_W / 2, y + THUMB_H + 10);
            menuCacheSprite.setTextDatum(top_left);
          }
          else // Content
          {
            int cIdx = i - 1;
            if (currentMenuTab == TAB_COMIC)
            {
              String coverPath = makePagePath(String(MANGA_ROOT) + "/" + mangaFolders[cIdx], 0);
              File coverFile = SD.open(coverPath.c_str());
              if (coverFile)
              {
                drawThumbnail(menuCacheSprite, coverFile, x + 2, y + 2, THUMB_W - 4, THUMB_H - 4);
                coverFile.close();
              }
              else
              {
                menuCacheSprite.setTextColor(UI_FG, UI_BG);
                menuCacheSprite.setFont(&fonts::DejaVu12);
                menuCacheSprite.setCursor(x + 10, y + THUMB_H / 2);
                menuCacheSprite.print("NO COVER");
              }
              String title = mangaFolders[cIdx];
              if (title.length() > 22) title = title.substring(0, 20) + "...";
              
              menuCacheSprite.setFont(&fonts::DejaVu12);
              menuCacheSprite.setTextColor(UI_FG, UI_BG);
              menuCacheSprite.setTextDatum(top_center);
              menuCacheSprite.drawString(title, x + THUMB_W / 2, y + THUMB_H + 10);
              menuCacheSprite.setTextDatum(top_left);
            }
            else // TAB_DOCUMENT
            {
              menuCacheSprite.fillRoundRect(x + 10, y + 10, THUMB_W - 20, THUMB_H - 20, UI_RADIUS, UI_ACCENT);
              int iconX = x + (THUMB_W - 96) / 2;
              int iconY = y + (THUMB_H - 96) / 2;
              menuCacheSprite.drawBitmap(iconX, iconY, book_material_icon, 96, 96, UI_FG);
              
              String title = bookFiles[cIdx];
              if (title.length() > 22) title = title.substring(0, 20) + "...";

              menuCacheSprite.setFont(&fonts::DejaVu12);
              menuCacheSprite.setTextColor(UI_FG, UI_BG);
              menuCacheSprite.setTextDatum(top_center);
              menuCacheSprite.drawString(title, x + THUMB_W / 2, y + THUMB_H + 10);
              menuCacheSprite.setTextDatum(top_left);
            }
          }
        }
      }
      menuCacheValid = true;
      drawnLastPage = -1; // Force resume bar redraw
      lastDrawnMenuScroll = menuScroll;
      forceFullMenuRedraw = true;
    }
  }

  bool isDocTab = (currentMenuTab == TAB_DOCUMENT);
  String currentResumeName = isDocTab ? currentBookPath : lastMangaName;
  int currentResumePage = isDocTab ? currentTextPage : lastPage;

  if (menuCacheValid &&
      (drawnLastMangaName != currentResumeName || 
       drawnLastPage != currentResumePage ||
       drawnLastMenuScroll != menuScroll))
  {
    String resumeName = currentResumeName;
    int resumePage = currentResumePage;

    if (isDocTab && resumeName.length() > 0) {
        int lastSlash = resumeName.lastIndexOf('/');
        if (lastSlash >= 0) resumeName = resumeName.substring(lastSlash + 1);
    }
    int barW = DISPLAY_W - 40;
    int barH = 60;
    int barX = 20;
    int barY = DISPLAY_H - 85;

    if (resumeName.length() == 0)
    {
      menuCacheSprite.fillRect(barX, barY - 2, barW + 4, barH + 4,
                               UI_BG); // Erase shadow + bar
    }
    else
    {
      // Shadow
      menuCacheSprite.fillRoundRect(barX + 2, barY + 2, barW, barH, UI_RADIUS,
                                    UI_SHADOW);
      // Pill
      menuCacheSprite.fillRoundRect(barX, barY, barW, barH, UI_RADIUS, UI_BG);
      menuCacheSprite.drawRoundRect(barX, barY, barW, barH, UI_RADIUS,
                                    UI_BORDER);

      menuCacheSprite.setTextColor(UI_FG, UI_BG);
      menuCacheSprite.setFont(&fonts::DejaVu12);
      menuCacheSprite.setCursor(barX + 15, barY + 12);
      menuCacheSprite.print("CONTINUE: ");

      menuCacheSprite.setFont(&fonts::DejaVu18);
      String shortName = resumeName;
      if (shortName.length() > 19)
        shortName = shortName.substring(0, 17) + "...";
      menuCacheSprite.print(shortName);

      menuCacheSprite.setFont(&fonts::DejaVu12);
      menuCacheSprite.setCursor(barX + 15, barY + 36);
      menuCacheSprite.printf("Page %d", resumePage + 1);

      // "RESUME" Button
      int btnW = 90;
      int btnH = 40;
      int btnX = barX + barW - btnW - 10;
      int btnY = barY + 10;
      drawModernButton(menuCacheSprite, btnX, btnY, btnW, btnH, "RESUME", true);
    }
    drawnLastMangaName = currentResumeName;
    drawnLastPage = currentResumePage;
    drawnLastMenuScroll = menuScroll;
    forceFullMenuRedraw = true;
  }

  static int prevMenuSelected = -1;
  static int lastMenuScrollForDirty = -1;
  bool isFullRedraw = forceFullMenuRedraw;
  if (lastMenuScrollForDirty != menuScroll)
    isFullRedraw = true;
  lastMenuScrollForDirty = menuScroll;
  forceFullMenuRedraw = false;

  M5.Display.startWrite();
  if (isFullRedraw)
  {
    if (menuCacheValid)
      menuCacheSprite.pushSprite(0, 0);
    else
      M5.Display.fillScreen(TFT_WHITE);
    for (int i = menuScroll; i < end; i++)
    {
      if (i == menuSelected)
      {
        int relIdx = i - menuScroll;
        int row = relIdx / GRID_COLS;
        int col = relIdx % GRID_COLS;
        int x = GRID_GUTTER + col * (THUMB_W + GRID_GUTTER);
        int y = GRID_Y_TOP + row * GRID_ROW_H;
        M5.Display.drawRoundRect(x - 5, y - 5, THUMB_W + 10, THUMB_H + 10,
                                 UI_RADIUS + 2, TFT_BLACK);
        M5.Display.drawRoundRect(x - 4, y - 4, THUMB_W + 8, THUMB_H + 8,
                                 UI_RADIUS + 1, TFT_BLACK);
        M5.Display.fillRect(x + (THUMB_W / 2) - 15, y + THUMB_H + 30, 30, 3,
                            TFT_BLACK);
      }
    }
  }
  else
  {
    if (prevMenuSelected >= menuScroll && prevMenuSelected < end)
    {
      int relIdx = prevMenuSelected - menuScroll;
      int row = relIdx / GRID_COLS;
      int col = relIdx % GRID_COLS;
      int x = GRID_GUTTER + col * (THUMB_W + GRID_GUTTER);
      int y = GRID_Y_TOP + row * GRID_ROW_H;
      M5.Display.setClipRect(x - 6, y - 6, THUMB_W + 12, THUMB_H + 40);
      menuCacheSprite.pushSprite(0, 0);
      M5.Display.clearClipRect();
    }
    if (menuSelected >= menuScroll && menuSelected < end)
    {
      int relIdx = menuSelected - menuScroll;
      int row = relIdx / GRID_COLS;
      int col = relIdx % GRID_COLS;
      int x = GRID_GUTTER + col * (THUMB_W + GRID_GUTTER);
      int y = GRID_Y_TOP + row * GRID_ROW_H;
      M5.Display.setClipRect(x - 6, y - 6, THUMB_W + 12, THUMB_H + 40);
      menuCacheSprite.pushSprite(0, 0);
      M5.Display.clearClipRect();
      M5.Display.drawRoundRect(x - 5, y - 5, THUMB_W + 10, THUMB_H + 10,
                               UI_RADIUS + 2, TFT_BLACK);
      M5.Display.drawRoundRect(x - 4, y - 4, THUMB_W + 8, THUMB_H + 8,
                               UI_RADIUS + 1, TFT_BLACK);
      M5.Display.fillRect(x + (THUMB_W / 2) - 15, y + THUMB_H + 30, 30, 3,
                          TFT_BLACK);
    }
  }
  prevMenuSelected = menuSelected;
  M5.Display.display();
  M5.Display.endWrite();
}

void drawControlCenter()
{
  forceFullMenuRedraw = true;
  M5.Display.setRotation(0);
  prepareSprite(gSprite, DISPLAY_W, DISPLAY_H, 8, true);
  if (!gSprite.getBuffer())
    return;
  gSprite.fillScreen(TFT_MAGENTA); // Use magenta as transparent color
  
  // Phone-like fast access menu at the top
  int modW = DISPLAY_W - 40;
  int modH = 220;
  int modX = 20;
  int modY = 40;

  // Drop shadow
  gSprite.fillRoundRect(modX + 6, modY + 6, modW, modH, UI_RADIUS, UI_SHADOW);

  // Modal
  gSprite.fillRoundRect(modX, modY, modW, modH, UI_RADIUS, UI_BG);
  gSprite.drawRoundRect(modX, modY, modW, modH, UI_RADIUS, UI_BORDER);
  gSprite.drawRoundRect(modX + 1, modY + 1, modW - 2, modH - 2, UI_RADIUS - 1,
                        UI_BORDER);
  gSprite.drawRoundRect(modX + 2, modY + 2, modW - 4, modH - 4, UI_RADIUS - 2,
                        UI_BORDER);

  // Status Bar Line: Time & Date (Left), Battery (Right)
  auto dt = M5.Rtc.getDateTime();
  char timeStr[10];
  char dateStr[15];
  sprintf(timeStr, "%02d:%02d", dt.time.hours, dt.time.minutes);
  sprintf(dateStr, "%02d-%02d-%04d", dt.date.date, dt.date.month, dt.date.year);

  int topPad = modY + 25;
  
  gSprite.setTextColor(UI_FG, UI_BG);
  gSprite.setTextDatum(top_left);
  gSprite.setFont(&fonts::DejaVu24);
  gSprite.drawString(timeStr, modX + 30, topPad);
  
  gSprite.setFont(&fonts::DejaVu18);
  gSprite.drawString(dateStr, modX + 30, topPad + 30);

  int bat = M5.Power.getBatteryLevel();
  char batStr[10];
  sprintf(batStr, "%d%%", bat);
  
  gSprite.setFont(&fonts::DejaVu24);
  gSprite.setTextDatum(top_right);
  gSprite.drawString(batStr, modX + modW - 65, topPad);
  
  // Draw simple battery icon next to text
  int batX = modX + modW - 63;
  int batY = topPad + 4;
  gSprite.drawRect(batX, batY, 30, 16, UI_FG);
  gSprite.fillRect(batX + 30, batY + 4, 3, 8, UI_FG);
  gSprite.fillRect(batX + 2, batY + 2, 26 * bat / 100, 12, UI_FG);
  gSprite.setTextDatum(top_left);

  // Divider
  gSprite.drawLine(modX, modY + 80, modX + modW, modY + 80, UI_BORDER);

  // Quick Action 1: Shutdown (Circle button)
  int btnR = 40;
  int btnX1 = modX + 160;
  int btnY = modY + 140;
  
  gSprite.fillCircle(btnX1, btnY, btnR, UI_ACCENT);
  gSprite.drawCircle(btnX1, btnY, btnR, UI_BORDER);
  gSprite.drawBitmap(btnX1 - 28, btnY - 28, power_material_icon, 56, 56, UI_FG);
  
  gSprite.setTextColor(UI_FG, UI_BG);
  gSprite.setFont(&fonts::DejaVu12);
  gSprite.setTextDatum(top_center);
  gSprite.drawString("Shutdown", btnX1, btnY + btnR + 15);

  // Quick Action 2: Settings / Files (Circle button)
  int btnX2 = modX + 340;
  
  gSprite.fillCircle(btnX2, btnY, btnR, UI_ACCENT);
  gSprite.drawCircle(btnX2, btnY, btnR, UI_BORDER);
  gSprite.drawBitmap(btnX2 - 28, btnY - 28, settings_material_icon, 56, 56, UI_FG);
  
  gSprite.drawString("Setting", btnX2, btnY + btnR + 15);
  gSprite.setTextDatum(top_left);

  M5.Display.startWrite();
  gSprite.pushSprite(0, 0, TFT_MAGENTA);
  M5.Display.display();
  M5.Display.endWrite();
}

void systemShutdown()
{
  setCpuFrequencyMhz(240);
  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  std::vector<String> pics;
  File root = SD.open(PIC_ROOT);
  if (root && root.isDirectory())
  {
    File entry;
    while ((entry = root.openNextFile()))
    {
      if (!entry.isDirectory())
      {
        String name = entry.name();
        if (name.endsWith(".jpg") || name.endsWith(".jpeg") ||
            name.endsWith(".JPG") || name.endsWith(".JPEG"))
        {
          pics.push_back(name);
        }
      }
      entry.close();
    }
    root.close();
  }
  bool drawn = false;
  if (!pics.empty())
  {
    int r = random(pics.size());
    String path = pics[r];
    if (!path.startsWith("/"))
      path = String(PIC_ROOT) + "/" + path;
    M5.Display.setRotation(0);
    prepareSprite(gSprite, DISPLAY_W, DISPLAY_H, 16, true);
    if (gSprite.getBuffer())
    {
      File f = SD.open(path.c_str());
      if (f)
      {
        if (drawJpgWithJpegDecFile(gSprite, f, 0, 0, DISPLAY_W, DISPLAY_H))
        {
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
  if (!drawn)
  {
    M5.Display.fillScreen(TFT_WHITE);
    M5.Display.display();
  }
  delay(2000);
  M5.Power.powerOff();
}

void drawBookConfig()
{
  forceFullMenuRedraw = true;
  M5.Display.setRotation(0);
  prepareSprite(gSprite, DISPLAY_W, DISPLAY_H, 8, true);
  if (!gSprite.getBuffer())
    return;
  gSprite.fillScreen(TFT_MAGENTA); // Use magenta as transparent color
  int modW = 460;
  int modH = (appState == STATE_TEXT_READER) ? 420 : 710;
  int modX = (DISPLAY_W - modW) / 2;
  int modY = (DISPLAY_H - modH) / 2;

  // Drop shadow
  gSprite.fillRoundRect(modX + 6, modY + 6, modW, modH, UI_RADIUS, UI_SHADOW);

  // Modal
  gSprite.fillRoundRect(modX, modY, modW, modH, UI_RADIUS, UI_BG);
  gSprite.drawRoundRect(modX, modY, modW, modH, UI_RADIUS, UI_BORDER);
  gSprite.drawRoundRect(modX + 1, modY + 1, modW - 2, modH - 2, UI_RADIUS - 1,
                        UI_BORDER);
  gSprite.drawRoundRect(modX + 2, modY + 2, modW - 4, modH - 4, UI_RADIUS - 2,
                        UI_BORDER);

  // Header
  gSprite.setTextColor(UI_FG, UI_BG);
  gSprite.setFont(&fonts::DejaVu24);
  gSprite.setTextDatum(top_center);
  gSprite.drawString("Book Menu", modX + modW / 2, modY + 25);
  gSprite.setTextDatum(top_left);
  gSprite.drawLine(modX, modY + 70, modX + modW, modY + 70, UI_BORDER);

  int barY = modY + 85;
  gSprite.drawRoundRect(modX + 20, barY, modW - 40, 150, UI_RADIUS, UI_BORDER);
  gSprite.setFont(&fonts::DejaVu24);
  gSprite.setCursor(modX + 50, barY + 25);
  gSprite.print("<<");
  gSprite.setCursor(modX + 120, barY + 25);
  gSprite.print("<");
  String pg;
  int total;
  if (appState == STATE_TEXT_READER)
  {
    total = std::max((int)textPageOffsets.size(), estimatedTotalPages);
    pg = String(bookConfigPendingPage + 1) + " / " + String(total);
  }
  else {
    total = totalPages;
    pg = String(bookConfigPendingPage + 1) + " / " + String(total);
  }
  int pgW = pg.length() * 14;
  gSprite.setCursor(modX + (modW - pgW) / 2, barY + 25);
  gSprite.print(pg);
  gSprite.setCursor(modX + modW - 132, barY + 25);
  gSprite.print(">");
  gSprite.setCursor(modX + modW - 75, barY + 25);
  gSprite.print(">>");

  // Second row: First / Last
  gSprite.drawLine(modX + 20, barY + 75, modX + modW - 20, barY + 75, UI_BORDER);
  gSprite.setFont(&fonts::DejaVu18);
  gSprite.setCursor(modX + 60, barY + 100);
  gSprite.print("First page");
  gSprite.setCursor(modX + modW - 150, barY + 100);
  gSprite.print("Last page");

  gSprite.drawLine(modX, modY + 250, modX + modW, modY + 250, UI_BORDER);

  int btnW = modW - 60;
  int btnX = modX + 30;
  int btnH = 60;

  int btnYBookmark, btnYReturn;

  if (appState != STATE_TEXT_READER)
  {
    int btnY0 = modY + 260;
    String ditherMsg = String("DITHER: ") + ditherModeName();
    drawModernButton(gSprite, btnX, btnY0, btnW, btnH, ditherMsg.c_str(), false);

    int btnY1 = modY + 330;
    String contrastMsg = String("CONTRAST: ") + contrastPresetName();
    drawModernButton(gSprite, btnX, btnY1, btnW, btnH, contrastMsg.c_str(),
                     false);

    int btnYMode = modY + 400;
    String modeMsg = "MODE: VERTICAL";
    if (orientationMode == ORIENT_LANDSCAPE) modeMsg = "MODE: HORIZONTAL";
    else if (orientationMode == ORIENT_AUTO) modeMsg = "MODE: AUTO";
    drawModernButton(gSprite, btnX, btnYMode, btnW, btnH, modeMsg.c_str(), false);

    int btnYFit = modY + 470;
    String fitMsg = String("FIT: ") + fitModeName();
    drawModernButton(gSprite, btnX, btnYFit, btnW, btnH, fitMsg.c_str(), false);

    btnYBookmark = modY + 540;
    btnYReturn = modY + 610;
  }
  else
  {
    btnYBookmark = modY + 260;
    btnYReturn = modY + 330;
  }

  drawModernButton(gSprite, btnX, btnYBookmark, btnW, btnH, "BOOKMARK PAGE", false);
  drawModernButton(gSprite, btnX, btnYReturn, btnW, btnH, "RETURN TO LIBRARY", true);

  M5.Display.startWrite();
  gSprite.pushSprite(0, 0, TFT_MAGENTA);
  M5.Display.display();
  M5.Display.endWrite();
}

void drawBookmarks()
{
  forceFullMenuRedraw = true;
  M5.Display.setRotation(0);
  prepareSprite(gSprite, DISPLAY_W, DISPLAY_H, 8, true);
  if (!gSprite.getBuffer())
    return;
  gSprite.fillScreen(UI_BG);
  gSprite.drawLine(0, 80, DISPLAY_W, 80, UI_BORDER);
  gSprite.setTextColor(UI_FG, UI_BG);
  gSprite.setFont(&fonts::DejaVu24);
  gSprite.setCursor(20, 30);
  if (selectedBookmarkFolder == "")
    gSprite.print("Bookmark Library");
  else
  {
    String t = selectedBookmarkFolder;
    if (t.length() > 19)
      t = t.substring(0, 17) + "...";
    gSprite.printf("< %s", t.c_str());
  }
  int itemsPerPage = (DISPLAY_H - 200) / 90;
  
  bool isDocument = (currentMenuTab == TAB_DOCUMENT);
  auto uniqueFolders = getUniqueBookmarkFolders(isDocument);
  
  int totalItems = 0;
  if (selectedBookmarkFolder == "") {
    totalItems = uniqueFolders.size();
  } else {
    for (const auto &b : bookmarks) {
      if (b.folder == selectedBookmarkFolder)
        totalItems++;
    }
  }

  if (totalItems == 0)
  {
    gSprite.setTextColor(UI_FG, UI_BG);
    gSprite.setFont(&fonts::DejaVu18);
    gSprite.setCursor(40, 150);
    gSprite.print("No bookmarks yet.");
  }
  else
  {
    int yOff = 100;
    int itemH = 80;
    if (selectedBookmarkFolder == "")
    {
      int count = 0;
      for (int i = 0; i < (int)uniqueFolders.size(); i++)
      {
        if (i < bookmarkScroll)
          continue;
        if (count >= itemsPerPage)
          break;
        gSprite.fillRoundRect(10 + 4, yOff + 4, DISPLAY_W - 20, itemH,
                              UI_RADIUS, UI_SHADOW);
        gSprite.fillRoundRect(10, yOff, DISPLAY_W - 20, itemH, UI_RADIUS,
                              UI_BG);
        gSprite.drawRoundRect(10, yOff, DISPLAY_W - 20, itemH, UI_RADIUS,
                              UI_BORDER);
        gSprite.setTextColor(UI_FG, UI_BG);
        gSprite.setFont(&fonts::DejaVu18);
        gSprite.setCursor(30, yOff + 30);
        String t = uniqueFolders[i];
        if (t.length() > 26)
          t = t.substring(0, 24) + "...";
        gSprite.print(t);
        gSprite.setFont(&fonts::DejaVu12);
        gSprite.setTextColor(UI_FG, UI_BG);
        gSprite.setCursor(DISPLAY_W - 80, yOff + 34);
        gSprite.print("OPEN >");
        yOff += itemH + 10;
        count++;
      }
    }
    else
    {
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
        {
          folderItemIdx++;
          continue;
        }
        gSprite.fillRoundRect(10 + 4, yOff + 4, DISPLAY_W - 20, itemH,
                              UI_RADIUS, UI_SHADOW);
        gSprite.fillRoundRect(10, yOff, DISPLAY_W - 20, itemH, UI_RADIUS,
                              UI_BG);
        gSprite.drawRoundRect(10, yOff, DISPLAY_W - 20, itemH, UI_RADIUS,
                              UI_BORDER);
        gSprite.setTextColor(UI_FG, UI_BG);
        gSprite.setFont(&fonts::DejaVu18);
        gSprite.setCursor(30, yOff + 30);
        gSprite.printf("Page %d", bookmarks[i].page + 1);

        drawModernButton(gSprite, DISPLAY_W - 100, yOff + 20, 70, 40, "DEL",
                         true);
        yOff += itemH + 10;
        count++;
        folderItemIdx++;
      }
    }
  }
  if (totalItems > 0)
  {
    int curPg = (bookmarkScroll / itemsPerPage) + 1;
    int maxPg = (totalItems + itemsPerPage - 1) / itemsPerPage;
    gSprite.setTextColor(UI_FG, UI_BG);
    gSprite.setFont(&fonts::DejaVu12);
    gSprite.setCursor(DISPLAY_W - 100, 30);
    gSprite.printf("Pg %d/%d", curPg, maxPg);
  }

  M5.Display.startWrite();
  gSprite.pushSprite(0, 0);
  M5.Display.display();
  M5.Display.endWrite();
}

void drawWifiServer()
{
  forceFullMenuRedraw = true;
  M5.Display.setRotation(0);
  if (!isWifiServerRunning())
    startWifiServer();
  prepareSprite(gSprite, DISPLAY_W, DISPLAY_H, 8, true);
  if (!gSprite.getBuffer())
    return;
  gSprite.fillScreen(UI_BG);
  gSprite.drawLine(0, 80, DISPLAY_W, 80, UI_BORDER);
  gSprite.setTextColor(UI_FG, UI_BG);
  gSprite.setFont(&fonts::DejaVu24);
  gSprite.setCursor(20, 30);
  gSprite.print("WiFi File Browser");
  gSprite.setFont(&fonts::DejaVu24);
  int y = 150;
  gSprite.setCursor(40, y);
  gSprite.print("Access Point Started");
  y += 60;
  gSprite.setFont(&fonts::DejaVu18);
  gSprite.setCursor(40, y);
  gSprite.print("Connect to:");
  y += 40;
  gSprite.setCursor(60, y);
  gSprite.print(getWifiSSID());
  y += 80;
  gSprite.setCursor(40, y);
  gSprite.print("Open in Browser:");
  y += 40;
  gSprite.setCursor(60, y);
  gSprite.printf("http://%s", getWifiIP().c_str());
  
  int btnW = 300;
  int btnH = 60;
  int btnX = (DISPLAY_W - btnW) / 2;
  int btnY = DISPLAY_H - 150;
  drawModernButton(gSprite, btnX, btnY, btnW, btnH, "STOP SERVER", true);
  M5.Display.startWrite();
  gSprite.pushSprite(0, 0);
  M5.Display.display();
  M5.Display.endWrite();
}

void drawPage()
{
  forceFullMenuRedraw = true;
  if (totalPages == 0)
  {
    drawError("No images in this manga.");
    return;
  }

  setCpuFrequencyMhz(240);

  // Set rotation based on mode
  M5.Display.setRotation(getActiveRotation());
  int screenW = M5.Display.width();
  int screenH = M5.Display.height();

  // 1. Instant turn logic: Check if requested page/strip is preloaded
  if (isNextPageReady && preloadedPage == currentPage &&
      preloadedStrip == currentStrip &&
      preloadedMangaPath == currentMangaPath)
  {
    Serial.printf("Instant turn for page %d strip %d\n", currentPage + 1, currentStrip);

    prepareSprite(gSprite, screenW, screenH, 16, true);
    nextPageSprite.pushSprite(&gSprite, 0, 0);

    isNextPageReady = false; // Used it up
  }
  else
  {
    String path = makePagePath(currentMangaPath, currentPage);
    Serial.printf("Drawing [%d/%d] Strip %d: %s\n", currentPage + 1, totalPages,
                  currentStrip, path.c_str());

    prepareSprite(gSprite, screenW, screenH, 16, true);
    if (!gSprite.getBuffer())
    {
      setCpuFrequencyMhz(80);
      drawError("Sprite alloc failed.");
      return;
    }

    File pageFile = SD.open(path.c_str());
    if (!pageFile)
    {
      setCpuFrequencyMhz(80);
      drawError("Cannot open image.");
      return;
    }

    size_t fileSize = pageFile.size();
    ensureJpgBuffer(fileSize + 1024);
    bool decodeSuccess = false;

    if (jpgBuffer)
    {
      pageFile.read(jpgBuffer, fileSize);
      pageFile.close();
      if (jpeg.openRAM(jpgBuffer, fileSize, drawMCU))
      {
        int imgW = jpeg.getWidth();
        int imgH = jpeg.getHeight();
        
        static LGFX_Sprite fullPageSprite(&M5.Display);
        prepareSprite(fullPageSprite, imgW, imgH, 16, true);
        
        JpegDrawContext ctx = {&fullPageSprite, 0, 0, imgW, imgH};
        jpeg.setPixelType(RGB565_BIG_ENDIAN);
        jpeg.setUserPointer(&ctx);
        decodeSuccess = jpeg.decode(0, 0, 0);
        jpeg.close();

        if (decodeSuccess)
        {
          gSprite.fillScreen(TFT_WHITE);

          float scale = 1.0f;
          FitMode effectiveFitMode = fitMode;

          if (effectiveFitMode == FIT_SMART)
          {
            float imgAspect = (float)imgW / (float)imgH;
            float screenAspect = (float)screenW / (float)screenH;
            // If image is significantly wider than screen aspect, or very tall, use FIT_WIDTH
            if (imgAspect > screenAspect * 1.1f || imgH > imgW * 1.5f)
              effectiveFitMode = FIT_WIDTH;
            else
              effectiveFitMode = FIT_SCREEN;
          }

          if (effectiveFitMode == FIT_SCREEN)
          {
            stripsPerPage = 1;
            currentStrip = 0;
            scale = std::min((float)screenW / imgW, (float)screenH / imgH);
            int outW = (int)(imgW * scale);
            int outH = (int)(imgH * scale);
            int offsetX = (screenW - outW) / 2;
            int offsetY = (screenH - outH) / 2;
            fullPageSprite.setPivot(imgW / 2, imgH / 2);
            fullPageSprite.pushRotateZoom(&gSprite, offsetX + outW / 2, offsetY + outH / 2, 0, scale, scale);
          }
          else if (effectiveFitMode == FIT_WIDTH)
          {
            scale = (float)screenW / imgW;
            int visibleImgH = (int)(screenH / scale);
            int overlapImg = (int)(stripOverlapPx / scale);
            if (overlapImg >= visibleImgH)
              overlapImg = visibleImgH / 2;

            int stepImg = visibleImgH - overlapImg;
            stripsPerPage = (imgH - overlapImg + stepImg - 1) / stepImg;
            if (stripsPerPage < 1)
              stripsPerPage = 1;

            if (currentStrip == -1)
              currentStrip = stripsPerPage - 1;
            if (currentStrip >= stripsPerPage)
              currentStrip = stripsPerPage - 1;

            int yStart = currentStrip * stepImg;
            if (yStart + visibleImgH > imgH)
              yStart = imgH - visibleImgH;
            if (yStart < 0)
              yStart = 0;

            fullPageSprite.setPivot(0, yStart);
            fullPageSprite.pushRotateZoom(&gSprite, 0, 0, 0, scale, scale);
          }
          else if (effectiveFitMode == FIT_HEIGHT)
          {
            scale = (float)screenH / imgH;
            int visibleImgW = (int)(screenW / scale);
            int overlapImg = (int)(stripOverlapPx / scale);
            if (overlapImg >= visibleImgW)
              overlapImg = visibleImgW / 2;

            int stepImg = visibleImgW - overlapImg;
            stripsPerPage = (imgW - overlapImg + stepImg - 1) / stepImg;
            if (stripsPerPage < 1)
              stripsPerPage = 1;

            if (currentStrip == -1)
              currentStrip = stripsPerPage - 1;
            if (currentStrip >= stripsPerPage)
              currentStrip = stripsPerPage - 1;

            int xStart = currentStrip * stepImg;
            if (xStart + visibleImgW > imgW)
              xStart = imgW - visibleImgW;
            if (xStart < 0)
              xStart = 0;

            fullPageSprite.setPivot(xStart, 0);
            fullPageSprite.pushRotateZoom(&gSprite, 0, 0, 0, scale, scale);
          }
        }
        fullPageSprite.deleteSprite();
      }
    }
    else
    {
      if (currentStrip == -1) currentStrip = 0; // Fallback
      // Fallback if jpgBuffer fails (rare)
      decodeSuccess = drawJpgWithJpegDecFile(gSprite, pageFile, 0, 0, screenW, screenH);
      pageFile.close();
    }

    if (!decodeSuccess)
    {
      setCpuFrequencyMhz(80);
      drawError("Cannot decode image.");
      return;
    }
    applyContrast(gSprite);
    if (ditherMode != DITHER_OFF)
    {
      applyDithering(gSprite);
    }
  }

  M5.Display.startWrite();
  gSprite.pushSprite(0, 0);
  M5.Display.display();
  M5.Display.endWrite();

  // 2. Preload NEXT content in background
  int nextPg = currentPage;
  int nextStrip = currentStrip + 1;
  if (stripsPerPage > 1)
  {
    if (nextStrip >= stripsPerPage)
    {
      nextPg++;
      nextStrip = 0;
    }
  }
  else
  {
    nextPg++;
    nextStrip = 0;
  }

  if (nextPg < totalPages)
  {
    preloadPage(nextPg, nextStrip);
  }

  setCpuFrequencyMhz(80);
}

void drawError(const char *msg)
{
  forceFullMenuRedraw = true;
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.setFont(&fonts::DejaVu18);
  M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
  M5.Display.setCursor(20, 40);
  M5.Display.println(msg);
  M5.Display.display();
}



// ── Buffered Text Reader ─────────────────────────────────────────────
// Reads from a PSRAM buffer instead of per-byte SD I/O.
// Supports virtual "position" tracking so word-break / seek logic stays identical.

#define TEXT_BUF_SIZE 16384  // 16 KB chunk

struct BufferedReader {
  File* file;
  uint8_t* buf;
  uint32_t bufStart;   // file offset of buf[0]
  int bufLen;          // valid bytes in buf
  uint32_t pos;        // current virtual file position
  uint32_t fileSize;

  BufferedReader() : file(nullptr), buf(nullptr), bufStart(0), bufLen(0), pos(0), fileSize(0) {}
  ~BufferedReader() { end(); }

  bool begin(File& f) {
    file = &f;
    fileSize = f.size();
    buf = (uint8_t*)heap_caps_malloc(TEXT_BUF_SIZE, MALLOC_CAP_SPIRAM);
    if (!buf) return false;
    bufStart = 0;
    bufLen = 0;
    pos = 0;
    return true;
  }

  void end() {
    if (buf) { heap_caps_free(buf); buf = nullptr; }
  }

  void fillBuffer() {
    if (!file || !buf) return;
    file->seek(pos);
    bufStart = pos;
    bufLen = file->read(buf, TEXT_BUF_SIZE);
  }

  // Ensure current pos is within the buffer
  inline void ensureBuffered() {
    if (pos < bufStart || pos >= bufStart + (uint32_t)bufLen) {
      fillBuffer();
    }
  }

  bool available() { return pos < fileSize; }

  uint32_t position() { return pos; }

  void seek(uint32_t p) { pos = p; }

  char read() {
    ensureBuffered();
    if (bufLen <= 0) return 0;
    return (char)buf[pos++ - bufStart];
  }

  char peek() {
    ensureBuffered();
    if (bufLen <= 0) return 0;
    return (char)buf[pos - bufStart];
  }
};

// ── Text Page Index Persistence ──────────────────────────────────────
// Format: [4B magic][4B fileSize][4B count][count × 4B offsets]

static const uint32_t TEXT_IDX_MAGIC = 0x54585449; // "TXTI"

static String makeIdxPath(const String& bookPath) {
  // Replace .txt/.TXT with .idx
  String idx = bookPath;
  int dot = idx.lastIndexOf('.');
  if (dot >= 0) idx = idx.substring(0, dot);
  idx += ".idx";
  return idx;
}

static bool loadTextIndex(const String& bookPath) {
  String idxPath = makeIdxPath(bookPath);
  File f = SD.open(idxPath.c_str(), FILE_READ);
  if (!f) return false;

  uint32_t magic, savedSize, count;
  if (f.read((uint8_t*)&magic, 4) != 4 || magic != TEXT_IDX_MAGIC) { f.close(); return false; }
  if (f.read((uint8_t*)&savedSize, 4) != 4 || savedSize != textFileSize) { f.close(); return false; }
  if (f.read((uint8_t*)&count, 4) != 4 || count == 0 || count > 100000) { f.close(); return false; }

  textPageOffsets.clear();
  textPageOffsets.resize(count);
  size_t bytesNeeded = count * 4;
  if (f.read((uint8_t*)textPageOffsets.data(), bytesNeeded) != bytesNeeded) {
    textPageOffsets.clear();
    textPageOffsets.push_back(0);
    f.close();
    return false;
  }
  f.close();

  // Sanity: first offset must be 0
  if (textPageOffsets[0] != 0) {
    textPageOffsets.clear();
    textPageOffsets.push_back(0);
    return false;
  }

  Serial.printf("Loaded text index: %d pages from %s\n", (int)count, idxPath.c_str());
  estimatedTotalPages = std::max((int)count, estimatedTotalPages);
  return true;
}

static void saveTextIndex(const String& bookPath) {
  String idxPath = makeIdxPath(bookPath);
  File f = SD.open(idxPath.c_str(), FILE_WRITE);
  if (!f) { Serial.println("Failed to save text index"); return; }

  uint32_t magic = TEXT_IDX_MAGIC;
  uint32_t count = textPageOffsets.size();
  f.write((uint8_t*)&magic, 4);
  f.write((uint8_t*)&textFileSize, 4);
  f.write((uint8_t*)&count, 4);
  f.write((uint8_t*)textPageOffsets.data(), count * 4);
  f.close();
  Serial.printf("Saved text index: %d pages to %s\n", (int)count, idxPath.c_str());
}

void drawTextPage()
{
  M5.Display.setRotation(0);
  prepareSprite(gSprite, DISPLAY_W, DISPLAY_H, 16, true);
  if (!gSprite.getBuffer())
    return;

  gSprite.fillScreen(TFT_WHITE);
  gSprite.setTextColor(TFT_BLACK);
  gSprite.setTextWrap(false);

  bool customFontLoaded = false;
  Serial.println("Applying embedded Literata font...");
  customFontLoaded = gSprite.loadFont(literata_vlw);
  if (customFontLoaded)
    Serial.println("SUCCESS: Literata font applied");
  else
    Serial.println("ERROR: Failed to apply embedded font");

  if (!customFontLoaded)
  {
    gSprite.setFont(&fonts::DejaVu18);
  }

  File f = SD.open(currentBookPath.c_str());
  if (!f)
  {
    Serial.printf("ERROR: Failed to open book: %s\n", currentBookPath.c_str());
    drawError("Failed to open book");
    return;
  }

  int leftMargin = 20;
  int rightMargin = 20;
  int topMargin = 10;
  int bottomMargin = 20;
  int lineH = TEXT_LINE_HEIGHT;

  uint32_t offset = 0;

  // discovery loop: if we want a page we haven't mapped yet, scan forward
  // Uses buffered reader for 10-20x faster scanning vs per-byte SD reads
  if (currentTextPage >= (int)textPageOffsets.size())
  {
    setCpuFrequencyMhz(240);
    int maxW = DISPLAY_W - leftMargin - rightMargin;
    int safetyBuffer = 8;
    int usableMaxW = maxW - safetyBuffer;

    BufferedReader br;
    bool buffered = br.begin(f);

    while (currentTextPage >= (int)textPageOffsets.size())
    {
      uint32_t pageStart = textPageOffsets.back();
      if (pageStart >= textFileSize) break;

      if (buffered) {
        br.seek(pageStart);
      } else {
        f.seek(pageStart);
      }
      int y = topMargin;
      bool isNewPara = (pageStart == 0);
      if (pageStart > 0)
      {
        if (buffered) {
          br.seek(pageStart - 1);
          char prev = br.read();
          if (prev == '\n' || prev == '\r') isNewPara = true;
          br.seek(pageStart);
        } else {
          f.seek(pageStart - 1);
          char prev = f.read();
          if (prev == '\n' || prev == '\r') isNewPara = true;
          f.seek(pageStart);
        }
      }

      // Macro-style lambdas for buffered/unbuffered access
      #define BR_AVAILABLE() (buffered ? br.available() : (bool)f.available())
      #define BR_READ()      (buffered ? br.read() : (char)f.read())
      #define BR_PEEK()      (buffered ? br.peek() : (char)f.peek())
      #define BR_POS()       (buffered ? br.position() : (uint32_t)f.position())
      #define BR_SEEK(p)     do { if (buffered) br.seek(p); else f.seek(p); } while(0)

      while (BR_AVAILABLE() && (y + lineH < DISPLAY_H - bottomMargin))
      {
        int indent = isNewPara ? 40 : 0;
        int effectiveMaxW = usableMaxW - indent;
        bool isParaEnd = false;
        
        // This must match the draw loop's word fitting logic exactly
        String testLine = "";
        bool hasWords = false;

        while (BR_AVAILABLE())
        {
          uint32_t wordStartPos = BR_POS();
          String word = "";
          bool hitNewline = false;
          bool hitSpace = false;
          
          while (BR_AVAILABLE())
          {
            char c = BR_READ();
            if (c == '\r') continue;
            if (c == '\n') { 
              hitNewline = true; 
              while (BR_AVAILABLE()) { 
                char next = BR_PEEK(); 
                if (next == '\n' || next == '\r') BR_READ(); 
                else break; 
              } 
              break; 
            }
            if (c == ' ') { hitSpace = true; break; }
            word += c;
          }
          
          if (word.length() == 0 && !hitNewline && !hitSpace) break;
          if (word.length() == 0 && hitSpace) continue;

          if (gSprite.textWidth(testLine + word) > effectiveMaxW)
          {
            if (hasWords) {
              BR_SEEK(wordStartPos);
              break;
            } else {
              // Word break logic for single long word
              BR_SEEK(wordStartPos);
              word = "";
              while (BR_AVAILABLE()) {
                uint32_t charStart = BR_POS();
                unsigned char c = BR_PEEK();
                if (c == ' ' || c == '\n' || c == '\r') break;
                
                int charLen = 1;
                if ((c & 0x80) != 0) {
                  if ((c & 0xE0) == 0xC0) charLen = 2;
                  else if ((c & 0xF0) == 0xE0) charLen = 3;
                  else if ((c & 0xF0) == 0xF0) charLen = 4;
                }
                
                String nextChar = "";
                for (int j = 0; j < charLen && BR_AVAILABLE(); j++) nextChar += (char)BR_READ();
                
                if (gSprite.textWidth(word + nextChar) > effectiveMaxW) {
                  if (word.length() == 0) {
                    word = nextChar; // Take at least one char to avoid infinite loop
                  } else {
                    BR_SEEK(charStart);
                  }
                  break;
                }
                word += nextChar;
              }
              testLine += word + " ";
              hasWords = true;
              isParaEnd = false;
              break;
            }
          }
          
          testLine += word + " ";
          hasWords = true;
          
          if (hitNewline) { isParaEnd = true; break; }
        }
        y += lineH;
        isNewPara = isParaEnd;
      }
      
      uint32_t nextPgStart = BR_POS();
      if (nextPgStart < textFileSize) {
        textPageOffsets.push_back(nextPgStart);
      } else {
        break; // Reached end of file
      }
      
      // Smart Math: Refine total page estimate
      if (textPageOffsets.size() >= 2) {
          float bytesConsumed = (float)nextPgStart;
          int pagesScanned = (int)textPageOffsets.size() - 1;
          if (pagesScanned > 0) {
              float avg = bytesConsumed / pagesScanned;
              estimatedTotalPages = (int)(textFileSize / avg);
          }
      }

      #undef BR_AVAILABLE
      #undef BR_READ
      #undef BR_PEEK
      #undef BR_POS
      #undef BR_SEEK
    }

    br.end();

    if (currentTextPage >= (int)textPageOffsets.size())
      currentTextPage = textPageOffsets.size() - 1;

    // Persist the discovered offsets for next time
    saveTextIndex(currentBookPath);
    setCpuFrequencyMhz(80);
  }

  if (currentTextPage < (int)textPageOffsets.size())
  {
    offset = textPageOffsets[currentTextPage];
  }
  f.seek(offset);

  int y = topMargin;
  int x = leftMargin;
  int maxW = DISPLAY_W - leftMargin - rightMargin;
  
  gSprite.setCursor(x, y);
  gSprite.setTextDatum(top_left);

  bool isNewParagraph = (offset == 0);
  if (offset > 0)
  {
    // Check if the previous character was a newline to determine if this is a new paragraph
    f.seek(offset - 1);
    char prev = f.read();
    if (prev == '\n' || prev == '\r')
      isNewParagraph = true;
    f.seek(offset); // Seek back to the page start
  }

  // Leave a small safety buffer for font rendering variations
  int safetyBuffer = 8;
  int usableMaxW = maxW - safetyBuffer;

  while (f.available() && (y + lineH < DISPLAY_H - bottomMargin))
  {
    std::vector<String> words;
    bool isParagraphEnd = false;

    int indent = isNewParagraph ? 40 : 0;
    int effectiveMaxW = usableMaxW - indent;

    while (f.available())
    {
      uint32_t wordStartPos = f.position();
      String word = "";
      bool hitNewline = false;
      bool hitSpace = false;

      while (f.available())
      {
        char c = f.read();
        if (c == '\r') continue;
        if (c == '\n') 
        { 
          hitNewline = true; 
          while (f.available()) {
            char next = f.peek();
            if (next == '\n' || next == '\r') f.read();
            else break;
          }
          break; 
        }
        if (c == ' ') {
          hitSpace = true;
          break;
        }
        word += c;
      }

      if (word.length() == 0 && !hitNewline && !hitSpace) break; 
      
      if (word.length() == 0 && hitSpace) continue; // Skip empty words from multiple spaces

      // Check if this word fits
      String testLine = "";
      for (const auto& w : words) testLine += w + " ";
      
      if (gSprite.textWidth(testLine + word) > effectiveMaxW)
      {
        if (words.empty()) {
          // Single word too long. Need to break it.
          f.seek(wordStartPos);
          word = "";
          while (f.available()) {
            uint32_t charStartPos = f.position();
            unsigned char c = f.peek();
            
            // Check if it's a breakable character
            if (c == ' ' || c == '\n' || c == '\r') break;
            
            int charLen = 1;
            if ((c & 0x80) != 0) {
              if ((c & 0xE0) == 0xC0) charLen = 2;
              else if ((c & 0xF0) == 0xE0) charLen = 3;
              else if ((c & 0xF0) == 0xF0) charLen = 4;
            }
            
            String nextChar = "";
            for (int j = 0; j < charLen && f.available(); j++) {
              nextChar += (char)f.read();
            }
            
            if (gSprite.textWidth(word + nextChar) > effectiveMaxW) {
              if (word.length() == 0) {
                word = nextChar; // Take at least one char to avoid infinite loop
              } else {
                f.seek(charStartPos);
              }
              break;
            }
            word += nextChar;
          }
          words.push_back(word);
          isParagraphEnd = false; // We broke a word, definitely not end of paragraph
        } else {
          // Word doesn't fit, back up to before this word
          f.seek(wordStartPos);
        }
        break; 
      }

      words.push_back(word);
      if (hitNewline) {
        isParagraphEnd = true;
        break;
      }
    }

    if (words.empty()) {
      if (isParagraphEnd) isNewParagraph = true;
      y += lineH;
      continue;
    }

    // Draw the line
    int startX = x + indent;
    if (isParagraphEnd || words.size() == 1)
    {
      String line = "";
      for (size_t i = 0; i < words.size(); ++i) {
        line += words[i] + (i == words.size() - 1 ? "" : " ");
      }
      gSprite.drawString(line, startX, y);
    }
    else
    {
      int totalWordsWidth = 0;
      for (const auto& w : words) totalWordsWidth += gSprite.textWidth(w);
      
      float gapWidth = (float)(effectiveMaxW - totalWordsWidth) / (words.size() - 1);
      float currentXPos = startX;
      for (size_t i = 0; i < words.size(); ++i)
      {
        gSprite.drawString(words[i], (int)currentXPos, y);
        currentXPos += gSprite.textWidth(words[i]) + gapWidth;
      }
    }
    
    y += lineH;
    isNewParagraph = isParagraphEnd;
  }

  uint32_t nextOffset = f.position();
  
  // Record next page offset if not already known
  if (currentTextPage + 1 == (int)textPageOffsets.size() && nextOffset < (uint32_t)f.size())
  {
    textPageOffsets.push_back(nextOffset);
    // Persist updated index incrementally
    saveTextIndex(currentBookPath);
  }
  f.close();

  // Draw footer
  gSprite.setFont(&fonts::DejaVu12);
  gSprite.setTextDatum(top_center);
  gSprite.drawString(String(currentTextPage + 1), DISPLAY_W / 2, DISPLAY_H - 30);
  gSprite.setTextDatum(top_left);

  M5.Display.startWrite();
  gSprite.pushSprite(0, 0);
  M5.Display.display();
  M5.Display.endWrite();
}

void openBook(int idx)
{
  if (idx < 0 || idx >= (int)bookFiles.size())
    return;
  String path = String(BOOK_ROOT) + "/" + bookFiles[idx];
  // Load per-book saved progress
  int savedPage = loadBookProgress(path);
  openBookPath(path, savedPage);
}

void openBookPath(const String &path, int page)
{
  currentBookPath = path;
  currentTextPage = page;
  textPageOffsets.clear();
  textPageOffsets.push_back(0);

  File f = SD.open(path.c_str());
  if (f)
  {
    textFileSize = f.size();
    // Start with a conservative estimate: ~1500 bytes per page
    estimatedTotalPages = std::max(1, (int)(textFileSize / 1500));
    f.close();
  }

  // Try to load cached page index for instant bookmark jumps
  if (loadTextIndex(path)) {
    Serial.printf("Text index loaded: %d pages cached, jumping to page %d\n",
                  (int)textPageOffsets.size(), page);
    if (currentTextPage >= (int)textPageOffsets.size()) 
      currentTextPage = (int)textPageOffsets.size() - 1;
  }
  if (currentTextPage < 0) currentTextPage = 0;

  appState = STATE_TEXT_READER;
  currentEpdMode = epd_mode_t::epd_text;
  needRedraw = true;
  saveProgress();
}

void quickScreenRefresh() // Unused, leave here for future use
{
  M5.Display.startWrite();
  M5.Display.clear(TFT_BLACK);
  M5.Display.display();
  M5.Display.endWrite();
}
