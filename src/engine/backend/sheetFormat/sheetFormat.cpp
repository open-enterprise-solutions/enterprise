#include "backend/sheetFormat/sheetFormat.h"
#include "backend/temp/tempStorage.h"   // ibTempFile — the formats over the session's temporary storage

#include <wx/filename.h>
#include <wx/wfstream.h>

#include <algorithm>
#include <cstring>

// ⭐⭐ THE REGISTRY — filled by the formats themselves, never edited here.
//
// A format's own file ends with SHEET_FORMAT_REGISTER(...), so writing a format
// and reaching it are one act. This file therefore names no format at all: it
// answers "which of you reads this name" and "what does a file dialog show", and
// gains nothing when a fourth format lands.
//
// ⚠ THE LIST IS A FUNCTION-LOCAL STATIC, and that is what makes registration at
// static-initialisation time safe: a registrar in another translation unit runs
// before or after this one in an order nobody controls, and a namespace-level
// vector could still be unbuilt when the first one calls. The function builds it
// on first use, whenever that is.
namespace {
std::vector<const ibSheetFormat*>& Registry()
{
	static std::vector<const ibSheetFormat*> s_formats;
	return s_formats;
}
} // namespace

void ibRegisterSheetFormat(const ibSheetFormat* format)
{
	if (format != nullptr)
		Registry().push_back(format);
}

const std::vector<const ibSheetFormat*>& ibSheetFormats()
{
	return Registry();
}

const ibSheetFormat* ibSheetFormatFor(const wxString& fileName)
{
	// BY EXTENSION, and case-blind: a file arrives as .XLSX from one mail client and
	// .xlsx from the next, and they are the same file.
	const wxString extension = wxFileName(fileName).GetExt();
	if (extension.IsEmpty())
		return nullptr;

	for (const ibSheetFormat* format : ibSheetFormats())
		if (extension.IsSameAs(format->GetExtension(), false))
			return format;

	return nullptr;
}

// ⚠ WHAT CAN BE OPENED, not what exists. A write-only format (Word today, PDF
// next) must not appear under Open — see ibSheetFormat::CanRead.
wxString ibSheetFormatMask()
{
	wxString mask;
	for (const ibSheetFormat* format : ibSheetFormats()) {
		if (!format->CanRead())
			continue;
		if (!mask.IsEmpty())
			mask += wxT(";");
		mask += wxT("*.") + format->GetExtension();
	}
	return mask;
}

wxString ibSheetFormatExtensions()
{
	wxString extensions;
	for (const ibSheetFormat* format : ibSheetFormats()) {
		if (!format->CanRead())
			continue;
		if (!extensions.IsEmpty())
			extensions += wxT(";");
		extensions += format->GetExtension();
	}
	return extensions;
}

bool ibSheetFormat::ReadFile(const wxString& fileName, ibSpreadsheetDescription& sheet) const
{
	wxFileInputStream file(fileName);
	return file.IsOk() && Read(file, sheet);
}

bool ibSheetFormat::WriteFile(const wxString& fileName, const ibSpreadsheetDescription& sheet) const
{
	wxFileOutputStream file(fileName);
	if (!file.IsOk() || !Write(file, sheet))
		return false;

	// ⚠ CLOSED EXPLICITLY and its answer read: a stream that is merely destructed can leave a file that exists, has a
	// size, and is missing its last block.
	return file.Close();
}

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

// …and the other direction, as the NAMED LINES a save dialog offers. Our own
// format first, because that is the one a person means when they just press Save.
wxString ibSheetFormatSaveFilter()
{
	wxString filter;
	for (const ibSheetFormat* format : ibSheetFormats()) {
		if (!format->CanWrite())
			continue;
		if (!filter.IsEmpty())
			filter += wxT("|");
		filter += wxString::Format(wxT("%s (*.%s)|*.%s"),
			format->GetName(), format->GetExtension(), format->GetExtension());
	}
	return filter;
}
