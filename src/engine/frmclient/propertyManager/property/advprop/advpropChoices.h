#ifndef __ADVPROP_CHOICES_H__
#define __ADVPROP_CHOICES_H__

#include "deskgui/univ/propgrid/propgrid.h"
#include "deskgui/univ/propgrid/props.h"
#include "frmclient/backend/backend_core.h"

class FRMCLIENT_API ibBackendProperty;

// -----------------------------------------------------------------------
// ibPGChoicesProperty
// -----------------------------------------------------------------------

// A SWITCH'S ITEMS are a table, and a table is not edited on a property row: the row NAMES them (the server's
// presentation — their texts) and the "…" opens the window where they live (win/dlgs/choices), as the choice
// parameters' row does. Typing is refused for the same reason.
class ibPGChoicesProperty : public ibPGProperty {
	IB_PG_DECLARE_PROPERTY_CLASS(ibPGChoicesProperty);
public:

	ibPGChoicesProperty(ibBackendProperty* property = nullptr, const wxString& label = ibPG_LABEL,
		const wxString& name = ibPG_LABEL, const ibVariant& value = ibVariant());

	virtual wxString ValueToString(ibVariant& value,
		ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override { return value.GetString(); }

	virtual bool StringToValue(ibVariant& variant, const wxString& text,
		ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override { return false; }

	// The click on "…" — the window edits a COPY and the value is posted only when it says something changed.
	virtual bool OnEvent(ibPropertyGrid* propgrid, wxWindow* primary, wxEvent& event) override;

protected:

	ibBackendProperty* m_property = nullptr;
};

#endif
