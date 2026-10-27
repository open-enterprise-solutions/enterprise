#ifndef __ADVPROP_GENERATION_H__
#define __ADVPROP_GENERATION_H__

#include <wx/propgrid/propgrid.h>
#include "frmclient/backend/backend_core.h"
#include "frmclient/backend/propertyManager/propertyObject.h"   // ibPropertyChoiceList — the property's own answer

class FRMCLIENT_API ibPropertyObject;

// -----------------------------------------------------------------------
// ibPGGenerationProperty
// -----------------------------------------------------------------------

// THE GENERATION EDITOR —what an object is
// entered on the basis of. What may fill it is the row's choices, as the server's property listed them (GetValueList).
class ibPGGenerationProperty : public wxPGProperty {
public:

	const ibPropertyObject* GetPropertyObject() const { return m_ownerProperty; }

	// The choices arrive — the property answers what may fill it. See advpropChartBinding.h.
	ibPGGenerationProperty(const ibPropertyObject* property = nullptr, const wxString& label = wxPG_LABEL,
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
	std::vector<std::uint64_t> m_choiceData;   // each choice's data — its class — by its index (ibPropertyChoiceList::GetData)
private:
	WX_PG_DECLARE_PROPERTY_CLASS(ibPGGenerationProperty);
};

#endif
