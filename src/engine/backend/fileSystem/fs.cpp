#include "fs.h"
#include "lz/lzhuf.h"

typedef unsigned char byte_t;

//------------------------------------------------------------------------------------
// Write
//------------------------------------------------------------------------------------

void	IWriter::open_chunk(u64 type)
{
	w_u64(type);
	m_chunk_pos.push(tell());
	w_u64(0);	// the place for 'size'
}

void	IWriter::close_chunk()
{
	wxASSERT(!m_chunk_pos.empty());

	int pos = tell();
	seek(m_chunk_pos.top());
	w_u64(pos - m_chunk_pos.top() - 8);
	seek(pos);
	m_chunk_pos.pop();
}

u32	IWriter::chunk_size() const // returns size of currently opened chunk, 0 otherwise
{
	if (m_chunk_pos.empty())	return 0;
	return tell() - m_chunk_pos.top() - 8;
}

void	IWriter::w_compressed(void* ptr, u32 count)
{
	byte_t* dest = 0;
	unsigned	dest_sz = 0;
	_compressLZ(&dest, &dest_sz, ptr, count);

	if (dest && dest_sz)
		w(dest, dest_sz);
	wxDELETE(dest);
}

void IWriter::w_compressed(const wxMemoryBuffer& m_data)
{
	w_compressed(m_data.GetData(), m_data.GetDataLen());
}

void	IWriter::w_chunk(u64 type, void* m_data, u32 size)
{
	open_chunk(type);
	w(m_data, size);
	close_chunk();
}

void IWriter::w_chunk(u64 type, const wxMemoryBuffer& m_data)
{
	w_chunk(type, m_data.GetData(), m_data.GetDataLen());
}

void	IWriter::w_printf(const char* format, ...)
{
	va_list mark;
	char buf[1024];

	va_start(mark, format);
	vsprintf_s(buf, format, mark);
	va_end(mark);

	w(buf, strlen(buf));
}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////
//---------------------------------------------------
// memory
CMemoryWriter::~CMemoryWriter()
{
	wxDELETE(m_data);
}

void CMemoryWriter::w(const void* ptr, u32 count)
{
	if (m_pos + count > m_mem_size) {
		// reallocate
		if (m_mem_size == 0)	
			m_mem_size = 128;
		
		while (m_mem_size <= (m_pos + count)) 
			m_mem_size *= 2;
		
		if (0 == m_data)		
			m_data = (byte_t*)malloc(m_mem_size);
		else				
			m_data = (byte_t*)realloc(m_data, m_mem_size);
	}

	memcpy(m_data + m_pos, ptr, count);

	m_pos += count;
	
	if (m_pos > m_file_size) 
		m_file_size = m_pos;
}

//static const u32 mb_sz = 0x1000000;
void* CMemoryWriter::save_to()
{
	return pointer();
}

//------------------------------------------------------------------------------------
// Read
//------------------------------------------------------------------------------------

#pragma warning (disable:4701)

IReader* IReader::open_chunk(u64 ID) const
{
	bool	bCompressed;

	u32	dwSize = find_chunk(ID, &bCompressed);
	if (dwSize != 0) {
		if (bCompressed) {
			byte_t* dest;
			unsigned	dest_sz;
			_decompressLZ(&dest, &dest_sz, pointer(), dwSize);
			return new IReader(dest, dest_sz, tell() + dwSize);
		}
		else {
			return new IReader(pointer(), dwSize, tell() + dwSize);
		}
	}
	else return 0;
};

void	IReader::close()
{
	delete((IReader*)this);
}

u64 IReader::find_chunk(u64 ID, bool* bCompressed) const
{
	u64	dwSize, dwType;
	bool success = false;

	if (m_last_pos != 0) {
		seek(m_last_pos);
		dwType = r_u64();
		dwSize = r_u64();

		if (dwType == ID)
			success = true;
	}

	if (!success) {
		rewind();
		while (!eof())
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
				advance(dwSize);
			}
		}

		if (!success)
		{
			m_last_pos = 0;
			return 0;
		}
	}

	wxASSERT((u64)tell() + dwSize <= (u64)length());
	if (bCompressed) *bCompressed = false;

	const int dwPos = tell();
	if (dwPos + dwSize < (u64)length())
	{
		m_last_pos = dwPos + dwSize;
	}
	else
	{
		m_last_pos = 0;
	}

	return dwSize;
}

IReader* IReader::open_chunk_iterator(u64& ID, IReader* _prev) const
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
	u64 _size = r_u64();

	if (false)
	{
		// compressed
		u8* dest;
		unsigned		dest_sz;
		_decompressLZ(&dest, &dest_sz, pointer(), _size);
		return new IReader(dest, dest_sz, tell() + _size);
	}
	else {
		// normal
		return new IReader(pointer(), _size, tell() + _size);
	}
}
 
void	IReader::r(void* p, int cnt) const
{
	wxASSERT(m_pos + cnt <= m_size);
	std::memcpy(p, pointer(), cnt);
	advance(cnt);
};

inline bool is_term(const wxUniChar &c) {
	return (c == 13) || (c == 10);
};

inline u32	IReader::advance_term_string() const
{
	u32 sz = 0;
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
void	IReader::r_string(char* dest, u32 tgt_sz) const
{
	char* src = (char*)m_data + m_pos;
	u32 sz = advance_term_string();
	wxASSERT(sz < (tgt_sz - 1));
	wxASSERT(!IsBadReadPtr((void*)src, sz));

	strncpy_s(dest, tgt_sz, src, sz);

	dest[sz] = 0;
}
void	IReader::r_string(std::string& dest) const
{
	char* src = (char*)m_data + m_pos;
	u32 sz = advance_term_string();
	dest.assign(src, sz);
}

void	IReader::r_string(wxString& dest) const
{
	char* src = (char*)m_data + m_pos;
	u32 sz = advance_term_string();
	dest = wxString::FromUTF8(src, sz);
}

wxString IReader::r_stringZ() const
{
	std::string destSrc = (char*)m_data + m_pos;
	m_pos += int(destSrc.size() + 1);
	return wxString::FromUTF8(destSrc);
}

void	IReader::r_stringZ(char* dest, u32 tgt_sz) const
{
	char* src = (char*)m_data;
	u32 sz = strlen(src);
	wxASSERT(sz < tgt_sz);
	while ((src[m_pos] != 0) && (!eof())) *dest++ = src[m_pos++];
	*dest = 0;
	m_pos++;
}

void	IReader::r_stringZ(std::string& dest) const
{
	dest = (char*)(m_data + m_pos);
	m_pos += int(dest.size() + 1);
}

void	IReader::r_stringZ(wxString& dest) const
{
	std::string destSrc = (char*)(m_data + m_pos);
	m_pos += int(destSrc.size() + 1);
	dest = wxString::FromUTF8(destSrc);
}

void	IReader::skip_stringZ() const
{
	char* src = (char*)m_data;
	while ((src[m_pos] != 0) && (!eof())) m_pos++;
	m_pos++;
}

CMemoryReader* CMemoryReader::open_chunk(u64 ID) const 
{
	bool	bCompressed;

	u32	dwSize = find_chunk(ID, &bCompressed);

	if (dwSize != 0) {
		if (bCompressed) {
			byte_t* dest;
			unsigned	dest_sz;
			_decompressLZ(&dest, &dest_sz, pointer(), dwSize);
			return new CMemoryReader(dest, dest_sz, tell() + dwSize);
		}
		else {
			return new CMemoryReader(pointer(), dwSize, tell() + dwSize);
		}
	}

	return nullptr;
};

CMemoryReader* CMemoryReader::open_chunk_iterator(u64& ID, CMemoryReader* _prev) const
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
	u64 _size = r_u64();

	if (false) {
		// compressed
		u8* dest;
		unsigned		dest_sz;
		_decompressLZ(&dest, &dest_sz, pointer(), _size);
		return new CMemoryReader(dest, dest_sz, tell() + _size);
	}
	else {
		// normal
		return new CMemoryReader(pointer(), _size, tell() + _size);
	}
}
