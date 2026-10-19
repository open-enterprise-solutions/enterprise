#ifndef __ADVPROP_SOURCE_H__
#define __ADVPROP_SOURCE_H__

#include <wx/propgrid/propgrid.h>

#include "frmclient/backend/backend_core.h"

class FRMCLIENT_API ibPropertyObject;

// -----------------------------------------------------------------------
// ibPGDataSourceProperty
// -----------------------------------------------------------------------

// THE DESKTOP'S SOURCE CELL (frontend/propertyManager/property/advprop/advpropSource.h) — a control's binding to a value
// of its form's data: the path as text, the "…" opening the tree of the form's attributes and their columns.
//
// ⚠ ON THE CLIENT IT SHOWS THE BINDING AND CLEARS IT, IT DOES NOT MAKE ONE YET. The tree is walked over the form's
// attributes, their tables and references (the owner's source factory) — the server's; so is the Type row the desktop
// unfolds beneath, the type the source's leaf has. The cell reads as the server wrote it, an empty text unbinds, and
// the button says the rest is not here.
class ibPGDataSourceProperty : public wxPGProperty {
public:

	ibPGDataSourceProperty(const ibPropertyObject* property = nullptr, const wxString& label = wxPG_LABEL,
		const wxString& name = wxPG_LABEL,
		const wxVariant& value = wxNullVariant);

	virtual wxString ValueToString(wxVariant& value,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;

	virtual bool StringToValue(wxVariant& variant,
		const wxString& text,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;

	virtual wxPGEditorDialogAdapter* GetEditorDialog() const override;

private:
	WX_PG_DECLARE_PROPERTY_CLASS(ibPGDataSourceProperty)
};

#endif
