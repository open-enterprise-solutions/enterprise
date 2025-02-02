////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : table models 
////////////////////////////////////////////////////////////////////////////

#include "valueTable.h"

//***********************************************************************************
//*                                  Model                                          *
//***********************************************************************************

void CValueTable::GetValueByRow(wxVariant& variant,
	const wxDataViewItem& row, unsigned int col) const
{
	wxValueTableRow* node = GetViewData<wxValueTableRow>(row);
	if (node == nullptr)
		return;
	node->GetValue(col, variant);
}

bool CValueTable::SetValueByRow(const wxVariant& variant,
	const wxDataViewItem& row, unsigned int col)
{
	const wxString& strData = variant.GetString();
	wxValueTableRow* node = GetViewData<wxValueTableRow>(row);
	if (node == nullptr)
		return false;
	const typeDescription_t& typeDescription =
		m_dataColumnCollection->GetColumnType(col);
	CValue cValue; node->GetValue(col, cValue);
	if (strData.Length() > 0) {
		std::vector<CValue> foundedObjects;
		if (cValue.FindValue(strData, foundedObjects)) {
			node->SetValue(
				col, CValueTypeDescription::AdjustValue(typeDescription, foundedObjects.at(0))
			);
		}
		else {
			return false;
		}
	}
	else {
		node->SetValue(
			col, CValueTypeDescription::AdjustValue(typeDescription)
		);
	}

	return true;
}