#ifndef _CORE_EXCEPTION_H__
#define _CORE_EXCEPTION_H__

#include <exception>
#include <string>
#include <system_error>

#include "core/core.h"
#include "core/fstring.h"

// A REFUSAL — what was wrong, as text, and the base of every refusal of the platform: the engine's (ibBackendException,
// carrying the rest of what the engine knows of it) and the client's alike. The core raises it itself — a read that
// cannot go on (a block read past its end, a value of another kind than asked, a malformed text) — so a handler that
// tells a person what went wrong catches this one, and sees them all.
// what() is UTF-8, prepared at construction: it must be noexcept, so it cannot be the place where the string is built.
class CORE_API ibCoreException : public std::exception {
public:

	virtual ~ibCoreException() = default;

	const char* what() const noexcept override;
	const ibString GetErrorDescription() const;

	// The refusal raised, its message made of its own arguments (ibString::Format).
	template <class... Args>
	[[noreturn]] static void Error(const ibString& format, const Args&... args) {
		throw ibCoreException(ibString::Format(format, args...));
	}

protected:

	explicit ibCoreException(const ibString& description);

private:

	std::string m_descriptionUtf8;
};

// WHY A CALL FAILED, as a refusal says it — UTF-8, as every one of them is read (ibString::FromUTF8). Not
// error_code::message(): on Windows that is the system's text in the ANSI code page.
std::string ibErrorText(const std::error_code& code);

#endif
