#ifndef _CORE_MEMORY_BUFFER_H__
#define _CORE_MEMORY_BUFFER_H__

/////////////////////////////////////////////////////////////////////////////
// ibMemoryBuffer — bytes the engine holds: the protocol's binary value, a picture, a blob, a file read whole,
// what a writer wrote and a reader reads.
//
// ⭐ THE STRING'S SHAPE (ibString, core/fstring.h), wxMemoryBuffer's METHODS, NO wx. One block from the pool
// (core/memory/memoryPool.h) holds the owners' count, the length, the room and the bytes; an empty buffer holds
// no block at all. A COPY IS ONE MORE OWNER — the bytes are copied only when one of the owners writes (Own) —
// and copying, moving and letting go are inline: an atomic step on the count, a pointer.
// wxMemoryBuffer (wx 3.3.2, buffer.h), which this replaced, counted its owners in a plain size_t (a buffer copied
// on a session's worker and dropped on the socket's thread raced on it), allocated a kilobyte for an empty one,
// had no move, grew a kilobyte at a time, and wrote through a copy into every other copy.
//
// ⚠ GetData() WRITES NOTHING AND SO OWNS NOTHING: write through its pointer only after a call that writes —
// SetDataLen, GetWriteBuf, GetAppendBuf — which is what made the block this buffer's alone.
/////////////////////////////////////////////////////////////////////////////

#include "core/core.h"

#include <atomic>
#include <cstddef>

class CORE_API ibMemoryBuffer
{
public:
	// Nothing held, nothing allocated.
	ibMemoryBuffer() noexcept = default;
	// Room for `size` bytes, none of them data yet.
	explicit ibMemoryBuffer(size_t size);

	ibMemoryBuffer(const ibMemoryBuffer& o) noexcept : m_impl(o.m_impl) { Hold(m_impl); }
	ibMemoryBuffer(ibMemoryBuffer&& o) noexcept : m_impl(o.m_impl) { o.m_impl = nullptr; }
	ibMemoryBuffer& operator=(const ibMemoryBuffer& o) noexcept {
		if (m_impl == o.m_impl) return *this;
		Hold(o.m_impl);   // first — `b = b` must not let go of the bytes it is about to keep
		Release(m_impl);
		m_impl = o.m_impl;
		return *this;
	}
	ibMemoryBuffer& operator=(ibMemoryBuffer&& o) noexcept {
		if (this != &o) { Release(m_impl); m_impl = o.m_impl; o.m_impl = nullptr; }
		return *this;
	}
	~ibMemoryBuffer() { Release(m_impl); }

	void* GetData() const noexcept { return m_impl != nullptr ? m_impl->Bytes() : nullptr; }
	size_t GetBufSize() const noexcept { return m_impl != nullptr ? m_impl->m_cap : 0; }
	size_t GetDataLen() const noexcept { return m_impl != nullptr ? m_impl->m_len : 0; }
	bool IsEmpty() const noexcept { return GetDataLen() == 0; }

	void SetBufSize(size_t size) { Own(size); }
	void SetDataLen(size_t len);
	void Clear() noexcept;

	// Room for `sizeNeeded` bytes from the start, and the length once they are written.
	void* GetWriteBuf(size_t sizeNeeded) { return Own(sizeNeeded); }
	void UngetWriteBuf(size_t sizeUsed) { SetDataLen(sizeUsed); }

	// …and the same after the data already held.
	void* GetAppendBuf(size_t sizeNeeded) { const size_t len = GetDataLen(); return static_cast<char*>(Own(len + sizeNeeded)) + len; }
	void UngetAppendBuf(size_t sizeUsed) { SetDataLen(GetDataLen() + sizeUsed); }

	void AppendByte(char data);
	void AppendData(const void* data, size_t len);

	operator const char* () const noexcept { return static_cast<const char*>(GetData()); }

private:
	// The block's header, as the string's: the owners' count, enough to copy and let go inline, and the length and
	// the room, with the bytes right after the header in the same block — enough to read inline. Making the block,
	// writing into it and freeing it stay behind the facade, in Impl (memoryBuffer.cpp).
	struct Shared {
		Shared(long owners, size_t room) noexcept : m_refCount(owners), m_len(0), m_cap(room) {}
		std::atomic<long> m_refCount;
		size_t            m_len;   // the bytes that are data
		size_t            m_cap;   // the bytes the block has room for
		char*       Bytes() noexcept       { return reinterpret_cast<char*>(this + 1); }
		const char* Bytes() const noexcept { return reinterpret_cast<const char*>(this + 1); }
	};
	struct Impl;                                  // : Shared — making and freeing the block, memoryBuffer.cpp

	static void Hold(Shared* shared) noexcept {
		if (shared != nullptr) shared->m_refCount.fetch_add(1, std::memory_order_relaxed);
	}
	static void Release(Shared* shared) noexcept {
		if (shared != nullptr && shared->m_refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
			Free(shared);
	}
	static void Free(Shared* shared) noexcept;    // the last owner went: the block goes back to the pool

	// To WRITE: the bytes, made this buffer's own, with room for `room` at least — copied first when anybody
	// else holds them, grown when they have no room.
	char* Own(size_t room);

	Shared* m_impl = nullptr;   // nullptr = empty: no bytes, nothing allocated
};

#endif // !_CORE_MEMORY_BUFFER_H__
