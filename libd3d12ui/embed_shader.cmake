# Converts a compiled shader bytecode file into a constexpr byte array header.
#
# Required variables:
#   INPUT  - compiled .cso file
#   OUTPUT - generated header
#   SYMBOL - C++ array symbol

if(NOT DEFINED INPUT OR NOT DEFINED OUTPUT OR NOT DEFINED SYMBOL)
    message(FATAL_ERROR "INPUT, OUTPUT and SYMBOL are required")
endif()

file(READ "${INPUT}" shader_hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," shader_bytes "${shader_hex}")

file(WRITE "${OUTPUT}" "#pragma once\n\n#include <cstdint>\n\ninline constexpr std::uint8_t ${SYMBOL}[] = {${shader_bytes}};\n")
