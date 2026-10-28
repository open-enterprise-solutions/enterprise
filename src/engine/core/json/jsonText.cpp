////////////////////////////////////////////////////////////////////////////
//	Description : how a string is spelled in JSON
////////////////////////////////////////////////////////////////////////////

#include "core/json/jsonText.h"

#include "core/fstring.h"   // ibString — appended into

#include <cstdio>   // std::snprintf — a \u escape

// Over the character buffer, a RUN at a time: a checked build registers every string iterator under one global
// lock, and a megabyte of text is a million of them and a million one-character appends.
void ibJsonText::AppendQuoted(ibString& out, const ibChar* text, size_t length)
{
	const ibChar* const begin = text;
	const ibChar* const end = begin + length;

	out += ibT("\"");
	const ibChar* run = begin;
	for (const ibChar* p = begin; p != end; ++p) {
		const unsigned long c = static_cast<unsigned long>(*p);
		const ibChar* escape = nullptr;
		switch (c) {
		case '"':  escape = ibT("\\\""); break;
		case '\\': escape = ibT("\\\\"); break;
		case '\b': escape = ibT("\\b"); break;
		case '\f': escape = ibT("\\f"); break;
		case '\n': escape = ibT("\\n"); break;
		case '\r': escape = ibT("\\r"); break;
		case '\t': escape = ibT("\\t"); break;
		default:
			if (c >= 0x20 && c != 0x2028 && c != 0x2029)
				continue;   // goes out as itself, with the run it is part of
		}
		if (p != run)
			out.append(run, static_cast<size_t>(p - run));
		if (escape != nullptr) {
			out += escape;
		}
		else {
			char unit[8];
			std::snprintf(unit, sizeof(unit), "\\u%04x", static_cast<unsigned>(c));
			for (const char* u = unit; *u != 0; ++u)
				out += static_cast<ibChar>(*u);
		}
		run = p + 1;
	}
	if (end != run)
		out.append(run, static_cast<size_t>(end - run));
	out += ibT("\"");
}

namespace {

void AppendUtf8(std::string& out, unsigned long c)
{
	if (c < 0x80) {
		out += static_cast<char>(c);
	}
	else if (c < 0x800) {
		out += static_cast<char>(0xC0 | (c >> 6));
		out += static_cast<char>(0x80 | (c & 0x3F));
	}
	else if (c < 0x10000) {
		out += static_cast<char>(0xE0 | (c >> 12));
		out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
		out += static_cast<char>(0x80 | (c & 0x3F));
	}
	else {
		out += static_cast<char>(0xF0 | (c >> 18));
		out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
		out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
		out += static_cast<char>(0x80 | (c & 0x3F));
	}
}

} // namespace

// The same escapes as the wide writer above; what goes out as itself is encoded on the way. A run of ASCII — most
// of what a protocol says — is appended whole.
void ibJsonText::AppendQuoted(std::string& out, const ibChar* text, size_t length)
{
	const ibChar* const end = text + length;

	out += '"';
	const ibChar* run = text;
	auto flush = [&out, &run](const ibChar* upto) {
		for (; run != upto; ++run)
			out += static_cast<char>(*run);
	};
	for (const ibChar* p = text; p != end; ++p) {
		unsigned long c = static_cast<unsigned long>(*p);
		const char* escape = nullptr;
		switch (c) {
		case '"':  escape = "\\\""; break;
		case '\\': escape = "\\\\"; break;
		case '\b': escape = "\\b"; break;
		case '\f': escape = "\\f"; break;
		case '\n': escape = "\\n"; break;
		case '\r': escape = "\\r"; break;
		case '\t': escape = "\\t"; break;
		default:
			if (c >= 0x20 && c < 0x80)
				continue;   // ASCII goes out as itself, with the run it is part of
		}
		flush(p);
		run = p + 1;
		if (escape != nullptr) {
			out += escape;
			continue;
		}
		if (c < 0x20 || c == 0x2028 || c == 0x2029) {
			char unit[8];
			std::snprintf(unit, sizeof(unit), "\\u%04x", static_cast<unsigned>(c));
			out += unit;
			continue;
		}
		// A UTF-16 pair is one character.
		if (c >= 0xD800 && c <= 0xDBFF && p + 1 != end
			&& static_cast<unsigned long>(p[1]) >= 0xDC00 && static_cast<unsigned long>(p[1]) <= 0xDFFF) {
			c = 0x10000 + ((c - 0xD800) << 10) + (static_cast<unsigned long>(p[1]) - 0xDC00);
			run = ++p + 1;
		}
		else if (c >= 0xD800 && c <= 0xDFFF) {
			c = 0xFFFD;
		}
		AppendUtf8(out, c);
	}
	flush(end);
	out += '"';
}

namespace {

// A text of the source, as the engine's: bytes decoded straight into it, characters copied once.
void Assign(ibString& out, const char* p, size_t n) { out.SetUtf8(p, n); }
void Assign(ibString& out, const ibChar* p, size_t n) { out = ibString(p, n); }

// A character an escape names, added to a text of the source's own kind.
void AppendCodePoint(std::string& text, unsigned long cp)
{
	AppendUtf8(text, cp);
}
void AppendCodePoint(std::u16string& text, unsigned long cp)
{
	if (cp > 0xFFFF) {
		text += static_cast<ibChar>(0xD800 + ((cp - 0x10000) >> 10));
		text += static_cast<ibChar>(0xDC00 + ((cp - 0x10000) & 0x3FF));
	}
	else {
		text += static_cast<ibChar>(cp);
	}
}

template<class Char>
bool ReadQuotedOf(const Char*& at, const Char* end, ibString& out)
{
	const Char* p = at;
	if (p == end || *p != Char('\"'))
		return false;
	++p;

	const Char* const start = p;
	while (p != end && *p != Char('\"') && *p != Char('\\'))
		++p;
	if (p == end)
		return false;   // unterminated
	if (*p == Char('\"')) {
		Assign(out, start, static_cast<size_t>(p - start));
		at = p + 1;
		return true;
	}

	std::basic_string<Char> text(start, static_cast<size_t>(p - start));
	const auto hex4 = [&p]() -> long {
		long v = 0;
		for (int i = 0; i < 4; i++) {
			const Char h = *p++;
			v <<= 4;
			if      (h >= '0' && h <= '9') v |= (h - '0');
			else if (h >= 'a' && h <= 'f') v |= (h - 'a' + 10);
			else if (h >= 'A' && h <= 'F') v |= (h - 'A' + 10);
			else return -1;
		}
		return v;
	};
	while (p != end) {
		const Char c = *p++;
		if (c == Char('\"')) {
			Assign(out, text.data(), text.size());
			at = p;
			return true;
		}
		if (c != Char('\\')) { text += c; continue; }
		if (p == end) return false;
		const Char e = *p++;
		switch (e) {
		case '\"': text += Char('\"'); break;
		case '\\': text += Char('\\'); break;
		case '/':  text += Char('/');  break;
		case 'b':  text += Char('\b'); break;
		case 'f':  text += Char('\f'); break;
		case 'n':  text += Char('\n'); break;
		case 'r':  text += Char('\r'); break;
		case 't':  text += Char('\t'); break;
		case 'u': {
			if (end - p < 4) return false;
			const long hi = hex4();
			if (hi < 0) return false;
			unsigned long cp = static_cast<unsigned long>(hi);
			if (hi >= 0xD800 && hi <= 0xDBFF && end - p >= 6 && p[0] == Char('\\') && p[1] == Char('u')) {
				p += 2;
				const long lo = hex4();
				if (lo < 0) return false;
				cp = 0x10000 + ((static_cast<unsigned long>(hi) - 0xD800) << 10) + (static_cast<unsigned long>(lo) - 0xDC00);
			}
			AppendCodePoint(text, cp);
			break;
		}
		default: return false;
		}
	}
	return false;   // unterminated
}

} // namespace

bool ibJsonText::ReadQuoted(const ibChar*& at, const ibChar* end, ibString& text)
{
	return ReadQuotedOf(at, end, text);
}

bool ibJsonText::ReadQuoted(const char*& at, const char* end, ibString& text)
{
	return ReadQuotedOf(at, end, text);
}
