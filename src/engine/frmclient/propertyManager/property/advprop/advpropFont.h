#ifndef __ADVPROP_FONT_H__
#define __ADVPROP_FONT_H__

#include "deskgui/univ/propgrid/propgrid.h"
#include "deskgui/univ/propgrid/editors.h"
#include "core/graphics/font.h"   // ibFont — what the cell holds, as its text

#include "frmclient/backend/backend_core.h"

// -----------------------------------------------------------------------
// ibPGFontProperty
// -----------------------------------------------------------------------

// ibFontProperty, copied — over ibFont's text instead of a wxFont, which is what the server writes:
// the cell's value goes back to the property as it is.
class ibPGFontProperty : public ibEditorDialogProperty {
	IB_PG_DECLARE_PROPERTY_CLASS(ibPGFontProperty)
public:

	ibPGFontProperty(const wxString& label = ibPG_LABEL, const wxString& name = ibPG_LABEL, const ibVariant& value = ibVariant());
	virtual ~ibPGFontProperty() = default;

	virtual ibVariant ChildChanged(ibVariant& thisValue, int childIndex,
		ibVariant& childValue) const override;
	virtual void RefreshChildren() override;

protected:
	virtual bool DisplayEditorDialog(ibPropertyGrid* pg, ibVariant& value) override;

	// The font a value holds — the cell's own when none is given; for no font at all, the system's,
	// which a change starts from.
	ibFont GetVal(const ibVariant* pVariant = nullptr) const;
};

#endif
