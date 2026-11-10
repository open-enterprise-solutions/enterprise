#ifndef __ADVPROP_SIZE_H__
#define __ADVPROP_SIZE_H__

#include "deskgui/univ/propgrid/propgrid.h"
#include "frmclient/backend/backend_core.h"

// -----------------------------------------------------------------------
// ibPGSizeProperty
// -----------------------------------------------------------------------

// The ibPropertyGrid sample's ibSizeProperty, as the old desktop's.
class ibPGSizeProperty : public ibPGProperty {
public:
	
	ibPGSizeProperty(const wxString& label = ibPG_LABEL,
		const wxString& name = ibPG_LABEL,
		const ibVariant& value = ibVariant());

	virtual ibVariant ChildChanged(ibVariant& thisValue, int childIndex,
		ibVariant& childValue) const override;

	virtual void RefreshChildren() override;

private:
	IB_PG_DECLARE_PROPERTY_CLASS(ibPGSizeProperty);
};

#endif