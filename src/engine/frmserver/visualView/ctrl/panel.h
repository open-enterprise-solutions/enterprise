#ifndef _PANEL_H__
#define _PANEL_H__

#include "window.h"

//********************************************************************************************
//*                                 define commom clsid										 *
//********************************************************************************************

constexpr ibClassID g_controlPanelCLSID = control_to_clsid("CT_PANL");

//********************************************************************************************
//*                                   Value Panel                                            *
//********************************************************************************************

// The desktop's (frontend/visualView/ctrl/panel.h): a plain WINDOW-typed container, a panel with its own
// sizer, nothing else. Lets a form hold several of these side by side and toggle each one's Visible
// property (free on any ibValueWindow — see window.cpp's OnUpdate) to swap which one shows, without going
// through a notebook at all. Its controls lie in the panel's own sizer on the client (frmclient's panel).
class ibValuePanel : public ibValueWindow {
public:

	ibValuePanel();

	//get title
	virtual wxString GetControlTitle() const override {
		return _("Panel");
	}

	//load & save object in control
	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:

	ibPropertyCategory* m_categoryPanel = ibPropertyObject::CreatePropertyCategory(wxT("Panel"), _("Panel"));
	ibPropertyEnum<ibValueEnumOrient>* m_propertyOrient = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumOrient>>(m_categoryPanel, wxT("Orient"), _("Orient"),
		_("How the panel lays out the controls placed on it: vertically (the default, one under another) or horizontally (side by side)."),
		wxVERTICAL);
};

#endif
