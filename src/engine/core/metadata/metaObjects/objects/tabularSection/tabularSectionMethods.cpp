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
	case enAdd: return new CTabularSectionDataObjectReturnLine(this, AppenRow());
	case enCount: return (unsigned int)m_aObjectValues.size();
	case enClear: m_aObjectValues.clear(); break;

	case enLoad: ITabularSectionDataObject::LoadDataFromTable(aParams[0].ConvertToType<IValueTable>()); break;
	case enUnload: return SaveDataToTable();

	case enGetMetadata: return m_metaTable;
	}

	return Ret;
}

unsigned int ITabularSectionDataObject::AppenRow(unsigned int before)
{
	std::map<meta_identifier_t, CValue> valueRow;

	for (auto attribute : m_metaTable->GetObjectAttributes()) {
		if (!m_metaTable->IsNumberLine(attribute->GetMetaID())) {
			valueRow.insert_or_assign(attribute->GetMetaID(), attribute->CreateValue());
		}
		else {
			valueRow.insert_or_assign(attribute->GetMetaID(), CValue());
		}
	}

	if (before > 0) {
		m_aObjectValues.insert(
			m_aObjectValues.begin() + before + 1, valueRow
		);
	}
	else {
		m_aObjectValues.push_back(valueRow);
	}

	if (!CTranslateError::IsSimpleMode()) {
		if (before > 0) {
			IValueTable::RowInserted(before);
			return before + 1;
		}
		IValueTable::RowPrepended();
		return m_aObjectValues.size() - 1;
	}

	return 0;
}

unsigned int CTabularSectionDataObjectRef::AppenRow(unsigned int before)
{
	if (!CTranslateError::IsSimpleMode()) {
		m_dataObject->Modify(true);
	}
	return ITabularSectionDataObject::AppenRow(before);
}