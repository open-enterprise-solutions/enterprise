#include "fs.h"
#include "lz/lzhuf.h"

//------------------------------------------------------------------------------------
// Write
//------------------------------------------------------------------------------------

void	IWriter::open_chunk(u64 type)
{
	w_u64(type);
	chunk_pos.push(tell());
	w_u64(0);	// the place for 'size'
}

void	IWriter::close_chunk()
{
	wxASSERT(!chunk_pos.empty());

	int pos = tell();
	seek(chunk_pos.top());
	w_u64(pos - chunk_pos.top() - 8);
	seek(pos);
	chunk_pos.pop();
}

u32	IWriter::chunk_size()					// returns size of currently opened chunk, 0 otherwise
{
	if (chunk_pos.empty())	return 0;
	return tell() - chunk_pos.top() - 8;
}

void	IWriter::w_compressed(void* ptr, u32 count)
{
	BYTE*		dest = 0;
	unsigned	dest_sz = 0;
	_compressLZ(&dest, &dest_sz, ptr, count);

	if (dest && dest_sz)
		w(dest, dest_sz);
	delete dest;
}

void	IWriter::w_chunk(u64 type, void* data, u32 size)
{
	open_chunk(type);
	w(data, size);
	close_chunk();
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
	delete data;
}

void CMemoryWriter::w(const void* ptr, u32 count)
{
	if (position + count > mem_size)
	{
		// reallocate
		if (mem_size == 0)	mem_size = 128;
		while (mem_size <= (position + count)) mem_size *= 2;
		if (0 == data)		data = (BYTE*)malloc(mem_size);
		else				data = (BYTE*)realloc(data, mem_size);
	}

	memcpy(data + position, ptr, count);

	position += count;
	if (position > file_size) file_size = position;
}

//static const u32 mb_sz = 0x1000000;
void *CMemoryWriter::save_to()
{
	return pointer();
}

//------------------------------------------------------------------------------------
// Read
//------------------------------------------------------------------------------------

#pragma warning (disable:4701)

template <typename T>
inline	u64 IReaderBase<T>::find_chunk(u64 ID, bool* bCompressed)
{
	u64	dwSize, dwType;

	bool success = false;

	if (m_last_pos != 0)
	{
		impl().seek(m_last_pos);
		dwType = r_u64();
		dwSize = r_u64();

		if (dwType == ID)
			success = true;
	}

	if (!success)
	{
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
				impl().advance(dwSize);
			}
		}

		if (!success)
		{
			m_last_pos = 0;
			return 0;
		}
	}

	wxASSERT((u64)impl().tell() + dwSize <= (u64)impl().length());
	if (bCompressed) *bCompressed = false;

	const int dwPos = impl().tell();
	if (dwPos + dwSize < (u64)impl().length())
	{
		m_last_pos = dwPos + dwSize;
	}
	else
	{
		m_last_pos = 0;
	}

	return dwSize;
}

IReader*	IReader::open_chunk(u64 ID)
{
	bool	bCompressed;

	u32	dwSize = find_chunk(ID, &bCompressed);
	if (dwSize != 0) {
		if (bCompressed) {
			BYTE*		dest;
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

u32 IReader::find_chunk(u64 ID, bool* bCompressed)
{
	return inherited::find_chunk(ID, bCompressed);
}

IReader*	IReader::open_chunk_iterator(u64& ID, IReader* _prev)
{
	if (0 == _prev) {
		// first
		rewind();
	}
	else {
		// next
		seek(_prev->iterpos);
		_prev->close();
	}

	//	open
	if (elapsed() < 8)
		return NULL;

	ID = r_u64();
	u64 _size = r_u64();

	if (false)
	{
		// compressed
		u8*				dest;
		unsigned		dest_sz;
		_decompressLZ(&dest, &dest_sz, pointer(), _size);
		return new IReader(dest, dest_sz, tell() + _size);
	}
	else {
		// normal
		return new IReader(pointer(), _size, tell() + _size);
	}
}

void	IReader::r(void *p, int cnt)
{
	wxASSERT(Pos + cnt <= Size);
	CopyMemory(p, pointer(), cnt);
	advance(cnt);
};

inline bool is_term(char c) { 
	return (c == 13) || (c == 10); 
};

inline u32	IReader::advance_term_string()
{
	u32 sz = 0;
	char *src = (char *)data;
	while (!eof()) {
		Pos++;
		sz++;
		if (!eof() && is_term(src[Pos]))
		{
			while (!eof() && is_term(src[Pos]))
				Pos++;
			break;
		}
	}
	return sz;
}
void	IReader::r_string(char *dest, u32 tgt_sz)
{
	char *src = (char *)data + Pos;
	u32 sz = advance_term_string();
	wxASSERT(sz < (tgt_sz - 1));
	wxASSERT(!IsBadReadPtr((void*)src, sz));

	strncpy_s(dest, tgt_sz, src, sz);

	dest[sz] = 0;
}
void	IReader::r_string(std::string& dest)
{
	char *src = (char *)data + Pos;
	u32 sz = advance_term_string();
	dest.assign(src, sz);
}

void	IReader::r_string(wxString& dest)
{
	char *src = (char *)data + Pos;
	u32 sz = advance_term_string();
	dest = wxString::FromUTF8(src, sz);
}

void	IReader::r_stringZ(char *dest, u32 tgt_sz)
{
	char *src = (char *)data;
	u32 sz = strlen(src);
	wxASSERT(sz < tgt_sz);
	while ((src[Pos] != 0) && (!eof())) *dest++ = src[Pos++];
	*dest = 0;
	Pos++;
}

void	IReader::r_stringZ(std::string& dest)
{
	dest = (char*)(data + Pos);
	Pos += int(dest.size() + 1);
}

void	IReader::r_stringZ(wxString& dest)
{
	std::string destSrc = (char*)(data + Pos);
	Pos += int(destSrc.size() + 1);
	dest = wxString::FromUTF8(destSrc);
}

void	IReader::skip_stringZ()
{
	char *src = (char *)data;
	while ((src[Pos] != 0) && (!eof())) Pos++;
	Pos++;
}

CMemoryReader*	CMemoryReader::open_chunk(u64 ID)
{
	bool	bCompressed;

	u32	dwSize = find_chunk(ID, &bCompressed);

	if (dwSize != 0) {
		if (bCompressed) {
			BYTE*		dest;
			unsigned	dest_sz;
			_decompressLZ(&dest, &dest_sz, pointer(), dwSize);
			return new CMemoryReader(dest, dest_sz, tell() + dwSize);
		}
		else {
			return new CMemoryReader(pointer(), dwSize, tell() + dwSize);
		}
	}

	return NULL;
};

CMemoryReader*	CMemoryReader::open_chunk_iterator(u64& ID, CMemoryReader* _prev)
{
	if (0 == _prev) {
		// first
		rewind();
	}
	else {
		// next
		seek(_prev->iterpos);
		_prev->close();
	}

	//	open
	if (elapsed() < 8)
		return NULL;

	ID = r_u64();
	u64 _size = r_u64();

	if (false)
	{
		// compressed
		u8*				dest;
		unsigned		dest_sz;
		_decompressLZ(&dest, &dest_sz, pointer(), _size);
		return new CMemoryReader(dest, dest_sz, tell() + _size);
	}
	else {
		// normal
		return new CMemoryReader(pointer(), _size, tell() + _size);
	}
}
