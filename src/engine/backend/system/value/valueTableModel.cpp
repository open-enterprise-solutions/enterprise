////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : table models 
////////////////////////////////////////////////////////////////////////////

#include "valueTable.h"

//***********************************************************************************
//*                                  Model                                          *
//***********************************************************************************

void ibValueModelTable::GetValueByRow(ibVariant& variant,
	const ibDataViewItem& row, unsigned int col) const
{
	ibComposerNode* node = GetViewData<ibComposerNode>(row);
	if (node == nullptr)
		return;
	// Lazy retype (mirror GetValueByMetaID): render the cell as the column's CURRENT type, so a column whose
	// Type was changed after its cells were written displays right without a one-off row sweep.
	// AdjustValue answers a retyped value of its own, and the cell takes it as it is.
	ibValue cell;
	node->GetValue(col, cell);
	variant = ibValueTypeDescription::AdjustValue(m_tableColumnCollection->GetColumnType(col), cell);
}

bool ibValueModelTable::SetValueByRow(const ibVariant& variant,
	const ibDataViewItem& row, unsigned int col)
{
	const ibString& strData = variant.GetString();
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