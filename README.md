# Photo Triage

A super-fast desktop app for triaging photos — **keep / reject / undo** — with a slick, keyboard-first workflow and RAW support.

![Screenshot](https://github.com/user-attachments/assets/ad8601f4-ba31-44c7-924e-6f8f677e8ac4)

---

## 🚀 New Features

* **File Browser**
  Added a file browser pane that displays image files from the selected folder. You can click around and **scrub between photos** without losing your place.

* **RAW Image Support**
  You can now triage RAW photos alongside your JPG/PNG images while keeping the same super-quick folder navigation. *(Note: RAW decoding availability may depend on your platform/Qt plugins.)*

---

## 🎨 UI/UX Enhancements

* **UI Colours**
  Updated the theme to be easier on the eyes. Added colour accents to the **keep**, **reject**, and **undo** buttons for at-a-glance clarity.

* **Arrow-key Navigation**
  Zip through the folder with **← / →**. You’re no longer locked into deciding on just the current photo.

---

## ⚡️ Performance & Stability

* **Asynchronous Thumbnail Loading**
  The file list appears instantly with lightweight placeholders; **thumbnails load asynchronously** to keep the UI responsive.

* **Optimized Image Pipeline**
  The lightning-fast image pipeline now uses a **symmetric sliding-window cache** around the current index so navigation stays snappy.

* **Thumbnail Queue**
  A **path-based thumbnail queue** with a small **concurrency gap** prioritizes relevant images and avoids stalls during heavy I/O.

* **Better Thread Management**
  Background tasks use **queued connections** and clean up their threads properly on completion, improving stability and resource usage.

---

## Basic Usage

When you launch the app, select a **source directory**. The program scans the folder for images and shows them one at a time. You can:

* **Keep** the current image — press **Z** or click **Keep**.
  The file moves to a sibling folder named `keep`. If a file with the same name exists, a numerical suffix is appended (e.g., `image.jpg` → `image_1.jpg`).

* **Reject** the current image — press **X** or click **Reject**.
  The file moves to a sibling folder named `discard` (same suffix behaviour).

* **Undo** the last action — press **U** or **Ctrl+Z** or click **Undo**.
  The most recent move is reversed; the file returns to its original location and position.

* **Open** a new source directory — press **O** and choose another folder.

The status bar shows the current index, total images, and filename. Images are scaled to fit while preserving aspect ratio.

### Keyboard Shortcuts

|        Key | Action                |
| ---------: | --------------------- |
|      **Z** | Keep                  |
|      **X** | Reject                |
|      **U** | Undo                  |
| **Ctrl+Z** | Undo                  |
|  **← / →** | Previous / Next image |
|      **O** | Open folder           |

---

## Supported Formats

* **Raster:** JPG, PNG, BMP, GIF, TIF/TIFF, WEBP, AVIF
* **RAW:** Common RAW image files (triage support; decoding depends on your platform/Qt setup)

> If a specific RAW from your camera doesn’t decode, it’s typically a codec/plugin issue rather than the triage workflow itself.

---

## Building the Application

Photo-Triage is built with **Qt** (tested with Qt 6; should also work with Qt 5). A `CMakeLists.txt` is provided.

**Requirements**

* A **C++17-compliant** compiler (e.g., GCC 10+, Clang 10+, MSVC 2019+)
* **Qt** development libraries (**Qt 6 recommended**)
* **CMake 3.15+**

**Configure & Build (Unix-like)**

```sh
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

**Windows Notes**

You may need to pass `-DCMAKE_PREFIX_PATH` to point CMake at your Qt installation. Refer to Qt’s docs for details.

After a successful build, the executable is placed in the **build** directory. On first run you’ll be prompted to select a folder of images.

---

## Implementation Notes

* **Natural Sorting**
  Files appear in a human-friendly order so `image2.jpg` comes before `image10.jpg`.

* **Dynamic Caching**
  Images are loaded on a worker thread and cached by index. The cache prevents redundant work if you advance quickly.

* **Undo Stack**
  Up to **20** move operations are retained. Undo restores both the file and your browsing position.

---

## Known Limitations

* **RAW Coverage Varies**
  RAW compatibility can differ by camera model/codec and your Qt/plugin setup.

* **Very Large Folders**
  Initial scanning of huge directories can take a moment before the first render, though navigation remains responsive thereafter.

* **Animated Images**
  Animated formats (e.g., GIF) are treated as single frames during triage.

---

## Why This Exists

I wanted a tool that lets me **skate around** a folder of photos at high speed, make confident keep/reject calls, and get out of the way. That’s the whole point: tight feedback, instant actions, and a workflow that feels effortless.

I pretty much couldn't find something that fit exactly my use case, I got annoyed, so I made it myself lol. Unironically, I use this app all the time.
