#ifndef _FRMCLIENT_BACKEND_META_TEMPLATE_OBJECT_H__
#define _FRMCLIENT_BACKEND_META_TEMPLATE_OBJECT_H__

#include <wx/icon.h>

#include "core/spreadsheet/spreadsheetObject.h"
#include "core/description/compositionDescription.h"   // ibCompositionDescription — what a composer's tab edits
#include "core/clsid.h"
#include "frmclient/backend/backend_picture.h"   // ibBackendPicture — the template's icon, as the server gave it

class ibMetaData;

// A TEMPLATE OF THE CONFIGURATION — the engine's ibValueMetaObjectTemplateBase and ibValueMetaObjectTemplate
// (backend/metaCollection/metaTemplateObject.h): the designer's, where an area of the editor is written into a
// spreadsheet template, and whose composer tab edits the composition a composer template stores (ibComposerEditor).
// The client's documents have no metaobject — ConvertMetaObjectToType answers none — so nothing here is reached at
// runtime.
class ibValueMetaObjectTemplateBase {
public:
	const ibString& GetName() const { return m_name; }
	bool IsEditable() const { return false; }
	ibSpreadsheetDescription& GetSpreadsheetDesc() { return m_spreadsheetDesc; }
	const ibSpreadsheetDescription& GetSpreadsheetDesc() const { return m_spreadsheetDesc; }
private:
	ibString                 m_name;
	ibSpreadsheetDescription m_spreadsheetDesc;
};

class ibValueMetaObjectTemplate : public ibValueMetaObjectTemplateBase {
public:
	const ibMetaData* GetMetaData() const { return nullptr; }
	ibCompositionDescription GetCompositionDesc() const { return ibCompositionDescription(); }
};

// The template's picture — by the engine's clsid (backend/metaCollection/metaObject.h).
constexpr ibClassID g_metaCommonTemplateCLSID = metadata_to_clsid("MD_CTMP");

#endif
