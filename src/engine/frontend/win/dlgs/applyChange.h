#ifndef __CHANGING_STRUCTURE_H__
#define __CHANGING_STRUCTURE_H__

#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
#include <wx/intl.h>
#include <wx/string.h>
#include <wx/stattext.h>
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/listbox.h>
#include <wx/sizer.h>
#include <wx/button.h>
#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/icon.h>
#include <wx/dialog.h>

///////////////////////////////////////////////////////////////////////////

#include "frontend/frontend.h"
#include "backend/metaCollection/metaObject.h"

///////////////////////////////////////////////////////////////////////////

class FRONTEND_API ibDialogApplyChange : public wxDialog
{
	wxStaticText* m_staticInformation;
	wxListBox* m_resultBox;
	wxButton* m_buttonApply;
	wxButton* m_buttonCancel;

private:
	ibDialogApplyChange(const ibRestructureInfo& info, wxWindow* parent);
public:

	static bool ShowApplyChange(const ibRestructureInfo& info, wxWindow* parent) {

		if (info.IsEmpty())
			return true;

		ibDialogApplyChange dlg(info, parent);
		return dlg.ShowModal() == wxID_OK;
	}
};

#endif