#include "core/dynamicLibrary.h"

#ifdef __IBMSW__
#include <windows.h>
#else
#include <dlfcn.h>
#endif

ibString ibDynamicLibrary::CanonicalizeName(const ibString& name)
{
#if defined(__IBMSW__)
	return name + Extension();
#else
	return ibT("lib") + name + Extension();
#endif
}

ibString ibDynamicLibrary::Extension()
{
#if defined(__IBMSW__)
	return ibT(".dll");
#elif defined(__IBOSX__)
	return ibT(".dylib");
#else
	return ibT(".so");
#endif
}

bool ibDynamicLibrary::Load(const ibString& path)
{
	Unload();
#ifdef __IBMSW__
	m_handle = static_cast<void*>(::LoadLibraryW(path.wc_str()));
#else
	m_handle = ::dlopen(path.ToUtf8().c_str(), RTLD_NOW);
#endif
	return m_handle != nullptr;
}

void ibDynamicLibrary::Unload()
{
	if (m_handle == nullptr)
		return;
#ifdef __IBMSW__
	::FreeLibrary(static_cast<HMODULE>(m_handle));
#else
	::dlclose(m_handle);
#endif
	m_handle = nullptr;
}

void* ibDynamicLibrary::GetSymbol(const ibString& name) const
{
	if (m_handle == nullptr)
		return nullptr;
	const std::string symbol = name.ToUtf8();   // a function's name is ASCII on every system
#ifdef __IBMSW__
	return reinterpret_cast<void*>(::GetProcAddress(static_cast<HMODULE>(m_handle), symbol.c_str()));
#else
	return ::dlsym(m_handle, symbol.c_str());
#endif
}
