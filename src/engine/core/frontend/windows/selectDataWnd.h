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

#include "compiler/compiler.h"

class CSelectDataTypeWnd : public wxDialog
{
	wxListCtrl* m_listData;
	wxButton* m_buttonOk;
	wxButton* m_buttonCancel;

	std::map<long, CLASS_ID> m_clsids;

public:

	bool ShowModal(CLASS_ID& clsid);

	CSelectDataTypeWnd(IMetadata* metaData, std::set<CLASS_ID>& clsids);
	virtual ~CSelectDataTypeWnd();

protected:

	virtual void OnListItemSelected(wxListEvent& event);
};

#endif