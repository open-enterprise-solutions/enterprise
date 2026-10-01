#ifndef __SHEET_FORMAT_H__
#define __SHEET_FORMAT_H__

/////////////////////////////////////////////////////////////////////////////
// ibSheetFormat — A FOREIGN FILE THAT IS A TABLE, read into our own document
// and written back out of it.
//
// ⭐⭐ A FORMAT IS A PROVIDER, not a method on the document — the arrangement
// ibFormatProvider already has one storey down (serialize/dataBuilder.h): the
// document states what it HOLDS, a provider states how that looks as bytes, and
// a second format is a second provider rather than an edit to either side.
//
// ⚠ IT LIVES IN THE BACKEND, and that is the point of the placement. Exporting a
// report is something a SCHEDULED JOB does at four in the morning with no window
// open; a reader living in the frontend would work only where somebody is
// looking at it.
//
// ⭐⭐ A WORKBOOK BECOMES ONE DOCUMENT, ITS SHEETS SEPARATED BY PAGE BREAKS (Max,
// 2026-08-26: *"when I open a workbook, the books turn into pages — you add a
// separator"*). We have no tabs and are not growing any: what a second sheet
// means for us is "this part prints separately", and the document already says
// that with a row break. So reading is a straight concatenation, and nothing in
// the grid, the report or the editor learns a new concept.
/////////////////////////////////////////////////////////////////////////////

#include "backend/backend.h"
#include "backend/spreadsheetDescription.h"

// WHAT A FOREIGN TABLE CAN CARRY ACROSS. Stated here rather than discovered per
// format, because it is the same answer for all of them and a person deserves to
// be told it before they save, not after:
//
//   ACROSS — the cell's text, its font, colours, borders and alignment, merged
//            cells, column widths and row heights, frozen panes, outline groups
//            and page breaks;
//   NOT    — the details PARAMETER a cell carries (our drill-down) and the named
//            AREAS. Those are ours; Excel has nowhere to put them, so they are
//            dropped on the way out and absent on the way in.
//
// ⚠ FORMULAS ARE READ AS THEIR RESULT. A cell in a foreign file may hold one, and
// we do not evaluate formulas — so what comes in is the value the other program
// last computed, which is what the person saw there.
class BACKEND_API ibSheetFormat {
public:
	virtual ~ibSheetFormat() = default;

	// The name a person sees when choosing (e.g. "Excel workbook") and the
	// extension it is chosen by, without the dot.
	virtual wxString GetName() const = 0;
	virtual wxString GetExtension() const = 0;

	// ⭐⭐ WHICH DIRECTIONS THIS FORMAT GOES, and it is a property of the format
	// rather than a gap in it. A Word file is a FLOW that may contain tables among
	// everything else, and a PDF is a picture of a page — sending a report out to
	// either is a real need; taking one back IN as a grid would have to guess which
	// part of it was the sheet.
	//
	// Asked by the file dialogs, so a format that cannot read never appears under
	// Open — a format offered there that then refuses every file is worse than one
	// honestly absent.
	virtual bool CanRead()  const { return true; }
	virtual bool CanWrite() const { return true; }

	// False on a file that is not this format, cannot be opened, or is damaged in
	// a way that leaves nothing to show. ⚠ NEVER a half-filled document: a caller
	// that gets false must be free to keep what it had.
	virtual bool Read(const wxString& fileName, ibSpreadsheetDescription& sheet) const = 0;
	virtual bool Write(const wxString& fileName, const ibSpreadsheetDescription& sheet) const = 0;
};

// THE FORMAT A FILE NAME MEANS — null when nothing reads it. One place, so every
// host (the editor's Open, a report's Save as, a scheduled job) picks the same
// reader for the same file rather than each keeping its own if-chain.
BACKEND_API const ibSheetFormat* ibSheetFormatFor(const wxString& fileName);

// …and the whole list. Ownership stays with the registrar below.
BACKEND_API const std::vector<const ibSheetFormat*>& ibSheetFormats();

// ⭐⭐ WHAT A FILE DIALOG ASKS — ready to use, not ingredients to assemble.
//
// 🛑 A CALLER MUST NOT BUILD THIS ITSELF. The first cut had docView looping over
// the formats and gluing a mask together, and that is the very habit fileKind.h
// exists to end: a list assembled at the point of use is a list that forgets the
// next entry. A format is registered, and everyone else ASKS.
//
// ⚠ AND THE TWO DIRECTIONS ANSWER DIFFERENTLY, because the formats do:
//
//   ibSheetFormatMask()        "*.oxl;*.xlsx"                what can be OPENED
//   ibSheetFormatExtensions()  "oxl;xlsx"
//   ibSheetFormatSaveFilter()  "Spreadsheet document (*.oxl)|*.oxl|Excel workbook (*.xlsx)|*.xlsx|Word document (*.docx)|*.docx"
//
// The save filter is a LIST OF NAMED LINES rather than one mask, because that is
// how a person chooses a format while saving: they pick "Word document" from the
// list instead of remembering to type an extension.
BACKEND_API wxString ibSheetFormatMask();
BACKEND_API wxString ibSheetFormatExtensions();
BACKEND_API wxString ibSheetFormatSaveFilter();

// ⭐ A FORMAT PUTS ITSELF ON THE LIST — the shape every type in this tree already
// registers by (VALUE_TYPE_REGISTER and its family). One line at the BOTTOM of the
// format's own file; nothing central to edit, so a format cannot be written and
// left unreachable.
//
//     SHEET_FORMAT_REGISTER(ibSheetFormatXlsx);
BACKEND_API void ibRegisterSheetFormat(const ibSheetFormat* format);

#define SHEET_FORMAT_REGISTER(format_class)                                  \
	namespace {                                                              \
	struct format_class##Registrar {                                         \
		format_class##Registrar() { ibRegisterSheetFormat(&m_format); }      \
		format_class m_format;                                               \
	};                                                                       \
	static format_class##Registrar s_##format_class##Registrar;              \
	}

#endif // !__SHEET_FORMAT_H__
