////////////////////////////////////////////////////////////////////////////
//	Description : base64 into and out of the engine's text
////////////////////////////////////////////////////////////////////////////

#include "base64.h"

#include "core/fstring.h"   // ibString — appended into, read in place

namespace {

const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Three bytes to four characters, a stack's worth at a time — every chunk but the last a multiple of three bytes, so
// the padding falls only at the end — each appended whole. `Text` — bytes or the engine's text, in its own `Char`.
template<class Char, class Text>
void AppendEncoded(Text& out, const void* data, size_t size)
{
	out.reserve(out.length() + (size + 2) / 3 * 4);
	const unsigned char* in = static_cast<const unsigned char*>(data);
	Char chunk[256];
	while (size != 0) {
		const size_t k = size < 192 ? size : 192;
		Char* at = chunk;
		size_t i = 0;
		for (; i + 3 <= k; i += 3) {
			*at++ = static_cast<Char>(kAlphabet[in[i] >> 2]);
			*at++ = static_cast<Char>(kAlphabet[((in[i] & 0x03) << 4) | (in[i + 1] >> 4)]);
			*at++ = static_cast<Char>(kAlphabet[((in[i + 1] & 0x0f) << 2) | (in[i + 2] >> 6)]);
			*at++ = static_cast<Char>(kAlphabet[in[i + 2] & 0x3f]);
		}
		if (i != k) {   // one byte left or two — the last chunk's
			const bool two = i + 1 != k;
			const unsigned char next = two ? in[i + 1] : 0;
			*at++ = static_cast<Char>(kAlphabet[in[i] >> 2]);
			*at++ = static_cast<Char>(kAlphabet[((in[i] & 0x03) << 4) | (next >> 4)]);
			*at++ = two ? static_cast<Char>(kAlphabet[(next & 0x0f) << 2]) : static_cast<Char>('=');
			*at++ = static_cast<Char>('=');
		}
		out.append(chunk, static_cast<size_t>(at - chunk));
		in += k;
		size -= k;
	}
}

// A character's six bits — kPad for '=', kNone for one that is not base64.
constexpr unsigned char kPad = 64;
constexpr unsigned char kNone = 65;

unsigned char ValueOf(wchar_t c)
{
	if (c >= L'A' && c <= L'Z') return static_cast<unsigned char>(c - L'A');
	if (c >= L'a' && c <= L'z') return static_cast<unsigned char>(c - L'a' + 26);
	if (c >= L'0' && c <= L'9') return static_cast<unsigned char>(c - L'0' + 52);
	if (c == L'+') return 62;
	if (c == L'/') return 63;
	if (c == L'=') return kPad;
	return kNone;
}

} // namespace

void ibBase64::Append(ibString& out, const void* data, size_t size)
{
	AppendEncoded<wchar_t>(out, data, size);
}

void ibBase64::Append(std::string& out, const void* data, size_t size)
{
	AppendEncoded<char>(out, data, size);
}

bool ibBase64::Read(const wchar_t* text, size_t length, ibMemoryBuffer& out)
{
	if (length % 4 != 0)
		return false;

	// Quartet by quartet, into a buffer of its own — `out` is given only what decoded whole. The padding may close
	// only the last quartet: "xx==" a byte, "xxx=" two.
	ibMemoryBuffer bytes(length / 4 * 3 + 1);
	unsigned char* const begin = static_cast<unsigned char*>(bytes.GetData());
	unsigned char* at = begin;
	for (size_t i = 0; i != length; i += 4) {
		const unsigned char a = ValueOf(text[i]), b = ValueOf(text[i + 1]);
		const unsigned char c = ValueOf(text[i + 2]), d = ValueOf(text[i + 3]);
		if (a >= kPad || b >= kPad || c == kNone || d == kNone)
			return false;
		const bool last = i + 4 == length;
		if (c == kPad) {
			if (!last || d != kPad)
				return false;
			*at++ = static_cast<unsigned char>(a << 2 | b >> 4);
			break;
		}
		if (d == kPad) {
			if (!last)
				return false;
			*at++ = static_cast<unsigned char>(a << 2 | b >> 4);
			*at++ = static_cast<unsigned char>(b << 4 | c >> 2);
			break;
		}
		*at++ = static_cast<unsigned char>(a << 2 | b >> 4);
		*at++ = static_cast<unsigned char>(b << 4 | c >> 2);
		*at++ = static_cast<unsigned char>(c << 6 | d);
	}
	bytes.SetDataLen(static_cast<size_t>(at - begin));
	out = bytes;
	return true;
}

bool ibBase64::Read(const ibString& text, ibMemoryBuffer& out)
{
	return Read(text.wc_str(), text.length(), out);
}
