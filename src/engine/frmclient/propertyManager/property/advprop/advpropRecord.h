#ifndef __ADVPROP_RECORD_H__
#define __ADVPROP_RECORD_H__

#include "deskgui/univ/propgrid/propgrid.h"
#include "frmclient/backend/backend_core.h"
#include "frmclient/backend/propertyManager/propertyObject.h"   // ibPropertyChoiceList — the property's own answer

class FRMCLIENT_API ibPropertyObject;

// -----------------------------------------------------------------------
// ibPGRecordProperty
// -----------------------------------------------------------------------

// THE RECORD EDITOR —the registers a document
// posts to, the sequences it is registered in. What may fill it is the row's choices, as the server's property listed
// them (GetValueList: the registers with a recorder).
class ibPGRecordProperty : public ibPGProperty {
public:

	const ibPropertyObject* GetPropertyObject() const { return m_ownerProperty; }

	// The choices arrive — the property answers what may fill it. See advpropChartBinding.h.
	ibPGRecordProperty(const ibPropertyObject* property = nullptr, const wxString& label = ibPG_LABEL,
		const wxString& name = ibPG_LABEL, const ibVariant& value = ibVariant(),
		const ibPropertyChoiceList& choices = ibPropertyChoiceList());

	virtual wxString ValueToString(ibVariant& value,
		ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;

	virtual bool StringToValue(ibVariant& variant,
		const wxString& text,
		ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;

	virtual bool IntToValue(ibVariant& value,
		int number,
		ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;

	virtual ibPGEditorDialogAdapter* GetEditorDialog() const override;

protected:
	const ibPropertyObject* m_ownerProperty = nullptr;
	std::vector<std::uint64_t> m_choiceData;   // each choice's data — its class — by its index (ibPropertyChoiceList::GetData)
private:
	IB_PG_DECLARE_PROPERTY_CLASS(ibPGRecordProperty);
};

#endif
