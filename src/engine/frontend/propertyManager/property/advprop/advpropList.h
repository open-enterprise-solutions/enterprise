#ifndef __ADVPROP_LIST_H__
#define __ADVPROP_LIST_H__

#include <wx/propgrid/propgrid.h>
#include "backend/backend_type.h"

// -----------------------------------------------------------------------
// ibPGListProperty
// -----------------------------------------------------------------------

class ibPGListProperty : public wxPGProperty {

public:
	ibPGListProperty(const wxString& label = wxPG_LABEL,
		const wxString& name = wxPG_LABEL, wxPGChoices choices = wxPGChoices(), int value = 0);

	virtual wxString ValueToString(wxVariant& value,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;

	virtual bool StringToValue(wxVariant& variant,
		const wxString& text,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;

	virtual bool IntToValue(wxVariant& value,
		int number,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;

	virtual wxPGEditorDialogAdapter* GetEditorDialog() const override;

private:
	WX_PG_DECLARE_PROPERTY_CLASS(ibPGListProperty);
};

#endif