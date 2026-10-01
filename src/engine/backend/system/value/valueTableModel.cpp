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
	// Lazy retype (mirror GetValueByMetaID): render the cell as the column's CURRENT type, so a column whose
	// Type was changed after its cells were written displays right without a one-off row sweep.
	//
	// AdjustValue returns a retyped TEMPORARY (not the node's stored cell), so the variant must OWN a copy:
	// ValueToVariant wraps ibVariantDataValueImpl<ibValue> (by value). The const-ref ibVariantDataValueModel
	// must NOT be used here — it would bind to the dying temporary and crash in GetType() on the next render
	// (AV), which is exactly what adding a new row did (dataview Select -> renderer reads the fresh cell).
	ibValue cell;
	node->GetValue(col, cell);
	ValueToVariant(variant,
		ibValueTypeDescription::AdjustValue(m_tableColumnCollection->GetColumnType(col), cell));
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