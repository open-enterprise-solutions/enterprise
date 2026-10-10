#include "core/fiber/fiberLocals.h"
#include "core/exception.h"

#include <wx/intl.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <new>
#include <utility>
#include <vector>

namespace {

struct ibFiberSlot {
	std::size_t offset = 0;
	std::size_t size = 0;
	std::size_t align = 0;
	void (*construct)(void*) = nullptr;
	void (*destroy)(void*) = nullptr;
	void (*save)(void*) = nullptr;
	void (*restore)(const void*) = nullptr;
};

struct ibFiberObjectSlot {
	void* (*create)() = nullptr;
	void (*destroy)(void*) = nullptr;
	void (*activate)(void*) = nullptr;
};

struct PodDeleter {
	std::size_t align = alignof(std::max_align_t);
	void operator()(unsigned char* p) const noexcept
	{
		if (p != nullptr)
			::operator delete(p, std::align_val_t(align));
	}
};

} // namespace

struct ibFiberLocals::Snapshot::Impl {
	std::unique_ptr<unsigned char[], PodDeleter> pod;
	// One per RegisterPerFiber slot. The scheduler's are all null: it has no objects of its own, and null is what
	// activate is told for "the thread default" and what the destructor skips.
	std::vector<void*> objects;
};

// ⭐ WRITTEN UNTIL THE FIRST SNAPSHOT, READ-ONLY AFTER IT. A registration after the seal aborts, so once `sealed` is
// set the vectors never change and every switch reads them without the lock — a switch happens at every task start,
// every park and every wake, on every worker, and a process-wide mutex there would serialise them all.
struct ibClearCheck {
	bool (*isClear)() = nullptr;
	const char* what = nullptr;
};

struct ibFiberLocals::Registry {
	std::mutex mutex;
	std::vector<ibFiberSlot> slots;
	std::vector<ibFiberObjectSlot> objects;
	std::vector<ibClearCheck> clearChecks;
	std::size_t podSize = 0;
	std::size_t podAlign = alignof(std::max_align_t);
	std::atomic<bool> sealed{ false };
};

ibFiberLocals::Registry& ibFiberLocals::Get()
{
	static Registry registry;
	return registry;
}

// The layout is fixed once, under the lock, by whoever comes first; the release store publishes it.
ibFiberLocals::Registry& ibFiberLocals::Sealed()
{
	Registry& registry = Get();
	if (registry.sealed.load(std::memory_order_acquire))
		return registry;
	std::lock_guard<std::mutex> lk(registry.mutex);
	if (registry.sealed.load(std::memory_order_relaxed))
		return registry;
	std::size_t offset = 0;
	std::size_t align = alignof(std::max_align_t);
	for (ibFiberSlot& slot : registry.slots) {
		if (slot.align == 0)
			slot.align = 1;
		if (slot.align > align)
			align = slot.align;
		offset = (offset + slot.align - 1) & ~(slot.align - 1);
		slot.offset = offset;
		offset += slot.size;
	}
	registry.podSize = offset;
	registry.podAlign = align;
	registry.sealed.store(true, std::memory_order_release);
	return registry;
}

namespace {

void ConstructAndSave(const ibFiberLocals::Registry& registry, ibFiberLocals::Snapshot::Impl& impl)
{
	if (registry.podSize == 0)
		return;
	void* mem = ::operator new(registry.podSize, std::align_val_t(registry.podAlign));
	impl.pod = std::unique_ptr<unsigned char[], PodDeleter>(
		static_cast<unsigned char*>(mem), PodDeleter{ registry.podAlign });
	for (const ibFiberSlot& slot : registry.slots)
		slot.construct(impl.pod.get() + slot.offset);
	for (const ibFiberSlot& slot : registry.slots)
		slot.save(impl.pod.get() + slot.offset);
}

void RefuseLateRegistration(const ibFiberLocals::Registry& registry, const char* what)
{
	if (!registry.sealed.load(std::memory_order_acquire))
		return;
	std::fprintf(stderr, "ibFiberLocals::%s after the first fiber snapshot\n", what);
	std::abort();
}

} // namespace

void ibFiberLocals::Register(
	std::size_t size, std::size_t align,
	void (*construct)(void*), void (*destroy)(void*),
	void (*save)(void*), void (*restore)(const void*))
{
	Registry& registry = Get();
	std::lock_guard<std::mutex> lk(registry.mutex);
	RefuseLateRegistration(registry, "Register");
	ibFiberSlot slot;
	slot.size = size;
	slot.align = align;
	slot.construct = construct;
	slot.destroy = destroy;
	slot.save = save;
	slot.restore = restore;
	registry.slots.push_back(slot);
}

void ibFiberLocals::RegisterPerFiber(
	void* (*create)(), void (*destroy)(void*), void (*activate)(void*))
{
	Registry& registry = Get();
	std::lock_guard<std::mutex> lk(registry.mutex);
	RefuseLateRegistration(registry, "RegisterPerFiber");
	ibFiberObjectSlot slot;
	slot.create = create;
	slot.destroy = destroy;
	slot.activate = activate;
	registry.objects.push_back(slot);
}

void ibFiberLocals::RegisterMustBeClear(bool (*isClear)(), const char* what)
{
	Registry& registry = Get();
	std::lock_guard<std::mutex> lk(registry.mutex);
	RefuseLateRegistration(registry, "RegisterMustBeClear");
	registry.clearChecks.push_back(ibClearCheck{ isClear, what != nullptr ? what : "a scope" });
}

void ibFiberLocals::AssertClear()
{
	// Sealed() takes the registry lock only until the first snapshot.
	// After that the check list is fixed and this read does not lock.
	const Registry& registry = Sealed();
	for (const ibClearCheck& check : registry.clearChecks) {
		if (check.isClear != nullptr && !check.isClear())
			ibCoreException::Error(_("a fiber cannot park while %s is set"), wxString::FromUTF8(check.what));
	}
}

ibFiberLocals::Snapshot::Snapshot() noexcept = default;

ibFiberLocals::Snapshot::Snapshot(std::unique_ptr<Impl> impl) noexcept
	: m_impl(std::move(impl))
{
}

ibFiberLocals::Snapshot::Snapshot(Snapshot&&) noexcept = default;

ibFiberLocals::Snapshot& ibFiberLocals::Snapshot::operator=(Snapshot&&) noexcept = default;

ibFiberLocals::Snapshot::~Snapshot()
{
	if (m_impl == nullptr)
		return;
	// A snapshot exists only after the seal, so the registry is read-only here; it is destroyed on its fiber's
	// home thread.
	const Registry& registry = Get();
	for (std::size_t i = 0; i < m_impl->objects.size() && i < registry.objects.size(); ++i) {
		if (m_impl->objects[i] != nullptr)
			registry.objects[i].destroy(m_impl->objects[i]);
		m_impl->objects[i] = nullptr;
	}
	if (m_impl->pod != nullptr) {
		for (const ibFiberSlot& slot : registry.slots)
			slot.destroy(m_impl->pod.get() + slot.offset);
	}
}

void ibFiberLocals::Snapshot::Capture()
{
	if (m_impl == nullptr || m_impl->pod == nullptr)
		return;
	const Registry& registry = Sealed();
	for (const ibFiberSlot& slot : registry.slots)
		slot.save(m_impl->pod.get() + slot.offset);
}

void ibFiberLocals::Snapshot::Install() const
{
	if (m_impl == nullptr)
		return;
	const Registry& registry = Sealed();
	if (m_impl->pod != nullptr) {
		for (const ibFiberSlot& slot : registry.slots)
			slot.restore(m_impl->pod.get() + slot.offset);
	}
	for (std::size_t i = 0; i < registry.objects.size(); ++i)
		registry.objects[i].activate(i < m_impl->objects.size() ? m_impl->objects[i] : nullptr);
}

ibFiberLocals::Snapshot ibFiberLocals::ForScheduler()
{
	const Registry& registry = Sealed();
	std::unique_ptr<Snapshot::Impl> impl(new Snapshot::Impl());
	ConstructAndSave(registry, *impl);
	impl->objects.assign(registry.objects.size(), nullptr);
	return Snapshot(std::move(impl));
}

ibFiberLocals::Snapshot ibFiberLocals::ForFiber()
{
	const Registry& registry = Sealed();
	std::unique_ptr<Snapshot::Impl> impl(new Snapshot::Impl());
	ConstructAndSave(registry, *impl);
	impl->objects.assign(registry.objects.size(), nullptr);
	// The snapshot holds the objects from here, so a create() that throws destroys the ones already built and the
	// placement-newed slots.
	Snapshot snap(std::move(impl));
	for (std::size_t i = 0; i < registry.objects.size(); ++i)
		snap.m_impl->objects[i] = registry.objects[i].create();
	return snap;
}
