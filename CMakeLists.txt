cmake_minimum_required(VERSION 3.15)
project(photo_triage_cpp VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)          # run moc automatically
set(CMAKE_AUTOUIC ON)          # (optional) for .ui files in future

# Find Qt libraries
find_package(Qt6 COMPONENTS Widgets REQUIRED)

# Enable C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add the main executable
add_executable(photo_triage_cpp
    src/main.cpp
    src/phototriagewindow.cpp
    src/phototriagewindow.h
    src/imageloader.cpp
    src/imageloader.h
)

target_link_libraries(photo_triage_cpp PRIVATE Qt6::Widgets)
