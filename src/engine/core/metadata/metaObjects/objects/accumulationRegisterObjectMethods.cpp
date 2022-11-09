////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : informationRegister object - methods
////////////////////////////////////////////////////////////////////////////

#include "accumulationRegister.h"
#include "compiler/methods.h"
#include "appData.h"

enum recordSet
{
	enAdd = 0,
	enCount,
	enClear,

	enLoad,
	enUnload,

	enWriteRecordSet,

	enModifiedRecordSet,

	enReadRecordSet,
	enSelectedRecordSet,

	enGetMetadataRecordSet,
};

//****************************************************************************
//*                              Support methods                             *
//****************************************************************************

CMethods* CRecordSetAccumulationRegister::GetPMethods() const
{
	PrepareNames();
	return m_methods;
};

void CRecordSetAccumulationRegister::PrepareNames() const
{
	std::vector<SEng> aMethods, aAttributes;

	aMethods = {
		{"add","add()"},
		{"count","count()"},
		{"clear","clear()"},

		{"write", "write(replace)"},

		{"load", "load(table)"},
		{"unload", "unload()"},

		{"modified", "modified()"},

		{"read", "read()"},
		{"selected", "selected()"},

		{"getMetadata", "getMetadata()"},
	};

	m_methods->PrepareMethods(aMethods.data(), aMethods.size());

	{
		//add self 
		SEng attribute;
		attribute.sName = thisObject;
		attribute.sSynonym = wxT("system");
		aAttributes.push_back(attribute);
	}

	{
		//add filter 
		SEng attribute;
		attribute.sName = wxT("filter");
		attribute.sSynonym = wxT("system");
		aAttributes.push_back(attribute);
	}

	m_methods->PrepareAttributes(aAttributes.data(), aAttributes.size());
}

#include "frontend/visualView/controls/form.h"

CValue CRecordSetAccumulationRegister::Method(methodArg_t& aParams)
{
	CValue Ret;

	switch (aParams.GetIndex())
	{
	case recordSet::enAdd:
		return new CRecordSetRegisterReturnLine(this, GetItem(AppendRow()));
	case recordSet::enCount:
		return (unsigned int)GetRowCount();
	case recordSet::enClear:
		IValueTable::Clear();
		break;
	case recordSet::enLoad:
		LoadDataFromTable(aParams[0].ConvertToType<IValueTable>());
		break;
	case recordSet::enUnload:
		return SaveDataToTable();
	case recordSet::enWriteRecordSet:
		WriteRecordSet(
			aParams.GetParamCount() > 0 ?
			aParams[0].GetBoolean() : true
		);
		break;
	case recordSet::enModifiedRecordSet:
		return m_objModified;
	case recordSet::enReadRecordSet:
		Read();
		break;
	case recordSet::enSelectedRecordSet:
		return Selected();
	case recordSet::enGetMetadataRecordSet:
		return GetMetaObject();
	}

	return Ret;
}