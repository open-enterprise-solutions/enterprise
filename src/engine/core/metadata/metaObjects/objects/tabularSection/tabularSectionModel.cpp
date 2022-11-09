////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : tabular sections - model
////////////////////////////////////////////////////////////////////////////

#include "tabularSection.h"

//***********************************************************************************
//*                                  Model                                          *
//***********************************************************************************

void ITabularSectionDataObject::GetValueByRow(wxVariant& variant,
	const wxDataViewItem& row, unsigned int col) const
{
	wxValueTableRow* node = GetViewData(row);
	if (node == NULL)
		return;
	if (m_metaTable->IsNumberLine(col))
		variant = wxString::Format("%i", GetRow(row) + 1);
	else
		node->GetValue(variant, col);
}

#include "metadata/metadata.h"

bool ITabularSectionDataObject::SetValueByRow(const wxVariant& variant,
	const wxDataViewItem& row, unsigned int col)
{
	wxString strData = variant.GetString();
	wxValueTableRow* node = GetViewData(row);
	if (node == NULL)
		return false;
	if (!m_metaTable->IsNumberLine(col)) {
		IMetadata* metaData = m_metaTable->GetMetadata();
		CValue selValue; node->GetValue(selValue, col);
		wxString className = metaData->GetNameObjectFromID(
			selValue.GetClassType()
		);
		CValue newValue = metaData->CreateObject(className);
		if (strData.Length() > 0) {
			std::vector<CValue> foundedObjects;
			if (newValue.FindValue(strData, foundedObjects)) {
				SetValueByMetaID(
					GetRow(row), col, foundedObjects.at(0));
			}
			else {
				return false;
			}
		}
		else {
			SetValueByMetaID(
				GetRow(row), col, newValue);
		}
	};

	return false;
}

#include "frontend/visualView/controls/form.h"

void ITabularSectionDataObject::AddValue(unsigned int before)
{
	long row = GetRow(GetSelection());
	if (row > 0)
		AppendRow(row);
	else AppendRow();
}

void ITabularSectionDataObject::CopyValue()
{
	wxDataViewItem currentItem = GetSelection();
	if (!currentItem.IsOk())
		return;
	wxValueTableRow* node = GetViewData(currentItem);
	if (node == NULL)
		return;
	modelArray_t valueRow;
	for (auto attribute : m_metaTable->GetObjectAttributes()) {
		if (!m_metaTable->IsNumberLine(attribute->GetMetaID())) {
			valueRow.insert_or_assign(attribute->GetMetaID(), node->GetValue(attribute->GetMetaID()));
		}
		else {
			valueRow.insert_or_assign(attribute->GetMetaID(), CValue());
		}
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
	if (!CTranslateError::IsSimpleMode()) {
		CValueForm* foundedForm = CValueForm::FindFormByGuid(
			m_dataObject->GetGuid()
		);
		if (foundedForm != NULL) {
			foundedForm->Modify(true);
		}
	}
}

void ITabularSectionDataObject::EditValue()
{
	IValueTable::RowValueStartEdit(GetSelection());
}

void ITabularSectionDataObject::DeleteValue()
{
	wxDataViewItem currentItem = GetSelection();
	if (!currentItem.IsOk())
		return;
	wxValueTableRow* node = GetViewData(currentItem);
	if (node == NULL)
		return;
	if (!CTranslateError::IsSimpleMode()) {
		CValueForm* foundedForm = CValueForm::FindFormByGuid(
			m_dataObject->GetGuid()
		);
		IValueTable::Remove(node);
		if (foundedForm != NULL) {
			foundedForm->Modify(true);
		}
	}
}
