#ifndef __CORE_CODE_PAGE_H__
#define __CORE_CODE_PAGE_H__

// THE RULES A TEXT BECOMES BYTES BY, AND BYTES A TEXT - one class for every road: a file a script reads and writes
// (TextReader / TextWriter), a body that goes over HTTP, BinaryData spelled from a text, an XML file in the encoding its
// declaration names, the session's temporary files.
//
// Two kinds of page. A page by NAME (Find) is a fixed table, the same bytes on every machine: windows-1251 is what a tax
// return's format demands everywhere - which is why it is not the machine's code page, and not a converter that falls
// back to Latin-1 without a word on a name it does not know. A page of the MACHINE (Of: kAnsi, kOem) is asked of the
// system by its number: a base moved from one machine to another reads the files of the machine it runs on. Elsewhere
// than Windows a system has one encoding, and it is UTF-8.

#include "core/core.h"
#include "core/fstring.h"               // ibString - the text
#include "core/memory/memoryBuffer.h"   // ibMemoryBuffer - its bytes

class CORE_API ibCodePage {
public:
	enum ibKind { kUnknown, kUtf8, kUtf16, kWindows1251, kCp866, kLatin1, kAnsi, kOem };

	static ibCodePage Find(const ibString& name);   // case-insensitive; aliases cp1251, IBM866, utf8, utf16; never the machine's
	// kAnsi / kOem: the machine's pages (UTF-8 elsewhere than Windows). kLatin1 (ISO-8859-1, a byte is the code point it
	// spells - what wxString::From8BitData read) is by kind only: an HTTP header that is not UTF-8; no name finds it, so
	// an XML file or a script's text file does not take it as a page to write in.
	static ibCodePage Of(ibKind kind);
	// What the XML writer can produce and its reader accepts; the reader also takes ISO-8859-1 and US-ASCII, which
	// expat decodes by itself.
	static ibString Supported();

	ibKind   Kind() const { return m_kind; }
	ibString Name() const;                             // what a declaration writes

	// Meaningful only when IsSingleByte() - a UTF kind answers the ASCII half only.
	bool     IsSingleByte() const { return m_table != nullptr; }
	char16_t ToChar(unsigned char byte) const;         // 0xFFFF where the page defines no character
	// False: the page has no byte for this code point - the XML writer then falls back to a `&#N;` reference.
	bool     ToByte(char32_t ch, unsigned char& byte) const;
	bool     Holds(char32_t ch) const;                 // a UTF kind holds every character

	// BYTES -> THE TEXT THEY SPELL. A byte-order mark (UTF-8; UTF-16 either way round, which the mark says) is read as a
	// mark and never handed over: it would be an invisible first character, and the first column of a CSV would never
	// equal the name it is compared with. STRICT: bytes the page does not spell - UTF-8 that is not well-formed, an odd
	// byte of UTF-16, a byte the table has no character for - are false, not a text with holes in it.
	bool     Decode(const ibMemoryBuffer& bytes, ibString& text) const;

	// THE TEXT -> ITS BYTES, appended to `bytes` - UTF-16 little-endian, and never a byte-order mark: a mark is a file's,
	// put there by the file's writer. False: a character the page has no byte for (`bytes` then as it was) - a text is
	// not written with a question mark where a letter should stand.
	bool     Encode(const ibString& text, ibMemoryBuffer& bytes) const;
	// ...or refused, `who` beginning the sentence: the XML writer has made every such character a reference already,
	// so only what a script wrote raw can be one.
	void     Encode(const ibString& text, const ibString& who, ibMemoryBuffer& bytes) const;

private:
	ibKind          m_kind = kUnknown;
	const char16_t* m_table = nullptr;   // every character either table holds is in the basic plane
	unsigned        m_number = 0;        // kAnsi / kOem: the page's number, as GetACP / GetOEMCP answer
};

#endif
