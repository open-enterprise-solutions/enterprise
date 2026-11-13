#include "core/codePage.h"
#include "core/exception.h"                 // ibCoreException - a character with no byte, refused
#include "core/localization/dictionary.h"   // _() - the refusals' words

#include <array>
#include <string>

#ifdef __IBMSW__
#include <windows.h>   // MultiByteToWideChar / WideCharToMultiByte - the machine's pages by number
#endif

namespace {

// windows-1251 and cp866, byte -> code point: Python's own codecs, ord(bytes([b]).decode('cp1251')) (and cp866)
// for every byte, a byte the page defines no character for mapped to 0xFFFF.
const char16_t k1251[256] = {
	0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007, 0x0008, 0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x000E, 0x000F,
	0x0010, 0x0011, 0x0012, 0x0013, 0x0014, 0x0015, 0x0016, 0x0017, 0x0018, 0x0019, 0x001A, 0x001B, 0x001C, 0x001D, 0x001E, 0x001F,
	0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027, 0x0028, 0x0029, 0x002A, 0x002B, 0x002C, 0x002D, 0x002E, 0x002F,
	0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037, 0x0038, 0x0039, 0x003A, 0x003B, 0x003C, 0x003D, 0x003E, 0x003F,
	0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047, 0x0048, 0x0049, 0x004A, 0x004B, 0x004C, 0x004D, 0x004E, 0x004F,
	0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057, 0x0058, 0x0059, 0x005A, 0x005B, 0x005C, 0x005D, 0x005E, 0x005F,
	0x0060, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067, 0x0068, 0x0069, 0x006A, 0x006B, 0x006C, 0x006D, 0x006E, 0x006F,
	0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077, 0x0078, 0x0079, 0x007A, 0x007B, 0x007C, 0x007D, 0x007E, 0x007F,
	0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021, 0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F,
	0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0xFFFF, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F,
	0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7, 0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407,
	0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7, 0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457,
	0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417, 0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F,
	0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427, 0x0428, 0x0429, 0x042A, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F,
	0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437, 0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F,
	0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447, 0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F,
};

const char16_t k866[256] = {
	0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007, 0x0008, 0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x000E, 0x000F,
	0x0010, 0x0011, 0x0012, 0x0013, 0x0014, 0x0015, 0x0016, 0x0017, 0x0018, 0x0019, 0x001A, 0x001B, 0x001C, 0x001D, 0x001E, 0x001F,
	0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027, 0x0028, 0x0029, 0x002A, 0x002B, 0x002C, 0x002D, 0x002E, 0x002F,
	0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037, 0x0038, 0x0039, 0x003A, 0x003B, 0x003C, 0x003D, 0x003E, 0x003F,
	0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047, 0x0048, 0x0049, 0x004A, 0x004B, 0x004C, 0x004D, 0x004E, 0x004F,
	0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057, 0x0058, 0x0059, 0x005A, 0x005B, 0x005C, 0x005D, 0x005E, 0x005F,
	0x0060, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067, 0x0068, 0x0069, 0x006A, 0x006B, 0x006C, 0x006D, 0x006E, 0x006F,
	0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077, 0x0078, 0x0079, 0x007A, 0x007B, 0x007C, 0x007D, 0x007E, 0x007F,
	0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417, 0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F,
	0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427, 0x0428, 0x0429, 0x042A, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F,
	0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437, 0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F,
	0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556, 0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
	0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F, 0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
	0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B, 0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
	0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447, 0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F,
	0x0401, 0x0451, 0x0404, 0x0454, 0x0407, 0x0457, 0x040E, 0x045E, 0x00B0, 0x2219, 0x00B7, 0x221A, 0x2116, 0x00A4, 0x25A0, 0x00A0,
};

// ISO-8859-1: every byte the code point of its own value.
constexpr std::array<char16_t, 256> k88591 = [] {
	std::array<char16_t, 256> table{};
	for (size_t i = 0; i < table.size(); ++i)
		table[i] = static_cast<char16_t>(i);
	return table;
}();

#ifdef __IBMSW__
// A page BY ITS NUMBER, converted by the system that owns it. A converter reached by NAME knows the names of some pages
// and not of others (737, 775, 857-869, 65001 as an ANSI page): asked for one it does not know it falls back to Latin-1
// without a word, and the file reads as garbage that looks like text. The number is what GetACP / GetOEMCP answer with,
// so nothing is spelled and nothing is guessed.
bool FromSystemPage(unsigned number, const unsigned char* bytes, size_t size, ibString& text)
{
	const char* const source = reinterpret_cast<const char*>(bytes);
	const int sourceLength = static_cast<int>(size);
	const int length = ::MultiByteToWideChar(number, MB_ERR_INVALID_CHARS, source, sourceLength, nullptr, 0);
	if (length <= 0)
		return false;
	std::wstring wide(static_cast<size_t>(length), L'\0');
	if (::MultiByteToWideChar(number, MB_ERR_INVALID_CHARS, source, sourceLength, wide.data(), length) != length)
		return false;
	text = ibString(wide.data(), wide.size());
	return true;
}

bool ToSystemPage(unsigned number, const ibString& text, ibMemoryBuffer& bytes)
{
	// UTF-8 and UTF-7 have bytes for everything and refuse to be asked whether a default was used.
	const bool lossy = number != CP_UTF8 && number != CP_UTF7;
	const DWORD flags = lossy ? WC_NO_BEST_FIT_CHARS : 0;
	const wchar_t* const source = text.wc_str();
	const int sourceLength = static_cast<int>(text.Len());
	BOOL usedDefault = FALSE;
	const int length = ::WideCharToMultiByte(number, flags, source, sourceLength, nullptr, 0,
		nullptr, lossy ? &usedDefault : nullptr);
	if (length <= 0 || usedDefault)
		return false;
	char* const at = static_cast<char*>(bytes.GetAppendBuf(static_cast<size_t>(length)));
	if (::WideCharToMultiByte(number, flags, source, sourceLength, at, length, nullptr, nullptr) != length)
		return false;
	bytes.UngetAppendBuf(static_cast<size_t>(length));
	return true;
}
#endif

} // namespace

ibCodePage ibCodePage::Find(const ibString& name)
{
	const ibString key = name.Lower();
	if (key == ibT("utf-8") || key == ibT("utf8"))
		return Of(kUtf8);
	if (key == ibT("utf-16") || key == ibT("utf16"))
		return Of(kUtf16);
	if (key == ibT("windows-1251") || key == ibT("cp1251"))
		return Of(kWindows1251);
	if (key == ibT("cp866") || key == ibT("ibm866"))
		return Of(kCp866);
	return ibCodePage();   // kUnknown: a page there is no table for
}

ibCodePage ibCodePage::Of(ibKind kind)
{
	ibCodePage page;
	switch (kind) {
	case kUtf8:
	case kUtf16:
		page.m_kind = kind;
		break;
	case kWindows1251:
		page.m_kind = kind;
		page.m_table = k1251;
		break;
	case kCp866:
		page.m_kind = kind;
		page.m_table = k866;
		break;
	case kLatin1:
		page.m_kind = kind;
		page.m_table = k88591.data();
		break;
	case kAnsi:
	case kOem:
#ifdef __IBMSW__
		page.m_kind = kind;
		page.m_number = kind == kAnsi ? ::GetACP() : ::GetOEMCP();
#else
		page.m_kind = kUtf8;
#endif
		break;
	case kUnknown:
	default:
		break;
	}
	return page;
}

ibString ibCodePage::Supported()
{
	return ibT("UTF-8, UTF-16, windows-1251, cp866");
}

ibString ibCodePage::Name() const
{
	switch (m_kind) {
	case kUtf8:         return ibT("UTF-8");
	case kUtf16:        return ibT("UTF-16");
	case kWindows1251:  return ibT("windows-1251");
	case kCp866:        return ibT("cp866");
	case kLatin1:       return ibT("ISO-8859-1");
	case kAnsi:         return ibT("ANSI");
	case kOem:          return ibT("OEM");
	case kUnknown:
	default:            return ibString();
	}
}

char16_t ibCodePage::ToChar(unsigned char byte) const
{
	if (m_table != nullptr)
		return m_table[byte];
	return byte < 0x80 ? static_cast<char16_t>(byte) : char16_t(0xFFFF);   // a UTF kind: only the ASCII half is one byte
}

bool ibCodePage::ToByte(char32_t ch, unsigned char& byte) const
{
	if (ch < 0x80) {
		byte = static_cast<unsigned char>(ch);
		return true;
	}
	// 0xFFFF is the table's own "no character here" - never a character a byte is asked for.
	if (m_table == nullptr || ch == 0xFFFF)
		return false;
	for (int i = 0x80; i <= 0xFF; ++i) {
		if (m_table[i] == ch) {
			byte = static_cast<unsigned char>(i);
			return true;
		}
	}
	return false;
}

bool ibCodePage::Holds(char32_t ch) const
{
	if (m_table != nullptr) {
		unsigned char byte = 0;
		return ToByte(ch, byte);
	}
	if (m_kind != kAnsi && m_kind != kOem)
		return m_kind != kUnknown;

	// The machine's page is asked by the one road it is written by.
	ibChar units[2] = { static_cast<ibChar>(ch), 0 };
	size_t count = 1;
	if (ch >= 0x10000) {
		units[0] = static_cast<ibChar>(0xD800 + ((ch - 0x10000) >> 10));
		units[1] = static_cast<ibChar>(0xDC00 + ((ch - 0x10000) & 0x3FF));
		count = 2;
	}
	ibMemoryBuffer bytes;
	return Encode(ibString(units, count), bytes);
}

bool ibCodePage::Decode(const ibMemoryBuffer& buffer, ibString& text) const
{
	text.clear();
	const unsigned char* bytes = static_cast<const unsigned char*>(buffer.GetData());
	size_t size = buffer.GetDataLen();

	bool bigEndian = false;
	size_t mark = 0;
	if (m_kind == kUtf8 && size >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) {
		mark = 3;
	}
	else if (m_kind == kUtf16 && size >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
		mark = 2;
	}
	else if (m_kind == kUtf16 && size >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF) {
		mark = 2;
		bigEndian = true;
	}
	if (size == mark)
		return m_kind != kUnknown;   // nothing, or a mark and nothing: an empty text
	bytes += mark;
	size -= mark;

	switch (m_kind) {
	case kUtf8:
		if (!ibIsUtf8(reinterpret_cast<const char*>(bytes), size))
			return false;
		text = ibString::FromUTF8(reinterpret_cast<const char*>(bytes), size);
		return true;
	case kUtf16: {
		if (size % 2 != 0)
			return false;   // half a character is not a letter
		std::u16string chars(size / 2, u'\0');
		for (size_t i = 0; i < chars.size(); i++) {
			const unsigned first = bytes[2 * i], second = bytes[2 * i + 1];
			chars[i] = static_cast<char16_t>(bigEndian ? (first << 8) | second : (second << 8) | first);
		}
		text = ibString(chars.data(), chars.size());
		return true;
	}
	case kWindows1251:
	case kCp866:
	case kLatin1: {
		std::u16string chars(size, u'\0');
		for (size_t i = 0; i < size; i++) {
			chars[i] = m_table[bytes[i]];
			if (chars[i] == 0xFFFF)
				return false;
		}
		text = ibString(chars.data(), chars.size());
		return true;
	}
	case kAnsi:
	case kOem:
#ifdef __IBMSW__
		return FromSystemPage(m_number, bytes, size, text);
#endif
	case kUnknown:
	default:
		return false;
	}
}

bool ibCodePage::Encode(const ibString& text, ibMemoryBuffer& bytes) const
{
	const ibChar* const chars = text.c_str();
	const size_t length = text.Len();
	if (length == 0)
		return m_kind != kUnknown;

	switch (m_kind) {
	case kUtf8: {
		const std::string utf8 = text.ToUtf8();
		bytes.AppendData(utf8.data(), utf8.size());
		return true;
	}
	case kUtf16: {
		char* const at = static_cast<char*>(bytes.GetAppendBuf(length * 2));
		for (size_t i = 0; i < length; i++) {
			at[2 * i] = static_cast<char>(chars[i] & 0xFF);
			at[2 * i + 1] = static_cast<char>((chars[i] >> 8) & 0xFF);
		}
		bytes.UngetAppendBuf(length * 2);
		return true;
	}
	case kWindows1251:
	case kCp866:
	case kLatin1: {
		// A surrogate is never in a table, so a character past the basic plane has no byte either.
		unsigned char* const at = static_cast<unsigned char*>(bytes.GetAppendBuf(length));
		for (size_t i = 0; i < length; i++) {
			if (!ToByte(chars[i], at[i]))
				return false;
		}
		bytes.UngetAppendBuf(length);
		return true;
	}
	case kAnsi:
	case kOem:
#ifdef __IBMSW__
		return ToSystemPage(m_number, text, bytes);
#endif
	case kUnknown:
	default:
		return false;
	}
}

void ibCodePage::Encode(const ibString& text, const ibString& who, ibMemoryBuffer& bytes) const
{
	if (m_kind == kUnknown)
		ibCoreException::Error(_("%s: no encoding to write in"), who);
	if (Encode(text, bytes))
		return;

	// Refused, so one character has no byte: named as the one character it is, a surrogate pair too.
	const ibChar* const chars = text.c_str();
	const size_t length = text.Len();
	for (size_t i = 0; i < length; i++) {
		char32_t ch = chars[i];
		if (ch >= 0xD800 && ch <= 0xDBFF && i + 1 < length && chars[i + 1] >= 0xDC00 && chars[i + 1] <= 0xDFFF)
			ch = 0x10000 + ((ch - 0xD800) << 10) + (chars[++i] - 0xDC00);
		if (!Holds(ch))
			ibCoreException::Error(_("%s: the character U+%04X has no byte in %s"), who, static_cast<unsigned int>(ch), Name());
	}
}
