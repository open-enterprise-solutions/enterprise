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
	wxValueTableRow* node = GetViewData<wxValueTableRow>(row);
	if (node == NULL)
		return;
	if (m_metaTable->IsNumberLine(col))
		variant = wxString::Format("%i", GetRow(row) + 1);
	else
		node->GetValue(col, variant);
}

#include "core/metadata/metadata.h"

bool ITabularSectionDataObject::SetValueByRow(const wxVariant& variant,
	const wxDataViewItem& row, unsigned int col)
{
	const wxString& strData = variant.GetString();
	wxValueTableRow* node = GetViewData<wxValueTableRow>(row);
	if (node == NULL)
		return false;
	if (!m_metaTable->IsNumberLine(col)) {
		IMetadata* metaData = m_metaTable->GetMetadata();
		wxASSERT(metaData);
		const CValue& selValue = node->GetTableValue(col);
		const CValue& newValue = metaData->CreateObject(selValue.GetTypeClass());
		if (strData.Length() > 0) {
			std::vector<CValue> foundedObjects;
			if (newValue.FindValue(strData, foundedObjects)) {
				SetValueByMetaID(row, col, foundedObjects.at(0));
			}
			else {
				return false;
			}
		}
		else {
			SetValueByMetaID(row, col, newValue);
		}
	};

	return true;
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
	wxValueTableRow* node = GetViewData<wxValueTableRow>(currentItem);
	if (node == NULL)
		return;
	wxValueTableRow* rowData = new wxValueTableRow();
	for (auto attribute : m_metaTable->GetObjectAttributes()) {
		if (!m_metaTable->IsNumberLine(attribute->GetMetaID())) {
			rowData->AppendTableValue(attribute->GetMetaID(), node->GetTableValue(attribute->GetMetaID()));
		}
		else {
			rowData->AppendTableValue(attribute->GetMetaID(), CValue());
		}
	}
	long currentLine = GetRow(currentItem);
	if (currentLine != wxNOT_FOUND) {
		IValueTable::Insert(rowData, currentLine, !CTranslateError::IsSimpleMode());
	}
	else {
		IValueTable::Append(rowData, !CTranslateError::IsSimpleMode());
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
	wxValueTableRow* node = GetViewData<wxValueTableRow>(currentItem);
	if (node == NULL)
		return;
	if (!CTranslateError::IsSimpleMode()) {
		IValueTable::Remove(node);
	}
}

void CTabularSectionDataObjectRef::CopyValue()
{
	ITabularSectionDataObject::CopyValue();

	if (!CTranslateError::IsSimpleMode()) {
		CValueForm* const foundedForm = CValueForm::FindFormByGuid(
			m_objectValue->GetGuid()
		);
		if (foundedForm != NULL) {
			foundedForm->Modify(true);
		}
	}
}

void CTabularSectionDataObjectRef::DeleteValue()
{
	ITabularSectionDataObject::DeleteValue();

	if (!CTranslateError::IsSimpleMode()) {
		CValueForm* const foundedForm = CValueForm::FindFormByGuid(
			m_objectValue->GetGuid()
		);
		if (foundedForm != NULL) {
			foundedForm->Modify(true);
		}
	}
}