////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : tabular sections - model
////////////////////////////////////////////////////////////////////////////

#include "tabularSection.h"
//***********************************************************************************
//*                                  Model                                          *
//***********************************************************************************

void ibValueTabularSectionDataObjectBase::GetValueByRow(wxVariant& variant,
	const ibDataViewItem& row, unsigned int col) const
{
	ibValueTableRow* node = GetViewData<ibValueTableRow>(row);
	if (node == nullptr) return;

	if (m_metaTable->IsNumberLine(col)) {
		// Sequential display number = position in the filter+sort
		// view, not raw m_nodeValues index — so user-visible
		// numbering stays 1..N regardless of insertion order or
		// filter state.  GetRow() returns the raw index (consumed
		// by save / iteration); we re-resolve through the view
		// here for rendering only.
		auto view = BuildVisibleView();
		auto it = std::find(view.begin(), view.end(), node);
		long pos = (it != view.end())
			? std::distance(view.begin(), it) : 0;
		variant = new ibVariantDataValueNumberLine(pos + 1);
	}
	else if (node->HasColumnValue(col))
		node->GetValue(col, variant);
}

#include "backend/metaData.h"

bool ibValueTabularSectionDataObjectBase::SetValueByRow(const wxVariant& variant,
	const ibDataViewItem& row, unsigned int col)
{
	ibValueTableRow* node = GetViewData<ibValueTableRow>(row);
	
	if (node == nullptr) return false;

	if (!m_metaTable->IsNumberLine(col)) {
		const ibMetaData* metaData = m_metaTable->GetMetaData();
		wxASSERT(metaData);
		const bool hasCol = node->HasColumnValue(col);
		
		if (hasCol) {

			const wxString& strData = variant.GetString();

			const ibValue& selValue = node->GetTableValue(col);
			const ibValue& newValue = metaData->CreateObject(selValue.GetClassType());

			if (strData.Length() > 0) {
				std::vector<ibValue> listValue;
				const bool found = newValue.FindValue(strData, listValue);
				
				if (found) {
					const bool ok = SetValueByMetaID(row, col, listValue.at(0));
					
				}
				else {
					return false;
				}
			}
			else {
				const bool ok = SetValueByMetaID(row, col, newValue);
				
			}
		}
		else {
			return false;
		}
	};

	// Append/Insert/Remove fire ItemInserted / ItemDeleted; SetValue path
	// fires ValueChanged.  Narrow notifies update the view in place
	// via the control's narrow ItemInserted / ValueChanged paths
	// (RAM-backed gate).  No NotifyReset on every mutation — the
	// heavy hammer caused user-visible flicker on copy / edit.
	return true;
}

void ibValueTabularSectionDataObjectBase::AddValue(unsigned int before)
{
	// Insert AFTER the active row when there is one (so user on
	// row 3 + Add → new row at position 4, focus moves there via
	// the ItemInserted handler's Select(item)).  No active row →
	// append at the bottom.
	const long row = GetRow(GetSelection());
	if (row >= 0) AppendRow(row + 1);
	else          AppendRow();
}

void ibValueTabularSectionDataObjectBase::CopyValue()
{
	const ibDataViewItem& currentItem = GetSelection();
	if (!currentItem.IsOk())
		return;
	ibValueTableRow* node = GetViewData<ibValueTableRow>(currentItem);
	if (node == nullptr)
		return;
	ibValueTableRow* rowData = new ibValueTableRow();
	for (const auto object : m_metaTable->GetAttributeArrayObject()) {
		if (!m_metaTable->IsNumberLine(object->GetMetaID())) {
			rowData->AppendTableValue(object->GetMetaID(), node->GetTableValue(object->GetMetaID()));
		}
		else {
			rowData->AppendTableValue(object->GetMetaID(), ibValue());
		}
	}
	// Copy goes AFTER the source row so the original keeps its
	// position; the new row lands at currentLine+1 and ItemInserted
	// pushes focus onto it via Select.
	long currentLine = GetRow(currentItem);
	if (currentLine != wxNOT_FOUND) {
		ibValueModelRamTableBase::Insert(rowData, currentLine + 1, !ibBackendException::IsEvalMode());
	}
	else {
		ibValueModelRamTableBase::Append(rowData, !ibBackendException::IsEvalMode());
	}
}

void ibValueTabularSectionDataObjectBase::EditValue()
{
	const ibDataViewItem& currentItem = GetSelection();
	if (!currentItem.IsOk())
		return;

	if (m_modelProvider != nullptr) {

		if (m_metaTable->IsNumberLine(m_modelProvider->GetCurrentModelColumn()))
			return;

		ibValueModelRamTableBase::RowValueStartEdit(currentItem, m_modelProvider->GetCurrentModelColumn());
	}
	else {
		ibValueModelRamTableBase::RowValueStartEdit(currentItem);
	}
}

void ibValueTabularSectionDataObjectBase::DeleteValue()
{
	const ibDataViewItem& currentItem = GetSelection();
	if (!currentItem.IsOk())
		return;
	ibValueTableRow* node = GetViewData<ibValueTableRow>(currentItem);
	if (node == nullptr)
		return;

	if (!ibBackendException::IsEvalMode()) {
		ibValueModelRamTableBase::Remove(node);
	}
}

void ibValueTabularSectionDataObjectRef::CopyValue()
{
	ibValueTabularSectionDataObjectBase::CopyValue();

	if (!ibBackendException::IsEvalMode()) {
		ibBackendValueForm* const foundedForm = ibBackendValueForm::FindFormByUniqueKey(
			m_objectValue->GetGuid()
		);
		if (foundedForm != nullptr) {
			foundedForm->Modify(true);
		}
	}
}

void ibValueTabularSectionDataObjectRef::DeleteValue()
{
	ibValueTabularSectionDataObjectBase::DeleteValue();

	if (!ibBackendException::IsEvalMode()) {
		ibBackendValueForm* const foundedForm = ibBackendValueForm::FindFormByUniqueKey(m_objectValue->GetGuid());
		if (foundedForm != nullptr) foundedForm->Modify(true);
	}
}