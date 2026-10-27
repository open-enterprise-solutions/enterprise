#include "typeName.h"

#include <cstdlib>
#include <memory>
#if defined(__GNUG__)
#include <cxxabi.h>   // abi::__cxa_demangle — the class's name as written
#endif

ibString ibTypeInfoName(const std::type_info& type)
{
	// The compiler's name of it: MSVC's reads "class ibPropertyBoolean", GCC's and Clang's are mangled.
	const char* const raw = type.name();
#if defined(__GNUG__)
	int status = 0;
	std::unique_ptr<char, void (*)(void*)> demangled(abi::__cxa_demangle(raw, nullptr, nullptr, &status), std::free);
	ibString name = ibString::FromUTF8(status == 0 ? demangled.get() : raw);
#else
	ibString name = ibString::FromUTF8(raw);
	if (!name.StartsWith(ibT("class "), &name))
		name.StartsWith(ibT("struct "), &name);
#endif
	// …a template by its own name.
	return name.BeforeFirst(ibT('<'));
}
