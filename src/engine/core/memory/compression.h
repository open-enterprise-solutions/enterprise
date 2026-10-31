#ifndef _CORE_MEMORY_COMPRESSION_H__
#define _CORE_MEMORY_COMPRESSION_H__

#include "core/core.h"

#include <cstddef>

class ibMemoryBuffer;

// BYTES PACKED IN ZLIB'S FORM (RFC 1950: a header, the deflated bytes, a checksum) — what a stored blob is kept in.
// A WebSocket message's raw deflate is another form, and another pair (ibDeflate / ibInflate, net/webSocketFrame.h).
//
// Each APPENDS to what it is given, so a caller writes its own header first. Inflate is false for bytes that are not
// a whole zlib stream — corrupt, cut short — and then what it appended is not a value.
CORE_API bool ibZlibDeflate(const void* data, std::size_t size, ibMemoryBuffer& packed);
CORE_API bool ibZlibInflate(const void* data, std::size_t size, ibMemoryBuffer& bytes);

#endif // _CORE_MEMORY_COMPRESSION_H__
