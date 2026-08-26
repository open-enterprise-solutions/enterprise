#ifndef __SHEET_FORMAT_XLSX_H__
#define __SHEET_FORMAT_XLSX_H__

/////////////////////////////////////////////////////////////////////////////
// ibSheetFormatXlsx — the Excel workbook (2007 and later), read and written
// with what the tree already has: a ZIP stream and an XML parser.
//
// ⭐ NOTHING VENDORED. An .xlsx IS a zip of XML parts, and both halves are in
// wxWidgets already (wxZipInputStream / wxZipOutputStream, and the XML parser
// over expat). What is written here is the mapping between those parts and our
// own document — which is the part no library could have supplied anyway.
//
// ⚠ THE OLD .xls IS A DIFFERENT ANIMAL and is deliberately not here: it is an
// OLE2 compound file (a filesystem inside a file) carrying a stream of BIFF
// records — its own string table, its own palette, its own formats. That is an
// arc of its own, or a third-party library and its licence. Excel itself has
// read and written .xlsx since 2007, so the practical loss is a conversion step
// on files somebody else sent (Max, 2026-08-26: "the second one is not needed
// for now").
/////////////////////////////////////////////////////////////////////////////

#include "backend/sheetFormat/sheetFormat.h"

class BACKEND_API ibSheetFormatXlsx : public ibSheetFormat {
public:

	virtual wxString GetName() const override { return _("Excel workbook"); }
	virtual wxString GetExtension() const override { return wxT("xlsx"); }

	// ⭐ EVERY SHEET OF THE WORKBOOK, ONE AFTER ANOTHER, with a page break between
	// them — see ibSheetFormat for why that is the right shape for us rather than
	// a loss. A single-sheet workbook therefore reads back with no break at all.
	virtual bool Read(const wxString& fileName, ibSpreadsheetDescription& sheet) const override;

	// …and out again as ONE sheet. The document's page breaks travel as Excel's
	// own row breaks, so a document that came from a three-sheet workbook prints
	// in three pages there too.
	virtual bool Write(const wxString& fileName, const ibSpreadsheetDescription& sheet) const override;
};

#endif // !__SHEET_FORMAT_XLSX_H__
