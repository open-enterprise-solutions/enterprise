#ifndef __ADVPROP_CHART_BINDING_H__
#define __ADVPROP_CHART_BINDING_H__

#include <wx/propgrid/propgrid.h>
#include "frmclient/backend/backend_core.h"
#include "frmclient/backend/propertyManager/propertyObject.h"   // ibPropertyChoiceList — the property's own answer

class FRMCLIENT_API ibPropertyObject;

// -----------------------------------------------------------------------
// ibPGChartBindingProperty
// -----------------------------------------------------------------------

// THE DESKTOP'S ONE EDITOR FOR EVERY CHART BINDING PROPERTY (frontend/propertyManager/property/advprop/
// advpropChartBinding.h) — the chart of accounts of an accounting register, the chart of characteristic types of a
// characteristic binding, the chart of calculation types of a calculation register, the kind of accounting, the
// functional options.
//
// ⭐ THE CHOICES ARRIVE, they are not worked out here. The property answers what may fill it — on the client the row's
// choices, as the server's property listed them (GetValueList) — and HOW MANY of them (the mode it returns), and the
// picker draws exactly that.
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
