#ifndef __ADVPROP_DATA_COMPOSITION_H__
#define __ADVPROP_DATA_COMPOSITION_H__

#include <wx/propgrid/propgrid.h>
#include <wx/propgrid/advprops.h>

#include "backend/backend_core.h"

class BACKEND_API ibPropertyObject;

// -----------------------------------------------------------------------
// ibPGDataCompositionProperty — frontend half of the data-composer settings
// designer property. Read-only, no editor, value is a click-flag variant;
// clicking fires OnSetValue which CallAfter-opens the COMPOSITION's own window
// (ibDialogComposerSettings) against the owner.
//
// ⭐ A CLASS OF ITS OWN, beside ibPGDynamicListProperty rather than inside it
// (Max, 2026-08-20). Both are "Settings…" and there the likeness ends: they open
// two different windows onto two different things. One property forking on what
// its owner turned out to be was a branch doing a type's work — and the registry
// already dispatches by type, so the fork had a door standing open next to it.
// -----------------------------------------------------------------------

class ibPGDataCompositionProperty : public wxPGProperty {
public:

	ibPGDataCompositionProperty(ibPropertyObject* property = nullptr, const wxString& label = wxPG_LABEL,
		const wxString& name = wxPG_LABEL, const wxVariant& value = wxNullVariant);
	virtual ~ibPGDataCompositionProperty();

	virtual wxString ValueToString(wxVariant& value,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;

	virtual bool StringToValue(wxVariant& variant,
		const wxString& text,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;

	virtual void OnSetValue() override;

	// Refresh values of child properties. Automatically called after value is set.
	virtual void RefreshChildren();

protected:
	ibPropertyObject* m_ownerProperty = nullptr;
private:
	WX_PG_DECLARE_PROPERTY_CLASS(ibPGDataCompositionProperty);
};

#endif // __ADVPROP_DATA_COMPOSITION_H__
