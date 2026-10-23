#ifndef _CORE_MEMORY_STREAM_H__
#define _CORE_MEMORY_STREAM_H__

/////////////////////////////////////////////////////////////////////////////
// The engine's bytes in order, over an ibMemoryBuffer (memoryBuffer.h): numbers as they lie in memory, strings
// zero-terminated, and CHUNKS — an id, a size, the content.
//
// ibWriterMemory writes into its buffer and hands it over as it is: what a writer wrote goes into a frame, a binary
// value, a blob parameter of the database layer, a reader — with no copy, and the writer's next write does not
// reach it (memoryBuffer.h says why).
// ibReaderMemory is a cursor over the bytes a writer wrote. Made on a buffer it HOLDS it — one more owner of its
// bytes, so a buffer handed over as a temporary stays alive as long as the reader, and a chunk it opens holds the
// same bytes; made on a pointer it borrows them. A read past the end of the block is refused, not performed.
/////////////////////////////////////////////////////////////////////////////

#include "core/core.h"
#include "core/memory/memoryBuffer.h"

#include <wx/string.h>   // the wxString overloads of w_stringZ / r_string / r_stringZ — until the callers use ibString

#include <cstdint>
#include <cstring>
#include <stack>
#include <string>

class ibString;   // core/fstring.h — the ibString overloads of w_stringZ / r_stringZ

//------------------------------------------------------------------------------------
// Write
//------------------------------------------------------------------------------------

class CORE_API ibWriterMemory
{
public:
	// kernel
	void			w(const void* ptr, std::uint32_t count);

	void			seek(std::uint32_t pos) const { m_pos = pos; }
	std::uint32_t				tell() const { return m_pos; }

	// generalized writing functions
	inline void			w_u64(std::uint64_t d) { w(&d, sizeof(std::uint64_t)); }
	inline void			w_u32(std::uint32_t d) { w(&d, sizeof(std::uint32_t)); }
	inline void			w_u16(std::uint16_t d) { w(&d, sizeof(std::uint16_t)); }
	inline void			w_u8(std::uint8_t d) { w(&d, sizeof(std::uint8_t)); }
	inline void			w_s64(std::int64_t d) { w(&d, sizeof(std::int64_t)); }
	inline void			w_s32(std::int32_t d) { w(&d, sizeof(std::int32_t)); }
	inline void			w_s16(std::int16_t d) { w(&d, sizeof(std::int16_t)); }
	inline void			w_s8(std::int8_t d) { w(&d, sizeof(std::int8_t)); }
	inline void			w_float(float d) { w(&d, sizeof(float)); }
	inline void			w_string(const char* p) { w(p, (std::uint32_t)strlen(p)); w_u8(13); w_u8(10); }
	inline void			w_stringZ(const char* p) { w(p, (std::uint32_t)strlen(p) + 1); }
	inline void			w_stringZ(const std::string& p) { w(p.c_str() ? p.c_str() : "", (std::uint32_t)p.size()); w_u8(0); }
	inline void			w_stringZ(const wxString& p) { const wxScopedCharBuffer s = p.utf8_str(); w(s.data() ? s.data() : "", (std::uint32_t)s.length()); w_u8(0); }
	void				w_stringZ(const ibString& p);
	void				w_stringZ(const wchar_t* p);   // a wide literal — one of the two above would both take it

#ifdef _MSC_VER
	void	__cdecl  	w_printf(const char* format, ...);
#else
	void	w_printf(const char* format, ...);
#endif

	// generalized chunking
	void			open_chunk(std::uint64_t type);
	void			close_chunk();
	std::uint32_t				chunk_size() const; // returns size of currently opened chunk, 0 otherwise
	void			w_compressed(void* ptr, std::uint32_t count);
	void			w_compressed(const ibMemoryBuffer& data);
	void			w_chunk(std::uint64_t type, void* m_data, std::uint32_t size);
	void			w_chunk(std::uint64_t type, const ibMemoryBuffer& data);

	// What was written, handed over without a copy.
	ibMemoryBuffer	buffer() const { return m_buffer; }

	inline std::uint8_t*		pointer() const { return static_cast<std::uint8_t*>(m_buffer.GetData()); }
	inline std::uint32_t		size() const { return static_cast<std::uint32_t>(m_buffer.GetDataLen()); }
	inline void		clear() { m_buffer.Clear(); m_pos = 0; }

private:
	ibMemoryBuffer		m_buffer;		// what was written: its length is the end of the furthest write
	mutable std::uint32_t			m_pos = 0;		// where the next write goes — a chunk's close seeks back to patch its size
	std::stack<std::uint64_t>		m_chunk_pos;	// the open chunks' size fields, innermost on top
};

//------------------------------------------------------------------------------------
// Read
//------------------------------------------------------------------------------------

class CORE_API ibReaderMemory
{
public:

	// The buffer held — the reader is one more owner of its bytes, so a temporary is as good as a variable.
	ibReaderMemory(const ibMemoryBuffer& buf, int _iterpos = 0) :
		ibReaderMemory(buf, buf.GetData(), static_cast<int>(buf.GetDataLen()), _iterpos)
	{
	}

	// Bytes borrowed: whoever handed them over keeps them alive while the reader reads.
	ibReaderMemory(void* _data, int _size, int _iterpos = 0) :
		m_data(static_cast<char*>(_data)), m_size(_size), m_iterpos(_iterpos)
	{
	}

	inline int			elapsed()	const { return m_size - m_pos; }
	inline int			tell()	const { return m_pos; }
	// The end itself (ptr == m_size) is a legal place to sit: that is eof().
	// Past it, or before the start, is a refusal in every build — a wxASSERT
	// is compiled out of Release, and the next read is then already off the block.
	inline void			seek(int ptr) const {
		if (ptr < 0 || ptr > m_size)
			RefuseSeek(ptr);
		m_pos = ptr;
	}
	inline int			length()	const { return m_size; }
	inline void* pointer()	const { return &(m_data[m_pos]); }
	inline void			advance(int cnt) const {
		if (cnt < 0 || (long long)m_pos + cnt > (long long)m_size)
			RefuseAdvance(cnt);
		m_pos += cnt;
	}

	inline bool			eof()	const { return elapsed() <= 0; }
	void			r(void* p, int cnt) const;

	inline std::uint64_t			r_u64() const { std::uint64_t   tmp;	r(&tmp, sizeof(tmp)); return tmp; }
	inline std::uint32_t			r_u32() const { std::uint32_t   tmp;	r(&tmp, sizeof(tmp)); return tmp; }
	inline std::uint16_t			r_u16() const { std::uint16_t   tmp;	r(&tmp, sizeof(tmp)); return tmp; }
	inline std::uint8_t			r_u8() const { std::uint8_t    tmp;	r(&tmp, sizeof(tmp)); return tmp; }
	inline std::int64_t			r_s64() const { std::int64_t   tmp;	r(&tmp, sizeof(tmp)); return tmp; }
	inline std::int32_t			r_s32() const { std::int32_t   tmp;	r(&tmp, sizeof(tmp)); return tmp; }
	inline std::int16_t			r_s16() const { std::int16_t   tmp;	r(&tmp, sizeof(tmp)); return tmp; }
	inline std::int8_t			r_s8() const { std::int8_t    tmp;	r(&tmp, sizeof(tmp)); return tmp; }
	inline float		r_float() const { float tmp;	r(&tmp, sizeof(tmp)); return tmp; }

	// Set file pointer to start of chunk m_data (0 for root chunk)
	inline	void		rewind() const { seek(0); }
	std::uint64_t 			    find_chunk(std::uint64_t ID, bool* bCompressed = nullptr) const;

	// A chunk's content (8-byte id, 8-byte size, the content) into `dest`.
	inline	bool		r_chunk(std::uint64_t ID, void* dest) const {
		m_last_pos = tell();
		std::uint32_t	dwSize = find_chunk(ID);
		if (dwSize != 0) {
			r(dest, dwSize);
			return true;
		}
		return false;
	}

	// …appended to a buffer.
	inline	bool		r_chunk(std::uint64_t ID, ibMemoryBuffer& dest) const {
		m_last_pos = tell();
		std::uint32_t	dwSize = find_chunk(ID);
		if (dwSize != 0) {
			r(dest.GetAppendBuf(dwSize), dwSize);
			dest.UngetAppendBuf(dwSize);
			return true;
		}
		return false;
	}

	// …the same, the content's size asserted.
	inline	bool		r_chunk_safe(std::uint64_t ID, void* dest, std::uint32_t dest_size) const {
		m_last_pos = tell();
		std::uint64_t	dwSize = find_chunk(ID);
		if (dwSize != 0) {
			wxASSERT(dwSize == dest_size);
			r(dest, dwSize);
			return true;
		}
		return false;
	}

	inline	bool		r_chunk_safe(std::uint64_t ID, ibMemoryBuffer& dest, std::uint32_t dest_size) const {
		m_last_pos = tell();
		std::uint64_t	dwSize = find_chunk(ID);
		if (dwSize != 0) {
			wxASSERT(dwSize == dest_size);
			r(dest.GetAppendBuf(dwSize), dwSize);
			dest.UngetAppendBuf(dwSize);
			return true;
		}
		return false;
	}

	void			r_string(char* dest, std::uint32_t tgt_sz) const;
	void			r_string(std::string& dest) const;
	void			r_string(wxString& dest) const;

	void			skip_stringZ() const;

	wxString		r_stringZ() const;

	void			r_stringZ(char* dest, std::uint32_t tgt_sz) const;
	void			r_stringZ(std::string& dest) const;
	void			r_stringZ(wxString& dest) const;
	void			r_stringZ(ibString& dest) const;

	// A chunk opened as a reader of its own — holding the same bytes — or nullptr when there is none.
	ibReaderMemory*	open_chunk(std::uint64_t ID) const;
	// iterators
	ibReaderMemory*	open_chunk_iterator(std::uint64_t& ID, ibReaderMemory* previous = nullptr) const;	// nullptr=first

	void			close();

private:

	// A chunk of the bytes another reader holds.
	ibReaderMemory(const ibMemoryBuffer& bytes, void* _data, int _size, int _iterpos) :
		m_bytes(bytes), m_data(static_cast<char*>(_data)), m_size(_size), m_iterpos(_iterpos)
	{
	}

	std::uint32_t 			advance_term_string() const;

	// The byte length of the C string at the cursor, not counting its NUL.
	// Does not move the cursor. No NUL in the bytes still left is a refusal.
	int				MeasureStringZ(const char*& start) const;

	void			RefuseSeek(int ptr) const;
	void			RefuseAdvance(int cnt) const;

	ibMemoryBuffer		m_bytes;		// the bytes read, held; empty when they are borrowed
	char*				m_data;
	mutable int			m_pos = 0;
	int					m_size;
	int					m_iterpos;		// where the chunk after this one starts in its parent
	mutable std::uint64_t			m_last_pos = 0;
};

#endif // !_CORE_MEMORY_STREAM_H__
