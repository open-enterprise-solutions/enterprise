#include "typeName.h"

#include <cstdlib>
#include <memory>
#if defined(__GNUG__)
#include <cxxabi.h>   // abi::__cxa_demangle — the class's name as written
#endif

wxString ibTypeInfoName(const std::type_info& type)
{
	// The compiler's name of it: MSVC's reads "class ibPropertyBoolean", GCC's and Clang's are mangled.
	const char* const raw = type.name();
#if defined(__GNUG__)
	int status = 0;
	std::unique_ptr<char, void (*)(void*)> demangled(abi::__cxa_demangle(raw, nullptr, nullptr, &status), std::free);
	wxString name = wxString::FromAscii(status == 0 ? demangled.get() : raw);
#else
	wxString name = wxString::FromAscii(raw);
	if (!name.StartsWith(wxT("class "), &name))
		name.StartsWith(wxT("struct "), &name);
#endif
	// …a template by its own name.
	return name.BeforeFirst(wxT('<'));
}
