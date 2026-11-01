#ifndef __ADVPROP_VALUE_H__
#define __ADVPROP_VALUE_H__

#include <wx/propgrid/propgrid.h>
#include "frmclient/backend/propertyManager/propertyObject.h"   // ibPropertyChoiceList — the server's choices

// -----------------------------------------------------------------------
// ibPGValueProperty
// -----------------------------------------------------------------------

// A VALUE OF THE TYPE A BINDING HOLDS (the server's ibPropertyValue) — a radio button's ChoiceValue. Where the binding's
// type is chosen (a boolean, an enumeration: the server's choices), a drop-down of them, the chosen one sent by its id;
// any other type is typed in as text, which the server reads as a value of the type.
class ibPGValueProperty : public wxPGProperty {
public:
	ibPGValueProperty(const wxString& label = wxPG_LABEL, const wxString& name = wxPG_LABEL,
		const ibPropertyChoiceList& choices = ibPropertyChoiceList(), const wxVariant& value = wxNullVariant);

	virtual const wxPGEditor* DoGetEditorClass() const override;

	virtual wxString ValueToString(wxVariant& value,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;
	virtual bool StringToValue(wxVariant& variant, const wxString& text,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;
	virtual bool IntToValue(wxVariant& value, int number,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;

	// The chosen one's place among the choices — the drop-down's selection.
	virtual int GetChoiceSelection() const override;

private:
	wxDECLARE_DYNAMIC_CLASS(ibPGValueProperty);
};

#endif
