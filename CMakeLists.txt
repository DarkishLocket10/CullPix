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
    # Fallbacks: pkg-config or manual
    find_package(PkgConfig QUIET)
    if (PkgConfig_FOUND)
        pkg_check_modules(LIBRAW QUIET libraw)
        if (LIBRAW_FOUND)
            message(STATUS "Found LibRaw via pkg-config")
            set(HAVE_LIBRAW TRUE)
            # pkg-config gives include dirs & libs below
            set(LIBRAW_INCLUDE_DIRS ${LIBRAW_INCLUDE_DIRS})
            set(LIBRAW_LIBRARIES    ${LIBRAW_LIBRARIES})
        endif()
    endif()

    if (NOT HAVE_LIBRAW)
        # Manual search as a last resort
        find_path(LIBRAW_INCLUDE_DIR libraw/libraw.h)
        find_library(LIBRAW_LIBRARY NAMES raw libraw)
        if (LIBRAW_INCLUDE_DIR AND LIBRAW_LIBRARY)
            message(STATUS "Found LibRaw via manual search")
            set(HAVE_LIBRAW TRUE)
            set(LIBRAW_INCLUDE_DIRS ${LIBRAW_INCLUDE_DIR})
            set(LIBRAW_LIBRARIES    ${LIBRAW_LIBRARY})
        endif()
    endif()
endif()

if (NOT HAVE_LIBRAW)
    message(FATAL_ERROR
        "LibRaw not found. If using vcpkg: `vcpkg install libraw:x64-windows` "
        "and configure with `-DCMAKE_TOOLCHAIN_FILE=.../vcpkg.cmake`."
    )
endif()

# -------- Sources --------
add_executable(photo_triage_cpp
    src/main.cpp
    src/phototriagewindow.cpp
    src/phototriagewindow.h
    src/imageloader.cpp
    src/imageloader.h
    src/fileworker.cpp
    src/fileworker.h
    src/rawloader.cpp
    src/rawloader.h
    src/appicon.rc
    resources/icons.qrc
)

# -------- Link libraries --------
target_link_libraries(photo_triage_cpp
    PRIVATE Qt6::Widgets
    PRIVATE Threads::Threads
)

# Link LibRaw once, after the target exists
if (LIBRAW_LINK_TARGET)  # imported target (CMake config)
    target_link_libraries(photo_triage_cpp PRIVATE ${LIBRAW_LINK_TARGET})
else()                   # pkg-config/manual fallback
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
