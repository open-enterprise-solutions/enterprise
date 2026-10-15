#include "backend/sheetFormat/sheetFormat.h"
#include "backend/temp/tempStorage.h"   // ibTempFile and its wx streams — the formats over the session's temporary storage

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
