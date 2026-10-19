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

// A plain WINDOW-typed container: a panel with its own sizer, nothing else. Lets a form hold
// several of these side by side and toggle each one's Visible property (free on any
// ibValueWindow — see window.cpp's UpdateWindow) to swap which one shows, without going
// through wxAuiNotebook at all. NotebookPage can't fill that role on its own: it hard-requires
// a wxAuiNotebook parent (see notebookPage.cpp's OnCreated/OnUpdated).
class ibValuePanel : public ibValueWindow {
public:

	ibValuePanel();

	//get title
	virtual wxString GetControlTitle() const {
		return _("Panel");
	}

	//control factory
	virtual wxObject* Create(ibFrontendWindow* wxparent, ibVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost, bool firstCreated) override;
	virtual void Update(wxObject* wxobject, ibVisualHost* visualHost) override;
	virtual void OnUpdated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* wxobject, ibVisualHost* visualHost) override;

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
