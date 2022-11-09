////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : tabular sections - methods
////////////////////////////////////////////////////////////////////////////

#include "tabularSection.h"
#include "compiler/methods.h"
#include "metadata/metaObjects/objects/object.h"
#include "utils/stringUtils.h"

CMethods ITabularSectionDataObject::m_methods;

enum
{
	enAdd = 0,
	enCount,
	enFind,
	enDelete,
	enClear,
	enLoad,
	enUnload,
	enGetMetadata,
};

//****************************************************************************
//*                              Support methods                             *
//****************************************************************************

CMethods* ITabularSectionDataObject::GetPMethods() const
{
	PrepareNames();
	return &m_methods;
};

void ITabularSectionDataObject::PrepareNames() const
{
	SEng aMethods[] =
	{
		{"add","add()"},
		{"count","count()"},
		{"find","find(value, col)"},
		{"delete","delete(row)"},
		{"clear","clear()"},
		{"load", "load(table)"},
		{"unload", "unload()"},
		{"getMetadata", "getMetadata()"},
	};

	int nCountM = sizeof(aMethods) / sizeof(aMethods[0]);
	m_methods.PrepareMethods(aMethods, nCountM);
}

#include "metadata/metadata.h"

CValue ITabularSectionDataObject::Method(methodArg_t& aParams)
{
	CValue Ret;

	switch (aParams.GetIndex())
	{
	case enAdd: 
		return new CTabularSectionDataObjectReturnLine(this, GetItem(AppendRow()));
	case enFind: {
		const wxDataViewItem& item = FindRowValue(aParams[0], aParams[1].GetString());
		if (item.IsOk())
			return GetRowAt(item);
		break;
	}
	case enCount:
		return (unsigned int)GetRowCount();
	case enDelete: {
		CTabularSectionDataObjectReturnLine* retLine = NULL;
		if (aParams[0].ConvertToValue(retLine)) {
			wxValueTableRow* node = GetViewData(retLine->GetLineItem());
			if (node != NULL)
				IValueTable::Remove(node);
		}
		else {
			number_t number = aParams[0].GetNumber();
			wxValueTableRow* node = GetViewData(GetItem(number.ToInt()));
			if (node != NULL)
				IValueTable::Remove(node);
		}
		break;
	}
	case enClear: 
		Clear(); 
		break;
	case enLoad: 
		ITabularSectionDataObject::LoadDataFromTable(aParams[0].ConvertToType<IValueTable>()); 
		break;
	case enUnload: 
		return SaveDataToTable();
	case enGetMetadata: 
		return m_metaTable;
	}

	return Ret;
}

long ITabularSectionDataObject::AppendRow(unsigned int before)
{
	modelArray_t valueRow;
	for (auto attribute : m_metaTable->GetObjectAttributes()) {
		if (!m_metaTable->IsNumberLine(attribute->GetMetaID())) {
			valueRow.insert_or_assign(attribute->GetMetaID(), attribute->CreateValue());
		}
		else {
			valueRow.insert_or_assign(attribute->GetMetaID(), CValue());
		}
	}

	if (before > 0)
		return IValueTable::Insert(
			new wxValueTableRow(valueRow), before, !CTranslateError::IsSimpleMode());

	return IValueTable::Append(
		new wxValueTableRow(valueRow), !CTranslateError::IsSimpleMode()
	);
}

long CTabularSectionDataObjectRef::AppendRow(unsigned int before)
{
	if (!CTranslateError::IsSimpleMode())
		m_dataObject->Modify(true);
	return ITabularSectionDataObject::AppendRow(before);
}