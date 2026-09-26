#ifndef __JSON_TEXT_H__
#define __JSON_TEXT_H__

#include "backend/backend.h"

#include <wx/string.h>

#include <string>

// ⭐ HOW A STRING IS SPELLED IN JSON - said ONCE. There were two escapers, the configuration's JSON view
// (jsonProvider.cpp) and the script's JSONWriter, each private to its file and already escaping different
// sets: a fix made in one would not have reached the other, and the two outputs would have drifted apart.
//
// What MUST be escaped is - the quote, the backslash, everything below U+0020 (with the short forms JSON has
// for five of them) - and so are U+2028 / U+2029, which are legal in JSON and end a line in JavaScript.
// Everything else goes out as itself: the text is Unicode here, and the bytes are whoever writes it out's
// business. A NUL inside the text is a character like any other (`\u0000`), not the end of it.
class BACKEND_API ibJsonText {
public:
	// `text` quoted and escaped, appended to `out`.
	static void AppendQuoted(wxString& out, const wxString& text);

	// The same as UTF-8 bytes - for a writer that builds its document in bytes.
	static std::string QuotedUtf8(const wxString& text);
};

#endif // !__JSON_TEXT_H__
