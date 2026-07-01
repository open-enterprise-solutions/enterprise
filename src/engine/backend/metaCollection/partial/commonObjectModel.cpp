////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : tabular sections - model
////////////////////////////////////////////////////////////////////////////

#include "commonObject.h"

//***********************************************************************************
//*                                  Model                                          *
//***********************************************************************************

void ibValueRecordSetObject::GetValueByRow(wxVariant& variant,
	const ibDataViewItem& row, unsigned int col) const
{
	ibComposerNode* node = GetViewData<ibComposerNode>(row);
	if (node == nullptr)
		return;
	node->GetValue(col, variant);
}

#include "backend/metaData.h"

bool ibValueRecordSetObject::SetValueByRow(const wxVariant& variant,
	const ibDataViewItem& row, unsigned int col)
{
	const wxString &strData = variant.GetString();
	ibComposerNode* node = GetViewData<ibComposerNode>(row);
	if (node == nullptr)
		return false;

	const ibValueMetaObjectRegisterData* metaObject = GetMetaObject();
	wxASSERT(metaObject);
	ibValueMetaObjectAttributeBase* metaObjectAttribute = metaObject->FindAnyAttributeObjectByFilter(col);
	wxASSERT(metaObject);
	ibValue newValue = metaObjectAttribute->CreateValue();
	if (strData.Length() > 0) {
		std::vector<ibValue> listValue;
		if (newValue.FindValue(strData, listValue)) {
			return node->SetValue(col, listValue.at(0));
		}
		else {
			return false;
		}
	}
	else {
		return node->SetValue(col, newValue);
	}


	return false;
}