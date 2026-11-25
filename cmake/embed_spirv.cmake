# Generates a C++ translation unit containing an embedded SPIR-V byte array.
# Inputs: INPUT (spv path), OUTPUT (cpp path), NAME (identifier suffix).

file(READ "${INPUT}" hex_content HEX)

string(LENGTH "${hex_content}" hex_len)
math(EXPR byte_count "${hex_len} / 2")

string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex_content}")

file(WRITE "${OUTPUT}"
"/* Generated automatically. Do not edit. */\n"
"#include <cstddef>\n"
"extern const unsigned char hcs_spv_${NAME}[];\n"
"extern const unsigned int hcs_spv_${NAME}_len;\n"
"extern const unsigned char hcs_spv_${NAME}[] = {${bytes}};\n"
"extern const unsigned int hcs_spv_${NAME}_len = ${byte_count}u;\n")
