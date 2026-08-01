cmake_minimum_required(VERSION 3.24)

project(CuteMac
    VERSION 0.1.0
    DESCRIPTION "Modular classic Macintosh emulator"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
find_package(Readline)

qt_standard_project_setup()

add_library(CuteMacCore
    src/config/Configuration.cpp
    src/core/Emulator.cpp
    src/cpu/m68k/M68kCpuCore.cpp
    src/cpu/m68k/engine/m68kcpu.c
    src/cpu/m68k/engine/m68kops.c
    src/cpu/m68k/engine/softfloat/softfloat.c
    src/cpu/ppc/PpcCpuCore.cpp
    src/devices/adb/AdbBus.cpp
    src/devices/iwm/IwmController.cpp
    src/devices/scsi/ScsiBus.cpp
    src/devices/scsi/ncr5380/Ncr5380.cpp
    src/devices/scc/Z8530Scc.cpp
    src/devices/via6522/Via6522.cpp
    src/devices/video/VideoDevice.cpp
    src/machines/MachineCatalog.cpp
    src/machines/macplus/MacPlusMachine.cpp
)

target_include_directories(CuteMacCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(CuteMacCore
    PUBLIC
        Qt6::Core
)

if(UNIX AND NOT APPLE)
    target_link_libraries(CuteMacCore
        PRIVATE
        m
    )
endif()

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

add_executable(CuteMacSession
    src/session/main.cpp
)

target_link_libraries(CuteMacSession
    PRIVATE
        CuteMacCore
        Qt6::Widgets
)

set_target_properties(CuteMacSession PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)

add_executable(CuteMacMacPlusSmoke
    tools/macplus_smoke.cpp
)

target_link_libraries(CuteMacMacPlusSmoke
    PRIVATE
        CuteMacCore
)

if(Readline_FOUND AND NOT EMSCRIPTEN)
    add_executable(CuteMacDebugSession
        src/debug/main.cpp
    )

    target_link_libraries(CuteMacDebugSession
        PRIVATE
            CuteMacCore
            Readline::Readline
    )
endif()
