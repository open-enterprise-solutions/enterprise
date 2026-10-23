#include "core/memory/memoryStream.h"
#include "core/memory/lz/lzhuf.h"

#include "core/fstring.h"          // ibString — w_stringZ / r_stringZ(ibString)
#include "core/exception.h"      // a read past the end of a block is refused, not asserted

#include <wx/translation.h>   // _() — the refusals' words

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

//------------------------------------------------------------------------------------
// Write
//------------------------------------------------------------------------------------

void ibWriterMemory::w(const void* ptr, std::uint32_t count)
{
	// A WRITE OF NOTHING IS NOT A WRITE. memcpy forbids a null source even for zero bytes, and
	// the writers above do hand one over for an empty payload (an empty string, a zero-length
	// chunk) - UBSan: "null pointer passed as argument 2, which is declared to never be null".
	// Leaving early also spares the buffer a growth it does not need.
	if (count == 0 || ptr == nullptr)
		return;

	// A write lands at the cursor — behind the end when a chunk's close patches its size — and the length
	// grows only past the furthest write.
	const size_t end = static_cast<size_t>(m_pos) + count;
	std::memcpy(static_cast<char*>(m_buffer.GetWriteBuf(end)) + m_pos, ptr, count);
	if (end > m_buffer.GetDataLen())
		m_buffer.UngetWriteBuf(end);

	m_pos += count;
}

void	ibWriterMemory::w_stringZ(const ibString& p)
{
	w_stringZ(p.ToUtf8());
}

void	ibWriterMemory::w_stringZ(const wchar_t* p)
{
	w_stringZ(ibString(p));
}

void	ibWriterMemory::w_printf(const char* format, ...)
{
	va_list mark;
	char buf[1024];

	va_start(mark, format);
#ifdef __WXMSW__
	vsprintf_s(buf, format, mark);
#else
	vsnprintf(buf, sizeof(buf), format, mark);
#endif
	va_end(mark);

	w(buf, strlen(buf));
}

void	ibWriterMemory::open_chunk(std::uint64_t type)
{
	w_u64(type);
	m_chunk_pos.push(tell());
	w_u64(0);	// the place for 'size'
}

void	ibWriterMemory::close_chunk()
{
	wxASSERT(!m_chunk_pos.empty());

	int pos = tell();
	seek(m_chunk_pos.top());
	w_u64(pos - m_chunk_pos.top() - 8);
	seek(pos);
	m_chunk_pos.pop();
}

std::uint32_t	ibWriterMemory::chunk_size() const // returns size of currently opened chunk, 0 otherwise
{
	if (m_chunk_pos.empty())	return 0;
	return tell() - m_chunk_pos.top() - 8;
}

void	ibWriterMemory::w_compressed(void* ptr, std::uint32_t count)
{
	std::uint8_t* dest = 0;
	unsigned	dest_sz = 0;
	_compressLZ(&dest, &dest_sz, ptr, count);

	if (dest && dest_sz)
		w(dest, dest_sz);
	std::free(dest);   // _compressLZ allocates with malloc — it was `delete`d here
}

void ibWriterMemory::w_compressed(const ibMemoryBuffer& data)
{
	w_compressed(data.GetData(), data.GetDataLen());
}

void	ibWriterMemory::w_chunk(std::uint64_t type, void* data, std::uint32_t size)
{
	open_chunk(type);
	w(data, size);
	close_chunk();
}

void ibWriterMemory::w_chunk(std::uint64_t type, const ibMemoryBuffer& data)
{
	w_chunk(type, data.GetData(), data.GetDataLen());
}

//------------------------------------------------------------------------------------
// Read
//------------------------------------------------------------------------------------

#pragma warning (disable:4701)

void	ibReaderMemory::r(void* p, int cnt) const
{
	// 🛑 REFUSES INSTEAD OF SAYING SO AND READING ANYWAY. This was a wxASSERT followed by the copy:
	// the assert is a line in the journal and nothing else, so an over-read was ANNOUNCED and then
	// performed, leaving m_pos past m_size for whoever read next. That is how the designer died on
	// 2026-09-23 while it was debugging - two asserts from one worker thread at 01:36:38 (this line
	// and advance's), and a second later the same thread walked off the end of the page inside
	// memcpy: access violation, 348 MB of dump, and a stack that named nothing because the binary
	// had been rebuilt since.
	//
	// Reading past the end of a block is never a legitimate thing to do, so it is an answer, not a
	// remark. The numbers are in the message because the question a reader of it asks next is
	// "by how much" - a frame short by four bytes is a truncated write, one short by thousands is
	// a different format altogether.
	if (cnt < 0 || (long long)m_pos + cnt > (long long)m_size)
		ibCoreException::Error(
			_("Reading %i bytes at offset %i would pass the end of a %i-byte block"),
			cnt, m_pos, m_size);

	std::memcpy(p, pointer(), cnt);
	advance(cnt);
};

void ibReaderMemory::RefuseSeek(int ptr) const
{
	ibCoreException::Error(
		_("Seeking to %i would pass the end of a %i-byte block"),
		ptr, m_size);
}

void ibReaderMemory::RefuseAdvance(int cnt) const
{
	ibCoreException::Error(
		_("Advancing %i bytes from offset %i would pass the end of a %i-byte block"),
		cnt, m_pos, m_size);
}

std::uint64_t ibReaderMemory::find_chunk(std::uint64_t ID, bool* bCompressed) const
{
	std::uint64_t	dwSize = 0, dwType = 0;
	bool success = false;

	// A chunk header is 2x std::uint64_t (type + size); never read it without that many bytes left,
	// or a malformed / truncated buffer drives r() past the end (access violation).
	if (m_last_pos != 0) {
		seek(m_last_pos);
		if (elapsed() >= 16) {
			dwType = r_u64();
			dwSize = r_u64();
			if (dwType == ID)
				success = true;
		}
	}

	if (!success) {
		rewind();
		while (elapsed() >= 16)
		{
			dwType = r_u64();
			dwSize = r_u64();
			if (dwType == ID)
			{
				success = true;
				break;
			}
			else
			{
				// ⚠ A SIZE THAT DOES NOT FIT IS NOT A CHUNK TO STEP OVER. A block of another format (a base
				// written before the configuration's layout changed) reads as a header with any number in
				// it; stepped over, the reader landed past its own end and stayed there (advance only
				// ASSERTS), and the next read walked off the page — the designer died opening an old base
				// (2026-09-27). Not the chunk sought, and nothing after it can be trusted: not found.
				if (dwSize > (std::uint64_t)elapsed())
					break;
				advance(dwSize);
			}
		}

		if (!success)
		{
			m_last_pos = 0;
			seek(length());   // at its end, as a search that read everything leaves it — and not past it
			return 0;
		}
	}

	// The found chunk must fit in the buffer — a corrupt size would otherwise build an
	// over-sized sub-reader that reads past the allocation.
	if ((std::uint64_t)tell() + dwSize > (std::uint64_t)length())
	{
		m_last_pos = 0;
		return 0;
	}
	if (bCompressed) *bCompressed = false;

	const int dwPos = tell();
	if (dwPos + dwSize < (std::uint64_t)length())
	{
		m_last_pos = dwPos + dwSize;
	}
	else
	{
		m_last_pos = 0;
	}

	return dwSize;
}

ibReaderMemory* ibReaderMemory::open_chunk(std::uint64_t ID) const
{
	bool	bCompressed;

	std::uint32_t	dwSize = find_chunk(ID, &bCompressed);
	if (dwSize == 0)
		return nullptr;

	if (bCompressed) {
		// The bytes unpacked are a buffer of their own, which the chunk's reader holds.
		std::uint8_t* dest;
		unsigned	dest_sz;
		_decompressLZ(&dest, &dest_sz, pointer(), dwSize);
		ibMemoryBuffer unpacked;
		unpacked.AppendData(dest, dest_sz);
		std::free(dest);
		return new ibReaderMemory(unpacked, tell() + dwSize);
	}

	return new ibReaderMemory(m_bytes, pointer(), dwSize, tell() + dwSize);
}

ibReaderMemory* ibReaderMemory::open_chunk_iterator(std::uint64_t& ID, ibReaderMemory* _prev) const
{
	if (0 == _prev) {
		// first
		rewind();
	}
	else {
		// next
		seek(_prev->m_iterpos);
		_prev->close();
	}

	//	open
	if (elapsed() < 8)
		return nullptr;

	ID = r_u64();
	std::uint64_t _size = r_u64();

	// 🛑 THE SIZE CAME OUT OF THE DATA, SO IT IS A CLAIM AND NOT A MEASUREMENT — and the line below
	// hands it to a new reader as though somebody had checked it. find_chunk, one road over, has
	// asked this question since a corrupt size was found building a reader on memory that was never
	// there; the ITERATOR never did, and an iterator is what walks a frame that arrived over a wire.
	// Refusing here is what keeps the difference between "declared" and "present" visible: by the
	// time the over-read happens it reads as a crash inside memcpy, subsystems away from the frame
	// that lied (2026-09-23, the designer, while it was debugging).
	if (_size > (std::uint64_t)elapsed())
		ibCoreException::Error(
			_("Chunk %llu declares %llu bytes, and %i are left in the block"),
			ID, _size, elapsed());

	return new ibReaderMemory(m_bytes, pointer(), _size, tell() + _size);
}

void	ibReaderMemory::close()
{
	delete this;
}

int ibReaderMemory::MeasureStringZ(const char*& start) const
{
	// 🛑 THE TERMINATOR HAS TO BE INSIDE THE BLOCK. r_stringZ used to construct a std::string
	// from (char*)(m_data + m_pos), and that constructor is strlen: it walks until a NUL,
	// whether or not the block still has one. A debugger frame with no terminator (and any
	// other block this reader walks) kept going into whatever bytes followed the buffer.
	// The cursor is left where it was, so a caller that catches the refusal has not skipped
	// a string it never found.
	if (m_pos < 0 || m_pos > m_size || m_data == nullptr)
		ibCoreException::Error(
			_("A zero-terminated string starts at %i, outside a %i-byte block"),
			m_pos, m_size);

	const int remain = m_size - m_pos;
	start = m_data + m_pos;
	const void* const nul = remain > 0
		? std::memchr(start, '\0', static_cast<size_t>(remain))
		: nullptr;
	if (nul == nullptr)
		ibCoreException::Error(
			_("A zero-terminated string at offset %i has no terminator in the %i bytes left"),
			m_pos, remain);

	return static_cast<int>(static_cast<const char*>(nul) - start);
}

inline bool is_term(const wxUniChar &c) {
	return (c == 13) || (c == 10);
};

inline std::uint32_t	ibReaderMemory::advance_term_string() const
{
	std::uint32_t sz = 0;
	char* src = (char*)m_data;
	while (!eof()) {
		m_pos++;
		sz++;
		if (!eof() && is_term(src[m_pos]))
		{
			while (!eof() && is_term(src[m_pos]))
				m_pos++;
			break;
		}
	}
	return sz;
}

void	ibReaderMemory::r_string(char* dest, std::uint32_t tgt_sz) const
{
	char* src = (char*)m_data + m_pos;
	std::uint32_t sz = advance_term_string();
	wxASSERT(sz < (tgt_sz - 1));
#ifdef __WXMSW__
	strncpy_s(dest, tgt_sz, src, sz);
#else
	strncpy(dest, src, tgt_sz - 1);
#endif

	dest[sz] = 0;
}

void	ibReaderMemory::r_string(std::string& dest) const
{
	char* src = (char*)m_data + m_pos;
	std::uint32_t sz = advance_term_string();
	dest.assign(src, sz);
}

void	ibReaderMemory::r_string(wxString& dest) const
{
	char* src = (char*)m_data + m_pos;
	std::uint32_t sz = advance_term_string();
	dest = wxString::FromUTF8(src, sz);
}

void	ibReaderMemory::skip_stringZ() const
{
	const char* start = nullptr;
	const int len = MeasureStringZ(start);
	m_pos += len + 1;
}

wxString ibReaderMemory::r_stringZ() const
{
	const char* start = nullptr;
	const int len = MeasureStringZ(start);
	m_pos += len + 1;
	return wxString::FromUTF8(start, len);
}

void	ibReaderMemory::r_stringZ(char* dest, std::uint32_t tgt_sz) const
{
	const char* start = nullptr;
	const int len = MeasureStringZ(start);
	if (tgt_sz == 0 || static_cast<std::uint32_t>(len) >= tgt_sz)
		ibCoreException::Error(
			_("A zero-terminated string of %i bytes does not fit a %u-byte buffer"),
			len, static_cast<unsigned>(tgt_sz));
	m_pos += len + 1;
	std::memcpy(dest, start, static_cast<size_t>(len));
	dest[len] = '\0';
}

void	ibReaderMemory::r_stringZ(std::string& dest) const
{
	const char* start = nullptr;
	const int len = MeasureStringZ(start);
	m_pos += len + 1;
	dest.assign(start, static_cast<size_t>(len));
}

void	ibReaderMemory::r_stringZ(wxString& dest) const
{
	const char* start = nullptr;
	const int len = MeasureStringZ(start);
	m_pos += len + 1;
	dest = wxString::FromUTF8(start, len);
}

void	ibReaderMemory::r_stringZ(ibString& dest) const
{
	const char* start = nullptr;
	const int len = MeasureStringZ(start);
	m_pos += len + 1;
	dest.SetUtf8(start, static_cast<size_t>(len));
}
