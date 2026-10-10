#ifndef __IB_FIBER_LOCALS_H__
#define __IB_FIBER_LOCALS_H__

// Per-fiber copies of the values a parked question must not leak into
// the next session that runs on the same OS thread.
//
// A fiber stays on the thread that created it, so OS-level TLS (the
// Firebird client, Win32 TLS) keeps its identity. That is not enough on
// its own. Several values are set on the way into a region and restored
// on the way out, and two sessions on one thread do not suspend in stack
// order: the restore-to-previous a scope would do on the way out restores
// the wrong session. Each switch saves the running fiber's set and
// installs the one that is about to run.
//
// Owners register a save/restore once, at static initialisation, from core
// or backend. The pool does not name them. A plugin or the file-base
// library must not register: the loader opens a ModuleLoadScope around
// the load and its initialize(), and a registration in that scope is
// refused and remembered. The loader then unloads the library. Nothing
// is inferred from a return address.
//
// A registration after the first snapshot, and outside any loading
// scope, aborts. That call is our own code. A slot that appeared late
// would be missing from fibers already parked, and the next session on
// the thread would see the previous one's values. The name is what the
// journal prints for the variable.

#include "core/core.h"

#include <wx/string.h>

#include <cstddef>
#include <memory>
#include <type_traits>

class CORE_API ibFiberLocals {
public:
	class CORE_API Snapshot {
	public:
		Snapshot() noexcept;
		~Snapshot();
		Snapshot(Snapshot&&) noexcept;
		Snapshot& operator=(Snapshot&&) noexcept;
		Snapshot(const Snapshot&) = delete;
		Snapshot& operator=(const Snapshot&) = delete;

		// Read the calling thread's registered values into this snapshot.
		void Capture();
		// Write them back, and activate this snapshot's per-fiber objects.
		// A null one (the scheduler's) means "the thread default".
		void Install() const;

		// Defined in fiberLocals.cpp. Public only so that translation
		// unit can name the type; the definition is not in this header.
		struct Impl;

	private:
		friend class ibFiberLocals;
		explicit Snapshot(std::unique_ptr<Impl> impl) noexcept;
		std::unique_ptr<Impl> m_impl;
	};

	// A value copied by save/restore. `construct`/`destroy` bracket the
	// storage inside the snapshot (placement new / explicit destructor).
	// `save` writes the calling thread's current value; `restore` copies
	// it back. All four run on the fiber's home thread. `name` is the
	// journal's name for the variable.
	static void Register(
		const char* name,
		std::size_t size,
		std::size_t align,
		void (*construct)(void* dst),
		void (*destroy)(void* dst),
		void (*save)(void* dst),
		void (*restore)(const void* src));

	template <typename T>
	static void RegisterTrivial(const char* name, void (*save)(void* dst), void (*restore)(const void* src))
	{
		static_assert(std::is_trivially_copyable<T>::value, "RegisterTrivial wants a trivial type");
		static_assert(std::is_trivially_destructible<T>::value, "RegisterTrivial wants a trivial type");
		Register(
			name,
			sizeof(T), alignof(T),
			[](void* dst) { new (dst) T(); },
			[](void* dst) { static_cast<T*>(dst)->~T(); },
			save, restore);
	}

	// One object per fiber, not per thread. The connection pool's
	// db_query pin is keyed by the holder's ADDRESS, so swapping the
	// bytes of the thread's holder would not move the pin. `create`
	// runs when a fiber snapshot is built; `activate` runs on install
	// (with nullptr for the scheduler); `destroy` runs on the home
	// thread after the fiber has unwound.
	static void RegisterPerFiber(
		const char* name,
		void* (*create)(),
		void (*destroy)(void* obj),
		void (*activate)(void* obj));

	// Open around a dynamic library's Load and its initialize(). While
	// one is open on this thread, every registration is refused and
	// recorded here. Nested scopes are a stack; the refusal is recorded
	// on the innermost. Destroy them in reverse order of construction.
	class CORE_API ModuleLoadScope {
	public:
		explicit ModuleLoadScope(const wxString& path);
		~ModuleLoadScope();
		ModuleLoadScope(const ModuleLoadScope&) = delete;
		ModuleLoadScope& operator=(const ModuleLoadScope&) = delete;

		bool Refused() const { return m_refused; }
		const wxString& Path() const { return m_path; }
		void Refuse() { m_refused = true; }

	private:
		ModuleLoadScope* m_previous;
		wxString         m_path;
		bool             m_refused;
	};

	// The calling thread's values, and no per-fiber objects: the
	// scheduler's snapshot.
	static Snapshot ForScheduler();
	// The calling thread's values, plus a fresh object per
	// RegisterPerFiber slot: a task fiber's snapshot.
	static Snapshot ForFiber();

	// Defined in fiberLocals.cpp, same as Snapshot::Impl.
	struct Registry;

private:
	static Registry& Get();
	// Get, with the layout fixed: the first call seals the registry.
	static Registry& Sealed();
};

#endif
