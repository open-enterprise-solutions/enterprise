#include "core/memory/compression.h"
#include "core/memory/memoryBuffer.h"
#include "core/net/webSocketFrame.h"   // ibDeflate / ibInflate - a WebSocket message's form, made by the same pair
#include "core/fstring.h"              // ibString - the text such a message is

#include <algorithm>
#include <climits>
#include <string>

#define ZLIB_CONST   // next_in is a const Bytef*: what is fed to zlib is read, never written
#include <zlib.h>

namespace {

// zlib's two forms, told by its windowBits: RFC 1950's (a header, the deflated bytes, a checksum), and the bare deflate
// of RFC 1951.
constexpr int kZlibForm = 15;
constexpr int kRawForm = -15;

// The input handed to a stream in pieces zlib can count (a uInt), the next one when the last is used up.
void Feed(z_stream& stream, const Bytef*& at, std::size_t& left)
{
	if (stream.avail_in != 0 || left == 0)
		return;
	const uInt piece = static_cast<uInt>(std::min<std::size_t>(left, UINT_MAX));
	stream.next_in = at;
	stream.avail_in = piece;
	at += piece;
	left -= piece;
}

// `size` bytes at `data` deflated in `form`, handed to `append` a chunk at a time.
template<class Append>
bool Deflate(const void* data, std::size_t size, int form, Append append)
{
	z_stream stream{};
	// Default level (6): a database charges per segment, and tight output saves segments too.
	if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, form, 8, Z_DEFAULT_STRATEGY) != Z_OK)
		return false;
	const Bytef* at = static_cast<const Bytef*>(data);
	std::size_t left = size;
	Bytef chunk[16384];   // on the stack: a session's fiber has a small one
	int state = Z_OK;
	do {
		Feed(stream, at, left);
		stream.next_out = chunk;
		stream.avail_out = sizeof(chunk);
		state = deflate(&stream, left == 0 ? Z_FINISH : Z_NO_FLUSH);
		if (stream.avail_out != sizeof(chunk))
			append(reinterpret_cast<const char*>(chunk), sizeof(chunk) - stream.avail_out);
	} while (state == Z_OK || state == Z_BUF_ERROR);
	deflateEnd(&stream);
	return state == Z_STREAM_END;
}

// …and back: the bytes `size` bytes in `form` hold, handed to `append` a chunk at a time - false for bytes that are not
// a whole stream of that form.
template<class Append>
bool Inflate(const void* data, std::size_t size, int form, Append append)
{
	z_stream stream{};
	if (inflateInit2(&stream, form) != Z_OK)
		return false;
	const Bytef* at = static_cast<const Bytef*>(data);
	std::size_t left = size;
	Bytef chunk[16384];
	int state = Z_OK;
	do {
		Feed(stream, at, left);
		stream.next_out = chunk;
		stream.avail_out = sizeof(chunk);
		state = inflate(&stream, Z_NO_FLUSH);
		if (stream.avail_out != sizeof(chunk))
			append(reinterpret_cast<const char*>(chunk), sizeof(chunk) - stream.avail_out);
		// A corrupt header or a bad checksum is the stream's own error; a cut-off body is input that ran out first.
		if (state == Z_BUF_ERROR && stream.avail_in == 0 && left == 0)
			break;
	} while (state == Z_OK || state == Z_BUF_ERROR);
	inflateEnd(&stream);
	return state == Z_STREAM_END;
}

} // namespace

bool ibZlibDeflate(const void* data, std::size_t size, ibMemoryBuffer& packed)
{
	return Deflate(data, size, kZlibForm, [&packed](const char* chunk, std::size_t got) { packed.AppendData(chunk, got); });
}

bool ibZlibInflate(const void* data, std::size_t size, ibMemoryBuffer& bytes)
{
	return Inflate(data, size, kZlibForm, [&bytes](const char* chunk, std::size_t got) { bytes.AppendData(chunk, got); });
}

bool ibDeflate(const std::string& bytes, std::string& packed)
{
	packed.clear();
	return Deflate(bytes.data(), bytes.size(), kRawForm,
		[&packed](const char* chunk, std::size_t got) { packed.append(chunk, got); });
}

bool ibInflate(const std::string& packed, ibString& text)
{
	std::string bytes;
	if (!Inflate(packed.data(), packed.size(), kRawForm,
			[&bytes](const char* chunk, std::size_t got) { bytes.append(chunk, got); }))
		return false;
	text.SetUtf8(bytes.data(), bytes.size());
	return true;
}

bool ibInflate(const std::string& packed, ibMemoryBuffer& bytes)
{
	bytes.SetDataLen(0);
	return Inflate(packed.data(), packed.size(), kRawForm,
		[&bytes](const char* chunk, std::size_t got) { bytes.AppendData(chunk, got); });
}
