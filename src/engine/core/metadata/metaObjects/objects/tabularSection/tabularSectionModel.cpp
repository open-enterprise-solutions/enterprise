////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : tabular sections - model
////////////////////////////////////////////////////////////////////////////

#include "tabularSection.h"

static int my_sort_reverse(int *v1, int *v2)
{
	return *v2 - *v1;
}

static int my_sort(int *v1, int *v2)
{
	return *v1 - *v2;
}

//***********************************************************************************
//*                                  Model                                          *
//***********************************************************************************

void ITabularSectionDataObject::Prepend(const wxString &text)
{
	RowPrepended();
}

void ITabularSectionDataObject::DeleteItem(const wxDataViewItem &item)
{
	unsigned int row = GetRow(item);
	RowDeleted(row);
}

void ITabularSectionDataObject::DeleteItems(const wxDataViewItemArray &items)
{
	wxArrayInt rows;

	for (unsigned i = 0; i < items.GetCount(); i++)
	{
		unsigned int row = GetRow(items[i]);
		rows.Add(row);
	}

	// Sort in descending order so that the last
	// row will be deleted first. Otherwise the
	// remaining indeces would all be wrong.
	rows.Sort(my_sort_reverse);

	// This is just to test if wxDataViewCtrl can
	// cope with removing rows not sorted in
	// descending order
	rows.Sort(my_sort);
	RowsDeleted(rows);
}

unsigned int ITabularSectionDataObject::GetColumnCount() const
{
	return 0;
}

wxString ITabularSectionDataObject::GetColumnType(unsigned int col) const
{
	return wxT("string");
}

void ITabularSectionDataObject::GetValueByRow(wxVariant &variant,
	unsigned int row, unsigned int col) const
{
	auto itFounded = m_aObjectValues.begin();
	std::advance(itFounded, row);
	auto &rowValues = *itFounded;

	auto foundedColumn = rowValues.find(col);
	if (foundedColumn != rowValues.end()) {
		if (m_metaTable->IsNumberLine(foundedColumn->first)) {
			variant = wxString::Format("%i", row + 1);
		}
		else {
			const CValue &cValue = foundedColumn->second;
			variant = cValue.GetString();
		}
	}
}

bool ITabularSectionDataObject::GetAttrByRow(unsigned int row, unsigned int col, wxDataViewItemAttr &attr) const
{
	return true;
}

#include "metadata/metadata.h"

bool ITabularSectionDataObject::SetValueByRow(const wxVariant &variant,
	unsigned int row, unsigned int col)
{
	wxString strData = variant.GetString();
	auto itFounded = m_aObjectValues.begin();
	std::advance(itFounded, row);
	auto &rowValues = *itFounded;
	IMetadata* metaData = m_metaTable->GetMetadata();
	auto foundedColumn = rowValues.find(col);
	if (foundedColumn != rowValues.end()) {
		if (!m_metaTable->IsNumberLine(foundedColumn->first)) {
			CValue selValue = GetValueByMetaID(row, foundedColumn->first);
			wxString className = metaData->GetNameObjectFromID(
				selValue.GetClassType()
			);			
			CValue newValue = metaData->CreateObject(className);
			if (strData.Length() > 0) {
				std::vector<CValue> foundedObjects;
				if (newValue.FindValue(strData, foundedObjects)) {
					SetValueByMetaID(row, foundedColumn->first, foundedObjects.at(0));
				}
				else {
					return false;
				}
			}
			else {
				SetValueByMetaID(row, foundedColumn->first, newValue);
			}
		}
	}

	return true;
}

#include "frontend/visualView/controls/form.h"

void ITabularSectionDataObject::AddValue(unsigned int before)
{
	AppenRow(before);
}

void ITabularSectionDataObject::CopyValue()
{
	int currentLine = GetSelectionLine();
	if (currentLine == wxNOT_FOUND)
		return;

	auto foundedIt = m_aObjectValues.begin();
	std::advance(foundedIt, currentLine);

	std::map<meta_identifier_t, CValue> valueRow;

	IMetadata *metaData = m_metaTable->GetMetadata();
	for (auto attribute : m_metaTable->GetObjectAttributes()) {
		if (!m_metaTable->IsNumberLine(attribute->GetMetaID())) {
			valueRow.insert_or_assign(attribute->GetMetaID(),
				foundedIt->at(attribute->GetMetaID()));
		}
		else {
			valueRow.insert_or_assign(attribute->GetMetaID(), CValue());
		}
	}

	if (currentLine != wxNOT_FOUND) {
		m_aObjectValues.insert(
			m_aObjectValues.begin() + currentLine + 1, valueRow
		);
	}
	else {
		m_aObjectValues.push_back(valueRow);
	}

	if (!CTranslateError::IsSimpleMode()) {

		CValueForm *foundedForm = CValueForm::FindFormByGuid(
			m_dataObject->GetGuid()
		);

		if (currentLine != wxNOT_FOUND) {
			IValueTable::RowInserted(currentLine);
		}
		else {
			IValueTable::RowPrepended();
		}

		if (foundedForm) {
			foundedForm->Modify(true);
		}
	}
}

void ITabularSectionDataObject::EditValue()
{
	int currentLine = GetSelectionLine();
	if (currentLine == wxNOT_FOUND)
		return;

	IValueTable::RowValueStartEdit(
		GetItem(currentLine)
	);
}

void ITabularSectionDataObject::DeleteValue()
{
	int currentLine = GetSelectionLine();
	if (currentLine == wxNOT_FOUND)
		return;

	if (!CTranslateError::IsSimpleMode()) {
		auto foundedIt = m_aObjectValues.begin();
		
		std::advance(foundedIt, currentLine);
		if (foundedIt != m_aObjectValues.end()) {
			m_aObjectValues.erase(foundedIt);
		}	
		
		CValueForm *foundedForm = CValueForm::FindFormByGuid(
			m_dataObject->GetGuid()
		);

		IValueTable::RowDeleted(currentLine);

		if (foundedForm) {
			foundedForm->Modify(true);
		}
	}
}
