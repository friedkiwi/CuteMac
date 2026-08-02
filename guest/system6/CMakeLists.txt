cmake_minimum_required(VERSION 3.20)
project(CuteMacSystem6GuestTools LANGUAGES C ASM)

add_executable(CuteMacGuestTools
    cutemac_guest_tools.S)

set_target_properties(CuteMacGuestTools PROPERTIES
    OUTPUT_NAME CuteMacGuestTools.flt
    LINK_FLAGS "-Wl,--mac-flat")

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/cutemac_guest_tools_da.bin"
    COMMAND "${CMAKE_ASM_COMPILER}" -c
        "${CMAKE_CURRENT_SOURCE_DIR}/cutemac_guest_tools_da.S"
        -o "${CMAKE_CURRENT_BINARY_DIR}/cutemac_guest_tools_da.o"
    COMMAND "${CMAKE_OBJCOPY}" -O binary -j .text
        "${CMAKE_CURRENT_BINARY_DIR}/cutemac_guest_tools_da.o"
        "${CMAKE_CURRENT_BINARY_DIR}/cutemac_guest_tools_da.bin"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/cutemac_guest_tools_da.S"
    VERBATIM)

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/CuteMacGuestTools.bin"
    COMMAND "${REZ}" "-I${REZ_INCLUDE_PATH}"
        "${CMAKE_CURRENT_SOURCE_DIR}/cutemac_guest_tools.r"
        -o "${CMAKE_CURRENT_BINARY_DIR}/CuteMacGuestTools.bin"
        --cc "${CMAKE_CURRENT_BINARY_DIR}/CuteMacGuestTools.dsk"
        --cc "CuteMac Guest Tools"
        -t INIT
    DEPENDS CuteMacGuestTools "${CMAKE_CURRENT_SOURCE_DIR}/cutemac_guest_tools.r"
        "${CMAKE_CURRENT_BINARY_DIR}/cutemac_guest_tools_da.bin"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    VERBATIM)

add_custom_target(CuteMacGuestTools_INIT ALL
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/CuteMacGuestTools.bin")
