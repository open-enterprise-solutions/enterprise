////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : table methods 
////////////////////////////////////////////////////////////////////////////

#include "valuetable.h"
#include "functions.h"

CValueTable::CValueTableReturnLine *CValueTable::AddRow(unsigned int before)
{
	std::map<unsigned int, CValue> valueRow;

	for (auto &colData : m_aDataColumns->m_aColumnInfo) {
		CValueTypeDescription *typeDescription = m_aDataColumns->GetColumnType(colData->GetColumnID());
		valueRow.insert_or_assign(colData->GetColumnID(), typeDescription ? typeDescription->AdjustValue(CValue()) : CValue());
	}

	if (!CTranslateError::IsSimpleMode()) {
		m_aObjectValues.push_back(valueRow);
	}

	if (!CTranslateError::IsSimpleMode()) {
		IValueTable::RowAppended();
	}

	return new CValueTableReturnLine(this, m_aObjectValues.size() - 1);
}

void CValueTable::EditRow()
{
	IValueTable::RowValueStartEdit(GetSelection());
}

void CValueTable::CopyRow()
{
	std::map<unsigned int, CValue> valueRow;

	for (auto &colData : m_aDataColumns->m_aColumnInfo) {
		CValueTypeDescription *typeDescription = colData->GetColumnTypes();
		wxASSERT(typeDescription);
		valueRow.insert_or_assign(colData->GetColumnID(), 
			typeDescription ? typeDescription->AdjustValue(CValue()) : CValue()
		);
	}
	if (!CTranslateError::IsSimpleMode()) {
		int row = GetSelectionLine();
		m_aObjectValues.insert(
			m_aObjectValues.begin() + row + 1, valueRow
		);
		IValueTable::RowInserted(row);
	}
}

void CValueTable::DeleteRow()
{
	unsigned int row = GetSelectionLine();

	if (m_aObjectValues.size() < row)
		return;

	if (!CTranslateError::IsSimpleMode()) {
		auto itValuePos = m_aObjectValues.begin();
		std::advance(itValuePos, row);
		m_aObjectValues.erase(itValuePos);
		IValueTable::RowDeleted(row);
	}
}

void CValueTable::Clear()
{
	if (CTranslateError::IsSimpleMode())
		return;
	m_aObjectValues.clear();
	IValueTable::Reset(0);
}