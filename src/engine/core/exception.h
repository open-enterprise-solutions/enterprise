#ifndef _CORE_EXCEPTION_H__
#define _CORE_EXCEPTION_H__

#include <exception>
#include <string>

#include <wx/string.h>

#include "core/core.h"

// A REFUSAL — what was wrong, as text, and the base of every refusal of the platform: the engine's (ibBackendException,
// carrying the rest of what the engine knows of it) and the client's alike. The core raises it itself — a read that
// cannot go on (a block read past its end, a value of another kind than asked, a malformed text) — so a handler that
// tells a person what went wrong catches this one, and sees them all.
// what() is UTF-8, prepared at construction: it must be noexcept, so it cannot be the place where the string is built.
class CORE_API ibCoreException : public std::exception {
public:

	virtual ~ibCoreException() = default;

	const char* what() const noexcept override;
	const wxString GetErrorDescription() const;

	WX_DEFINE_VARARG_FUNC(static void, Error, 1, (const wxFormatString&), DoErrorWchar, DoErrorUtf8);

protected:

	explicit ibCoreException(const wxString& description);

private:

#if !wxUSE_UTF8_LOCALE_ONLY
	static void DoErrorWchar(const wxChar* format, ...);
#endif
#if wxUSE_UNICODE_UTF8
	static void DoErrorUtf8(const wxChar* format, ...);
#endif

	std::string m_descriptionUtf8;
};

#endif
