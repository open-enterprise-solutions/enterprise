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
	if (node == nullptr)
		return;
	if (m_metaTable->IsNumberLine(col))
		variant = wxString::Format("%i", GetRow(row) + 1);
	else
		node->GetValue(col, variant);
}

#include "backend/metaData.h"

bool ITabularSectionDataObject::SetValueByRow(const wxVariant& variant,
	const wxDataViewItem& row, unsigned int col)
{
	const wxString& strData = variant.GetString();
	wxValueTableRow* node = GetViewData<wxValueTableRow>(row);
	if (node == nullptr)
		return false;
	if (!m_metaTable->IsNumberLine(col)) {
		IMetaData* metaData = m_metaTable->GetMetaData();
		wxASSERT(metaData);
		const CValue& selValue = node->GetTableValue(col);
		const CValue& newValue = metaData->CreateObject(selValue.GetClassType());
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
	if (node == nullptr)
		return;
	wxValueTableRow* rowData = new wxValueTableRow();
	for (auto& obj : m_metaTable->GetObjectAttributes()) {
		if (!m_metaTable->IsNumberLine(obj->GetMetaID())) {
			rowData->AppendTableValue(obj->GetMetaID(), node->GetTableValue(obj->GetMetaID()));
		}
		else {
			rowData->AppendTableValue(obj->GetMetaID(), CValue());
		}
	}
	long currentLine = GetRow(currentItem);
	if (currentLine != wxNOT_FOUND) {
		IValueTable::Insert(rowData, currentLine, !CBackendException::IsEvalMode());
	}
	else {
		IValueTable::Append(rowData, !CBackendException::IsEvalMode());
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
	if (node == nullptr)
		return;
	if (!CBackendException::IsEvalMode()) {
		IValueTable::Remove(node);
	}
}

void CTabularSectionDataObjectRef::CopyValue()
{
	ITabularSectionDataObject::CopyValue();

	if (!CBackendException::IsEvalMode()) {
		IBackendValueForm* const foundedForm = IBackendValueForm::FindFormByUniqueKey(
			m_objectValue->GetGuid()
		);
		if (foundedForm != nullptr) {
			foundedForm->Modify(true);
		}
	}
}

void CTabularSectionDataObjectRef::DeleteValue()
{
	ITabularSectionDataObject::DeleteValue();

	if (!CBackendException::IsEvalMode()) {
		IBackendValueForm* const foundedForm = IBackendValueForm::FindFormByUniqueKey(
			m_objectValue->GetGuid()
		);
		if (foundedForm != nullptr) {
			foundedForm->Modify(true);
		}
	}
}