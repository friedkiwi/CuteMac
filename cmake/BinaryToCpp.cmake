if(NOT DEFINED INPUT_FILE OR NOT DEFINED OUTPUT_FILE OR NOT DEFINED ARRAY_NAME)
    message(FATAL_ERROR "BinaryToCpp.cmake requires INPUT_FILE, OUTPUT_FILE, and ARRAY_NAME")
endif()

file(READ "${INPUT_FILE}" binary_hex HEX)
string(LENGTH "${binary_hex}" hex_length)
math(EXPR byte_count "${hex_length} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," byte_values "${binary_hex}")
file(WRITE "${OUTPUT_FILE}"
    "// Generated from the CuteMac Video 68k driver; do not edit.\n"
    "constexpr std::array<std::uint8_t, ${byte_count}> ${ARRAY_NAME} = {${byte_values}};\n")
