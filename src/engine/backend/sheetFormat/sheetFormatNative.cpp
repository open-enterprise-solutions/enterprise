#include "backend/sheetFormat/sheetFormatNative.h"

#include "backend/fileKind.h"           // the one table that names our files
#include "backend/fileSystem/fs.h"      // ibReaderMemory / ibWriterMemory

#include <wx/wfstream.h>

wxString ibSheetFormatNative::GetName() const
{
	return _("Spreadsheet document");
}

wxString ibSheetFormatNative::GetExtension() const
{
	// NOT A LITERAL. What our files are called is fileKind.h's answer, and it stays
	// the only one — that table exists because ".oxl" used to be typed in eight
	// places and a rename had to find all of them.
	return ibFileExtension(ibFileKind::Table);
}

bool ibSheetFormatNative::Read(const wxString& fileName, ibSpreadsheetDescription& sheet) const
{
	wxFileInputStream file(fileName);
	if (!file.IsOk())
		return false;

	const wxFileOffset length = file.GetLength();
	if (length <= 0)
		return false;

	wxMemoryBuffer buffer(static_cast<size_t>(length));
	file.Read(buffer.GetWriteBuf(static_cast<size_t>(length)), static_cast<size_t>(length));
	buffer.SetDataLen(static_cast<size_t>(file.LastRead()));

	if (buffer.GetDataLen() == 0)
		return false;

	// ⚠ THE BUFFER IS A NAMED VARIABLE — ibReaderMemory borrows its bytes and would
	// otherwise read freed memory the moment the expression ended.
	ibReaderMemory reader(buffer);
	if (reader.eof())
		return false;

	return ibSpreadsheetDescriptionMemory::LoadData(reader, sheet);
}

bool ibSheetFormatNative::Write(const wxString& fileName, const ibSpreadsheetDescription& sheet) const
{
	ibWriterMemory writer;
	if (!ibSpreadsheetDescriptionMemory::SaveData(writer, sheet))
		return false;

	wxFileOutputStream file(fileName);
	if (!file.IsOk())
		return false;

	file.Write(writer.pointer(), writer.size());

	// ⚠ CLOSED EXPLICITLY and its answer read: a stream that is merely destructed
	// can leave a file that exists, has a size, and is missing its last block.
	return file.Close();
}

///////////////////////////////////////////////////////////////////////////////
//							Runtime register
///////////////////////////////////////////////////////////////////////////////

SHEET_FORMAT_REGISTER(ibSheetFormatNative);
