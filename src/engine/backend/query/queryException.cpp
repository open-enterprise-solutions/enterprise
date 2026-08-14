#include "backend/query/queryException.h"

// Thrown by value, caught by const reference (project convention).

void ibBackendQueryException::Throw(Kind kind, const wxString& message)
{
	throw ibBackendQueryException(kind, message);
}

// The vararg pair mirrors ibBackendCoreException's exactly — same macro, same Do*Wchar / Do*Utf8
// split, so the format checking and the locale build modes behave identically here.

#if !wxUSE_UTF8_LOCALE_ONLY
void ibBackendQuerySourceException::DoErrorAtWchar(unsigned int line, unsigned int col, const wxChar* format, ...)
{
	va_list args;
	va_start(args, format);
	const wxString message = wxString::FormatV(format, args);
	va_end(args);

	throw ibBackendQuerySourceException(message, line, col);
}

void ibBackendQuerySourceException::DoErrorWchar(const wxChar* format, ...)
{
	va_list args;
	va_start(args, format);
	const wxString message = wxString::FormatV(format, args);
	va_end(args);

	throw ibBackendQuerySourceException(message, 0, 0);
}
#endif

#if wxUSE_UNICODE_UTF8
void ibBackendQuerySourceException::DoErrorAtUtf8(unsigned int line, unsigned int col, const wxChar* format, ...)
{
	va_list args;
	va_start(args, format);
	const wxString message = wxString::FormatV(format, args);
	va_end(args);

	throw ibBackendQuerySourceException(message, line, col);
}

void ibBackendQuerySourceException::DoErrorUtf8(const wxChar* format, ...)
{
	va_list args;
	va_start(args, format);
	const wxString message = wxString::FormatV(format, args);
	va_end(args);

	throw ibBackendQuerySourceException(message, 0, 0);
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
