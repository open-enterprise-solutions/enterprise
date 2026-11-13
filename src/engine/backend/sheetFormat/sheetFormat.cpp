#include "backend/sheetFormat/sheetFormat.h"
#include "backend/temp/tempStorage.h"   // ibTempFile and its streams — the formats over the session's temporary storage

#include <istream>
#include <ostream>

bool ibSheetFormatReadTempFile(const ibString& id, ibSpreadsheetDescription& sheet)
{
	ibTempFile file(id);
	const ibSheetFormat* const format = file.IsOpened() ? ibSheetFormatFor(file.GetName()) : nullptr;
	if (format == nullptr || !format->CanRead())
		return false;

	ibTempFileReader reader(file);
	std::istream input(&reader);
	return format->Read(input, sheet);
}

bool ibSheetFormatWriteTempFile(const ibString& id, const ibSpreadsheetDescription& sheet)
{
	ibTempFile file;
	if (!file.Create(id, true))
		return false;
	const ibSheetFormat* const format = ibSheetFormatFor(file.GetName());
	if (format == nullptr || !format->CanWrite())
		return false;

	ibTempFileWriter writer(file);
	std::ostream output(&writer);
	return format->Write(output, sheet) && writer.Finish();
}
