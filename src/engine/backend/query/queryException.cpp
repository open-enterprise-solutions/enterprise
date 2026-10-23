#include "backend/query/queryException.h"

// Thrown by value, caught by const reference (project convention).

// ⭐ THE ENGINE'S DOOR FOR THE CORE'S PARSER: what the text could not be read as, raised as the engine raises it — so
// every handler that catches ibBackendQuerySyntaxException by type, and reads its span, still does.
void ibBackendQuerySyntaxException::Raise(const ibQueryRefusal& refusal)
{
	throw ibBackendQuerySyntaxException(refusal.GetErrorDescription(), refusal.m_currLine, refusal.m_currPos);
}

void ibBackendQueryException::Throw(Kind kind, const wxString& message)
{
	throw ibBackendQueryException(kind, message);
}

// The vararg pair mirrors ibBackendCoreException's exactly — same macro, same Do*Wchar / Do*Utf8
// split, so the format checking and the locale build modes behave identically here.

#if !wxUSE_UTF8_LOCALE_ONLY
void ibBackendQuerySyntaxException::DoErrorAtWchar(unsigned int line, unsigned int col, const ibChar* format, ...)
{
	va_list args;
	va_start(args, format);
	const wxString message = wxString::FormatV(format, args);
	va_end(args);

	throw ibBackendQuerySyntaxException(message, line, col);
}

void ibBackendQueryNameException::DoErrorAtWchar(unsigned int line, unsigned int col, const ibChar* format, ...)
{
	va_list args;
	va_start(args, format);
	const wxString message = wxString::FormatV(format, args);
	va_end(args);

	throw ibBackendQueryNameException(message, line, col);
}

void ibBackendQueryNameException::DoErrorWchar(const ibChar* format, ...)
{
	va_list args;
	va_start(args, format);
	const wxString message = wxString::FormatV(format, args);
	va_end(args);

	throw ibBackendQueryNameException(message, 0, 0);
}
#endif

#if wxUSE_UNICODE_UTF8
void ibBackendQuerySyntaxException::DoErrorAtUtf8(unsigned int line, unsigned int col, const ibChar* format, ...)
{
	va_list args;
	va_start(args, format);
	const wxString message = wxString::FormatV(format, args);
	va_end(args);

	throw ibBackendQuerySyntaxException(message, line, col);
}

void ibBackendQueryNameException::DoErrorAtUtf8(unsigned int line, unsigned int col, const ibChar* format, ...)
{
	va_list args;
	va_start(args, format);
	const wxString message = wxString::FormatV(format, args);
	va_end(args);

	throw ibBackendQueryNameException(message, line, col);
}

void ibBackendQueryNameException::DoErrorUtf8(const ibChar* format, ...)
{
	va_list args;
	va_start(args, format);
	const wxString message = wxString::FormatV(format, args);
	va_end(args);

	throw ibBackendQueryNameException(message, 0, 0);
}
#endif

#if !wxUSE_UTF8_LOCALE_ONLY
void ibBackendQueryLinqException::DoErrorWchar(const ibChar* format, ...)
{
	va_list args;
	va_start(args, format);
	const wxString message = wxString::FormatV(format, args);
	va_end(args);

	throw ibBackendQueryLinqException(message);
}
#endif

#if wxUSE_UNICODE_UTF8
void ibBackendQueryLinqException::DoErrorUtf8(const ibChar* format, ...)
{
	va_list args;
	va_start(args, format);
	const wxString message = wxString::FormatV(format, args);
	va_end(args);

	throw ibBackendQueryLinqException(message);
}
#endif
