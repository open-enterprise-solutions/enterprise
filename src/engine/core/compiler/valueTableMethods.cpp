////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : table methods 
////////////////////////////////////////////////////////////////////////////

#include "valuetable.h"
#include "functions.h"

long CValueTable::AppendRow(unsigned int before)
{
	modelArray_t valueRow;
	for (auto& colData : m_dataColumnCollection->m_columnInfo) {
		CValueTypeDescription* typeDescription = m_dataColumnCollection->GetColumnType(colData->GetColumnID());
		valueRow.insert_or_assign(colData->GetColumnID(), typeDescription ? typeDescription->AdjustValue(CValue()) : CValue());
	}

	return IValueTable::Append(
		new wxValueTableRow(valueRow), !CTranslateError::IsSimpleMode()
	);
}

void CValueTable::EditRow()
{
	IValueTable::RowValueStartEdit(GetSelection());
}

void CValueTable::CopyRow()
{
	wxDataViewItem currentItem = GetSelection();
	if (!currentItem.IsOk())
		return;
	wxValueTableRow* node = GetViewData(currentItem);
	if (node == NULL)
		return;
	modelArray_t valueRow;
	for (auto& colData : m_dataColumnCollection->m_columnInfo) {
		valueRow.insert_or_assign(
			colData->GetColumnID(), node->GetValue((meta_identifier_t)colData->GetColumnID())
		);
	}
	long currentLine = GetRow(currentItem);
	if (currentLine != wxNOT_FOUND) {
		IValueTable::Insert(
			new wxValueTableRow(valueRow), currentLine, !CTranslateError::IsSimpleMode()
		);
	}
	else {
		IValueTable::Append(
			new wxValueTableRow(valueRow), !CTranslateError::IsSimpleMode()
		);
	}
}

void CValueTable::DeleteRow()
{
	wxDataViewItem currentItem = GetSelection();
	if (!currentItem.IsOk())
		return;
	wxValueTableRow* node = GetViewData(currentItem);
	if (node == NULL)
		return;
	if (!CTranslateError::IsSimpleMode())
		IValueTable::Remove(node);
}

void CValueTable::Clear()
{
	if (CTranslateError::IsSimpleMode())
		return;
	IValueTable::Clear();
}