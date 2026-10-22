#ifndef __ADVPROP_HYPERLINK_H__
#define __ADVPROP_HYPERLINK_H__

#include <wx/propgrid/propgrid.h>
#include <wx/propgrid/advprops.h>

#include "frmclient/backend/backend_core.h"
#include "frmclient/backend/metaData.h"   // ibMetaData — the configuration of the object it opens

// -----------------------------------------------------------------------
// ibPGHyperLinkProperty
// -----------------------------------------------------------------------

// THE LINK OPENS AN OBJECT — a module, a form, a template, whichever the server named in the row (ibProperties::Object):
// what OpenForm takes, its id in its configuration. What the object is, the link does not know.
class ibPGHyperLinkProperty : public wxPGProperty {
public:

	ibPGHyperLinkProperty(ibMetaID id = 0, const ibMetaData& metaData = ibMetaData(), const wxString& label = wxPG_LABEL,
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
	ibMetaID m_metaId = 0;
	ibMetaData m_metaData;
private:
	WX_PG_DECLARE_PROPERTY_CLASS(ibPGHyperLinkProperty);
};

#endif
