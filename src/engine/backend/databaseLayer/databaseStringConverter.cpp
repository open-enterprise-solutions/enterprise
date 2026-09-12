#include "databaseStringConverter.h"

#include <cstring>
#include <vector>

namespace {

// How many bytes `w` takes in UTF-8, up to its first NUL — counted from its own code units, nothing
// converted: below 0x80 one byte, below 0x800 two, a surrogate pair four (a character past the BMP where
// wchar_t is sixteen bits), any other unit three, and a 32-bit wchar_t past 0xFFFF four. `false` for what
// UTF-8 cannot carry as it is — a lone surrogate, a value past U+10FFFF — which is left to the converter,
// as it always was.
bool EncodedLength(const wchar_t* w, size_t& bytes)
{
	bytes = 0;
	for (size_t i = 0; w[i] != 0; ++i) {
		const unsigned long u = static_cast<unsigned long>(w[i]);
		if (u < 0x80)
			bytes += 1;
		else if (u < 0x800)
			bytes += 2;
		else if (u >= 0xD800 && u <= 0xDFFF) {
			const unsigned long next = static_cast<unsigned long>(w[i + 1]);   // the NUL at worst
			if (sizeof(wchar_t) != 2 || u > 0xDBFF || next < 0xDC00 || next > 0xDFFF)
				return false;
			bytes += 4;
			++i;
		}
		else if (u < 0x10000)
			bytes += 3;
		else if (u <= 0x10FFFF)
			bytes += 4;
		else
			return false;
	}
	return true;
}

} // namespace

// ⭐ THE LENGTH IS COUNTED, NOT PRODUCED. A string parameter used to be converted twice — once here only to
// learn how long it would be, and once more by ConvertToUnicodeStream to have the bytes.
unsigned int ibDatabaseStringConverter::GetEncodedStreamLength(const wxString& inputString)
{
	const wxWX2WCbuf wide = inputString.wc_str();
	size_t bytes = 0;
	if (EncodedLength(wide, bytes))
		return static_cast<unsigned int>(bytes);
	// …and for what UTF-8 cannot carry as it is, the length of exactly what ConvertToUnicodeStream hands
	// over for it — the two are asked together, and must agree.
	return static_cast<unsigned int>(ConvertToUnicodeStream(inputString).length());
}

// wxString -> UTF-8, in ONE PASS into a buffer of exactly the encoded length (and its NUL). The converter
// it replaces walked the string to size a buffer, walked it again to fill it, and was called once more by
// GetEncodedStreamLength — for every statement's text and every string parameter bound.
const wxCharBuffer ibDatabaseStringConverter::ConvertToUnicodeStream(const wxString& inputString)
{
	const wxWX2WCbuf wide = inputString.wc_str();
	const wchar_t* const w = wide;
	size_t bytes = 0;
	if (!EncodedLength(w, bytes))
		return wxConvUTF8.cWC2MB(w);   // not text UTF-8 carries as it is — the converter's road, as before

	wxCharBuffer out(bytes);
	unsigned char* p = reinterpret_cast<unsigned char*>(out.data());
	for (size_t i = 0; w[i] != 0; ++i) {
		unsigned long u = static_cast<unsigned long>(w[i]);
		if (u >= 0xD800 && u <= 0xDBFF)   // the first half of a pair, and EncodedLength vouched for the second
			u = 0x10000 + ((u - 0xD800) << 10) + (static_cast<unsigned long>(w[++i]) - 0xDC00);
		if (u < 0x80)
			*p++ = static_cast<unsigned char>(u);
		else if (u < 0x800) {
			*p++ = static_cast<unsigned char>(0xC0 | (u >> 6));
			*p++ = static_cast<unsigned char>(0x80 | (u & 0x3F));
		}
		else if (u < 0x10000) {
			*p++ = static_cast<unsigned char>(0xE0 | (u >> 12));
			*p++ = static_cast<unsigned char>(0x80 | ((u >> 6) & 0x3F));
			*p++ = static_cast<unsigned char>(0x80 | (u & 0x3F));
		}
		else {
			*p++ = static_cast<unsigned char>(0xF0 | (u >> 18));
			*p++ = static_cast<unsigned char>(0x80 | ((u >> 12) & 0x3F));
			*p++ = static_cast<unsigned char>(0x80 | ((u >> 6) & 0x3F));
			*p++ = static_cast<unsigned char>(0x80 | (u & 0x3F));
		}
	}
	return out;
}

// UTF-8 -> wxString, in ONE PASS straight into the characters, handed to the string whole. Every text
// field of every row read comes through here, and the converter it replaces walked the bytes twice, built
// a wide buffer, copied it into the string and compared the string with an empty one to see whether it
// had worked — per field, forty thousand names in one batch of references (stack samples 2026-09-12).
//
// ⚠ STRICT, AND IT SAYS SO RATHER THAN GUESSING: a stray continuation byte, a cut-off tail, an overlong
// form, a surrogate spelled in UTF-8 or a value past U+10FFFF sends the bytes down the converter's road,
// exactly as before. So every string the converter accepted comes out the same — this is a shorter road
// to the same answer, not a second opinion about the bytes.
wxString ibDatabaseStringConverter::ConvertFromUnicodeStream(const char* inputBuffer)
{
	// 🛑 A NULL FIELD HAS NO BYTES, AND IT READS AS THE EMPTY STRING. SQLite answers a NULL column with a null
	// pointer (sqlite3_column_text), and the converter this replaced turned that into "" on both of its
	// roads; this one went straight to strlen and took the process down — the audit log, reading its own
	// SQLite journal beside a posting, 2026-09-12 (dump enterprise_25428_t28368).
	if (inputBuffer == nullptr)
		return wxString();
	const unsigned char* const bytes = reinterpret_cast<const unsigned char*>(inputBuffer);
	const size_t len = std::strlen(inputBuffer);

	// Each byte yields at most one code unit (a four-byte sequence yields two), so a buffer as long as the
	// bytes always holds the text — on the stack for a short field, which is the ordinary one.
	// (Not called `small` — that is a macro for `char` in the Windows headers.)
	wchar_t onStack[256];
	std::vector<wchar_t> onHeap;
	wchar_t* text = onStack;
	if (len > sizeof(onStack) / sizeof(onStack[0])) {
		onHeap.resize(len);
		text = onHeap.data();
	}

	size_t units = 0;
	bool wellFormed = true;
	for (size_t i = 0; i < len && wellFormed;) {
		const unsigned char c = bytes[i];
		if (c < 0x80) {
			text[units++] = static_cast<wchar_t>(c);
			++i;
			continue;
		}
		size_t n;
		unsigned long cp;
		if ((c & 0xE0) == 0xC0)      { n = 2; cp = c & 0x1F; }
		else if ((c & 0xF0) == 0xE0) { n = 3; cp = c & 0x0F; }
		else if ((c & 0xF8) == 0xF0) { n = 4; cp = c & 0x07; }
		else { wellFormed = false; break; }
		if (i + n > len) { wellFormed = false; break; }
		for (size_t k = 1; k < n; ++k) {
			const unsigned char cc = bytes[i + k];
			if ((cc & 0xC0) != 0x80) { wellFormed = false; break; }
			cp = (cp << 6) | (cc & 0x3F);
		}
		if (!wellFormed)
			break;
		if ((n == 2 && cp < 0x80) || (n == 3 && cp < 0x800) || (n == 4 && cp < 0x10000)
			|| cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
			wellFormed = false;
			break;
		}
		if (sizeof(wchar_t) == 2 && cp >= 0x10000) {
			cp -= 0x10000;
			text[units++] = static_cast<wchar_t>(0xD800 + (cp >> 10));
			text[units++] = static_cast<wchar_t>(0xDC00 + (cp & 0x3FF));
		}
		else
			text[units++] = static_cast<wchar_t>(cp);
		i += n;
	}
	if (wellFormed)
		return wxString(text, units);

	// The converter's road — and, if that answers nothing, the default conversion — as it always was.
	wxString strReturn(wxConvUTF8.cMB2WC(inputBuffer));
	if (strReturn == wxEmptyString)
		strReturn << wxString(inputBuffer);
	return strReturn;
}
