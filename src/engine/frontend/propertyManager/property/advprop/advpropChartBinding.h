#ifndef __ADVPROP_CHART_BINDING_H__
#define __ADVPROP_CHART_BINDING_H__

#include <wx/propgrid/propgrid.h>
#include "backend/backend_type.h"
#include "backend/propertyManager/propertyObject.h"   // ibPropertyChoiceList — the property's own answer

class BACKEND_API ibPropertyObject;

// -----------------------------------------------------------------------
// ibPGChartBindingProperty
// -----------------------------------------------------------------------

// ⭐ ONE EDITOR FOR EVERY CHART BINDING PROPERTY — the chart of accounts of an accounting register,
// the chart of characteristic types of a characteristic binding, the chart of calculation types of a
// calculation register. They were one class per chart, copies of each other down to the comments, and the
// copies had already drifted: the accounting one let TWO charts be ticked for a binding that names one.
// A third copy was about to be written for calculation types; instead the class stopped knowing which
// chart it is about.
//
// ⭐ THE CHOICES ARRIVE, they are not worked out here. The property answers what may fill it
// (GetValueList — the charts of its kind in this configuration) and HOW MANY of them (the mode it
// returns: one chart for a register, a set for the charts a chart's base may come from), and the picker
// draws exactly that. The class used to walk the configuration itself over a hardcoded clsid — the only
// place in the product that knew what may fill a relationship, and one that answered for one kind of
// chart only.
class ibPGChartBindingProperty : public wxPGProperty {
public:

	const ibPropertyObject* GetPropertyObject() const { return m_ownerProperty; }
	bool IsSet() const { return m_choiceMode == ibPropertyChoiceMode::Mult; }

	ibPGChartBindingProperty(const ibPropertyObject* property = nullptr, const wxString& label = wxPG_LABEL,
		const wxString& name = wxPG_LABEL, const wxVariant& value = wxNullVariant,
		const ibPropertyChoiceList& choices = ibPropertyChoiceList(),
		ibPropertyChoiceMode mode = ibPropertyChoiceMode::Single);

	virtual wxString ValueToString(wxVariant& value,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;

	virtual bool StringToValue(wxVariant& variant,
		const wxString& text,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;

	virtual bool IntToValue(wxVariant& value,
		int number,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;

	virtual wxPGEditorDialogAdapter* GetEditorDialog() const override;

protected:
	const ibPropertyObject* m_ownerProperty = nullptr;
	ibPropertyChoiceMode m_choiceMode = ibPropertyChoiceMode::Single;
private:
	WX_PG_DECLARE_PROPERTY_CLASS(ibPGChartBindingProperty);
};

#endif
