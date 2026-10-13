#ifndef __ADVPROP_FONT_H__
#define __ADVPROP_FONT_H__

#include <wx/propgrid/propgrid.h>
#include <wx/propgrid/editors.h>
#include "core/graphics/font.h"   // ibFont — what the cell holds, as its text

#include "backend/backend_core.h"

// -----------------------------------------------------------------------
// ibPGFontProperty
// -----------------------------------------------------------------------

// wxFontProperty, copied — over ibFont's text instead of a wxFont, which is what ibPropertyFont
// holds: the cell's value goes to the property as it is.
class ibPGFontProperty : public wxEditorDialogProperty {
	WX_PG_DECLARE_PROPERTY_CLASS(ibPGFontProperty)
public:

	ibPGFontProperty(const wxString& label = wxPG_LABEL, const wxString& name = wxPG_LABEL, const ibFont& value = ibFont());
	virtual ~ibPGFontProperty() = default;

	virtual wxVariant ChildChanged(wxVariant& thisValue, int childIndex,
		wxVariant& childValue) const override;
	virtual void RefreshChildren() override;

protected:
	virtual bool DisplayEditorDialog(wxPropertyGrid* pg, wxVariant& value) override;

	// The font a value holds — the cell's own when none is given; for no font at all, the system's,
	// which a change starts from.
	ibFont GetVal(const wxVariant* pVariant = nullptr) const;
};

#endif
