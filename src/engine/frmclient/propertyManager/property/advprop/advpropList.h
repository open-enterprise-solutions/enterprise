#ifndef __ADVPROP_LIST_H__
#define __ADVPROP_LIST_H__

#include "deskgui/univ/propgrid/propgrid.h"
#include "frmclient/backend/backend_type.h"
#include "frmclient/backend/propertyManager/propertyObject.h"   // ibPropertyChoiceList — the backend list this ctor converts

// -----------------------------------------------------------------------
// ibPGListProperty
// -----------------------------------------------------------------------

class ibPGListProperty : public ibPGProperty {

public:
	// Takes the backend's choice list and turns it into ibPGChoices here — the conversion
	// belongs on this side of the seam (see ibPropertyChoiceList in propertyObject.h).
	ibPGListProperty(const wxString& label = ibPG_LABEL,
		const wxString& name = ibPG_LABEL, const ibPropertyChoiceList& choices = ibPropertyChoiceList(),
		const ibVariant& value = ibVariant());

	virtual wxString ValueToString(ibVariant& value,
		ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;

	virtual bool StringToValue(ibVariant& variant,
		const wxString& text,
		ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;

	virtual bool IntToValue(ibVariant& value,
		int number,
		ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;

	// The chosen one's place among the choices — the drop-down's selection.
	virtual int GetChoiceSelection() const override;

	virtual ibPGEditorDialogAdapter* GetEditorDialog() const override;

private:
	IB_PG_DECLARE_PROPERTY_CLASS(ibPGListProperty);
};

#endif