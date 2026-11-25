#ifndef __SHEET_FORMAT_PDF_H__
#define __SHEET_FORMAT_PDF_H__

/////////////////////////////////////////////////////////////////////////////
// ibSheetFormatPdf — the document written out as a PDF file, laid out on pages as the printout lays it out.
//
// ⭐⭐ THE PRINTOUT'S PAGES, ON THE SERVER. A4 upright, the printout's margins, its bands of columns and of rows, a
// page break where the sheet asks for one and never through a merged cell, then the fills, the text and the rules —
// what a person gets on paper from the window (frmclient's gridPrintout.cpp) is what a scheduled job at four in the
// morning gets in a file, with no window and no printer anywhere.
//
// ⭐⭐ THE FONTS ARE THE SERVER'S — the program's own folder `fonts/` first, then the folders the system keeps its
// fonts in. A cell's face is looked up by its name; a face this machine does not have is drawn in the one its
// metrics were copied for (Arial in Liberation Sans, Times New Roman in Liberation Serif, Courier New in Liberation
// Mono — the program ships those), so the lines break and the pages turn where they do on the screen.
//
// ⭐ WRITE ONLY, as the Word file: a PDF is a picture of a page, and taking one back in as a grid would have to guess
// which of its marks were the sheet.
/////////////////////////////////////////////////////////////////////////////

#include "backend/sheetFormat/sheetFormat.h"

class BACKEND_API ibSheetFormatPdf : public ibSheetFormat {
public:

	virtual ibString GetName() const override { return _("PDF document"); }
	virtual ibString GetExtension() const override { return ibT("pdf"); }

	// See above — this one only goes out.
	virtual bool CanRead() const override { return false; }

	virtual bool Read(std::istream& input, ibSpreadsheetDescription& sheet) const override;
	virtual bool Write(std::ostream& output, const ibSpreadsheetDescription& sheet) const override;
};

#endif // !__SHEET_FORMAT_PDF_H__
