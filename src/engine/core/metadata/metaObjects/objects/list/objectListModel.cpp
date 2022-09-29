////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : list model 
////////////////////////////////////////////////////////////////////////////

#include "objectList.h"

//***********************************************************************************
//*                                  Model                                          *
//***********************************************************************************

void CListDataObjectRef::GetValueByRow(wxVariant& variant,
	unsigned int row, unsigned int col) const
{
	auto itFounded = m_aObjectValues.begin();
	std::advance(itFounded, row);
	auto& rowValues = itFounded->second;

	auto foundedColumn = rowValues.find(col);
	if (foundedColumn != rowValues.end()) {
		const CValue& cValue = foundedColumn->second;
		variant = cValue.GetString();
	}
}

bool CListDataObjectRef::SetValueByRow(const wxVariant& variant,
	unsigned int row, unsigned int col)
{
	auto itFounded = m_aObjectValues.begin();
	std::advance(itFounded, row);
	auto& rowValues = itFounded->second;

	auto foundedColumn = rowValues.find(col);
	if (foundedColumn != rowValues.end()) {
		CValue& cValue = foundedColumn->second;
		cValue.SetValue(variant.GetString());
	}

	return false;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////

void CListDataObjectGroupRef::GetValueByRow(wxVariant& variant,
	unsigned int row, unsigned int col) const
{
	auto itFounded = m_aObjectValues.begin();
	std::advance(itFounded, row);
	auto& rowValues = itFounded->second;

	auto foundedColumn = rowValues.find(col);
	if (foundedColumn != rowValues.end()) {
		const CValue& cValue = foundedColumn->second;
		variant = cValue.GetString();
	}
}

bool CListDataObjectGroupRef::SetValueByRow(const wxVariant& variant,
	unsigned int row, unsigned int col)
{
	auto itFounded = m_aObjectValues.begin();
	std::advance(itFounded, row);
	auto& rowValues = itFounded->second;

	auto foundedColumn = rowValues.find(col);
	if (foundedColumn != rowValues.end()) {
		CValue& cValue = foundedColumn->second;
		cValue.SetValue(variant.GetString());
	}

	return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////

void CListRegisterObject::GetValueByRow(wxVariant& variant,
	unsigned int row, unsigned int col) const
{
	auto itFounded = m_aObjectValues.begin();
	std::advance(itFounded, row);
	auto& rowValues = itFounded->second;

	auto foundedColumn = rowValues.find(col);
	if (foundedColumn != rowValues.end()) {
		const CValue& cValue = foundedColumn->second;
		variant = cValue.GetString();
	}
}

bool CListRegisterObject::SetValueByRow(const wxVariant& variant,
	unsigned int row, unsigned int col)
{
	auto itFounded = m_aObjectValues.begin();
	std::advance(itFounded, row);
	auto& rowValues = itFounded->second;

	auto foundedColumn = rowValues.find(col);
	if (foundedColumn != rowValues.end()) {
		CValue& cValue = foundedColumn->second;
		cValue.SetValue(variant.GetString());
	}

	return false;
}