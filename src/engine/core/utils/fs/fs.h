#ifndef _FS_H__
#define _FS_H__

#include <wx/wx.h>

#include <stack>
#include <string>

#include "types.h"

//------------------------------------------------------------------------------------
// Write
//------------------------------------------------------------------------------------
class  IWriter
{
private:
	std::stack<u64>		chunk_pos;
public:
	std::string			fName;
public:
	IWriter()
	{
	}
	virtual	~IWriter()
	{
	}

	// kernel
	virtual void	seek(u32 pos) = 0;
	virtual u32		tell() = 0;

	virtual void	w(const void* ptr, u32 count) = 0;

	// generalized writing functions
	inline void			w_u64(u64 d) { w(&d, sizeof(u64)); }
	inline void			w_u32(u32 d) { w(&d, sizeof(u32)); }
	inline void			w_u16(u16 d) { w(&d, sizeof(u16)); }
	inline void			w_u8(u8 d) { w(&d, sizeof(u8)); }
	inline void			w_s64(s64 d) { w(&d, sizeof(s64)); }
	inline void			w_s32(s32 d) { w(&d, sizeof(s32)); }
	inline void			w_s16(s16 d) { w(&d, sizeof(s16)); }
	inline void			w_s8(s8 d) { w(&d, sizeof(s8)); }
	inline void			w_float(float d) { w(&d, sizeof(float)); }
	inline void			w_string(const char* p) { w(p, (u32)strlen(p)); w_u8(13); w_u8(10); }
	inline void			w_stringZ(const char* p) { w(p, (u32)strlen(p) + 1); }
	inline void			w_stringZ(const std::string& p) { w(p.c_str() ? p.c_str() : "", (u32)p.size()); w_u8(0); }
	inline void			w_stringZ(const wxString& p) { const wxScopedCharBuffer s = p.utf8_str(); w(s.data() ? s.data() : "", (u32)s.length()); w_u8(0); }

	void	__cdecl  	w_printf(const char* format, ...);

	// generalized chunking
	u32				align();
	void			open_chunk(u64 type);
	void			close_chunk();
	u32				chunk_size();					// returns size of currently opened chunk, 0 otherwise
	void			w_compressed(void* ptr, u32 count);
	void			w_compressed(const wxMemoryBuffer& data);
	void			w_chunk(u64 type, void* data, u32 size);
	void			w_chunk(u64 type, const wxMemoryBuffer& data);
	virtual bool	valid() { return true; }
	virtual	void	flush() = 0;
};

class CMemoryWriter : public IWriter
{
	u8* data;
	u32				position;
	u32				mem_size;
	u32				file_size;
public:
	
	CMemoryWriter() {
		data = 0;
		position = 0;
		mem_size = 0;
		file_size = 0;
	}
	
	virtual	~CMemoryWriter();

	// kernel
	virtual void	w(const void* ptr, u32 count);

	virtual void	seek(u32 pos) { position = pos; }
	virtual u32		tell() { return position; }

	// get buffer 
	wxMemoryBuffer buffer() const {
		wxMemoryBuffer bufferData(file_size);
		bufferData.AppendData(data, file_size);
		return bufferData;
	}

	// specific
	inline u8* pointer() { return data; }
	inline u32			size() const { return file_size; }
	inline void			clear() { file_size = 0; position = 0; }
#pragma warning(push)
#pragma warning(disable:4995)
	inline void			free() { file_size = 0; position = 0; mem_size = 0; delete data; }
#pragma warning(pop)
	void* save_to();
	virtual	void	flush() { };
};

//------------------------------------------------------------------------------------
// Read
//------------------------------------------------------------------------------------

template <typename implementation_type>
class IReaderBase
{
public:

	inline			 IReaderBase() : m_last_pos(0) {}
	virtual			~IReaderBase() {}

	inline implementation_type& impl() {
		return *(implementation_type*)this;
	}

	inline const implementation_type& impl() const {
		return *(implementation_type*)this;
	}

	inline bool			eof()	const { return impl().elapsed() <= 0; }
	inline void			r(void* p, int cnt) { impl().r(p, cnt); }

	inline u64			r_u64() { u64   tmp;	r(&tmp, sizeof(tmp)); return tmp; }
	inline u32			r_u32() { u32   tmp;	r(&tmp, sizeof(tmp)); return tmp; }
	inline u16			r_u16() { u16   tmp;	r(&tmp, sizeof(tmp)); return tmp; }
	inline u8			r_u8() { u8    tmp;	r(&tmp, sizeof(tmp)); return tmp; }
	inline s64			r_s64() { s64   tmp;	r(&tmp, sizeof(tmp)); return tmp; }
	inline s32			r_s32() { s32   tmp;	r(&tmp, sizeof(tmp)); return tmp; }
	inline s16			r_s16() { s16   tmp;	r(&tmp, sizeof(tmp)); return tmp; }
	inline s8			r_s8() { s8    tmp;	r(&tmp, sizeof(tmp)); return tmp; }
	inline float		r_float() { float tmp;	r(&tmp, sizeof(tmp)); return tmp; }

	// Set file pointer to start of chunk data (0 for root chunk)
	inline	void		rewind() { impl().seek(0); }
	u64 			    find_chunk(u64 ID, bool* bCompressed);

	inline	bool		r_chunk(u64 ID, void* dest) {	// чтение Chunk'ов (4b-ID,4b-size,??b-data)
		m_last_pos = impl().tell();
		u32	dwSize = ((implementation_type*)this)->find_chunk(ID);
		if (dwSize != 0) {
			r(dest, dwSize);
			return true;
		}
		return false;
	}

	inline	bool		r_chunk(u64 ID, wxMemoryBuffer& dest) {	// чтение Chunk'ов (4b-ID,4b-size,??b-data)
		m_last_pos = impl().tell();
		u32	dwSize = ((implementation_type*)this)->find_chunk(ID);
		if (dwSize != 0) {
			r(dest.GetAppendBuf(dwSize), dwSize);
			dest.UngetAppendBuf(dwSize);
			return true;
		}
		return false;
	}

	inline	bool		r_chunk_safe(u64 ID, void* dest, u32 dest_size) { // чтение Chunk'ов (4b-ID,4b-size,??b-data)
		m_last_pos = impl().tell();
		u64	dwSize = ((implementation_type*)this)->find_chunk(ID);
		if (dwSize != 0) {
			wxASSERT(dwSize == dest_size);
			r(dest, dwSize);
			return true;
		}
		return false;
	}

	inline	bool		r_chunk_safe(u64 ID, wxMemoryBuffer& dest, u32 dest_size) { // чтение Chunk'ов (4b-ID,4b-size,??b-data)
		m_last_pos = impl().tell();
		u64	dwSize = ((implementation_type*)this)->find_chunk(ID);
		if (dwSize != 0) {
			wxASSERT(dwSize == dest_size);
			r(dest.GetAppendBuf(dwSize), dwSize);
			dest.UngetAppendBuf(dwSize);
			return true;
		}
		return false;
	}

private:
	u64					m_last_pos;
};

class IReader : public IReaderBase<IReader>
{
protected:
	char* data;
	int				Pos;
	int				Size;
	int				iterpos;

public:
	inline				IReader()
	{
		Pos = 0;
	}

	virtual			~IReader() {}

	inline				IReader(void* _data, int _size, int _iterpos = 0) {
		data = (char*)_data;
		Size = _size;
		Pos = 0;
		iterpos = _iterpos;
	}

protected:
	inline u32			correction(u32 p) {
		if (p % 16) {
			return ((p % 16) + 1) * 16 - p;
		}
		return 0;
	}

	u32 			advance_term_string();

public:
	inline int			elapsed()	const { return Size - Pos; }
	inline int			tell()	const { return Pos; }
	inline void			seek(int ptr) { Pos = ptr; wxASSERT((Pos <= Size) && (Pos >= 0)); }
	inline int			length()	const { return Size; }
	inline void* pointer()	const { return &(data[Pos]); }
	inline void			advance(int cnt) { Pos += cnt; wxASSERT((Pos <= Size) && (Pos >= 0)); }

public:
	void			r(void* p, int cnt);

	void			r_string(char* dest, u32 tgt_sz);
	void			r_string(std::string& dest);
	void			r_string(wxString& dest);

	void			skip_stringZ();

	wxString		r_stringZ();

	void			r_stringZ(char* dest, u32 tgt_sz);
	void			r_stringZ(std::string& dest);
	void			r_stringZ(wxString& dest);

public:

	void			close();

public:

	// поиск Chunk'ов - возврат - размер или 0
	IReader* open_chunk(u64 ID);
	// iterators
	IReader* open_chunk_iterator(u64& ID, IReader* previous = NULL);	// NULL=first

	u32 			find_chunk(u64 ID, bool* bCompressed = NULL);

private:
	typedef IReaderBase<IReader>	inherited;
};

class CMemoryReader : public IReader
{
public:

	CMemoryReader(const wxMemoryBuffer& buf, int _iterpos = 0) :
		IReader(buf.GetData(), buf.GetDataLen(), _iterpos)
	{
	}

	CMemoryReader(void* _data, int _size, int _iterpos = 0) :
		IReader(_data, _size, _iterpos)
	{
	}

	virtual		~CMemoryReader() {
		/*delete data;*/
	}

	// поиск Chunk'ов - возврат - размер или 0
	CMemoryReader* open_chunk(u64 ID);
	// iterators
	CMemoryReader* open_chunk_iterator(u64& ID, CMemoryReader* previous = NULL);	// NULL=first
};

#endif // !_FS_H__
