# M5Manga
### A fun vibe-coded project for reading manga using M5PaperS3

### Some image:

<table>
  <tr>
    <td><img width="300" src="https://github.com/user-attachments/assets/e66626ce-f0b4-44ee-ba6c-09bd6ad23f59" /></td>
    <td><img width="300" src="https://github.com/user-attachments/assets/b0349270-2c16-4a3b-965a-c684c1fa5909" /></td>
    <td><img width="300" src="https://github.com/user-attachments/assets/f5f9a39d-05a0-475d-b03e-70a599b6549f" /></td>
  </tr>
</table>

### Perfect for reading BL btw

#

# AI generated readme:

## M5PaperS3 Manga Reader

A high-performance, commercial-grade manga reader for the **M5PaperS3** e-ink device. Featuring an optimized 8-level grayscale rendering stack, dithering algorithms, high-contrast modern UI, persistent bookmarks, and a built-in WiFi file manager. Built with **PlatformIO**, utilizing **M5GFX** for fast partial updates and **JPEGDEC** for rapid image decoding.

---

## Project Structure

```
M5Manga/
├── platformio.ini      # Build configuration & dependencies
└── src/
    ├── main.cpp        # Entry point & main loop
    ├── config.h        # Hardware, UI constants, & color palettes
    ├── state.h/cpp     # Global application state & enums
    ├── storage.h/cpp   # SD card, file search, & JSON bookmark storage
    ├── input.h/cpp     # Touch gesture handling & interaction feedback
    ├── ui.h/cpp        # Modern UI rendering (menus, modals, components)
    ├── wifi_server.cpp # Asynchronous web server for file management
    ├── bookmarks.cpp   # Bookmark persistence logic
    └── navigation.h    # Navigation helpers
```

---

## SD Card Layout

Format your SD card as **FAT32**. Create the following structure:

```
/manga/
  MySeries1/
    m5_0000.jpg
    m5_0001.jpg
    ...
  AnotherManga/
    m5_0000.jpg
    m5_0001.jpg
    ...
```

- **Root folder**: Must be named `/manga`.
- **Sub-folders**: Each sub-folder is treated as a separate manga title.
- **Filenames**: Images must follow the pattern `m5_XXXX.jpg` (e.g., `m5_0000.jpg`, `m5_0001.jpg`).
- **Resolution**: The display is **540 × 960 px** (Portrait). For best performance and full-screen fit, pre-scale your images to these dimensions.

---

## Controls

### Library Menu
| Action | Result |
|---|---|
| **Tap Thumbnail** | Open manga title |
| **Swipe Up** | Refresh / Jump to top |
| **Swipe Down** | Next page |
| **Swipe Left/Right** | Previous/Next page |
| **Tap "CONTINUE" Bar** | Quick resume last read manga |
| **Tap "BOOKMARKS" Pill** | Open bookmarks library |
| **Tap "FILES" Pill** | Start WiFi file browser |

### Manga Reader
| Action | Result |
|---|---|
| **Tap Right/Left Half** | Next/Previous page |
| **Swipe Down** | Open **Control Center** (Battery, Shutdown) |
| **Swipe Up** | Open **Book Menu** (Page jump, Display settings, Bookmarking) |

### Bookmarks View
| Action | Result |
|---|---|
| **Tap Folder/Page** | Open specific bookmark |
| **Tap "DEL"** | Delete bookmark |
| **Swipe Up** | Go back |

---

## Features

- **Ultra-Fast Rendering**: Uses `M5GFX` sprites for direct display buffer writing and partial e-ink updates.
- **Hardware Decoding**: Utilizes `JPEGDEC` to directly stream and map 8-bit grayscale pixels.
- **Advanced Display Controls**: Toggle between multiple dithering modes (Floyd-Steinberg, Atkinson, Ordered) and contrast presets for optimal image clarity on the e-ink screen.
- **Modern High-Contrast UI**: Beautiful, commercial-grade aesthetic with 8px rounded corners, dynamic touch feedback, and 4px drop-shadow modals.
- **WiFi File Server**: Built-in async web server allows you to upload, delete, and organize manga zip/folders from any browser without removing the SD card.
- **Persistent Bookmarks**: JSON-backed bookmarking system (`ArduinoJson`) to save and resume reading positions seamlessly.
- **Smart Resume**: "Continue Reading" pill bar remembers your exact location across reboots.

---

## Technical Requirements

- **Hardware**: M5PaperS3 (ESP32-S3).
- **PSRAM**: Mandatory (configured in `platformio.ini`).
- **Libraries**:
    - `m5stack/M5Unified` (Power & Touch management)
    - `m5stack/M5GFX` (Display rendering)
    - `bitbank2/JPEGDEC` (Image decoding)
    - `bblanchon/ArduinoJson` (Settings & bookmarks storage)
    - `ESPAsyncWebServer` & `AsyncTCP` (WiFi file management)

---

## Building & Flashing

1. Install [PlatformIO IDE](https://platformio.org/) (VS Code extension).
2. Open the project folder.
3. **Enter Download Mode**: Long-press the power button until the back LED blinks red.
4. Click **Upload** in VS Code or run:
```bash
pio run --target upload
```

---

## Notes

- If your images are not showing, verify the `m5_` prefix and 4-digit zero-padding.
- The reader uses 16-bit color depth sprites in PSRAM for JPEG decoding to ensure high-quality grayscale rendering on the e-ink panel.
