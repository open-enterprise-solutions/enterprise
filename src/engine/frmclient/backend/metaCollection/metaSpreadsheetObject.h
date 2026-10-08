#ifndef _FRMCLIENT_BACKEND_META_SPREADSHEET_OBJECT_H__
#define _FRMCLIENT_BACKEND_META_SPREADSHEET_OBJECT_H__

#include <wx/icon.h>

#include "frmclient/backend/backend_spreadsheet.h"
#include "core/clsid.h"
#include "frmclient/backend/backend_picture.h"   // ibBackendPicture — the template's icon, as the server gave it

// A SPREADSHEET TEMPLATE OF THE CONFIGURATION — the engine's ibValueMetaObjectSpreadsheetBase
// (backend/metaCollection/metaSpreadsheetObject.h): the designer's, where an area of the editor is written into the
// template. The client's documents have no metaobject — ConvertMetaObjectToType answers none — so nothing here is
// reached at runtime.
class ibValueMetaObjectSpreadsheetBase {
public:
	wxString GetName() const { return m_name; }
	bool IsEditable() const { return false; }
	ibSpreadsheetDescription& GetSpreadsheetDesc() { return m_spreadsheetDesc; }
	const ibSpreadsheetDescription& GetSpreadsheetDesc() const { return m_spreadsheetDesc; }
private:
	wxString                 m_name;
	ibSpreadsheetDescription m_spreadsheetDesc;
};

// The template's picture — by the engine's clsid (backend/metaCollection/metaObject.h).
constexpr ibClassID g_metaCommonTemplateCLSID = metadata_to_clsid("MD_CTMP");

#endif
