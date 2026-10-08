#ifndef __ADVPROP_FONT_H__
#define __ADVPROP_FONT_H__

#include <wx/propgrid/propgrid.h>
#include "frmclient/win/fontcontainer.h"

#include "frmclient/backend/backend_core.h"

// -----------------------------------------------------------------------
// ibPGFontProperty
// -----------------------------------------------------------------------

class ibPGFontProperty : public wxPGProperty {
public:

	ibPGFontProperty(const wxString& label = wxPG_LABEL, const wxString& name = wxPG_LABEL, const wxFontContainer& value = *wxNORMAL_FONT);
	virtual ~ibPGFontProperty() override;

	virtual wxVariant ChildChanged(wxVariant& thisValue, int childIndex,
		wxVariant& childValue) const override;

	virtual void RefreshChildren() override;

	virtual void OnSetValue() override;
	
	virtual wxString GetValueAsString(wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;

	virtual bool OnEvent(wxPropertyGrid* propgrid, wxWindow* primary, wxEvent& event) override;

private:
	WX_PG_DECLARE_PROPERTY_CLASS(ibPGFontProperty);
};

#endif