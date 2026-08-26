#ifndef __SHEET_FORMAT_NATIVE_H__
#define __SHEET_FORMAT_NATIVE_H__

/////////////////////////////////////////////////////////////////////////////
// ibSheetFormatNative — OUR OWN table file (.oxl), stated as a format like any
// other.
//
// ⭐⭐ WHY IT IS HERE AT ALL. Without it "our own" would be the ELSE of every
// question: the document would ask "is this a foreign format? then that reader,
// otherwise my own bytes", and the file dialog would glue our extension onto a
// list of the others. One branch and one glue — two places to forget the day a
// third format lands.
//
// So the native layout takes its place in the same registry: the document asks
// WHICH format reads this name and gets an answer for every name it can open,
// and the dialog asks for the mask and gets all of them.
//
// The bytes themselves are not written here — they are
// ibSpreadsheetDescriptionMemory's, exactly as before. This file only says that
// they are a format, and which extension they answer to (ibFileKind::Table, so
// the name still lives in the one table that names files).
/////////////////////////////////////////////////////////////////////////////

#include "backend/sheetFormat/sheetFormat.h"

class BACKEND_API ibSheetFormatNative : public ibSheetFormat {
public:

	virtual wxString GetName() const override;
	virtual wxString GetExtension() const override;

	virtual bool Read(const wxString& fileName, ibSpreadsheetDescription& sheet) const override;
	virtual bool Write(const wxString& fileName, const ibSpreadsheetDescription& sheet) const override;
};

#endif // !__SHEET_FORMAT_NATIVE_H__
