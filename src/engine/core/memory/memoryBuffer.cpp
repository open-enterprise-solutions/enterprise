// =============================================================================
// ibMemoryBuffer — the module: the bytes behind the facade (memoryBuffer.h), made, written into and freed the way
// ibString's text is (fstring.cpp).
// =============================================================================

#include "core/memory/memoryBuffer.h"
#include "core/memory/memoryPool.h"   // the blocks the bytes are made in

#include <cstring>
#include <new>

// One block per buffer: the owners' count, the length, the room, then the bytes — one allocation from the pool.
struct ibMemoryBuffer::Impl : ibMemoryBuffer::Shared
{
	explicit Impl(size_t cap) noexcept : Shared(1, cap) {}

	static size_t BytesFor(size_t cap) noexcept { return sizeof(Shared) + cap; }

	// Room for `cap` bytes at least — and for as many more as the pool's block holds anyway.
	static Impl* Make(size_t cap) {
		const size_t asked = BytesFor(cap);
		const int c = ibMemoryPool::detail::ClassOf(asked);
		const size_t bytes = c < 0 ? asked : ibMemoryPool::detail::kClasses[c];
		void* const place = c < 0 ? ::operator new(bytes) : ibMemoryPool::AllocateClass(c);
		return new (place) Impl(bytes - sizeof(Shared));
	}
	static Impl* Copy(const char* data, size_t len, size_t cap) {
		Impl* const impl = Make(cap < len ? len : cap);
		if (len != 0) std::memcpy(impl->Bytes(), data, len);
		impl->m_len = len;
		return impl;
	}

	static Impl* Of(Shared* shared) noexcept { return static_cast<Impl*>(shared); }
	bool Alone() const noexcept { return m_refCount.load(std::memory_order_acquire) == 1; }
};

ibMemoryBuffer::ibMemoryBuffer(size_t size)
{
	if (size != 0) m_impl = Impl::Make(size);
}

void ibMemoryBuffer::Free(Shared* shared) noexcept
{
	Impl* const impl = Impl::Of(shared);
	const size_t bytes = Impl::BytesFor(impl->m_cap);
	impl->~Impl();
	ibMemoryPool::Deallocate(impl, bytes);
}

// Bytes somebody else holds are copied first, with exactly the room asked for; bytes of its own without room grow
// by half again, so a buffer appended to in a loop is copied a logarithmic number of times — the string's rule.
char* ibMemoryBuffer::Own(size_t room)
{
	Impl* const held = m_impl != nullptr ? Impl::Of(m_impl) : nullptr;
	const size_t len = held != nullptr ? held->m_len : 0;
	if (room < len) room = len;
	if (held != nullptr && held->Alone()) {
		if (held->m_cap >= room) return held->Bytes();
		const size_t grown = held->m_cap + held->m_cap / 2;
		if (room < grown) room = grown;
	}
	Impl* const own = Impl::Copy(held != nullptr ? held->Bytes() : nullptr, len, room);
	Release(m_impl);
	m_impl = own;
	return own->Bytes();
}

// A length past the room grows the room: the bytes between are the block's, never memory beyond it.
void ibMemoryBuffer::SetDataLen(size_t len)
{
	if (len == 0) { Clear(); return; }
	Own(len);
	m_impl->m_len = len;
}

void ibMemoryBuffer::Clear() noexcept
{
	if (m_impl != nullptr && Impl::Of(m_impl)->Alone()) m_impl->m_len = 0;
	else { Release(m_impl); m_impl = nullptr; }
}

void ibMemoryBuffer::AppendByte(char data)
{
	const size_t len = GetDataLen();
	Own(len + 1)[len] = data;
	m_impl->m_len = len + 1;
}

void ibMemoryBuffer::AppendData(const void* data, size_t len)
{
	if (len == 0) return;
	const size_t held = GetDataLen();
	std::memcpy(Own(held + len) + held, data, len);
	m_impl->m_len = held + len;
}
