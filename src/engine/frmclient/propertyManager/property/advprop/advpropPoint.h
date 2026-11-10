#ifndef __ADVPROP_POINT_H__
#define __ADVPROP_POINT_H__

#include "deskgui/univ/propgrid/propgrid.h"
#include "core/graphics/geometry.h"   // ibPoint — what the cell holds
#include "frmclient/backend/backend_core.h"

// -----------------------------------------------------------------------
// ibPGPointProperty
// -----------------------------------------------------------------------

// The sample's ibPointProperty, over the node ibPropertyPoint stores (its text, `x,y`) instead of a wxPoint: the
// cell's value goes to the property as it is.
class ibPGPointProperty : public ibPGProperty {
public:
	ibPGPointProperty(const wxString& label = ibPG_LABEL,
		const wxString& name = ibPG_LABEL,
		const ibVariant& value = ibVariant());
	virtual ~ibPGPointProperty() override;

	virtual ibVariant ChildChanged(ibVariant& thisValue, int childIndex,
		ibVariant& childValue) const override;

	virtual void RefreshChildren() override;

protected:
	// The point a value holds — the cell's own when none is given.
	ibPoint GetVal(const ibVariant* pVariant = nullptr) const;
private:
	IB_PG_DECLARE_PROPERTY_CLASS(ibPGPointProperty);
};

#endif
