Photo‑Triage (C++ Port)
=======================

This directory contains a C++ implementation of a lightweight photo triage
utility inspired by the original Python program.  The goal of this port is to
retain the fast and responsive workflow of the original while taking
advantage of C++'s performance characteristics and Qt's native widget
toolkit.

The current version focuses on the core **keep/reject** workflow.  Features
such as star ratings and mobile front‑ends can be layered on later.

## Building the application

The application is built using Qt (tested with Qt 6, but should also work
with Qt 5).  A `CMakeLists.txt` is provided to simplify building on
platforms where CMake is available.  To build the program you will need

* A C++17‑compliant compiler (e.g. GCC 10+, Clang 10+, MSVC 2019+).
* Qt development libraries (Qt 6 recommended).
* CMake 3.15 or newer.

To configure and build on a Unix‑like system, run:

```sh
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

On Windows you may wish to supply additional flags to `cmake` such as
`-DCMAKE_PREFIX_PATH` to point at your Qt installation.  Refer to Qt's
documentation for further details.

After a successful build the executable will be placed in the build
directory.  When run it will prompt you to select a folder of images.
JPG, PNG, BMP, GIF, TIF, WEBP and AVIF files are currently recognised.

## Basic usage

When you launch the application you will be asked to select a source
directory.  The program scans the selected folder for image files and
displays them one at a time.  You may perform the following actions:

* **Keep** the current image: press **Z** or click the **Keep** button.
  The file is moved into a sibling folder named `keep`.  If a file of
  the same name already exists in `keep` a numerical suffix is appended
  (e.g. `image.jpg` → `image_1.jpg`).
* **Reject** the current image: press **X** or click the **Reject** button.
  The file is moved into a sibling folder named `discard` (similar
  suffix handling applies).
* **Undo** the last action: press **U** or **Ctrl+Z** or click **Undo**.
  The most recent move operation is reversed and the file is restored to
  its original location and position in the sequence.
* **Open** a new source directory: press **O** and choose another folder.

The status bar at the bottom of the window shows the current index,
total number of images and the name of the file being reviewed.  The
image itself is scaled to fit the window while preserving its aspect
ratio.  The application preloads the next image in the background to
keep navigation snappy.

## Implementation notes

* **Natural sorting**: files are presented in a human‑friendly order
  such that `image2.jpg` appears before `image10.jpg`.  This is
  implemented using a custom comparator that parses numeric runs and
  compares them as integers.
* **Dynamic caching**: the next image is loaded on a worker thread using
  `ImageLoader`, a subclass of `QThread`.  When the loading finishes
  the image is stored in a cache keyed by its index.  If the user
  advances to the next image before loading has finished, the cache
  prevents redundant work.
* **Undo stack**: up to 20 move operations are retained.  Undoing an
  operation restores both the file and the browsing position.

This codebase is intended as a starting point.  Additional features
such as star ratings, bulk exporting, or a cross‑platform mobile UI can
be implemented by extending the existing classes.