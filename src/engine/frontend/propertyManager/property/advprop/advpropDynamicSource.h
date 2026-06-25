#ifndef __ADVPROP_DYNAMIC_SOURCE_H__
#define __ADVPROP_DYNAMIC_SOURCE_H__

#include <wx/propgrid/propgrid.h>
#include <wx/propgrid/advprops.h>

#include "backend/backend_core.h"

#include <vector>
#include <utility>

class BACKEND_API ibPropertyObject;

// -----------------------------------------------------------------------
// ibPGDynamicSourceProperty — frontend chooser for the dynamic-list "Source".
// The options come straight from the queryable factory (GetDescriptors) — no event
// callback, no ibPropertyList: a "..." button opens a list of the registered sources,
// the pick is pushed back via ibPropertyDynamicSource::SetSource(ns, name). An empty
// value means "no source chosen yet" (the variant exists, just empty). Registered via
// ibPG_IMPLEMENT_PROPERTY_CALLBACK(ibPGDynamicSourceProperty,
//   ibPropertyDynamicSource::ms_propertyDynamicSource).
// -----------------------------------------------------------------------
class ibPGDynamicSourceProperty : public wxPGProperty {
public:

	ibPGDynamicSourceProperty(ibPropertyObject* property = nullptr, const wxString& label = wxPG_LABEL,
		const wxString& name = wxPG_LABEL, const wxVariant& value = wxNullVariant);
	virtual ~ibPGDynamicSourceProperty();

	virtual wxString ValueToString(wxVariant& value,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;

	virtual bool StringToValue(wxVariant& variant, const wxString& text,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;

	// Drop-down pick: the combobox lists the registered sources (m_choices); choosing item
	// `number` applies that source through the owner list and yields the new variant.
	virtual bool IntToValue(wxVariant& value, int number,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;

	// m_choices is used, but the value is a variant (not an index) — like ibPGTypeProperty,
	// the cell text comes from ValueToString and no choice is pre-highlighted.
	virtual int GetChoiceSelection() const override { return wxNOT_FOUND; }

	// The "..." button opens the same source chooser as a dialog (handy for a long list).
	virtual wxPGEditorDialogAdapter* GetEditorDialog() const override;

	ibPropertyObject* GetOwnerProperty() const { return m_ownerProperty; }

protected:
	ibPropertyObject* m_ownerProperty = nullptr;
	// Parallel to m_choices (same index order): (namespace, name) of each registered source,
	// so IntToValue maps the picked combobox index back to a source identity.
	std::vector<std::pair<wxString, wxString>> m_sources;
private:
	WX_PG_DECLARE_PROPERTY_CLASS(ibPGDynamicSourceProperty);
};

#endif // __ADVPROP_DYNAMIC_SOURCE_H__
