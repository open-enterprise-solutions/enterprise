#include "backend/sheetFormat/sheetFormat.h"

#include <wx/filename.h>

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
