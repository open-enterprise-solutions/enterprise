#ifndef __SHEET_FORMAT_H__
#define __SHEET_FORMAT_H__

/////////////////////////////////////////////////////////////////////////////
// THE TABLE FORMATS OVER THE SESSION'S TEMPORARY STORAGE — the backend's half of
// the list that lives in the core (core/spreadsheet/sheetFormat.h). The formats
// themselves are here too, each registering into that list from its own file.
/////////////////////////////////////////////////////////////////////////////

#include "backend/backend.h"
#include "core/spreadsheet/sheetFormat.h"

// ⭐ THE SAME FORMATS OVER THE SESSION'S TEMPORARY STORAGE, BY ID — a file a thin client handed over, read by the
// format the name it came under says; a sheet written into a file of it, by the format the name it was made under says.
// The parts stream straight through: no file on disk.
BACKEND_API bool ibSheetFormatReadTempFile(const wxString& id, ibSpreadsheetDescription& sheet);
BACKEND_API bool ibSheetFormatWriteTempFile(const wxString& id, const ibSpreadsheetDescription& sheet);

#endif // !__SHEET_FORMAT_H__
