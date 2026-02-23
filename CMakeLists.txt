cmake_minimum_required(VERSION 3.15)
project(DSplit LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(DSplit WIN32
    src/main.cpp
    src/MainWindow.cpp
    src/DriveInfo.cpp
    src/FileTree.cpp
    src/Migration.cpp
    src/TransferLog.cpp
    src/Utils.cpp
    resources/app.rc
)

target_include_directories(DSplit PRIVATE src)

target_link_libraries(DSplit PRIVATE
    comctl32
    ole32
    shell32
    uuid
)

target_compile_definitions(DSplit PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# Disable auto-generated manifest since we embed it via .rc
set_target_properties(DSplit PROPERTIES
    LINK_FLAGS "/MANIFEST:NO"
)
