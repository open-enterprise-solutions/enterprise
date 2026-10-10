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
// Owners register a save/restore once, at static initialisation. The pool
// does not name them. Registration after the first snapshot is a fault:
// a slot that appears late would be missing from fibers already parked.

#include "core/core.h"

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
	// it back. All four run on the fiber's home thread.
	static void Register(
		std::size_t size,
		std::size_t align,
		void (*construct)(void* dst),
		void (*destroy)(void* dst),
		void (*save)(void* dst),
		void (*restore)(const void* src));

	template <typename T>
	static void RegisterTrivial(void (*save)(void* dst), void (*restore)(const void* src))
	{
		static_assert(std::is_trivially_copyable<T>::value, "RegisterTrivial wants a trivial type");
		static_assert(std::is_trivially_destructible<T>::value, "RegisterTrivial wants a trivial type");
		Register(
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
		void* (*create)(),
		void (*destroy)(void* obj),
		void (*activate)(void* obj));

	// A scope a question must not be inside. `isClear` is true when the
	// value is at rest. Await asks before it parks; a false answer is a
	// logic error, because the next fiber on the thread would see it and
	// the scope's own restore would write that fiber's previous value.
	// Registered at static init, with the slots.
	static void RegisterMustBeClear(bool (*isClear)(), const char* what);
	static void AssertClear();

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
