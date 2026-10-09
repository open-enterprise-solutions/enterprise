#ifndef _FRMCLIENT_BACKEND_SHEET_FORMAT_H__
#define _FRMCLIENT_BACKEND_SHEET_FORMAT_H__

#include <wx/string.h>

struct ibSpreadsheetDescription;

// THE TABLE FORMATS — the server's (backend/sheetFormat/): a file of the person's is read and written there, and a
// sheet comes to the client already read. The client's sheet knows none of them — no name is one it reads, and
// LoadFromFile / SaveToFile answer false.
struct ibSheetFormat {
	bool ReadFile(const wxString& WXUNUSED(fileName), ibSpreadsheetDescription& WXUNUSED(sheet)) const { return false; }
	bool WriteFile(const wxString& WXUNUSED(fileName), const ibSpreadsheetDescription& WXUNUSED(sheet)) const { return false; }
};

inline const ibSheetFormat* ibSheetFormatFor(const wxString& WXUNUSED(fileName)) { return nullptr; }

// What a sheet saves as — the server's to say (a tab's Formats); none from here.
inline wxString ibSheetFormatSaveFilter() { return wxString(); }

#endif
