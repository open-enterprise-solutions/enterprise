#include "backend/sheetFormat/sheetFormat.h"
#include "backend/temp/tempStorage.h"   // ibTempFile — the formats over the session's temporary storage

#include <algorithm>
#include <cstring>

namespace {

// A TEMPORARY FILE READ AS A wx STREAM — one part in memory at a time, the next fetched when this one is read (the
// wx twin of ibTempFileReader).
class ibTempFileInputStream : public wxInputStream {
public:

	explicit ibTempFileInputStream(const ibTempFile& file) : m_file(file) {}

protected:

	virtual size_t OnSysRead(void* buffer, size_t size) override {
		// An empty part is a part, so it is passed over, not taken for the end.
		while (m_offset == m_part.GetDataLen()) {
			if (!m_file.ReadPart(m_index, m_part)) {
				m_lasterror = wxSTREAM_EOF;
				return 0;
			}
			++m_index;
			m_offset = 0;
		}
		const size_t count = std::min(size, m_part.GetDataLen() - m_offset);
		std::memcpy(buffer, static_cast<const char*>(m_part.GetData()) + m_offset, count);
		m_offset += count;
		return count;
	}

private:

	const ibTempFile& m_file;
	int               m_index = 0;
	wxMemoryBuffer    m_part;
	size_t            m_offset = 0;
};

// A TEMPORARY FILE WRITTEN AS A wx STREAM — a part goes down every kPartSize bytes, and Close() writes the rest (the wx
// twin of ibTempFileWriter).
class ibTempFileOutputStream : public wxOutputStream {
public:

	explicit ibTempFileOutputStream(ibTempFile& file) : m_file(file) {}

	virtual bool Close() override { return WritePart() && wxOutputStream::Close(); }

protected:

	virtual size_t OnSysWrite(const void* buffer, size_t size) override {
		const char* from = static_cast<const char*>(buffer);
		for (size_t left = size; left > 0;) {
			const size_t count = std::min(left, ibTempStorage::kPartSize - m_part.GetDataLen());
			m_part.AppendData(from, count);
			from += count;
			left -= count;
			if (m_part.GetDataLen() == ibTempStorage::kPartSize && !WritePart()) {
				m_lasterror = wxSTREAM_WRITE_ERROR;
				return size - left;
			}
		}
		return size;
	}

private:

	bool WritePart() {
		if (m_part.GetDataLen() == 0)
			return true;
		const bool written = m_file.Write(m_part.GetData(), m_part.GetDataLen());
		m_part.SetDataLen(0);
		return written;
	}

	ibTempFile&    m_file;
	wxMemoryBuffer m_part;
};

} // namespace

bool ibSheetFormatReadTempFile(const wxString& id, ibSpreadsheetDescription& sheet)
{
	ibTempFile file(id);
	const ibSheetFormat* const format = file.IsOpened() ? ibSheetFormatFor(file.GetName()) : nullptr;
	if (format == nullptr || !format->CanRead())
		return false;

	ibTempFileInputStream input(file);
	return format->Read(input, sheet);
}

bool ibSheetFormatWriteTempFile(const wxString& id, const ibSpreadsheetDescription& sheet)
{
	ibTempFile file;
	if (!file.Create(id, true))
		return false;
	const ibSheetFormat* const format = ibSheetFormatFor(file.GetName());
	if (format == nullptr || !format->CanWrite())
		return false;

	ibTempFileOutputStream output(file);
	return format->Write(output, sheet) && output.Close();
}
