cmake_minimum_required(VERSION 3.15)
project(photo_triage_cpp VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)          # run moc automatically
set(CMAKE_AUTOUIC ON)

# Find Qt libraries
find_package(Qt6 COMPONENTS Widgets REQUIRED)

# Find threading library for std::thread support on platforms that require it
find_package(Threads REQUIRED)

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
    src/fileworker.cpp
    src/fileworker.h
    src/rawloader.h
    src/rawloader.cpp
)

target_link_libraries(photo_triage_cpp PRIVATE Qt6::Widgets)
target_link_libraries(photo_triage_cpp PRIVATE Threads::Threads)
