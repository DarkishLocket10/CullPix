cmake_minimum_required(VERSION 3.15)

project(photo_triage_cpp
    VERSION 0.1
    LANGUAGES CXX
)

# Enable automatic Qt processing
set(CMAKE_AUTOMOC ON)   # Run moc automatically
set(CMAKE_AUTOUIC ON)   # Run uic automatically

# Enable C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required libraries
find_package(Qt6 COMPONENTS Widgets REQUIRED)
find_package(Threads REQUIRED)

# Add the main executable
add_executable(photo_triage_cpp
    src/main.cpp
    src/phototriagewindow.cpp
    src/phototriagewindow.h
    src/imageloader.cpp
    src/imageloader.h
    src/fileworker.cpp
    src/fileworker.h
)

# Link libraries
target_link_libraries(photo_triage_cpp
    PRIVATE Qt6::Widgets
    PRIVATE Threads::Threads
)
