cmake_minimum_required(VERSION 3.24)

project(CuteMac
    VERSION 0.1.0
    DESCRIPTION "Modular classic Macintosh emulator"
    LANGUAGES C CXX
)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(Qt6 REQUIRED COMPONENTS Core Gui Network Widgets)
find_package(tomlplusplus REQUIRED CONFIG)
find_package(Readline)

qt_standard_project_setup()

add_library(CuteMacCore
    src/config/Configuration.cpp
    src/core/EmulationSession.cpp
    src/core/MachineScheduler.cpp
    src/core/SessionRunner.cpp
    src/cpu/m68k/M68kCpuCore.cpp
    src/cpu/m68k/engine/m68kcpu.c
    src/cpu/m68k/engine/m68kdasm.c
    src/cpu/m68k/engine/m68kops.c
    src/cpu/m68k/engine/softfloat/softfloat.c
    src/cpu/ppc/PpcCpuCore.cpp
    src/devices/adb/AdbBus.cpp
    src/devices/floppy/FloppyDiskImage.cpp
    src/devices/iwm/IwmController.cpp
    src/devices/scsi/ScsiBlockDevice.cpp
    src/devices/scsi/ScsiBus.cpp
    src/devices/scsi/ncr5380/Ncr5380.cpp
    src/devices/scc/Z8530Scc.cpp
    src/devices/via6522/Via6522.cpp
    src/devices/video/VideoDevice.cpp
    src/machines/MachineCatalog.cpp
    src/machines/macplus/MacPlusMachine.cpp
    src/rom/RomPatcher.cpp
)

target_include_directories(CuteMacCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(CuteMacCore
    PUBLIC
        Qt6::Core
        tomlplusplus::tomlplusplus
)

if(UNIX AND NOT APPLE)
    target_link_libraries(CuteMacCore
        PRIVATE
        m
    )
endif()

add_library(CuteMacWidgets
    src/ui/ConfigurationDialog.cpp
)

target_link_libraries(CuteMacWidgets
    PUBLIC
        CuteMacCore
        Qt6::Widgets
)

target_include_directories(CuteMacWidgets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(CuteMac
    src/app/main.cpp
)

target_link_libraries(CuteMac
    PRIVATE
        CuteMacCore
        CuteMacWidgets
        Qt6::Widgets
)

set_target_properties(CuteMac PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)

add_executable(CuteMacSession
    src/session/main.cpp
    src/session/SessionApplication.cpp
    src/session/SessionControlServer.cpp
    src/session/FramebufferRenderer.cpp
    src/session/HostInputMapper.cpp
)

target_link_libraries(CuteMacSession
    PRIVATE
        CuteMacCore
        CuteMacWidgets
        Qt6::Network
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
            Qt6::Gui
            Qt6::Network
            Readline::Readline
    )
endif()

if(BUILD_TESTING)
    add_executable(CuteMacIwmTests
        tests/iwm_tests.cpp
    )

    target_link_libraries(CuteMacIwmTests
        PRIVATE
            CuteMacCore
    )

    add_test(NAME iwm-register-protocol COMMAND CuteMacIwmTests)

    add_executable(CuteMacViaTests
        tests/via_tests.cpp
    )

    target_link_libraries(CuteMacViaTests
        PRIVATE
            CuteMacCore
    )

    add_test(NAME via-port-inputs COMMAND CuteMacViaTests)

    add_executable(CuteMacRomPatchTests
        tests/rom_patch_tests.cpp
    )

    target_link_libraries(CuteMacRomPatchTests
        PRIVATE
            CuteMacCore
    )

    add_test(NAME rom-patches COMMAND CuteMacRomPatchTests)

    add_executable(CuteMacConfigurationTests
        tests/configuration_tests.cpp
    )

    target_link_libraries(CuteMacConfigurationTests
        PRIVATE
            CuteMacCore
    )

    add_test(NAME configuration-toml COMMAND CuteMacConfigurationTests)

    add_executable(CuteMacSchedulerTests tests/scheduler_tests.cpp)
    target_link_libraries(CuteMacSchedulerTests PRIVATE CuteMacCore)
    add_test(NAME machine-scheduler COMMAND CuteMacSchedulerTests)
endif()
