#include "fiberLocals.h"

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

struct ibFiberOwned {
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
	std::vector<void*> owned;
	bool ownsObjects = false;
};

struct ibFiberLocals::Registry {
	std::mutex mutex;
	std::vector<ibFiberSlot> slots;
	std::vector<ibFiberOwned> owned;
	std::size_t podSize = 0;
	std::size_t podAlign = alignof(std::max_align_t);
	bool sealed = false;
};

ibFiberLocals::Registry& ibFiberLocals::Get()
{
	static Registry registry;
	return registry;
}

namespace {

void Seal(ibFiberLocals::Registry& registry)
{
	if (registry.sealed)
		return;
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
	registry.podAlign = align == 0 ? alignof(std::max_align_t) : align;
	registry.sealed = true;
}

void ConstructAndSave(ibFiberLocals::Registry& registry, ibFiberLocals::Snapshot::Impl& impl)
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

} // namespace

void ibFiberLocals::Register(
	std::size_t size, std::size_t align,
	void (*construct)(void*), void (*destroy)(void*),
	void (*save)(void*), void (*restore)(const void*))
{
	Registry& registry = Get();
	std::lock_guard<std::mutex> lk(registry.mutex);
	if (registry.sealed) {
		std::fputs("ibFiberLocals::Register after the first fiber snapshot\n", stderr);
		std::abort();
	}
	ibFiberSlot slot;
	slot.size = size;
	slot.align = align;
	slot.construct = construct;
	slot.destroy = destroy;
	slot.save = save;
	slot.restore = restore;
	registry.slots.push_back(slot);
}

void ibFiberLocals::RegisterOwned(
	void* (*create)(), void (*destroy)(void*), void (*activate)(void*))
{
	Registry& registry = Get();
	std::lock_guard<std::mutex> lk(registry.mutex);
	if (registry.sealed) {
		std::fputs("ibFiberLocals::RegisterOwned after the first fiber snapshot\n", stderr);
		std::abort();
	}
	ibFiberOwned owned;
	owned.create = create;
	owned.destroy = destroy;
	owned.activate = activate;
	registry.owned.push_back(owned);
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
	// Slots and owned-specs are immutable once any snapshot exists, and a
	// snapshot is destroyed on the thread that published it (the fiber's
	// home thread), which already synchronized with Seal.
	Registry& registry = Get();
	if (m_impl->ownsObjects) {
		const std::size_t n = m_impl->owned.size();
		for (std::size_t i = 0; i < n; ++i) {
			if (m_impl->owned[i] != nullptr && i < registry.owned.size())
				registry.owned[i].destroy(m_impl->owned[i]);
			m_impl->owned[i] = nullptr;
		}
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
	Registry& registry = Get();
	std::lock_guard<std::mutex> lk(registry.mutex);
	Seal(registry);
	for (const ibFiberSlot& slot : registry.slots)
		slot.save(m_impl->pod.get() + slot.offset);
}

void ibFiberLocals::Snapshot::Install() const
{
	if (m_impl == nullptr)
		return;
	Registry& registry = Get();
	std::lock_guard<std::mutex> lk(registry.mutex);
	Seal(registry);
	if (m_impl->pod != nullptr) {
		for (const ibFiberSlot& slot : registry.slots)
			slot.restore(m_impl->pod.get() + slot.offset);
	}
	const std::size_t n = registry.owned.size();
	for (std::size_t i = 0; i < n; ++i) {
		void* obj = i < m_impl->owned.size() ? m_impl->owned[i] : nullptr;
		registry.owned[i].activate(obj);
	}
}

ibFiberLocals::Snapshot ibFiberLocals::CaptureNeutral()
{
	Registry& registry = Get();
	std::unique_ptr<Snapshot::Impl> impl(new Snapshot::Impl());
	{
		std::lock_guard<std::mutex> lk(registry.mutex);
		Seal(registry);
		ConstructAndSave(registry, *impl);
		impl->owned.assign(registry.owned.size(), nullptr);
		impl->ownsObjects = false;
	}
	return Snapshot(std::move(impl));
}

ibFiberLocals::Snapshot ibFiberLocals::MakeForFiber()
{
	Registry& registry = Get();
	std::size_t ownedCount = 0;
	std::unique_ptr<Snapshot::Impl> impl(new Snapshot::Impl());
	{
		std::lock_guard<std::mutex> lk(registry.mutex);
		Seal(registry);
		ConstructAndSave(registry, *impl);
		ownedCount = registry.owned.size();
	}
	impl->owned.assign(ownedCount, nullptr);
	impl->ownsObjects = true;
	// The snapshot owns the objects from here, so a create() that throws
	// destroys the ones already built and the placement-newed slots.
	Snapshot snap(std::move(impl));
	for (std::size_t i = 0; i < ownedCount; ++i)
		snap.m_impl->owned[i] = registry.owned[i].create();
	return snap;
}
