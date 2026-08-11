cmake_minimum_required(VERSION 3.24)

# Before project(): the toolchain file has to be in place for the first
# compiler check, and vcpkg installs the manifest during that step.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(VcpkgBootstrap)

project(CuteMac
    VERSION 0.1.0
    DESCRIPTION "Modular classic Macintosh emulator"
    LANGUAGES C CXX
)

if(APPLE)
    enable_language(OBJCXX)
endif()

include(CTest)
include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Every third-party dependency is declared in vcpkg.json and resolved through
# the vendored vcpkg in third_party/. Do not vendor individual libraries or fall
# back to system packages; that is what made the Windows libslirp gap invisible.
find_package(Qt6 REQUIRED COMPONENTS Core Gui Multimedia Network Widgets)
find_package(tomlplusplus CONFIG REQUIRED)
find_package(Readline)

option(CUTEMAC_ENABLE_SLIRP "Build SLIP/PPP guest networking through libslirp" ON)
option(CUTEMAC_ENABLE_PANIC_DUMP "Build panic-dump capture and archive loading" ON)

if(EMSCRIPTEN AND CUTEMAC_ENABLE_SLIRP)
    message(STATUS "CuteMac: libslirp networking disabled; browsers have no raw sockets")
    set(CUTEMAC_ENABLE_SLIRP OFF CACHE BOOL "" FORCE)
endif()

if(EMSCRIPTEN AND CUTEMAC_ENABLE_PANIC_DUMP)
    message(STATUS "CuteMac: panic dumps disabled; the browser has no debug_dumps directory")
    set(CUTEMAC_ENABLE_PANIC_DUMP OFF CACHE BOOL "" FORCE)
endif()

if(CUTEMAC_ENABLE_PANIC_DUMP)
    # Declared in vcpkg.json. A missing optional dependency has to fail loudly
    # rather than silently dropping a shipped feature.
    find_package(libzip CONFIG REQUIRED)
    message(STATUS "CuteMac: panic dumps enabled for Debug configurations (libzip ${libzip_VERSION})")
else()
    message(STATUS "CuteMac: panic dumps disabled")
endif()

# Build provenance for panic-dump manifests. A dump that cannot name the commit
# it came from is not much use a week later.
find_package(Git QUIET)
set(CUTEMAC_GIT_SHA "unknown")
if(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE CUTEMAC_GIT_SHA
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(CUTEMAC_GIT_SHA STREQUAL "")
        set(CUTEMAC_GIT_SHA "unknown")
    endif()
endif()

if(CUTEMAC_ENABLE_SLIRP)
    # libslirp installs pkg-config metadata rather than a CMake package config,
    # and the vcpkg toolchain does not put pkg-config on PATH. The manifest
    # therefore provides pkgconf as a host tool; prefer it over any system copy.
    if(DEFINED VCPKG_INSTALLED_DIR)
        find_program(PKG_CONFIG_EXECUTABLE
            NAMES pkgconf pkg-config
            HINTS
                "${VCPKG_INSTALLED_DIR}/${VCPKG_HOST_TRIPLET}/tools/pkgconf"
                "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/pkgconf"
        )

        set(CUTEMAC_PKGCONFIG_ROOT "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
        set(CUTEMAC_PKGCONFIG_PATHS
            "${CUTEMAC_PKGCONFIG_ROOT}/lib/pkgconfig"
            "${CUTEMAC_PKGCONFIG_ROOT}/share/pkgconfig"
            "${CUTEMAC_PKGCONFIG_ROOT}/debug/lib/pkgconfig"
        )
        if(CMAKE_HOST_WIN32)
            list(JOIN CUTEMAC_PKGCONFIG_PATHS ";" CUTEMAC_PKGCONFIG_PATH)
        else()
            list(JOIN CUTEMAC_PKGCONFIG_PATHS ":" CUTEMAC_PKGCONFIG_PATH)
        endif()
        set(ENV{PKG_CONFIG_PATH} "${CUTEMAC_PKGCONFIG_PATH}")
    endif()

    set(PKG_CONFIG_USE_CMAKE_PREFIX_PATH ON)
    if(VCPKG_TARGET_TRIPLET MATCHES "static")
        # Static triplets need the transitive glib link line, not just -lslirp.
        set(PKG_CONFIG_ARGN --static)
    endif()

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(CUTEMAC_LIBSLIRP REQUIRED IMPORTED_TARGET slirp)
    message(STATUS "CuteMac: SLIP/PPP networking enabled (libslirp ${CUTEMAC_LIBSLIRP_VERSION})")
else()
    message(STATUS "CuteMac: SLIP/PPP networking disabled; modem SLIP and PPP targets report NO CARRIER")
endif()

qt_standard_project_setup()

set(CUTEMAC_RETRO68_TOOLCHAIN_BIN "" CACHE PATH "Directory containing the Retro68 cross tools")
find_program(CUTEMAC_RETRO68_AS
    NAMES m68k-apple-macos-as
    HINTS "${CUTEMAC_RETRO68_TOOLCHAIN_BIN}" /opt/Retro68/toolchain/bin
)
find_program(CUTEMAC_RETRO68_OBJCOPY
    NAMES m68k-apple-macos-objcopy
    HINTS "${CUTEMAC_RETRO68_TOOLCHAIN_BIN}" /opt/Retro68/toolchain/bin
)

set(CUTEMAC_VIDEO_DRIVER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/devices/video/nubus/guest/cutemac_video_driver.S")
set(CUTEMAC_ACCEL_VIDEO_EXTENSION "${CMAKE_CURRENT_SOURCE_DIR}/src/devices/video/nubus/guest/cutemac_accelerated_video_extension.S")
set(CUTEMAC_VIDEO_ROM_CACHE "${CMAKE_CURRENT_SOURCE_DIR}/src/devices/video/nubus/guest/prebuilt")

if(CUTEMAC_RETRO68_AS AND CUTEMAC_RETRO68_OBJCOPY)
    message(STATUS "Retro68 found; rebuilding CuteMac Video declaration ROMs")
    set(CUTEMAC_VIDEO_DRIVER_OBJECT "${CMAKE_CURRENT_BINARY_DIR}/generated/cutemac_video_driver.o")
    set(CUTEMAC_VIDEO_DRIVER_BINARY "${CMAKE_CURRENT_BINARY_DIR}/generated/cutemac_video_driver.bin")
    set(CUTEMAC_VIDEO_DRIVER_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/cutemac_video_driver.generated.h")
    add_custom_command(
        OUTPUT "${CUTEMAC_VIDEO_DRIVER_HEADER}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
        COMMAND "${CUTEMAC_RETRO68_AS}" -m68020 -o "${CUTEMAC_VIDEO_DRIVER_OBJECT}" "${CUTEMAC_VIDEO_DRIVER_SOURCE}"
        COMMAND "${CUTEMAC_RETRO68_OBJCOPY}" -O binary -j .text "${CUTEMAC_VIDEO_DRIVER_OBJECT}" "${CUTEMAC_VIDEO_DRIVER_BINARY}"
        COMMAND "${CMAKE_COMMAND}" "-DINPUT_FILE=${CUTEMAC_VIDEO_DRIVER_BINARY}"
            "-DOUTPUT_FILE=${CUTEMAC_VIDEO_DRIVER_HEADER}" -DARRAY_NAME=videoDriver
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/BinaryToCpp.cmake"
        DEPENDS "${CUTEMAC_VIDEO_DRIVER_SOURCE}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/BinaryToCpp.cmake"
        COMMENT "Building CuteMac Video declaration-ROM driver with Retro68"
        VERBATIM
    )
    add_custom_target(CuteMacVideoRom DEPENDS "${CUTEMAC_VIDEO_DRIVER_HEADER}")

    set(CUTEMAC_ACCEL_VIDEO_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/generated/cutemac_accelerated_video_driver.S")
    set(CUTEMAC_ACCEL_VIDEO_OBJECT "${CMAKE_CURRENT_BINARY_DIR}/generated/cutemac_accelerated_video_driver.o")
    set(CUTEMAC_ACCEL_VIDEO_BINARY "${CMAKE_CURRENT_BINARY_DIR}/generated/cutemac_accelerated_video_driver.bin")
    set(CUTEMAC_ACCEL_VIDEO_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/cutemac_accelerated_video_driver.generated.h")
    add_custom_command(
        OUTPUT "${CUTEMAC_ACCEL_VIDEO_HEADER}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
        COMMAND "${CMAKE_COMMAND}" "-DBASE_SOURCE=${CUTEMAC_VIDEO_DRIVER_SOURCE}"
            "-DEXTENSION_SOURCE=${CUTEMAC_ACCEL_VIDEO_EXTENSION}" "-DOUTPUT_SOURCE=${CUTEMAC_ACCEL_VIDEO_SOURCE}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/BuildAcceleratedVideoDriver.cmake"
        COMMAND "${CUTEMAC_RETRO68_AS}" -m68020 -o "${CUTEMAC_ACCEL_VIDEO_OBJECT}" "${CUTEMAC_ACCEL_VIDEO_SOURCE}"
        COMMAND "${CUTEMAC_RETRO68_OBJCOPY}" -O binary -j .text "${CUTEMAC_ACCEL_VIDEO_OBJECT}" "${CUTEMAC_ACCEL_VIDEO_BINARY}"
        COMMAND "${CMAKE_COMMAND}" "-DINPUT_FILE=${CUTEMAC_ACCEL_VIDEO_BINARY}"
            "-DOUTPUT_FILE=${CUTEMAC_ACCEL_VIDEO_HEADER}" -DARRAY_NAME=acceleratedVideoDriver
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/BinaryToCpp.cmake"
        DEPENDS "${CUTEMAC_VIDEO_DRIVER_SOURCE}" "${CUTEMAC_ACCEL_VIDEO_EXTENSION}"
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/BuildAcceleratedVideoDriver.cmake"
        COMMENT "Building CuteMac Video Accelerated declaration-ROM driver with Retro68"
        VERBATIM
    )
    add_custom_target(CuteMacAcceleratedVideoRom DEPENDS "${CUTEMAC_ACCEL_VIDEO_HEADER}")
else()
    message(STATUS "Retro68 not found; using checked-in CuteMac Video declaration ROMs")
    set(CUTEMAC_VIDEO_DRIVER_HEADER "${CUTEMAC_VIDEO_ROM_CACHE}/cutemac_video_driver.generated.h")
    set(CUTEMAC_ACCEL_VIDEO_HEADER "${CUTEMAC_VIDEO_ROM_CACHE}/cutemac_accelerated_video_driver.generated.h")
    if(NOT EXISTS "${CUTEMAC_VIDEO_DRIVER_HEADER}" OR NOT EXISTS "${CUTEMAC_ACCEL_VIDEO_HEADER}")
        message(FATAL_ERROR "Retro68 is unavailable and the checked-in CuteMac Video declaration ROMs are missing")
    endif()
endif()

add_library(CuteMacCore
    "${CUTEMAC_VIDEO_DRIVER_HEADER}"
    "${CUTEMAC_ACCEL_VIDEO_HEADER}"
    src/config/Configuration.cpp
    src/config/ProfileResolver.cpp
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
    src/devices/bus/ByteWideMmioAdapter.cpp
    src/devices/audio/AppleSoundChip.cpp
    src/devices/adb/AdbTransceiver.cpp
    src/devices/floppy/FloppyDiskImage.cpp
    src/devices/iwm/IwmController.cpp
    src/devices/modem/HayesModem.cpp
    src/devices/modem/NullModem.cpp
    src/devices/network/nubus/AppleNuBusEthernetCard.cpp
    src/devices/network/SlirpEthernetBackend.cpp
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
    src/devices/video/DafbVideo.cpp
    src/devices/video/SonoraVideo.cpp
    src/devices/video/nubus/AppleDisplayCard.cpp
    src/devices/video/nubus/CuteMacAcceleratedVideoCard.cpp
    src/devices/video/nubus/CuteMacVideoCard.cpp
    src/devices/video/nubus/MacintoshIIVideoCard.cpp
    src/debug/SadMacDetector.cpp
    src/debug/SnapshotBuilder.cpp
    src/machines/MachineCatalog.cpp
    src/machines/macplus/MacPlusMachine.cpp
    src/machines/maciicx/MacIIcxMachine.cpp
    src/machines/quadra700/Quadra700Machine.cpp
    src/machines/powermac8100/PowerMac8100Machine.cpp
    src/rom/RomPatcher.cpp
    src/rom/RomCatalog.cpp
    src/storage/DiskImageManager.cpp
    src/storage/PngPageSink.cpp
)

if(TARGET CuteMacVideoRom)
    add_dependencies(CuteMacCore CuteMacVideoRom CuteMacAcceleratedVideoRom)
endif()

target_include_directories(CuteMacCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/generated
        ${CUTEMAC_VIDEO_ROM_CACHE}
)

target_link_libraries(CuteMacCore
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Network
        tomlplusplus::tomlplusplus
)

if(UNIX AND NOT APPLE)
    target_link_libraries(CuteMacCore
        PRIVATE
        m
    )
endif()

if(CUTEMAC_ENABLE_SLIRP)
    target_compile_definitions(CuteMacCore PRIVATE CUTEMAC_HAS_LIBSLIRP=1)
    target_link_libraries(CuteMacCore PRIVATE PkgConfig::CUTEMAC_LIBSLIRP)
else()
    target_compile_definitions(CuteMacCore PRIVATE CUTEMAC_HAS_LIBSLIRP=0)
endif()

# Panic dumps live in CuteMacCore so the frontend writes them and the debug
# session reads them back without a second copy of the format. Debug
# configurations only, through generator expressions so the multi-config CI
# generator resolves per configuration -- a Release artifact must not carry
# libzip or the button.
if(CUTEMAC_ENABLE_PANIC_DUMP)
    target_sources(CuteMacCore
        PRIVATE
            "$<$<CONFIG:Debug>:${CMAKE_CURRENT_SOURCE_DIR}/src/debug/HostLogRing.cpp>"
            "$<$<CONFIG:Debug>:${CMAKE_CURRENT_SOURCE_DIR}/src/debug/PanicArchive.cpp>"
            "$<$<CONFIG:Debug>:${CMAKE_CURRENT_SOURCE_DIR}/src/debug/PanicDump.cpp>"
            "$<$<CONFIG:Debug>:${CMAKE_CURRENT_SOURCE_DIR}/src/debug/SnapshotMachine.cpp>"
    )
    target_link_libraries(CuteMacCore PRIVATE "$<$<CONFIG:Debug>:libzip::zip>")
    target_compile_definitions(CuteMacCore PUBLIC CUTEMAC_ENABLE_PANIC_DUMP=$<IF:$<CONFIG:Debug>,1,0>)
else()
    target_compile_definitions(CuteMacCore PUBLIC CUTEMAC_ENABLE_PANIC_DUMP=0)
endif()

target_compile_definitions(CuteMacCore PRIVATE CUTEMAC_GIT_SHA="${CUTEMAC_GIT_SHA}")

add_library(CuteMacWidgets
    src/ui/ConfigurationDialog.cpp
    src/ui/DiskImageWidgets.cpp
    src/ui/FileDialogs.cpp
    src/ui/RomManagerDialog.cpp
)

target_link_libraries(CuteMacWidgets
    PUBLIC
        CuteMacCore
        Qt6::Widgets
)

target_include_directories(CuteMacWidgets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# The application's own windows. CuteMacWidgets holds dialogs any frontend can
# reuse; this library holds the session manager and emulator windows that make
# up the CuteMac application itself.
add_library(CuteMacFrontend
    src/session/EmulatorWindow.cpp
    src/session/SessionWindowManager.cpp
    src/session/HostRelativeMouseCapture.cpp
    src/session/FramebufferRenderer.cpp
    src/session/AudioOutput.cpp
    src/session/HostInputMapper.cpp
    src/ui/ProfileManagerWindow.cpp
)

target_sources(CuteMacFrontend
    PRIVATE
        "$<$<CONFIG:Debug>:${CMAKE_CURRENT_SOURCE_DIR}/src/session/SessionControlServer.cpp>"
        "$<$<PLATFORM_ID:Darwin>:${CMAKE_CURRENT_SOURCE_DIR}/src/session/MacRelativeMouseCapture.mm>"
)

target_link_libraries(CuteMacFrontend
    PUBLIC
        CuteMacCore
        CuteMacWidgets
        Qt6::Multimedia
        "$<$<CONFIG:Debug>:Qt6::Network>"
        Qt6::Widgets
)

if(APPLE)
    target_link_libraries(CuteMacFrontend PRIVATE "-framework Cocoa" "-framework CoreGraphics")
endif()

target_compile_definitions(CuteMacFrontend
    PUBLIC
        "$<$<CONFIG:Debug>:CUTEMAC_ENABLE_SESSION_CONTROL>"
)

target_include_directories(CuteMacFrontend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# One binary serves both roles: no argument opens the session manager, a profile
# name or profile file path starts that machine directly.
add_executable(CuteMac
    src/app/main.cpp
)

target_link_libraries(CuteMac
    PRIVATE
        CuteMacFrontend
)

target_compile_definitions(CuteMac PRIVATE CUTEMAC_VERSION="${PROJECT_VERSION}")

# Debug builds keep a Windows console: the panic dump announces itself on
# stdout, and a GUI-subsystem binary has nowhere to print it. Release artifacts
# stay windowed.
set_target_properties(CuteMac PROPERTIES
    WIN32_EXECUTABLE $<NOT:$<CONFIG:Debug>>
    MACOSX_BUNDLE TRUE
)

# Windows and macOS ship self-contained artifacts, so the Qt runtime has to be
# copied next to the binary (Windows) or into the bundle (macOS). Linux packages
# declare their dependencies instead and need no deployment step.
if(WIN32 OR APPLE)
    option(CUTEMAC_DEPLOY_QT "Deploy the Qt runtime alongside the built application" ON)
endif()

if((WIN32 OR APPLE) AND CUTEMAC_DEPLOY_QT)
    get_target_property(CUTEMAC_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(CUTEMAC_QT_TOOL_DIR "${CUTEMAC_QMAKE_EXECUTABLE}" DIRECTORY)
endif()

if(WIN32 AND CUTEMAC_DEPLOY_QT)
    # Every target lands in one output directory, so deploying once against
    # CuteMac also covers the tests, smoke tools, and debug session.
    find_program(CUTEMAC_WINDEPLOYQT NAMES windeployqt HINTS "${CUTEMAC_QT_TOOL_DIR}")

    if(CUTEMAC_WINDEPLOYQT)
        # Let windeployqt classify the binary itself. Passing --release
        # explicitly makes it fail to match the platform plugin on llvm-mingw Qt
        # kits, where its debug/release detection misfires.
        add_custom_command(TARGET CuteMac POST_BUILD
            COMMAND "${CUTEMAC_WINDEPLOYQT}"
                --no-translations
                --compiler-runtime
                --dir "$<TARGET_FILE_DIR:CuteMac>"
                "$<TARGET_FILE:CuteMac>"
            COMMENT "Deploying the Qt runtime into the build tree"
            VERBATIM
        )
    else()
        message(WARNING
            "windeployqt was not found; build-tree executables will not start unless Qt is on PATH")
    endif()
endif()

if(APPLE AND CUTEMAC_DEPLOY_QT)
    # macdeployqt copies the Qt frameworks and the resolvable non-Qt dylibs
    # (libslirp, glib and its dependencies) into CuteMac.app and rewrites their
    # install names, so the bundle does not reference Homebrew paths that only
    # exist on a developer machine.
    find_program(CUTEMAC_MACDEPLOYQT NAMES macdeployqt HINTS "${CUTEMAC_QT_TOOL_DIR}")

    if(CUTEMAC_MACDEPLOYQT)
        add_custom_command(TARGET CuteMac POST_BUILD
            COMMAND "${CUTEMAC_MACDEPLOYQT}"
                "$<TARGET_BUNDLE_DIR:CuteMac>"
                -always-overwrite
            COMMENT "Deploying the Qt runtime into CuteMac.app"
            VERBATIM
        )
    else()
        message(WARNING
            "macdeployqt was not found; CuteMac.app will not be self-contained")
    endif()
endif()

# Keep the distributable product explicit. Development utilities, smoke tools,
# and tests intentionally have no install rules, so CPack-based ZIP, DEB,
# MSIX, and macOS bundle packaging cannot pick them up by scanning build output.
if(WIN32 OR APPLE)
    set(CUTEMAC_RUNTIME_DESTINATION ".")
else()
    set(CUTEMAC_RUNTIME_DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()

install(TARGETS CuteMac
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

add_executable(CuteMacQuadra700Smoke tools/quadra700_smoke.cpp)
target_link_libraries(CuteMacQuadra700Smoke PRIVATE CuteMacCore)

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

    add_executable(CuteMacAppleSoundChipTests tests/apple_sound_chip_tests.cpp)
    target_link_libraries(CuteMacAppleSoundChipTests PRIVATE CuteMacCore)
    add_test(NAME apple-sound-chip COMMAND CuteMacAppleSoundChipTests)

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

    add_executable(CuteMacProfileResolverTests tests/profile_resolver_tests.cpp)
    target_link_libraries(CuteMacProfileResolverTests PRIVATE CuteMacCore)
    add_test(NAME profile-resolver COMMAND CuteMacProfileResolverTests)

    add_executable(CuteMacSchedulerTests tests/scheduler_tests.cpp)
    target_link_libraries(CuteMacSchedulerTests PRIVATE CuteMacCore)
    add_test(NAME machine-scheduler COMMAND CuteMacSchedulerTests)

    if(CUTEMAC_ENABLE_PANIC_DUMP)
        add_executable(CuteMacPanicDumpTests tests/panic_dump_tests.cpp)
        target_link_libraries(CuteMacPanicDumpTests PRIVATE CuteMacCore)
        add_test(NAME panic-dump COMMAND CuteMacPanicDumpTests)
    endif()

    add_executable(CuteMacPhysicalMemoryMapTests tests/physical_memory_map_tests.cpp)
    target_link_libraries(CuteMacPhysicalMemoryMapTests PRIVATE CuteMacCore)
    add_test(NAME physical-memory-map COMMAND CuteMacPhysicalMemoryMapTests)

    add_executable(CuteMacM68kMmuCacheTests tests/m68k_mmu_cache_tests.cpp)
    target_include_directories(CuteMacM68kMmuCacheTests PRIVATE src/cpu/m68k/engine)
    target_link_libraries(CuteMacM68kMmuCacheTests PRIVATE CuteMacCore)
    add_test(NAME m68k-mmu-cache COMMAND CuteMacM68kMmuCacheTests)

    add_executable(CuteMacScsiBlockDeviceTests tests/scsi_block_device_tests.cpp)
    target_link_libraries(CuteMacScsiBlockDeviceTests PRIVATE CuteMacCore)
    add_test(NAME scsi-block-device COMMAND CuteMacScsiBlockDeviceTests)

    add_executable(CuteMacSccTests tests/scc_tests.cpp)
    target_link_libraries(CuteMacSccTests PRIVATE CuteMacCore Qt6::Core)
    add_test(NAME scc-register-protocol COMMAND CuteMacSccTests)

    add_executable(CuteMacDiskImageManagerTests tests/disk_image_manager_tests.cpp)
    target_link_libraries(CuteMacDiskImageManagerTests PRIVATE CuteMacCore)
    add_test(NAME disk-image-manager COMMAND CuteMacDiskImageManagerTests)

    add_executable(CuteMacRtcTests tests/rtc_tests.cpp)
    target_link_libraries(CuteMacRtcTests PRIVATE CuteMacCore)
    add_test(NAME mac-rtc COMMAND CuteMacRtcTests)

    add_executable(CuteMacNuBusVideoTests tests/nubus_video_tests.cpp)
    target_link_libraries(CuteMacNuBusVideoTests PRIVATE CuteMacCore)
    add_test(NAME nubus-video-cards COMMAND CuteMacNuBusVideoTests)

    add_executable(CuteMacDafbVideoTests tests/dafb_video_tests.cpp)
    target_link_libraries(CuteMacDafbVideoTests PRIVATE CuteMacCore)
    add_test(NAME dafb-video COMMAND CuteMacDafbVideoTests)

    add_executable(CuteMacQuadra700Tests tests/quadra700_tests.cpp)
    target_link_libraries(CuteMacQuadra700Tests PRIVATE CuteMacCore)
    add_test(NAME quadra700-bringup COMMAND CuteMacQuadra700Tests)

    add_executable(CuteMacNuBusEthernetTests tests/nubus_ethernet_tests.cpp)
    target_link_libraries(CuteMacNuBusEthernetTests PRIVATE CuteMacCore)
    add_test(NAME nubus-ethernet-cards COMMAND CuteMacNuBusEthernetTests)

    add_executable(CuteMacFramebufferRendererTests
        tests/framebuffer_renderer_tests.cpp
        src/session/FramebufferRenderer.cpp
    )
    target_link_libraries(CuteMacFramebufferRendererTests PRIVATE CuteMacCore Qt6::Gui)

    add_executable(CuteMacHostInputMapperTests
        tests/host_input_mapper_tests.cpp
        src/session/HostInputMapper.cpp
    )
    target_link_libraries(CuteMacHostInputMapperTests PRIVATE CuteMacCore Qt6::Gui)
    add_test(NAME host-input-mapper COMMAND CuteMacHostInputMapperTests)
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

    add_executable(CuteMacModemTests tests/modem_tests.cpp)
    target_link_libraries(CuteMacModemTests PRIVATE CuteMacCore Qt6::Network)
    if(CUTEMAC_ENABLE_SLIRP)
        target_compile_definitions(CuteMacModemTests PRIVATE CUTEMAC_HAS_LIBSLIRP=1)
    else()
        target_compile_definitions(CuteMacModemTests PRIVATE CUTEMAC_HAS_LIBSLIRP=0)
    endif()
    add_test(NAME modem COMMAND CuteMacModemTests)
endif()
