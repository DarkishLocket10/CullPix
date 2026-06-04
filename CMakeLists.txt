cmake_minimum_required(VERSION 3.15)

project(photo_triage_cpp
    VERSION 0.1
    LANGUAGES CXX
)

# Enable automatic Qt processing
set(CMAKE_AUTOMOC ON)   # Run moc automatically
set(CMAKE_AUTOUIC ON)   # Run uic automatically
set(CMAKE_AUTORCC ON)


# Enable C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build the macOS target as a relocatable .app bundle (used by the CI release
# packaging, which runs macdeployqt + builds a .dmg). Off by default so local
# dev builds stay a plain, quick-to-launch executable.
option(CULLPIX_MACOS_BUNDLE "Build the macOS target as an .app bundle" OFF)

# -------- Qt & Threads --------
find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(Threads REQUIRED)

# -------- LibRaw (handle vcpkg + other installs) --------
# Try official exported target
find_package(LibRaw CONFIG QUIET)
# vcpkg commonly exports it as 'unofficial-libraw'
find_package(unofficial-libraw CONFIG QUIET)

set(HAVE_LIBRAW FALSE)
set(LIBRAW_LINK_TARGET "")       # imported target name if found
set(LIBRAW_INCLUDE_DIRS "")      # for pkg-config/manual fallback
set(LIBRAW_LIBRARIES "")

if (TARGET LibRaw::LibRaw)
    message(STATUS "Found LibRaw: LibRaw::LibRaw")
    set(HAVE_LIBRAW TRUE)
    set(LIBRAW_LINK_TARGET LibRaw::LibRaw)
elseif (TARGET unofficial::libraw::libraw)
    message(STATUS "Found LibRaw: unofficial::libraw::libraw (vcpkg)")
    set(HAVE_LIBRAW TRUE)
    set(LIBRAW_LINK_TARGET unofficial::libraw::libraw)
else()
    # Resolve LibRaw via cached find_path/find_library. Unlike
    # pkg_check_modules' convenience variables (which can go empty on a cached
    # reconfigure and silently drop libraw from the compile/link line), the
    # find_* cache entries persist across reconfigures. pkg-config, when
    # present, only supplies search hints; CMAKE_PREFIX_PATH covers Homebrew.
    find_package(PkgConfig QUIET)
    if (PkgConfig_FOUND)
        pkg_check_modules(PC_LIBRAW QUIET libraw)
    endif()
    find_path(LIBRAW_INCLUDE_DIR
        NAMES libraw/libraw.h
        HINTS ${PC_LIBRAW_INCLUDEDIR} ${PC_LIBRAW_INCLUDE_DIRS})
    find_library(LIBRAW_LIBRARY
        NAMES raw libraw
        HINTS ${PC_LIBRAW_LIBDIR} ${PC_LIBRAW_LIBRARY_DIRS})
    if (LIBRAW_INCLUDE_DIR AND LIBRAW_LIBRARY)
        message(STATUS "Found LibRaw: ${LIBRAW_LIBRARY}")
        set(HAVE_LIBRAW TRUE)
        set(LIBRAW_INCLUDE_DIRS ${LIBRAW_INCLUDE_DIR})
        set(LIBRAW_LIBRARIES    ${LIBRAW_LIBRARY})
    endif()
endif()

if (NOT HAVE_LIBRAW)
    message(FATAL_ERROR
        "LibRaw not found. If using vcpkg: `vcpkg install libraw:x64-windows` "
        "and configure with `-DCMAKE_TOOLCHAIN_FILE=.../vcpkg.cmake`."
    )
endif()

# -------- Sources --------
# Windows-only resource script (icon). Ignored on other platforms.
set(PLATFORM_SOURCES "")
if (WIN32)
    list(APPEND PLATFORM_SOURCES src/appicon.rc)
endif()

add_executable(photo_triage_cpp
    src/main.cpp
    src/phototriagewindow.cpp
    src/phototriagewindow.h
    src/imageloader.cpp
    src/imageloader.h
    src/imageview.cpp
    src/imageview.h
    src/fileworker.cpp
    src/fileworker.h
    src/rawloader.cpp
    src/rawloader.h
    ${PLATFORM_SOURCES}
    resources/icons.qrc
)

# Package as a macOS .app bundle only when requested (CI). macdeployqt then
# bundles the Qt frameworks + image-format plugins so the app runs on machines
# without Qt installed.
if (APPLE AND CULLPIX_MACOS_BUNDLE)
    set_target_properties(photo_triage_cpp PROPERTIES MACOSX_BUNDLE TRUE)
endif()

# -------- Link libraries --------
target_link_libraries(photo_triage_cpp
    PRIVATE Qt6::Widgets
    PRIVATE Threads::Threads
)

# Link LibRaw once, after the target exists
if (LIBRAW_LINK_TARGET)  # imported target from a real CMake config package
    target_link_libraries(photo_triage_cpp PRIVATE ${LIBRAW_LINK_TARGET})
else()                   # pkg-config / manual fallback: full lib path + include dir
    target_include_directories(photo_triage_cpp PRIVATE ${LIBRAW_INCLUDE_DIRS})
    target_link_libraries(photo_triage_cpp PRIVATE ${LIBRAW_LIBRARIES})
endif()

# Optional: define for conditional compilation
target_compile_definitions(photo_triage_cpp PRIVATE HAVE_LIBRAW=1)

# (Optional) Warnings
if (MSVC)
    target_compile_options(photo_triage_cpp PRIVATE /permissive- /W4)
else()
    target_compile_options(photo_triage_cpp PRIVATE -Wall -Wextra -Wpedantic)
endif()
