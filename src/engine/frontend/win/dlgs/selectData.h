#ifndef _SELECT_DATA_WND_H__
#define _SELECT_DATA_WND_H__

#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
#include <wx/listctrl.h>
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/string.h>
#include <wx/button.h>
#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/icon.h>
#include <wx/sizer.h>
#include <wx/dialog.h>

#include "backend/metaData.h"

class CDialogSelectDataType : public wxDialog {
	wxListCtrl* m_listData;
	wxButton* m_buttonOk;
	wxButton* m_buttonCancel;
	std::map<long, class_identifier_t> m_clsids;
public:
	bool ShowModal(class_identifier_t& clsid);
	CDialogSelectDataType(IMetaData* metaData, std::set<class_identifier_t>& clsids);
	virtual ~CDialogSelectDataType();

protected:

	virtual void OnListItemSelected(wxListEvent& event);
};

#endif