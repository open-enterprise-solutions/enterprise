#ifndef __ADVPROP_POINT_H__
#define __ADVPROP_POINT_H__

#include <wx/propgrid/propgrid.h>
#include "core/graphics/geometry.h"   // ibPoint — what the cell holds
#include "backend/backend_core.h"

// -----------------------------------------------------------------------
// ibPGPointProperty
// -----------------------------------------------------------------------

// The sample's wxPointProperty, over ibVariantDataPoint instead of a wxPoint, which is what
// ibPropertyPoint holds: the cell's value goes to the property as it is.
class ibPGPointProperty : public wxPGProperty {
public:
	ibPGPointProperty(const wxString& label = wxPG_LABEL,
		const wxString& name = wxPG_LABEL,
		const ibPoint& value = ibDefaultPosition);
	virtual ~ibPGPointProperty() override;

	virtual wxVariant ChildChanged(wxVariant& thisValue, int childIndex,
		wxVariant& childValue) const override;

	virtual void RefreshChildren() override;

protected:
	void DoSetValue(const ibPoint& value);
	// The point a value holds — the cell's own when none is given.
	ibPoint GetVal(const wxVariant* pVariant = nullptr) const;
private:
	WX_PG_DECLARE_PROPERTY_CLASS(ibPGPointProperty);
};

#endif
