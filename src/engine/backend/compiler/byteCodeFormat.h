#ifndef __BYTE_CODE_FORMAT_H__
#define __BYTE_CODE_FORMAT_H__

#include <cstdint>

// Whether this engine may run a compiled module.
//
// Written into every AOT blob (byteCodeAOT.cpp) and half of the bytecode
// cache key (byteCodeCache.cpp). A row from another number is not found,
// so the module is compiled again instead of run.
//
// Bump it when an opcode is added, removed or renumbered, or when an
// existing instruction changes what it does. Each bump is recorded in
// byteCodeAOT.cpp. tests/test_byteCodeFormat.cpp fails when the opcode
// list in codeDef.h moves and this number does not.
//
// 35 is the list that still contains OPER_ITER. Compiling ?(cond, a, b)
// like If removes that opcode and sets this to 36. When both are in the
// tree the number here is 36: the cache key is this constant, and a blob
// numbered 35 was produced by an engine that ran both branches.
constexpr std::uint16_t kAOTFormatVersion = 35;

#endif // __BYTE_CODE_FORMAT_H__
