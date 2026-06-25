#ifndef __ADVPROP_DYNAMIC_LIST_H__
#define __ADVPROP_DYNAMIC_LIST_H__

#include <wx/propgrid/propgrid.h>
#include <wx/propgrid/advprops.h>

#include "backend/backend_core.h"

class BACKEND_API ibPropertyObject;

// -----------------------------------------------------------------------
// ibPGDynamicListProperty — frontend half of the dynamic-list-settings
// designer property. Modelled 1:1 on ibPGHyperLinkProperty: read-only, no
// editor, value is a click-flag variant; clicking fires OnSetValue which
// CallAfter-opens ibDialogListSettings against the OWNER attribute's dynamic
// list (its GetSourceQueryable() fields + GetListSettings()). Registered via
// ibPG_IMPLEMENT_PROPERTY_CALLBACK(ibPGDynamicListProperty,
//   ibPropertyDynamicList::ms_propertyDynamicList).
// -----------------------------------------------------------------------

class ibPGDynamicListProperty : public wxPGProperty {
public:

	ibPGDynamicListProperty(ibPropertyObject* property = nullptr, const wxString& label = wxPG_LABEL,
		const wxString& name = wxPG_LABEL, const wxVariant& value = wxNullVariant);
	virtual ~ibPGDynamicListProperty();

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
	WX_PG_DECLARE_PROPERTY_CLASS(ibPGDynamicListProperty);
};

#endif // __ADVPROP_DYNAMIC_LIST_H__
