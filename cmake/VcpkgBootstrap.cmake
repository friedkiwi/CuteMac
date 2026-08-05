# Makes a bare `cmake ..` behave like the presets in CMakePresets.json.
#
# Presets are the supported way to configure CuteMac, but `cmake ..` is what
# IDEs and muscle memory produce, and without the toolchain file that silently
# falls back to system packages -- or, on a machine with no system Qt, fails at
# find_package(Qt6). Point CMake at the vendored vcpkg ourselves, bootstrapping
# it and wiring up the shared binary cache first.
#
# Must be included before project().

set(CUTEMAC_VCPKG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/vcpkg")

if(NOT EXISTS "${CUTEMAC_VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
    message(FATAL_ERROR
        "The vendored vcpkg is missing from third_party/vcpkg. Run:\n"
        "    git submodule update --init --recursive")
endif()

# Every dependency, Qt included, is a source build on a cache miss. Default to
# the shared HTTP cache unless the environment already chose something.
if(NOT DEFINED ENV{VCPKG_BINARY_SOURCES})
    set(ENV{VCPKG_BINARY_SOURCES}
        "clear;http,http://buildcache.cyber.gent/ac/{sha},readwrite")
endif()
message(STATUS "CuteMac: VCPKG_BINARY_SOURCES=$ENV{VCPKG_BINARY_SOURCES}")

if(CMAKE_HOST_WIN32)
    set(CUTEMAC_VCPKG_EXE "${CUTEMAC_VCPKG_ROOT}/vcpkg.exe")
    set(CUTEMAC_VCPKG_BOOTSTRAP "${CUTEMAC_VCPKG_ROOT}/bootstrap-vcpkg.bat")
else()
    set(CUTEMAC_VCPKG_EXE "${CUTEMAC_VCPKG_ROOT}/vcpkg")
    set(CUTEMAC_VCPKG_BOOTSTRAP "${CUTEMAC_VCPKG_ROOT}/bootstrap-vcpkg.sh")
endif()

if(NOT EXISTS "${CUTEMAC_VCPKG_EXE}")
    message(STATUS "CuteMac: bootstrapping vcpkg in third_party/vcpkg")
    execute_process(
        COMMAND "${CUTEMAC_VCPKG_BOOTSTRAP}" -disableMetrics
        WORKING_DIRECTORY "${CUTEMAC_VCPKG_ROOT}"
        RESULT_VARIABLE CUTEMAC_VCPKG_BOOTSTRAP_RESULT
    )
    if(NOT CUTEMAC_VCPKG_BOOTSTRAP_RESULT EQUAL 0)
        message(FATAL_ERROR "vcpkg bootstrap failed (${CUTEMAC_VCPKG_BOOTSTRAP_RESULT})")
    endif()
endif()

# MSVC's cl.exe is not long-path aware: it reports
#     fatal error C1083: Cannot open compiler generated file: '': Invalid argument
# for any output past MAX_PATH (260), regardless of the LongPathsEnabled policy.
# vcpkg builds under third_party/vcpkg/buildtrees, and Qt's own object paths are
# long enough that a checkout in a normal user directory crosses the limit --
# qtdeclarative's QQmlNativeDebugConnectorFactoryPlugin object lands at 262
# characters. Give vcpkg a short scratch root instead of hoping the checkout is
# shallow. Only the intermediate trees move; downloads, the binary cache, and
# the installed tree stay where they are.
if(CMAKE_HOST_WIN32)
    if(DEFINED ENV{CUTEMAC_VCPKG_SCRATCH})
        file(TO_CMAKE_PATH "$ENV{CUTEMAC_VCPKG_SCRATCH}" CUTEMAC_VCPKG_SCRATCH)
    else()
        string(REGEX MATCH "^[A-Za-z]:" CUTEMAC_VCPKG_DRIVE "${CMAKE_CURRENT_SOURCE_DIR}")
        if(NOT CUTEMAC_VCPKG_DRIVE)
            set(CUTEMAC_VCPKG_DRIVE "C:")
        endif()
        set(CUTEMAC_VCPKG_SCRATCH "${CUTEMAC_VCPKG_DRIVE}/cutemac-vcpkg")
    endif()

    file(MAKE_DIRECTORY "${CUTEMAC_VCPKG_SCRATCH}/bt" "${CUTEMAC_VCPKG_SCRATCH}/pkg")
    if(NOT IS_DIRECTORY "${CUTEMAC_VCPKG_SCRATCH}/bt")
        message(FATAL_ERROR
            "CuteMac: could not create the vcpkg scratch root at ${CUTEMAC_VCPKG_SCRATCH}. "
            "Set CUTEMAC_VCPKG_SCRATCH to a short writable path such as C:/cutemac-vcpkg.")
    endif()

    # Appending unconditionally would duplicate the flags on every reconfigure.
    if(NOT VCPKG_INSTALL_OPTIONS MATCHES "x-buildtrees-root")
        list(APPEND VCPKG_INSTALL_OPTIONS
            "--x-buildtrees-root=${CUTEMAC_VCPKG_SCRATCH}/bt"
            "--x-packages-root=${CUTEMAC_VCPKG_SCRATCH}/pkg")
        set(VCPKG_INSTALL_OPTIONS "${VCPKG_INSTALL_OPTIONS}"
            CACHE STRING "Additional install options to pass to vcpkg" FORCE)
    endif()
    message(STATUS "CuteMac: vcpkg scratch root=${CUTEMAC_VCPKG_SCRATCH} (MSVC MAX_PATH)")
endif()

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CUTEMAC_VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "Vendored vcpkg toolchain")
    message(STATUS "CuteMac: using vendored vcpkg toolchain (configure with a preset to be explicit)")
elseif(NOT CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg.cmake$" AND NOT DEFINED VCPKG_CHAINLOAD_TOOLCHAIN_FILE)
    # A cross-compilation toolchain belongs in VCPKG_CHAINLOAD_TOOLCHAIN_FILE so
    # vcpkg still resolves the dependencies. Emscripten chainloads its own.
    message(WARNING
        "CMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE} bypasses the vendored vcpkg; "
        "dependencies will not be resolved from vcpkg.json.")
endif()
