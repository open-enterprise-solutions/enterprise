////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : tabular sections - model
////////////////////////////////////////////////////////////////////////////

#include "baseObject.h"

static int my_sort_reverse(int* v1, int* v2)
{
	return *v2 - *v1;
}

static int my_sort(int* v1, int* v2)
{
	return *v1 - *v2;
}

//***********************************************************************************
//*                                  Model                                          *
//***********************************************************************************

void IRecordSetObject::Prepend(const wxString& text)
{
	RowPrepended();
}

void IRecordSetObject::DeleteItem(const wxDataViewItem& item)
{
	unsigned int row = GetRow(item);
	RowDeleted(row);
}

void IRecordSetObject::DeleteItems(const wxDataViewItemArray& items)
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

unsigned int IRecordSetObject::GetColumnCount() const
{
	return 0;
}

wxString IRecordSetObject::GetColumnType(unsigned int col) const
{
	return wxT("string");
}

void IRecordSetObject::GetValueByRow(wxVariant& variant,
	unsigned int row, unsigned int col) const
{
	auto itFounded = m_aObjectValues.begin();
	std::advance(itFounded, row);
	auto& rowValues = *itFounded;

	auto foundedColumn = rowValues.find(col);
	if (foundedColumn != rowValues.end()) {
		const CValue& cValue = foundedColumn->second;
		variant = cValue.GetString();
	}
}

bool IRecordSetObject::GetAttrByRow(unsigned int row, unsigned int col, wxDataViewItemAttr& attr) const
{
	return true;
}

#include "metadata/metadata.h"

bool IRecordSetObject::SetValueByRow(const wxVariant& variant,
	unsigned int row, unsigned int col)
{
	wxString stringData = variant.GetString();
	auto itFounded = m_aObjectValues.begin();
	std::advance(itFounded, row);
	auto& rowValues = *itFounded;
	IMetaObjectRegisterData* metaObject = GetMetaObject();
	wxASSERT(metaObject);
	auto foundedColumn = rowValues.find(col);
	if (foundedColumn != rowValues.end()) {
		IMetaAttributeObject* metaObjectAttribute = wxDynamicCast(
			metaObject->FindGenericAttribute(foundedColumn->first), IMetaAttributeObject
		);
		wxASSERT(metaObject);
		CValue newValue = metaObjectAttribute->CreateValue();
		if (stringData.Length() > 0) {
			std::vector<CValue> foundedObjects;
			if (newValue.FindValue(stringData, foundedObjects)) {
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

	return true;
}