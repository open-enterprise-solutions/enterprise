#ifndef __JSON_TEXT_H__
#define __JSON_TEXT_H__

#include "core/core.h"

#include <wx/string.h>

#include <cstddef>
#include <string>

class ibString;   // core/fstring.h — the engine's text, written into

// ⭐ HOW A STRING IS SPELLED IN JSON - said ONCE. There were two escapers, the configuration's JSON view
// (jsonProvider.cpp) and the script's JSONWriter, each private to its file and already escaping different
// sets: a fix made in one would not have reached the other, and the two outputs would have drifted apart.
//
// What MUST be escaped is - the quote, the backslash, everything below U+0020 (with the short forms JSON has
// for five of them) - and so are U+2028 / U+2029, which are legal in JSON and end a line in JavaScript.
// Everything else goes out as itself: the text is Unicode here, and the bytes are whoever writes it out's
// business. A NUL inside the text is a character like any other (`\u0000`), not the end of it.
class CORE_API ibJsonText {
public:
	// `text` quoted and escaped, appended to `out`. The characters themselves are all it reads, so a runtime
	// string (ibString) is escaped where it lies rather than copied into a wxString first.
	static void AppendQuoted(wxString& out, const wchar_t* text, size_t length);
	static void AppendQuoted(wxString& out, const wxString& text) { AppendQuoted(out, text.wc_str(), text.length()); }

	// The same into the engine's text — a run of characters appended whole, nothing encoded.
	static void AppendQuoted(ibString& out, const wchar_t* text, size_t length);

	// The same as UTF-8 bytes - for a writer that builds its document in bytes: encoded as it is escaped, straight
	// into `out`, with no text of its own in between. A surrogate with no pair goes out as U+FFFD.
	static void AppendQuoted(std::string& out, const wchar_t* text, size_t length);

	// …AND READ BACK — the quoted text `at` points to (its opening quote), `at` left past its closing one; false when
	// it is not one or does not end. Unescaped is what the writers escape, and \/ \b \f besides, which a hand-edited
	// text may carry; \uXXXX is a UTF-16 unit, a surrogate pair joined. A text with no escape — nearly every one — is
	// taken whole where it lies: characters copied once, bytes decoded once.
	static bool ReadQuoted(const wchar_t*& at, const wchar_t* end, ibString& text);
	static bool ReadQuoted(const char*& at, const char* end, ibString& text);
};

#endif // !__JSON_TEXT_H__
