////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : table models 
////////////////////////////////////////////////////////////////////////////

#include "valueTable.h"

//***********************************************************************************
//*                                  Model                                          *
//***********************************************************************************

void ibValueModelTable::GetValueByRow(wxVariant& variant,
	const ibDataViewItem& row, unsigned int col) const
{
	ibComposerNode* node = GetViewData<ibComposerNode>(row);
	if (node == nullptr)
		return;
	node->GetValue(col, variant);
}

bool ibValueModelTable::SetValueByRow(const wxVariant& variant,
	const ibDataViewItem& row, unsigned int col)
{
	const wxString& strData = variant.GetString();
	ibComposerNode* node = GetViewData<ibComposerNode>(row);
	if (node == nullptr)
		return false;
	const ibTypeDescription& typeDescription =
		m_tableColumnCollection->GetColumnType(col);
	ibValue cValue; node->GetValue(col, cValue);
	if (strData.Length() > 0) {
		std::vector<ibValue> listValue;
		if (cValue.FindValue(strData, listValue)) {
			node->SetValue(
				col, ibValueTypeDescription::AdjustValue(typeDescription, listValue.at(0))
			);
		}
		else {
			return false;
		}
	}
	else {
		node->SetValue(
			col, ibValueTypeDescription::AdjustValue(typeDescription)
		);
	}

	return true;
}