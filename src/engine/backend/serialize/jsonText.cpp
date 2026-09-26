////////////////////////////////////////////////////////////////////////////
//	Description : how a string is spelled in JSON
////////////////////////////////////////////////////////////////////////////

#include "jsonText.h"

// Over the character buffer, a RUN at a time: a checked build registers every string iterator under one global
// lock, and a megabyte of text is a million of them and a million one-character appends.
void ibJsonText::AppendQuoted(wxString& out, const wxString& text)
{
	const wchar_t* const begin = text.wc_str();
	const wchar_t* const end = begin + text.length();

	out += wxT('"');
	const wchar_t* run = begin;
	for (const wchar_t* p = begin; p != end; ++p) {
		const unsigned long c = static_cast<unsigned long>(*p);
		const wxChar* escape = nullptr;
		switch (c) {
		case '"':  escape = wxT("\\\""); break;
		case '\\': escape = wxT("\\\\"); break;
		case '\b': escape = wxT("\\b"); break;
		case '\f': escape = wxT("\\f"); break;
		case '\n': escape = wxT("\\n"); break;
		case '\r': escape = wxT("\\r"); break;
		case '\t': escape = wxT("\\t"); break;
		default:
			if (c >= 0x20 && c != 0x2028 && c != 0x2029)
				continue;   // goes out as itself, with the run it is part of
		}
		if (p != run)
			out.append(run, static_cast<size_t>(p - run));
		if (escape != nullptr)
			out += escape;
		else
			out += wxString::Format(wxT("\\u%04x"), static_cast<unsigned>(c));
		run = p + 1;
	}
	if (end != run)
		out.append(run, static_cast<size_t>(end - run));
	out += wxT('"');
}

std::string ibJsonText::QuotedUtf8(const wxString& text)
{
	wxString quoted;
	quoted.reserve(text.length() + 2);
	AppendQuoted(quoted, text);
	const wxScopedCharBuffer utf8 = quoted.utf8_str();
	return std::string(utf8.data(), utf8.length());
}
