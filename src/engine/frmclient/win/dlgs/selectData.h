#ifndef _SELECT_DATA_WND_H__
#define _SELECT_DATA_WND_H__

#include <map>

#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
#include "deskgui/univ/ctrl/listCtrl.h"
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/string.h>
#include "deskgui/univ/ctrl/button.h"
#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/icon.h>
#include <wx/sizer.h>
#include "deskgui/univ/dialog.h"

#include "frmclient/backend/metaData.h"

class ibDialogSelectDataType : public ibDialog {
	ibListCtrl* m_listData;
	ibButton* m_buttonOk;
	ibButton* m_buttonCancel;
	std::map<long, ibClassID> m_listTypeClass;
public:
	bool ShowModal(ibClassID& clsid);
	ibDialogSelectDataType(const ibMetaData* metaData, const std::vector<ibClassID>& array);
	virtual ~ibDialogSelectDataType();

protected:

	virtual void OnListItemSelected(wxListEvent& event);
};

#endif