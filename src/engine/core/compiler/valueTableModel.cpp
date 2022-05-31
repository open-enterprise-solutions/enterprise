////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : table models 
////////////////////////////////////////////////////////////////////////////

#include "valueTable.h"

//***********************************************************************************
//*                                  Model                                          *
//***********************************************************************************

unsigned int CValueTable::GetColumnCount() const
{
	return m_dataColumnCollection->GetColumnCount();
}

wxString CValueTable::GetColumnType(unsigned int col) const
{
	return wxT("string");
}

void CValueTable::GetValueByRow(wxVariant& variant,
	unsigned int row, unsigned int col) const
{
	auto itFounded = m_aObjectValues.begin();
	std::advance(itFounded, row);

	if (itFounded->find(col) != itFounded->end()) {
		const CValue& cValue = itFounded->at(col);
		variant = cValue.GetString();
	}
}

bool CValueTable::GetAttrByRow(unsigned int row, unsigned int col, wxDataViewItemAttr& attr) const
{
	return true;
}

bool CValueTable::SetValueByRow(const wxVariant& variant,
	unsigned int row, unsigned int col)
{
	wxString strData = variant.GetString();
	auto itFounded = m_aObjectValues.begin();
	std::advance(itFounded, row);
	if (itFounded->find(col) != itFounded->end()) {
		CValue& cValue = itFounded->at(col);
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
	}

	return false;
}