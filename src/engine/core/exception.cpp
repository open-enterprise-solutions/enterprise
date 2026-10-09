#include "core/exception.h"

#include <cstdarg>

ibCoreException::ibCoreException(const wxString& description)
	: m_descriptionUtf8(description.utf8_string())
{
}

const char* ibCoreException::what() const noexcept
{
	// The stored bytes themselves — nothing is built here, which is what lets this be noexcept.
	return m_descriptionUtf8.c_str();
}

const wxString ibCoreException::GetErrorDescription() const
{
	return wxString::FromUTF8(m_descriptionUtf8);
}

#if !wxUSE_UTF8_LOCALE_ONLY
void ibCoreException::DoErrorWchar(const wxChar* format, ...)
{
	va_list args;
	va_start(args, format);
	const wxString description = wxString::FormatV(format, args);
	va_end(args);
	throw ibCoreException(description);
}
#endif

#if wxUSE_UNICODE_UTF8
void ibCoreException::DoErrorUtf8(const wxChar* format, ...)
{
	va_list args;
	va_start(args, format);
	const wxString description = wxString::FormatV(format, args);
	va_end(args);
	throw ibCoreException(description);
}
#endif
