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
	wxValueTableRow* node = GetViewData(row);
	if (node == NULL)
		return;
	node->GetValue(variant, col);
}

bool CValueTable::SetValueByRow(const wxVariant& variant,
	const wxDataViewItem& row, unsigned int col)
{
	wxString strData = variant.GetString();
	wxValueTableRow* node = GetViewData(row);
	if (node == NULL)
		return false;
	CValue cValue; node->GetValue(cValue, col);
	if (strData.Length() > 0) {
		std::vector<CValue> foundedObjects;
		if (cValue.FindValue(strData, foundedObjects)) {
			cValue = foundedObjects.at(0);
		}
		else {
			return false;
		}
	}
	else {
		CValueTypeDescription* typeDescription =
			m_dataColumnCollection->GetColumnType(col);
		if (typeDescription != NULL) {
			cValue = typeDescription->AdjustValue();
		}
		else {
			cValue.Reset();
		}
	}

	return true;
}