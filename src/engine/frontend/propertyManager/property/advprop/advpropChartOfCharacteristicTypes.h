#ifndef __ADVPROP_CHART_OF_CHARACTERISTIC_TYPES_H__
#define __ADVPROP_CHART_OF_CHARACTERISTIC_TYPES_H__

#include <wx/propgrid/propgrid.h>
#include "backend/backend_type.h"
#include "backend/propertyManager/propertyObject.h"   // ibPropertyChoiceList — the property's own answer

class BACKEND_API ibPropertyObject;

// -----------------------------------------------------------------------
// ibPGChartOfCharacteristicTypesProperty
// -----------------------------------------------------------------------

class ibPGChartOfCharacteristicTypesProperty : public wxPGProperty {
public:

	const ibPropertyObject* GetPropertyObject() const { return m_ownerProperty; }

	// ⭐ THE CHOICES ARRIVE, they are no longer worked out here. This class used to hold its own
	// FillByClsid over a hardcoded clsid — one of five identical copies across this folder, and the
	// only place in the product that knew what may fill a relationship. The property answers now.
	ibPGChartOfCharacteristicTypesProperty(const ibPropertyObject* property = nullptr, const wxString& label = wxPG_LABEL,
		const wxString& name = wxPG_LABEL, const wxVariant& value = wxNullVariant,
		const ibPropertyChoiceList& choices = ibPropertyChoiceList());

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
private:
	WX_PG_DECLARE_PROPERTY_CLASS(ibPGChartOfCharacteristicTypesProperty);
};

#endif