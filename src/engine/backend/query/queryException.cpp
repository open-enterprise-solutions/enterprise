#include "backend/query/queryException.h"
#include "core/query/queryRefusal.h"   // the core lexer and parser refuse through it

// ⭐ THE ENGINE'S RAISE FOR THE CORE'S QUERY PARSER: a text that does not read is ibBackendQuerySyntaxException here,
// as it was when the lexer and the parser were the engine's — every handler that catches it by type still does.
// Given when the library loads, so no parse runs before it.
namespace {

void RaiseQuerySyntax(unsigned int line, unsigned int col, const wxString& message)
{
	ibBackendQuerySyntaxException::ErrorAt(line, col, wxT("%s"), message);
}

[[maybe_unused]] const bool s_queryRefusalGiven = (ibQueryRefusal::SetRaise(&RaiseQuerySyntax), true);

} // namespace

// Thrown by value, caught by const reference (project convention).

void ibBackendQueryException::Throw(Kind kind, const wxString& message)
{
	throw ibBackendQueryException(kind, message);
}

// The vararg pair mirrors ibBackendCoreException's exactly — same macro, same Do*Wchar / Do*Utf8
// split, so the format checking and the locale build modes behave identically here.

#if !wxUSE_UTF8_LOCALE_ONLY
void ibBackendQuerySyntaxException::DoErrorAtWchar(unsigned int line, unsigned int col, const wxChar* format, ...)
{
	va_list args;
	va_start(args, format);
	const wxString message = wxString::FormatV(format, args);
	va_end(args);

	throw ibBackendQuerySyntaxException(message, line, col);
}

void ibBackendQueryNameException::DoErrorAtWchar(unsigned int line, unsigned int col, const wxChar* format, ...)
{
	va_list args;
	va_start(args, format);
	const wxString message = wxString::FormatV(format, args);
	va_end(args);

	throw ibBackendQueryNameException(message, line, col);
}

void ibBackendQueryNameException::DoErrorWchar(const wxChar* format, ...)
{
	va_list args;
	va_start(args, format);
	const wxString message = wxString::FormatV(format, args);
	va_end(args);

	throw ibBackendQueryNameException(message, 0, 0);
}
#endif

#if wxUSE_UNICODE_UTF8
void ibBackendQuerySyntaxException::DoErrorAtUtf8(unsigned int line, unsigned int col, const wxChar* format, ...)
{
	va_list args;
	va_start(args, format);
	const wxString message = wxString::FormatV(format, args);
	va_end(args);

	throw ibBackendQuerySyntaxException(message, line, col);
}

void ibBackendQueryNameException::DoErrorAtUtf8(unsigned int line, unsigned int col, const wxChar* format, ...)
{
	va_list args;
	va_start(args, format);
	const wxString message = wxString::FormatV(format, args);
	va_end(args);

	throw ibBackendQueryNameException(message, line, col);
}

void ibBackendQueryNameException::DoErrorUtf8(const wxChar* format, ...)
{
	va_list args;
	va_start(args, format);
	const wxString message = wxString::FormatV(format, args);
	va_end(args);

	throw ibBackendQueryNameException(message, 0, 0);
}
#endif

#if !wxUSE_UTF8_LOCALE_ONLY
void ibBackendQueryLinqException::DoErrorWchar(const wxChar* format, ...)
{
	va_list args;
	va_start(args, format);
	const wxString message = wxString::FormatV(format, args);
	va_end(args);

	throw ibBackendQueryLinqException(message);
}
#endif

#if wxUSE_UNICODE_UTF8
void ibBackendQueryLinqException::DoErrorUtf8(const wxChar* format, ...)
{
	va_list args;
	va_start(args, format);
	const wxString message = wxString::FormatV(format, args);
	va_end(args);

	throw ibBackendQueryLinqException(message);
}
#endif
