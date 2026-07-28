#ifndef __ADVPROP_COMMAND_SOURCE_H__
#define __ADVPROP_COMMAND_SOURCE_H__

#include "frontend/propertyManager/property/advprop/advpropType.h"

class BACKEND_API ibPropertyObject;

// -----------------------------------------------------------------------
// ibPGCommandSourceProperty — the inspector picker for a button's COMMAND SOURCE
// -----------------------------------------------------------------------
//
// The command-door twin of ibPGDataSourceProperty. A text-and-button cell whose "…" opens a DIALOG showing
// the form's command sources (own object · main attribute · each table · general — the shared GatherFormCommands)
// grouped as a tree; picking one binds its command-hop path to the button. Registered for ibPropertyCommandSource.
class ibPGCommandSourceProperty : public wxPGProperty {
public:

	const ibPropertyObject* GetPropertyObject() const { return m_propertyObject; }

	ibPGCommandSourceProperty(const ibPropertyObject* property = nullptr, const wxString& label = wxPG_LABEL,
		const wxString& name = wxPG_LABEL, const wxVariant& value = wxNullVariant);

	virtual wxString ValueToString(wxVariant& value,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;

	// Show the bound command's OWN picture in the cell (before the text) so the user sees WHICH command is pulled —
	// resolved live when the value is set (m_valueBitmapBundle is the base wxPGProperty value image).
	virtual void OnSetValue() override;

	virtual bool StringToValue(wxVariant& variant, const wxString& text,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;

	virtual wxPGEditorDialogAdapter* GetEditorDialog() const override;

private:
	const ibPropertyObject* m_propertyObject = nullptr;   // the command bar item — vends the form to gather from
	WX_PG_DECLARE_PROPERTY_CLASS(ibPGCommandSourceProperty)
};

#endif // !__ADVPROP_COMMAND_SOURCE_H__
