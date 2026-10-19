#ifndef __ADVPROP_COMMAND_SOURCE_H__
#define __ADVPROP_COMMAND_SOURCE_H__

#include <wx/propgrid/propgrid.h>

#include "frmclient/backend/backend_core.h"

class FRMCLIENT_API ibPropertyObject;

// -----------------------------------------------------------------------
// ibPGCommandSourceProperty — the inspector picker for a button's COMMAND SOURCE
// -----------------------------------------------------------------------
//
// The desktop's (frontend/propertyManager/property/advprop/advpropCommandSource.h), the command-door twin of
// ibPGDataSourceProperty. A text-and-button cell whose "…" opens a DIALOG showing the form's command sources (own
// object · main attribute · each table · general) grouped as a tree; picking one binds its command-hop path to the
// button.
//
// ⚠ ON THE CLIENT IT SHOWS THE BINDING, IT DOES NOT MAKE ONE YET. The command sources are gathered by the form's
// command door (ibFrontendCommandReceiver, GatherFormCommands) over the form's commands and its attributes'
// — the server's; the cell reads as the server wrote it, and the button says so.
class ibPGCommandSourceProperty : public wxPGProperty {
public:

	const ibPropertyObject* GetPropertyObject() const { return m_propertyObject; }

	ibPGCommandSourceProperty(const ibPropertyObject* property = nullptr, const wxString& label = wxPG_LABEL,
		const wxString& name = wxPG_LABEL, const wxVariant& value = wxNullVariant);

	virtual wxString ValueToString(wxVariant& value,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;

	// The bound command's OWN picture in the cell, before the text — on the desktop resolved live through the
	// command door when the value is set; none on the client yet.
	virtual void OnSetValue() override;

	virtual bool StringToValue(wxVariant& variant, const wxString& text,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;

	virtual wxPGEditorDialogAdapter* GetEditorDialog() const override;

private:
	const ibPropertyObject* m_propertyObject = nullptr;   // the command bar item — vends the form to gather from
	WX_PG_DECLARE_PROPERTY_CLASS(ibPGCommandSourceProperty)
};

#endif // !__ADVPROP_COMMAND_SOURCE_H__
