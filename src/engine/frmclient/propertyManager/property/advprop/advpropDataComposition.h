#ifndef __ADVPROP_DATA_COMPOSITION_H__
#define __ADVPROP_DATA_COMPOSITION_H__

#include "deskgui/univ/propgrid/propgrid.h"
#include "deskgui/univ/propgrid/advprops.h"

#include "frmclient/backend/backend_core.h"

#include <functional>   // the settings window a registration hands in — see ibSettingsDoor

class FRMCLIENT_API ibPropertyObject;
class ibMetaData;
struct ibCompositionDescription;

// -----------------------------------------------------------------------
// ibPGDataCompositionProperty — the desktop's "Settings…" cell (frontend advpropDataComposition.h) over the server's
// row: the composition is the row's value as the server stores it (ibPropertyDataComposition::WriteNodeValue); a click
// shows the window over a copy read from it and hands the result back through SetValueInEvent as the same property's
// value, the node the inspector sends.
//
// What the window asks of the configuration it asks of the owner's — on a form, the form's tab
// (ibQueryFieldsOfText: fetch {Tab, Request: {Kind: Fields, Text}}).
// -----------------------------------------------------------------------

class ibPGDataCompositionProperty : public ibPGProperty {
public:

	using ibSettingsDoor = std::function<bool(ibCompositionDescription& desc, const ibMetaData* metaData)>;

	ibPGDataCompositionProperty(const ibPropertyObject* property = nullptr, const wxString& label = ibPG_LABEL,
		const wxString& name = ibPG_LABEL, const ibVariant& value = ibVariant(),
		ibSettingsDoor door = ibSettingsDoor());
	virtual ~ibPGDataCompositionProperty() override;

	const ibPropertyObject* GetPropertyObject() const { return m_property; }

	virtual wxString ValueToString(ibVariant& value,
		ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;

	virtual bool StringToValue(ibVariant& variant,
		const wxString& text,
		ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;

	virtual bool OnEvent(ibPropertyGrid* propgrid, wxWindow* wnd_primary, wxEvent& event) override;

	// Refresh values of child properties. Automatically called after value is set.
	virtual void RefreshChildren() override;

protected:

	// The object whose row this cell shows — what the window asks (its configuration) and what is told.
	const ibPropertyObject* m_property = nullptr;

	// Which window this cell opens — handed in at registration.
	ibSettingsDoor m_door;

private:
	IB_PG_DECLARE_PROPERTY_CLASS(ibPGDataCompositionProperty);
};

#endif // __ADVPROP_DATA_COMPOSITION_H__
