cmake_minimum_required(VERSION 3.24)

project(CuteMac
    VERSION 0.1.0
    DESCRIPTION "Modular classic Macintosh emulator"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

qt_standard_project_setup()

add_library(CuteMacCore
    src/config/Configuration.cpp
    src/core/Emulator.cpp
    src/cpu/m68k/M68kCpuCore.cpp
    src/cpu/ppc/PpcCpuCore.cpp
    src/devices/adb/AdbBus.cpp
    src/devices/scsi/ScsiBus.cpp
    src/devices/video/VideoDevice.cpp
    src/machines/MachineCatalog.cpp
)

target_include_directories(CuteMacCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(CuteMacCore
    PUBLIC
        Qt6::Core
)

add_executable(CuteMac
    src/app/main.cpp
)

target_link_libraries(CuteMac
    PRIVATE
        CuteMacCore
        Qt6::Widgets
)

set_target_properties(CuteMac PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)
