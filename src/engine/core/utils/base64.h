#ifndef __BASE64_H__
#define __BASE64_H__

#include "core/core.h"

#include "core/memory/memoryBuffer.h"

#include <cstddef>
#include <string>

class ibString;   // core/fstring.h — the engine's text, written into and read where it lies

// ⭐ BASE64 (RFC 4648) STRAIGHT INTO AND OUT OF THE ENGINE'S TEXT. wx's spells it in a wxString and reads it back
// through an ASCII copy of one, so a file's part, a picture, a binary of the JSON view paid a text and a copy on each
// side of the wire. Here the characters are written into the string that carries them and read from it in place.
class CORE_API ibBase64 {
public:
	// `size` bytes at `data`, encoded and appended to `out` — padded, with no line breaks.
	static void Append(ibString& out, const void* data, size_t size);
	// The same as bytes — for a writer that builds its document in bytes.
	static void Append(std::string& out, const void* data, size_t size);

	// …AND READ BACK — `length` characters at `text` decoded into `out`; false, `out` as it was, when one is not
	// base64, the padding stands where it may not, or the text does not end on a whole quartet. Whitespace is not
	// skipped: what is written above has none.
	static bool Read(const ibChar* text, size_t length, ibMemoryBuffer& out);
	static bool Read(const ibString& text, ibMemoryBuffer& out);
};

#endif // !__BASE64_H__
