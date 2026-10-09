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
// 35 is the list that still contains OPER_ITER. #225 removes that opcode
// and sets this to 36, and it merges first. This branch still has the
// opcode, so the number stays 35: a 36 here would name two engines, the
// one that still runs both branches of ?() and the one that does not.
// When #225 is in the tree the number is 36 and the fingerprint in
// tests/test_byteCodeFormat.cpp is kOpcodeFingerprintOnceOperIterLeaves.
constexpr std::uint16_t kAOTFormatVersion = 35;

#endif // __BYTE_CODE_FORMAT_H__
