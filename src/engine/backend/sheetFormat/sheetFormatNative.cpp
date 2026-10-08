#include "backend/sheetFormat/sheetFormatNative.h"

#include "backend/fileKind.h"           // the one table that names our files
#include "core/fileSystem/fs.h"      // ibReaderMemory / ibWriterMemory

#include <wx/mstream.h>

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

bool ibSheetFormatNative::Read(wxInputStream& input, ibSpreadsheetDescription& sheet) const
{
	// Whole, to its end — a stream from the temporary storage has no length to ask for up front.
	wxMemoryOutputStream whole;
	input.Read(whole);

	wxMemoryBuffer buffer(whole.GetLength());
	whole.CopyTo(buffer.GetWriteBuf(whole.GetLength()), whole.GetLength());
	buffer.UngetWriteBuf(whole.GetLength());

	if (buffer.GetDataLen() == 0)
		return false;

	// ⚠ THE BUFFER IS A NAMED VARIABLE — ibReaderMemory borrows its bytes and would
	// otherwise read freed memory the moment the expression ended.
	ibReaderMemory reader(buffer);
	if (reader.eof())
		return false;

	return ibSpreadsheetDescriptionMemory::LoadData(reader, sheet);
}

bool ibSheetFormatNative::Write(wxOutputStream& output, const ibSpreadsheetDescription& sheet) const
{
	ibWriterMemory writer;
	if (!ibSpreadsheetDescriptionMemory::SaveData(writer, sheet))
		return false;

	output.Write(writer.pointer(), writer.size());
	return output.IsOk();
}

///////////////////////////////////////////////////////////////////////////////
//							Runtime register
///////////////////////////////////////////////////////////////////////////////

SHEET_FORMAT_REGISTER(ibSheetFormatNative);
