#ifndef __SHEET_FORMAT_DOCX_H__
#define __SHEET_FORMAT_DOCX_H__

/////////////////////////////////////////////////////////////////////////////
// ibSheetFormatDocx — the document written out as a Word file (.docx).
//
// ⭐ THE SAME TWO BRICKS AS THE WORKBOOK. A .docx is a zip of XML parts, exactly
// as an .xlsx is; what differs is the vocabulary inside — WordprocessingML,
// where our sheet becomes ONE TABLE: a row per row, a cell per cell, the merges
// expressed as horizontal and vertical spans.
//
// ⭐⭐ WRITE ONLY, AND THAT IS A PROPERTY OF THE FORMAT, not a gap to fill later.
// A Word file is a FLOW of paragraphs that may contain tables, pictures, section
// breaks and anything else a person put there; reading one back as a grid would
// have to guess which of its tables is "the sheet" and would silently drop
// everything around it. Sending a report out is a real need; taking a Word
// document in as a spreadsheet is a different feature with different questions.
//
// ⚠ SO IT ANSWERS FALSE TO CanRead, and the file dialog for OPEN never offers
// it — a format that appears in Open and then refuses every file is worse than
// one that is honestly absent.
/////////////////////////////////////////////////////////////////////////////

#include "backend/sheetFormat/sheetFormat.h"

class BACKEND_API ibSheetFormatDocx : public ibSheetFormat {
public:

	virtual wxString GetName() const override { return _("Word document"); }
	virtual wxString GetExtension() const override { return wxT("docx"); }

	// See above — this one only goes out.
	virtual bool CanRead() const override { return false; }

	virtual bool Read(const wxString& fileName, ibSpreadsheetDescription& sheet) const override;
	virtual bool Write(const wxString& fileName, const ibSpreadsheetDescription& sheet) const override;
};

#endif // !__SHEET_FORMAT_DOCX_H__
