#ifndef __ADVPROP_HYPERLINK_H__
#define __ADVPROP_HYPERLINK_H__

#include <wx/propgrid/propgrid.h>
#include <wx/propgrid/advprops.h>

#include "frmclient/backend/backend_core.h"

class FRMCLIENT_API ibPropertyObject;

// -----------------------------------------------------------------------
// ibPGHyperLinkProperty
// -----------------------------------------------------------------------

class ibPGHyperLinkProperty : public wxPGProperty {
public:

	ibPGHyperLinkProperty(ibPropertyObject* property = nullptr, const wxString& label = wxPG_LABEL,
		const wxString& name = wxPG_LABEL, const wxVariant& value = wxNullVariant);
	virtual ~ibPGHyperLinkProperty();

	virtual wxString ValueToString(wxVariant& value,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;

	virtual bool StringToValue(wxVariant& variant,
		const wxString& text,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;

	virtual void OnSetValue() override;

	// Refresh values of child properties.
	// Automatically called after value is set.
	virtual void RefreshChildren();

protected:
	ibPropertyObject* m_ownerProperty = nullptr;
private:
	WX_PG_DECLARE_PROPERTY_CLASS(ibPGHyperLinkProperty);
};

#endif
