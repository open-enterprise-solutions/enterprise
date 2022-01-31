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
	return m_aDataColumns->GetColumnCount();
}

wxString CValueTable::GetColumnType(unsigned int col) const
{
	return wxT("string");
}

void CValueTable::GetValueByRow(wxVariant &variant,
	unsigned int row, unsigned int col) const
{
	auto itFounded = m_aObjectValues.begin();
	std::advance(itFounded, row);

	if (itFounded->find(col) != itFounded->end())
	{
		const CValue &cValue = itFounded->at(col);
		variant = cValue.GetString();
	}
}

bool CValueTable::GetAttrByRow(unsigned int row, unsigned int col, wxDataViewItemAttr &attr) const
{
	return true;
}

bool CValueTable::SetValueByRow(const wxVariant &variant,
	unsigned int row, unsigned int col)
{
	auto itFounded = m_aObjectValues.begin();
	std::advance(itFounded, row);

	if (itFounded->find(col) != itFounded->end())
	{
		CValue &cValue = itFounded->at(col);
		CValueTypeDescription *m_typeDescription = m_aDataColumns->GetColumnType(col);

		if (m_typeDescription)
		{
			CValue cString(variant.GetString());
			cValue = m_typeDescription->AdjustValue(cString);
		}
		else
		{
			cValue.SetValue(variant.GetString());
		}
	}

	return false;
}