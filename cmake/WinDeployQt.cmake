# Runs windeployqt with the Qt runtime directory of the configuration being
# built prepended to PATH.
#
# Two things make a plain windeployqt call fail against the vcpkg Qt layout in
# a Debug build. vcpkg keeps the debug Qt runtime in <triplet>/debug/bin while
# the qt.conf next to windeployqt points at the release <triplet>/bin, so the
# debug modules have to be resolved through the qtpaths.debug.bat wrapper that
# vcpkg ships for exactly that purpose. And windeployqt locates the ICU
# libraries with a bare PATH search (findInPath() in qtbase's
# windeployqt/main.cpp), which no qt.conf influences, so the same directory has
# to be on PATH or deployment stops at "Unable to locate ICU library
# icuind78.dll".
#
# Expected variables:
#   CUTEMAC_WINDEPLOYQT   path to windeployqt
#   CUTEMAC_QT_RUNTIME_DIR directory holding the Qt DLLs for this configuration
#   CUTEMAC_QTPATHS       qtpaths wrapper to read the Qt layout from, may be empty
#   CUTEMAC_DEPLOY_DIR    directory to deploy into
#   CUTEMAC_TARGET_FILE   executable to deploy for

cmake_minimum_required(VERSION 3.21)

foreach(required IN ITEMS
    CUTEMAC_WINDEPLOYQT CUTEMAC_QT_RUNTIME_DIR CUTEMAC_DEPLOY_DIR CUTEMAC_TARGET_FILE)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "WinDeployQt.cmake requires -D${required}=<value>")
    endif()
endforeach()

file(TO_NATIVE_PATH "${CUTEMAC_QT_RUNTIME_DIR}" CUTEMAC_QT_RUNTIME_DIR_NATIVE)
set(ENV{PATH} "${CUTEMAC_QT_RUNTIME_DIR_NATIVE};$ENV{PATH}")

set(qtpaths_argument "")
if(CUTEMAC_QTPATHS)
    set(qtpaths_argument --qtpaths "${CUTEMAC_QTPATHS}")
endif()

# Let windeployqt classify the binary itself. Passing --release explicitly
# makes it fail to match the platform plugin on llvm-mingw Qt kits.
execute_process(
    COMMAND "${CUTEMAC_WINDEPLOYQT}"
        ${qtpaths_argument}
        --no-translations
        --compiler-runtime
        --dir "${CUTEMAC_DEPLOY_DIR}"
        "${CUTEMAC_TARGET_FILE}"
    RESULT_VARIABLE deploy_result
)

if(NOT deploy_result EQUAL 0)
    message(FATAL_ERROR "windeployqt failed (${deploy_result})")
endif()
