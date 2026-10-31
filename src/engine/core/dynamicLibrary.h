#ifndef __CORE_DYNAMIC_LIBRARY_H__
#define __CORE_DYNAMIC_LIBRARY_H__

#include "core/core.h"

// A LIBRARY LOADED AT RUN TIME — a database's client (fbclient, libpq, odbc32), found by name and asked for its
// functions. Unloaded when this goes, and not copied: one handle, one owner.
class CORE_API ibDynamicLibrary {
public:

	ibDynamicLibrary() = default;
	~ibDynamicLibrary() { Unload(); }

	ibDynamicLibrary(const ibDynamicLibrary&) = delete;
	ibDynamicLibrary& operator=(const ibDynamicLibrary&) = delete;

	// The file a library's bare name is on this system: "fbclient" is fbclient.dll on Windows, libfbclient.so on
	// Linux and libfbclient.dylib on macOS — and the ending alone, what a folder of plugins is searched for.
	static ibString CanonicalizeName(const ibString& name);
	static ibString Extension();

	// By its path, or by a file name the system looks for in its own places. False when it could not be loaded;
	// a library loaded before is let go first.
	bool Load(const ibString& path);
	bool IsLoaded() const { return m_handle != nullptr; }
	void Unload();

	// A function of the library by its name, or nullptr when it has none — and whether it has one.
	void* GetSymbol(const ibString& name) const;
	bool HasSymbol(const ibString& name) const { return GetSymbol(name) != nullptr; }

private:

	void* m_handle = nullptr;
};

#endif // __CORE_DYNAMIC_LIBRARY_H__
