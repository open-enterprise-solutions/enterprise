#include "core/spreadsheet/sheetFormat.h"

#include "core/fileSystem.h"   // ibPathOf — std::filesystem reads the extension

#include <wx/wfstream.h>

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

const ibSheetFormat* ibSheetFormatFor(const ibString& fileName)
{
	// BY EXTENSION, and case-blind: a file arrives as .XLSX from one mail client and
	// .xlsx from the next, and they are the same file.
	const ibString extension = ibString(ibPathOf(fileName).extension().wstring()).Mid(1);
	if (extension.IsEmpty())
		return nullptr;

	for (const ibSheetFormat* format : ibSheetFormats())
		if (extension.IsSameAs(format->GetExtension(), false))
			return format;

	return nullptr;
}

// ⚠ WHAT CAN BE OPENED, not what exists. A write-only format (Word today, PDF
// next) must not appear under Open — see ibSheetFormat::CanRead.
ibString ibSheetFormatMask()
{
	ibString mask;
	for (const ibSheetFormat* format : ibSheetFormats()) {
		if (!format->CanRead())
			continue;
		if (!mask.IsEmpty())
			mask += ibT(";");
		mask += ibT("*.") + format->GetExtension();
	}
	return mask;
}

ibString ibSheetFormatExtensions()
{
	ibString extensions;
	for (const ibSheetFormat* format : ibSheetFormats()) {
		if (!format->CanRead())
			continue;
		if (!extensions.IsEmpty())
			extensions += ibT(";");
		extensions += format->GetExtension();
	}
	return extensions;
}

// …and the other direction, as the NAMED LINES a save dialog offers. Our own
// format first, because that is the one a person means when they just press Save.
ibString ibSheetFormatSaveFilter()
{
	ibString filter;
	for (const ibSheetFormat* format : ibSheetFormats()) {
		if (!format->CanWrite())
			continue;
		if (!filter.IsEmpty())
			filter += ibT("|");
		filter += ibString::Format(ibT("%s (*.%s)|*.%s"),
			format->GetName(), format->GetExtension(), format->GetExtension());
	}
	return filter;
}

bool ibSheetFormat::ReadFile(const ibString& fileName, ibSpreadsheetDescription& sheet) const
{
	wxFileInputStream file(fileName);
	return file.IsOk() && Read(file, sheet);
}

bool ibSheetFormat::WriteFile(const ibString& fileName, const ibSpreadsheetDescription& sheet) const
{
	wxFileOutputStream file(fileName);
	if (!file.IsOk() || !Write(file, sheet))
		return false;

	// ⚠ CLOSED EXPLICITLY and its answer read: a stream that is merely destructed can leave a file that exists, has a
	// size, and is missing its last block.
	return file.Close();
}
