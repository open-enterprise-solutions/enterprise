////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : tabular sections - model
////////////////////////////////////////////////////////////////////////////

#include "object.h"

//***********************************************************************************
//*                                  Model                                          *
//***********************************************************************************

void IRecordSetObject::GetValueByRow(wxVariant& variant,
	const wxDataViewItem& row, unsigned int col) const
{
	wxValueTableRow* node = GetViewData<wxValueTableRow>(row);
	if (node == NULL)
		return;
	node->GetValue(col, variant);
}

#include "core/metadata/metadata.h"

bool IRecordSetObject::SetValueByRow(const wxVariant& variant,
	const wxDataViewItem& row, unsigned int col)
{
	const wxString &strData = variant.GetString();
	wxValueTableRow* node = GetViewData<wxValueTableRow>(row);
	if (node == NULL)
		return false;

	IMetaObjectRegisterData* metaObject = GetMetaObject();
	wxASSERT(metaObject);
	IMetaAttributeObject* metaObjectAttribute = metaObject->FindGenericAttribute(col);
	wxASSERT(metaObject);
	CValue newValue = metaObjectAttribute->CreateValue();
	if (strData.Length() > 0) {
		std::vector<CValue> foundedObjects;
		if (newValue.FindValue(strData, foundedObjects)) {
			return node->SetValue(col, foundedObjects.at(0));
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