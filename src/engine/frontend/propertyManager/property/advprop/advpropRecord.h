#ifndef __ADVPROP_RECORD_H__
#define __ADVPROP_RECORD_H__

#include <wx/propgrid/propgrid.h>
#include "backend/backend_type.h"
#include "backend/propertyManager/propertyObject.h"   // ibPropertyChoiceList — the property's own answer

class BACKEND_API ibPropertyObject;

// -----------------------------------------------------------------------
// ibPGRecordProperty
// -----------------------------------------------------------------------

class ibPGRecordProperty : public wxPGProperty {
public:

	const ibPropertyObject* GetPropertyObject() const { return m_ownerProperty; }

	// The choices arrive — the property answers what may fill it. See advpropChartOfCharacteristicTypes.h.
	ibPGRecordProperty(const ibPropertyObject* property = nullptr, const wxString& label = wxPG_LABEL,
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
	WX_PG_DECLARE_PROPERTY_CLASS(ibPGRecordProperty);
};

#endif