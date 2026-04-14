# M5PaperS3 Manga Reader

A JPEG gallery/manga reader for the **M5PaperS3** e-ink device, built with
**PlatformIO** and the **M5Unified** + **M5GFX** libraries.

---

## Project Structure

```
manga_reader/
├── platformio.ini
└── src/
    └── main.cpp
```

---

## SD Card Layout

Format your SD card as **FAT32**. Create the following structure:

```
/manga/
  MyManga1/
    001.jpg
    002.jpg
    003.jpg
    ...
  AnotherSeries/
    001.jpg
    002.jpg
    ...
```

- Only **sub-folders** of `/manga` are recognised as manga titles.
- Only `.jpg` / `.jpeg` files inside each sub-folder are shown as pages.
- Files are sorted **alphabetically**, so zero-padding (001, 002 …) is recommended.

> **Tip:** The M5PaperS3 display runs in **portrait mode: 540 × 960 px**.
> Pre-scale your manga pages to 540 × 960 for a perfect full-screen fit with
> no letterboxing. A 30 px black status bar is reserved at the top for the
> page counter, so the usable image area is 540 × 930.

---

## Controls

| Gesture | Action |
|---|---|
| **Tap right half** of screen | Next page |
| **Tap left half** of screen | Previous page |
| **Swipe up** (≥ 60 px upward) | Return to manga list menu |
| **Swipe up** on menu | Refresh folder list |
| **Swipe down** on menu | Scroll selection up |
| **Tap a row** on menu | Select / open manga |

---

## Dependencies

| Library | Version |
|---|---|
| `m5stack/M5Unified` | ≥ 0.2.5 (tested 0.2.13) |
| `m5stack/M5GFX` | ≥ 0.2.7 |

Both are declared in `platformio.ini` and fetched automatically.

---

## Building & Flashing

1. Install [PlatformIO IDE](https://platformio.org/) (VS Code extension) or
   use the CLI.
2. Open the `manga_reader/` folder as a PlatformIO project.
3. **Enter download mode** on the M5PaperS3: long-press the power button until
   the back LED blinks red.
4. Connect via USB-C and run:

```bash
pio run --target upload
```

or click **Upload** in VS Code.

---

## Notes

- **PSRAM** is mandatory. The `platformio.ini` sets `qio_opi` memory mode and
  `BOARD_HAS_PSRAM`, matching the M5PaperS3 hardware requirement.
- `drawJpgFile` decodes JPEG in PSRAM, so large images are handled without
  running out of heap.
- The e-ink display uses `epd_quality` mode for page images (crisp, slower
  refresh) and `epd_fast` / `epd_text` mode for the UI overlays.
- If your images are taller than wide (portrait manga pages), set the manga
  images to 540 × 960 and rotate the display to `setRotation(0)` in `main.cpp`.
