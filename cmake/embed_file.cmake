# Embed a file as a NUL-terminated C byte-array header.
#
# Run in script mode:
#   cmake -DINPUT=<file> -DOUTPUT=<header> -DSYMBOL=<name> -P embed_file.cmake
#
# Produces:
#   static const unsigned char <name>[]     = { ... , 0x00 };
#   static const unsigned int  <name>_len   = <byte count, excluding the NUL>;
#
# Used for the JS bundle (stub in M0, esbuild output in M1+).

file(READ "${INPUT}" _hex HEX)
string(LENGTH "${_hex}" _hexlen)
math(EXPR _len "${_hexlen} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _bytes "${_hex}")
get_filename_component(_name "${INPUT}" NAME)

file(WRITE  "${OUTPUT}" "// Generated from ${_name}. Do not edit.\n")
file(APPEND "${OUTPUT}" "static const unsigned char ${SYMBOL}[] = {\n${_bytes}0x00\n};\n")
file(APPEND "${OUTPUT}" "static const unsigned int ${SYMBOL}_len = ${_len}u;\n")
