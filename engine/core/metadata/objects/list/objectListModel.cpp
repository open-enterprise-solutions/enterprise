////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : list model 
////////////////////////////////////////////////////////////////////////////

#include "objectList.h"

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


void IDataObjectList::Prepend(const wxString &text)
{
	RowPrepended();
}

void IDataObjectList::DeleteItem(const wxDataViewItem &item)
{
	unsigned int row = GetRow(item);
	RowDeleted(row);
}

void IDataObjectList::DeleteItems(const wxDataViewItemArray &items)
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

// implementation of base class virtuals to define model
unsigned int IDataObjectList::GetColumnCount() const
{
	return 0;
}

wxString IDataObjectList::GetColumnType(unsigned int col) const
{
	return wxT("string");
}

void IDataObjectList::GetValueByRow(wxVariant &variant,
	unsigned int row, unsigned int col) const
{
	auto itFounded = m_aObjectValues.begin();
	std::advance(itFounded, row);
	auto &rowValues = itFounded->second;

	auto foundedColumn = rowValues.find(col);
	if (foundedColumn != rowValues.end()) {
		const CValue &cValue = foundedColumn->second;
		variant = cValue.GetString();
	}
}

bool IDataObjectList::GetAttrByRow(unsigned int row, unsigned int col, wxDataViewItemAttr &attr) const
{
	return true;
}

bool IDataObjectList::SetValueByRow(const wxVariant &variant,
	unsigned int row, unsigned int col)
{
	auto itFounded = m_aObjectValues.begin();
	std::advance(itFounded, row);
	auto &rowValues = itFounded->second;

	auto foundedColumn = rowValues.find(col);
	if (foundedColumn != rowValues.end()) {
		CValue &cValue = foundedColumn->second;
		cValue.SetValue(variant.GetString());
	}

	return false;
}