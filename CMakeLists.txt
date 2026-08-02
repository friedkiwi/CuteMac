cmake_minimum_required(VERSION 3.24)

project(CuteMac
    VERSION 0.1.0
    DESCRIPTION "Modular classic Macintosh emulator"
    LANGUAGES C CXX
)

include(CTest)
include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(Qt6 REQUIRED COMPONENTS Core Gui Multimedia Network Widgets)
find_package(tomlplusplus REQUIRED CONFIG)
find_package(Readline)

qt_standard_project_setup()

set(CUTEMAC_RETRO68_TOOLCHAIN_BIN "" CACHE PATH "Directory containing the Retro68 cross tools")
find_program(CUTEMAC_RETRO68_AS
    NAMES m68k-apple-macos-as
    HINTS "${CUTEMAC_RETRO68_TOOLCHAIN_BIN}" /opt/Retro68/toolchain/bin
    REQUIRED
)
find_program(CUTEMAC_RETRO68_OBJCOPY
    NAMES m68k-apple-macos-objcopy
    HINTS "${CUTEMAC_RETRO68_TOOLCHAIN_BIN}" /opt/Retro68/toolchain/bin
    REQUIRED
)

set(CUTEMAC_VIDEO_DRIVER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/devices/video/nubus/guest/cutemac_video_driver.S")
set(CUTEMAC_VIDEO_DRIVER_OBJECT "${CMAKE_CURRENT_BINARY_DIR}/generated/cutemac_video_driver.o")
set(CUTEMAC_VIDEO_DRIVER_BINARY "${CMAKE_CURRENT_BINARY_DIR}/generated/cutemac_video_driver.bin")
set(CUTEMAC_VIDEO_DRIVER_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/cutemac_video_driver.generated.h")
add_custom_command(
    OUTPUT "${CUTEMAC_VIDEO_DRIVER_HEADER}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND "${CUTEMAC_RETRO68_AS}" -m68020 -o "${CUTEMAC_VIDEO_DRIVER_OBJECT}" "${CUTEMAC_VIDEO_DRIVER_SOURCE}"
    COMMAND "${CUTEMAC_RETRO68_OBJCOPY}" -O binary -j .text "${CUTEMAC_VIDEO_DRIVER_OBJECT}" "${CUTEMAC_VIDEO_DRIVER_BINARY}"
    COMMAND "${CMAKE_COMMAND}"
        "-DINPUT_FILE=${CUTEMAC_VIDEO_DRIVER_BINARY}"
        "-DOUTPUT_FILE=${CUTEMAC_VIDEO_DRIVER_HEADER}"
        -DARRAY_NAME=videoDriver
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/BinaryToCpp.cmake"
    DEPENDS "${CUTEMAC_VIDEO_DRIVER_SOURCE}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/BinaryToCpp.cmake"
    COMMENT "Building CuteMac Video declaration-ROM driver with Retro68"
    VERBATIM
)
add_custom_target(CuteMacVideoRom DEPENDS "${CUTEMAC_VIDEO_DRIVER_HEADER}")

add_library(CuteMacCore
    "${CUTEMAC_VIDEO_DRIVER_HEADER}"
    src/config/Configuration.cpp
    src/core/EmulationSession.cpp
    src/core/MachineScheduler.cpp
    src/core/SessionRunner.cpp
    src/devices/cuda/CudaController.cpp
    src/cpu/m68k/M68kCpuCore.cpp
    src/cpu/m68k/engine/m68kcpu.c
    src/cpu/m68k/engine/m68kdasm.c
    src/cpu/m68k/engine/m68kops.c
    src/cpu/m68k/engine/softfloat/softfloat.c
    src/cpu/ppc/PpcCpuCore.cpp
    src/devices/adb/AdbBus.cpp
    src/devices/audio/AppleSoundChip.cpp
    src/devices/adb/AdbTransceiver.cpp
    src/devices/floppy/FloppyDiskImage.cpp
    src/devices/iwm/IwmController.cpp
    src/devices/printer/ImageWriterII.cpp
    src/devices/nubus/NuBusBus.cpp
    src/devices/rtc/MacRtc.cpp
    src/devices/scsi/ScsiBlockDevice.cpp
    src/devices/scsi/ScsiCdRomDevice.cpp
    src/devices/scsi/ScsiBus.cpp
    src/devices/scsi/ncr5380/MacintoshNcr5380Bus.cpp
    src/devices/scsi/ncr5380/Ncr5380.cpp
    src/devices/scsi/ncr53c94/Ncr53c94.cpp
    src/devices/scc/Z8530Scc.cpp
    src/devices/via6522/Via6522.cpp
    src/devices/video/VideoDevice.cpp
    src/devices/video/SonoraVideo.cpp
    src/devices/video/nubus/CuteMacVideoCard.cpp
    src/devices/video/nubus/MacintoshIIVideoCard.cpp
    src/debug/SadMacDetector.cpp
    src/machines/MachineCatalog.cpp
    src/machines/macplus/MacPlusMachine.cpp
    src/machines/maciicx/MacIIcxMachine.cpp
    src/machines/powermac8100/PowerMac8100Machine.cpp
    src/rom/RomPatcher.cpp
    src/rom/RomCatalog.cpp
    src/storage/DiskImageManager.cpp
    src/storage/PngPageSink.cpp
)

add_dependencies(CuteMacCore CuteMacVideoRom)

target_include_directories(CuteMacCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_link_libraries(CuteMacCore
    PUBLIC
        Qt6::Core
        Qt6::Gui
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
    src/ui/DiskImageWidgets.cpp
    src/ui/RomManagerDialog.cpp
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
    src/session/AudioOutput.cpp
    src/session/HostInputMapper.cpp
)

target_link_libraries(CuteMacSession
    PRIVATE
        CuteMacCore
        CuteMacWidgets
        Qt6::Network
        Qt6::Multimedia
        Qt6::Widgets
)

set_target_properties(CuteMacSession PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)

# Keep the distributable product explicit. Development utilities, smoke tools,
# and tests intentionally have no install rules, so CPack-based ZIP, DEB,
# MSIX, and macOS bundle packaging cannot pick them up by scanning build output.
if(WIN32 OR APPLE)
    set(CUTEMAC_RUNTIME_DESTINATION ".")
else()
    set(CUTEMAC_RUNTIME_DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()

install(TARGETS CuteMac CuteMacSession
    RUNTIME DESTINATION "${CUTEMAC_RUNTIME_DESTINATION}"
        COMPONENT Runtime
    BUNDLE DESTINATION "."
        COMPONENT Runtime
)

install(FILES README.md
    DESTINATION "."
    COMPONENT Runtime
)

install(DIRECTORY licenses
    DESTINATION "."
    COMPONENT Runtime
)

# When a platform package is introduced through CPack, keep the same product
# boundary unless that packaging definition deliberately opts into more.
set(CPACK_COMPONENTS_ALL Runtime)

add_executable(CuteMacMacPlusSmoke
    tools/macplus_smoke.cpp
)

target_link_libraries(CuteMacMacPlusSmoke
    PRIVATE
        CuteMacCore
)

add_executable(CuteMacIIcxSmoke
    tools/maciicx_smoke.cpp
)

target_link_libraries(CuteMacIIcxSmoke
    PRIVATE
        CuteMacCore
)

add_executable(CuteMacPowerMac8100Smoke
    tools/powermac8100_smoke.cpp
)

target_link_libraries(CuteMacPowerMac8100Smoke PRIVATE CuteMacCore)

add_executable(CuteMacWrapHfsScsi tools/wrap_hfs_scsi.cpp)
target_link_libraries(CuteMacWrapHfsScsi PRIVATE Qt6::Core)

if(Readline_FOUND AND NOT EMSCRIPTEN)
    add_executable(CuteMacDebugSession
        src/debug/main.cpp
        src/session/FramebufferRenderer.cpp
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

    add_executable(CuteMacRomCatalogTests tests/rom_catalog_tests.cpp)
    target_link_libraries(CuteMacRomCatalogTests PRIVATE CuteMacCore)
    add_test(NAME rom-catalog COMMAND CuteMacRomCatalogTests)

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

    add_executable(CuteMacScsiBlockDeviceTests tests/scsi_block_device_tests.cpp)
    target_link_libraries(CuteMacScsiBlockDeviceTests PRIVATE CuteMacCore)
    add_test(NAME scsi-block-device COMMAND CuteMacScsiBlockDeviceTests)

    add_executable(CuteMacDiskImageManagerTests tests/disk_image_manager_tests.cpp)
    target_link_libraries(CuteMacDiskImageManagerTests PRIVATE CuteMacCore)
    add_test(NAME disk-image-manager COMMAND CuteMacDiskImageManagerTests)

    add_executable(CuteMacRtcTests tests/rtc_tests.cpp)
    target_link_libraries(CuteMacRtcTests PRIVATE CuteMacCore)
    add_test(NAME mac-rtc COMMAND CuteMacRtcTests)

    add_executable(CuteMacNuBusVideoTests tests/nubus_video_tests.cpp)
    target_link_libraries(CuteMacNuBusVideoTests PRIVATE CuteMacCore)
    add_test(NAME nubus-video-cards COMMAND CuteMacNuBusVideoTests)

    add_executable(CuteMacFramebufferRendererTests
        tests/framebuffer_renderer_tests.cpp
        src/session/FramebufferRenderer.cpp
    )
    target_link_libraries(CuteMacFramebufferRendererTests PRIVATE CuteMacCore Qt6::Gui)
    add_test(NAME framebuffer-renderer COMMAND CuteMacFramebufferRendererTests)

    add_executable(CuteMacAdbTests tests/adb_tests.cpp)
    target_link_libraries(CuteMacAdbTests PRIVATE CuteMacCore)
    add_test(NAME adb-pic-byte-protocol COMMAND CuteMacAdbTests)

    add_executable(CuteMacPpc601Tests tests/ppc601_tests.cpp)
    target_link_libraries(CuteMacPpc601Tests PRIVATE CuteMacCore)
    add_test(NAME ppc601-core COMMAND CuteMacPpc601Tests)

    add_executable(CuteMacPowerMac8100Tests tests/powermac8100_tests.cpp)
    target_link_libraries(CuteMacPowerMac8100Tests PRIVATE CuteMacCore)
    add_test(NAME powermac-8100-machine COMMAND CuteMacPowerMac8100Tests)

    add_executable(CuteMacDebugCpuAccessTests tests/debug_cpu_access_tests.cpp)
    target_link_libraries(CuteMacDebugCpuAccessTests PRIVATE CuteMacCore)
    add_test(NAME debug-cpu-access COMMAND CuteMacDebugCpuAccessTests)

    add_executable(CuteMacSadMacDetectorTests tests/sad_mac_detector_tests.cpp)
    target_link_libraries(CuteMacSadMacDetectorTests PRIVATE CuteMacCore)
    add_test(NAME sad-mac-detector COMMAND CuteMacSadMacDetectorTests)

    add_executable(CuteMacImageWriterTests tests/imagewriter_tests.cpp)
    target_link_libraries(CuteMacImageWriterTests PRIVATE CuteMacCore Qt6::Gui)
    add_test(NAME imagewriter COMMAND CuteMacImageWriterTests)
endif()
