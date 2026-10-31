// =============================================================================
// The single-byte code pages XML is written and read in - windows-1251 above all, which the tax service's
// format requires. The expected tables are Python's codecs, pasted as literals: the reference is computed
// OUTSIDE the mechanism under test.
// =============================================================================

#include <gtest/gtest.h>

#include <initializer_list>

#include "core/xml/xmlText.h"

namespace {
// python3 -c "import sys; c=sys.argv[1]; print(', '.join('0x%04X' % ord(bytes([b]).decode(c,'replace')) for b in range(128,256)).replace('0xFFFD','0xFFFF'))" cp1251
// (and cp866) - the upper half; the lower half is ASCII in both pages.
const char16_t kPython1251Upper[128] = {
	0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021,
	0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F,
	0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
	0xFFFF, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F,
	0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7,
	0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407,
	0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7,
	0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457,
	0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417,
	0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F,
	0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427,
	0x0428, 0x0429, 0x042A, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F,
	0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437,
	0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F,
	0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447,
	0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F,
};
const char16_t kPython866Upper[128] = {
	0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417,
	0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F,
	0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427,
	0x0428, 0x0429, 0x042A, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F,
	0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437,
	0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F,
	0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
	0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
	0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,
	0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
	0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
	0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
	0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447,
	0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F,
	0x0401, 0x0451, 0x0404, 0x0454, 0x0407, 0x0457, 0x040E, 0x045E,
	0x00B0, 0x2219, 0x00B7, 0x221A, 0x2116, 0x00A4, 0x25A0, 0x00A0,
};
}

TEST(XmlCodePage, NamesAreFoundWhateverTheirCase) {
	EXPECT_EQ(ibXmlCodePage::Find(ibT("windows-1251")).Kind(), ibXmlCodePage::kWindows1251);
	EXPECT_EQ(ibXmlCodePage::Find(ibT("CP1251")).Kind(), ibXmlCodePage::kWindows1251);
	EXPECT_EQ(ibXmlCodePage::Find(ibT("WINDOWS-1251")).Kind(), ibXmlCodePage::kWindows1251);
	EXPECT_EQ(ibXmlCodePage::Find(ibT("ibm866")).Kind(), ibXmlCodePage::kCp866);
	EXPECT_EQ(ibXmlCodePage::Find(ibT("CP866")).Kind(), ibXmlCodePage::kCp866);
	EXPECT_EQ(ibXmlCodePage::Find(ibT("UTF-8")).Kind(), ibXmlCodePage::kUtf8);
	EXPECT_EQ(ibXmlCodePage::Find(ibT("utf8")).Kind(), ibXmlCodePage::kUtf8);
	EXPECT_EQ(ibXmlCodePage::Find(ibT("UTF-16")).Kind(), ibXmlCodePage::kUtf16);
	EXPECT_EQ(ibXmlCodePage::Find(ibT("koi8-u")).Kind(), ibXmlCodePage::kUnknown);
}

TEST(XmlCodePage, EveryByteOfBothPagesIsTheReferenceCharacter) {
	for (int b = 0; b < 256; ++b) {
		const char16_t want1251 = b < 0x80 ? static_cast<char16_t>(b) : kPython1251Upper[b - 0x80];
		const char16_t want866  = b < 0x80 ? static_cast<char16_t>(b) : kPython866Upper[b - 0x80];
		EXPECT_EQ(ibXmlCodePage::Find(ibT("windows-1251")).ToChar(static_cast<unsigned char>(b)), want1251) << "byte " << b;
		EXPECT_EQ(ibXmlCodePage::Find(ibT("cp866")).ToChar(static_cast<unsigned char>(b)), want866) << "byte " << b;
	}
}

TEST(XmlCodePage, ACharacterGoesBackToItsByteOrSaysItHasNone) {
	const ibXmlCodePage page = ibXmlCodePage::Find(ibT("windows-1251"));
	unsigned char byte = 0;
	ASSERT_TRUE(page.ToByte(0x0404, byte)); EXPECT_EQ(byte, 0xAA);   // U+0404
	ASSERT_TRUE(page.ToByte(0x20AC, byte)); EXPECT_EQ(byte, 0x88);   // €
	ASSERT_TRUE(page.ToByte(0x0456, byte)); EXPECT_EQ(byte, 0xB3);   // U+0456
	EXPECT_FALSE(page.ToByte(0x4E2D, byte));                        // 中 - not in the page
}

TEST(XmlCodePage, AsciiIsTheSameByteAndCharacterOnEveryPage) {
	for (int b = 0; b < 0x80; ++b) {
		for (const ibString& name : { ibT("UTF-8"), ibT("UTF-16"), ibT("windows-1251"), ibT("cp866") }) {
			const ibXmlCodePage page = ibXmlCodePage::Find(name);
			EXPECT_EQ(page.ToChar(static_cast<unsigned char>(b)), static_cast<char16_t>(b));
			unsigned char byte = 0xFF;
			ASSERT_TRUE(page.ToByte(static_cast<char32_t>(b), byte));
			EXPECT_EQ(byte, static_cast<unsigned char>(b));
		}
	}
}

TEST(XmlCodePage, OnlyTheTablePagesAreSingleByte) {
	EXPECT_TRUE(ibXmlCodePage::Find(ibT("windows-1251")).IsSingleByte());
	EXPECT_TRUE(ibXmlCodePage::Find(ibT("cp866")).IsSingleByte());
	EXPECT_FALSE(ibXmlCodePage::Find(ibT("UTF-8")).IsSingleByte());
	EXPECT_FALSE(ibXmlCodePage::Find(ibT("UTF-16")).IsSingleByte());
	EXPECT_FALSE(ibXmlCodePage::Find(ibT("koi8-u")).IsSingleByte());
}

TEST(XmlCodePage, NameIsWhatTheDeclarationWrites) {
	EXPECT_EQ(ibXmlCodePage::Find(ibT("utf8")).Name(), ibT("UTF-8"));
	EXPECT_EQ(ibXmlCodePage::Find(ibT("utf-16")).Name(), ibT("UTF-16"));
	EXPECT_EQ(ibXmlCodePage::Find(ibT("cp1251")).Name(), ibT("windows-1251"));
	EXPECT_EQ(ibXmlCodePage::Find(ibT("ibm866")).Name(), ibT("cp866"));
}

TEST(XmlCodePage, SupportedListsAllFourByTheirCanonicalName) {
	EXPECT_EQ(ibXmlCodePage::Supported(), ibT("UTF-8, UTF-16, windows-1251, cp866"));
}

TEST(XmlCodePage, EveryCharacterOfAPageGoesBackToTheByteItCameFrom) {
	for (const ibString& name : { ibString(ibT("windows-1251")), ibString(ibT("cp866")) }) {
		const ibXmlCodePage page = ibXmlCodePage::Find(name);
		for (int b = 0; b < 256; ++b) {
			const char16_t ch = page.ToChar(static_cast<unsigned char>(b));
			if (ch == 0xFFFF) {
				continue;
			}
			unsigned char back = 0;
			ASSERT_TRUE(page.ToByte(ch, back)) << name << " byte " << b;
			EXPECT_EQ(back, b);
		}
		unsigned char byte = 0;
		EXPECT_FALSE(page.ToByte(0xFFFF, byte)) << name;   // the sentinel is never a character
	}
	unsigned char byte = 0;
	EXPECT_FALSE(ibXmlCodePage::Find(ibT("UTF-8")).ToByte(0x0410, byte));
}
