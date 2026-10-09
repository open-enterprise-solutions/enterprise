#ifndef __ADVPROP_SIZE_H__
#define __ADVPROP_SIZE_H__

#include <wx/propgrid/propgrid.h>
#include "frmclient/backend/backend_core.h"

// -----------------------------------------------------------------------
// ibPGSizeProperty
// -----------------------------------------------------------------------

class ibPGSizeProperty : public wxPGProperty {
public:
	
	ibPGSizeProperty(const wxString& label = wxPG_LABEL,
		const wxString& name = wxPG_LABEL,
		const wxSize& value = wxSize());

	virtual wxVariant ChildChanged(wxVariant& thisValue, int childIndex,
		wxVariant& childValue) const override;

	virtual void RefreshChildren() override;

protected:
	void DoSetValue(const wxSize& value) { m_value = WXVARIANT(value); }
private:
	WX_PG_DECLARE_PROPERTY_CLASS(ibPGSizeProperty);
};

#endif