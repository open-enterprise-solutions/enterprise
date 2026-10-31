#include "core/memory/compression.h"
#include "core/memory/memoryBuffer.h"

#include <wx/mstream.h>
#include <wx/zstream.h>

bool ibZlibDeflate(const void* data, std::size_t size, ibMemoryBuffer& packed)
{
	wxMemoryOutputStream out;
	{
		// Default level (6): a database charges per segment, and tight output saves segments too.
		wxZlibOutputStream zip(out, wxZ_DEFAULT_COMPRESSION, wxZLIB_ZLIB);
		zip.Write(data, size);
		if (!zip.Close())
			return false;
	}
	const wxStreamBuffer* const buffer = out.GetOutputStreamBuffer();
	packed.AppendData(buffer->GetBufferStart(), static_cast<std::size_t>(out.GetLength()));
	return true;
}

bool ibZlibInflate(const void* data, std::size_t size, ibMemoryBuffer& bytes)
{
	wxMemoryInputStream in(data, size);
	wxZlibInputStream unzip(in, wxZLIB_ZLIB);
	char chunk[16384];   // on the stack: a session's fiber has a small one
	for (;;) {
		unzip.Read(chunk, sizeof(chunk));
		const std::size_t got = unzip.LastRead();
		if (got == 0)
			break;
		bytes.AppendData(chunk, got);
		if (unzip.Eof())
			break;
	}
	// A corrupt header, a cut-off body or a bad checksum is told by the stream's state, not by a short read.
	return unzip.GetLastError() == wxSTREAM_NO_ERROR || unzip.GetLastError() == wxSTREAM_EOF;
}
