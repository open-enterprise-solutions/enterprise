#ifndef __BYTE_CODE_FORMAT_H__
#define __BYTE_CODE_FORMAT_H__

#include <cstdint>

// Whether this engine may run a compiled module.
//
// Written into every AOT blob (byteCodeAOT.cpp) and into the bytecode cache
// key, beside the build-time hash of compiler/ and system/ (byteCodeCache.cpp).
// A row from another number is not found, so the module is compiled again.
//
// Bump it when an opcode is added, removed or renumbered, or when an
// existing instruction changes what it does. Each bump is recorded in
// byteCodeAOT.cpp. tests/test_byteCodeFormat.cpp fails when the opcode
// list in codeDef.h moves and this number does not. That test is the
// second guard: the hash moves by itself when those sources move, and
// this number is what a person sets when the meaning changes.
//
// 37: OPER_CALL_CLOSURE left the opcode list (a lambda takes cells, not frames).
constexpr std::uint16_t kAOTFormatVersion = 37;

#endif // __BYTE_CODE_FORMAT_H__
