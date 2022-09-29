////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : table methods 
////////////////////////////////////////////////////////////////////////////

#include "valuetable.h"
#include "functions.h"

CValueTable::CValueTableReturnLine *CValueTable::AddRow(unsigned int before)
{
	std::map<unsigned int, CValue> valueRow;

	for (auto &colData : m_dataColumnCollection->m_aColumnInfo) {
		CValueTypeDescription *typeDescription = m_dataColumnCollection->GetColumnType(colData->GetColumnID());
		valueRow.insert_or_assign(colData->GetColumnID(), typeDescription ? typeDescription->AdjustValue(CValue()) : CValue());
	}

	/*if (!CTranslateError::IsSimpleMode())*/ {
		m_aObjectValues.push_back(valueRow);
	}

	if (!CTranslateError::IsSimpleMode()) {
		IValueTable::RowAppended();
	}

	return new CValueTableReturnLine(this, m_aObjectValues.size() - 1);
}

void CValueTable::EditRow()
{
	int currentLine = GetSelectionLine();
	if (currentLine == wxNOT_FOUND)
		return;

	IValueTable::RowValueStartEdit(GetSelection());
}

void CValueTable::CopyRow()
{
	int currentLine = GetSelectionLine();
	if (currentLine == wxNOT_FOUND)
		return;

	auto foundedIt = m_aObjectValues.begin();
	std::advance(foundedIt, currentLine);

	std::map<unsigned int, CValue> valueRow;

	for (auto &colData : m_dataColumnCollection->m_aColumnInfo) {
		valueRow.insert_or_assign(
			colData->GetColumnID(), 
			foundedIt->at(colData->GetColumnID())
		);
	}
	if (!CTranslateError::IsSimpleMode()) {
		m_aObjectValues.insert(
			m_aObjectValues.begin() + currentLine + 1, valueRow
		);
		IValueTable::RowInserted(currentLine);
	}
}

void CValueTable::DeleteRow()
{
	unsigned int currentLine = GetSelectionLine();
	if (currentLine == wxNOT_FOUND)
		return;

	if (!CTranslateError::IsSimpleMode()) {
		auto itValuePos = m_aObjectValues.begin();
		std::advance(itValuePos, currentLine);
		m_aObjectValues.erase(itValuePos);
		IValueTable::RowDeleted(currentLine);
	}
}

void CValueTable::Clear()
{
	if (CTranslateError::IsSimpleMode())
		return;
	IValueTable::Reset(0);
	m_aObjectValues.clear();
}