#ifndef __ADVPROP_HYPERLINK_H__
#define __ADVPROP_HYPERLINK_H__

#include "deskgui/univ/propgrid/propgrid.h"
#include "deskgui/univ/propgrid/advprops.h"

#include "frmclient/backend/backend_core.h"
#include "frmclient/backend/metaData.h"   // ibMetaData — the configuration of the object it opens

// -----------------------------------------------------------------------
// ibPGHyperLinkProperty
// -----------------------------------------------------------------------

// THE LINK OPENS AN OBJECT — a module, a form, a template, whichever the server named in the row (ibProperties::Object):
// what OpenForm takes, its id in its configuration. What the object is, the link does not know.
class ibPGHyperLinkProperty : public ibPGProperty {
public:

	ibPGHyperLinkProperty(ibMetaID id = 0, const ibMetaData& metaData = ibMetaData(), const wxString& label = ibPG_LABEL,
		const wxString& name = ibPG_LABEL, const ibVariant& value = ibVariant());
	virtual ~ibPGHyperLinkProperty();

	virtual wxString ValueToString(ibVariant& value,
		ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;

	virtual bool StringToValue(ibVariant& variant,
		const wxString& text,
		ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;

	virtual bool OnEvent(ibPropertyGrid* propgrid, wxWindow* wnd_primary, wxEvent& event) override;

	// Refresh values of child properties.
	// Automatically called after value is set.
	virtual void RefreshChildren();

protected:
	ibMetaID m_metaId = 0;
	ibMetaData m_metaData;
private:
	IB_PG_DECLARE_PROPERTY_CLASS(ibPGHyperLinkProperty);
};

#endif
