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

A high-performance JPEG manga reader for the **M5PaperS3** e-ink device, featuring a grid-based library, bookmarking system, and smooth navigation. Built with **PlatformIO** and the **M5Unified** + **M5GFX** libraries.

---

## Project Structure

```
M5Manga/
├── platformio.ini      # Build configuration & dependencies
└── src/
    ├── main.cpp        # Entry point & main loop
    ├── config.h        # Hardware & UI constants
    ├── state.h/cpp     # Global application state
    ├── storage.h/cpp   # SD card, file search, & progress saving
    ├── input.h/cpp     # Touch gesture handling
    ├── ui.h/cpp        # Rendering logic for menu, reader, & overlays
    ├── bookmarks.h/cpp # Bookmark management
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
| **Tap Item** | Select / Open manga |
| **Swipe Up** | Refresh folder list |
| **Swipe Down** | Scroll to next page of titles |
| **Tap Bottom Bar** | Quick resume last read manga |
| **Tap "★ Bookmarks"** | Open bookmarks library |

### Manga Reader
| Action | Result |
|---|---|
| **Tap Right Half** | Next page |
| **Tap Left Half** | Previous page |
| **Swipe Down (Top)** | Open **System Menu** (Shutdown, Battery) |
| **Swipe Up (Bottom)** | Open **Book Menu** (Page jump, Bookmark, Exit) |

### Bookmarks View
| Action | Result |
|---|---|
| **Tap Folder/Page** | Open specific bookmark |
| **Tap "DEL"** | Delete bookmark |
| **Swipe Up** | Go back |

---

## Features

- **Full-Screen Reading**: Optimized 540x960 rendering with no UI overlays during reading.
- **Smart Resume**: Remembers your last read manga and page automatically.
- **Binary Search Loading**: Quickly indexes thousands of pages in seconds.
- **Battery Management**: Real-time battery level in System Menu.
- **Performance**: Uses `epd_quality` for crisp manga pages and `epd_fast` for responsive UI menus.
- **Power Efficiency**: Dynamic CPU frequency scaling (80MHz idle / 240MHz loading).

---

## Technical Requirements

- **Hardware**: M5PaperS3 (ESP32-S3).
- **PSRAM**: Mandatory (configured in `platformio.ini`).
- **Libraries**:
    - `m5stack/M5Unified` (tested 0.2.13)
    - `m5stack/M5GFX` (tested 0.2.7)

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
