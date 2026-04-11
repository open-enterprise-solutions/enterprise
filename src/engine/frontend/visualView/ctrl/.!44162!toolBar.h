#ifndef _TOOLBAR_H__
#define _TOOLBAR_H__

#include "window.h"
#include "frontend/win/ctrls/toolBar.h"

class ibValueToolBarItem;
class ibValueToolBarSeparator;

//********************************************************************************************
//*                                 define commom clsid									     *
//********************************************************************************************

//COMMON FORM
const ibClassID g_controlToolBarCLSID = string_to_clsid("CT_TLBR");
const ibClassID g_controlToolBarItemCLSID = string_to_clsid("CT_TLIT");
const ibClassID g_controlToolBarSeparatorCLSID = string_to_clsid("CT_TLIS");

//********************************************************************************************
//*                                 Value Toolbar                                            *
//********************************************************************************************

class ibValueToolbar : public ibValueWindow {
	wxDECLARE_DYNAMIC_CLASS(ibValueToolbar);
public:

	void SetActionSrc(const ibFormID& action) { return m_actSource->SetValue(action); }
	ibFormID GetActionSrc() const { return m_actSource->GetValueAsInteger(); }

	ibValueToolbar();

	//get title
	virtual wxString GetControlTitle() const {

		if (!m_actSource->IsEmptyProperty()) {
			ibValue pvarPropVal;
			if (m_actSource->GetDataValue(pvarPropVal))
				return _("ToolBar") + wxT(": ") + stringUtils::GenerateSynonym(pvarPropVal.GetClassName());
		}

		return _("ToolBar") + wxT(": ") + _("<empty source>");
	}

	//control factory 
	virtual wxObject* Create(wxWindow* wxparent, ibVisualHost* visualHost) override;
