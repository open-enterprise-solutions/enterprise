#ifndef __ADVPROP_SIZE_H__
#define __ADVPROP_SIZE_H__

#include <wx/propgrid/propgrid.h>
#include "core/graphics/geometry.h"   // ibSize — what the cell holds
#include "backend/backend_core.h"

// -----------------------------------------------------------------------
// ibPGSizeProperty
// -----------------------------------------------------------------------

// The sample's wxSizeProperty, over ibVariantDataSize instead of a wxSize, which is what
// ibPropertySize holds: the cell's value goes to the property as it is.
class ibPGSizeProperty : public wxPGProperty {
public:

	ibPGSizeProperty(const wxString& label = wxPG_LABEL,
		const wxString& name = wxPG_LABEL,
		const ibSize& value = ibDefaultSize);

	virtual wxVariant ChildChanged(wxVariant& thisValue, int childIndex,
		wxVariant& childValue) const override;

	virtual void RefreshChildren() override;

protected:
	void DoSetValue(const ibSize& value);
	// The size a value holds — the cell's own when none is given.
	ibSize GetVal(const wxVariant* pVariant = nullptr) const;
private:
	WX_PG_DECLARE_PROPERTY_CLASS(ibPGSizeProperty);
};

#endif
