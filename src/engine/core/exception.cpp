#include "core/exception.h"

#include <cstring>

#ifdef __IBMSW__
#include <windows.h>
#endif

ibCoreException::ibCoreException(const ibString& description)
	: m_descriptionUtf8(description.ToUtf8())
{
}

const char* ibCoreException::what() const noexcept
{
	// The stored bytes themselves — nothing is built here, which is what lets this be noexcept.
	return m_descriptionUtf8.c_str();
}

const ibString ibCoreException::GetErrorDescription() const
{
	return ibString::FromUTF8(m_descriptionUtf8.data(), m_descriptionUtf8.size());
}

std::string ibErrorText(const std::error_code& code)
{
#ifdef __IBMSW__
	// The standard library asks FormatMessageA: the ANSI code page, Cyrillic on a Russian Windows, which read as UTF-8
	// is nothing at all — "Assistant access did not start: ." (2026-10-23). Asked here in Unicode. So for asio's,
	// under a category of its own ("asio.system") for the same codes — every socket's error is of it: "the server's
	// socket dropped: " said nothing more (2026-10-24, a cluster's server killed).
	if (code.category() == std::system_category() || std::strcmp(code.category().name(), "asio.system") == 0) {
		wchar_t* text = nullptr;
		const DWORD length = ::FormatMessageW(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
			static_cast<DWORD>(code.value()), 0, reinterpret_cast<wchar_t*>(&text), 0, nullptr);
		ibString said = text != nullptr ? ibString(text, length) : ibString();
		::LocalFree(text);
		return said.Trim().ToUtf8();   // the system ends its sentence with a line break
	}
#endif
	return code.message();
}
