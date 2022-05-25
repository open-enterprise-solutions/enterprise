////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : informationRegister object - methods
////////////////////////////////////////////////////////////////////////////

#include "informationRegister.h"
#include "compiler/methods.h"
#include "appData.h"

enum recordManager
{
	enCopyRecordManager,
	enWriteRecordManager,
	enDeleteRecordManager,

	enModifiedRecordManager,

	enReadRecordManager,
	enSelectedRecordManager,

	enGetFormRecord,
	enGetMetadataRecordManager
};

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

CMethods* CRecordSetInformationRegister::GetPMethods() const
{
	PrepareNames();
	return m_methods;
};

CMethods* CRecordManagerInformationRegister::GetPMethods() const
{
	CRecordManagerInformationRegister::PrepareNames();
	return m_methods;
};

void CRecordSetInformationRegister::PrepareNames() const
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

void CRecordManagerInformationRegister::PrepareNames() const
{
	std::vector<SEng> aMethods, aAttributes;

	aMethods = {
	{"copy", "copy()"},
	{"write", "write(replace)"},
	{"delete", "delete()"},

	{"modified", "modified()"},

	{"read", "read()"},
	{"selected", "selected()"},

	{"getFormRecord", "getFormRecord(string, owner, guid)"},
	{"getMetadata", "getMetadata()"},
	};

	//fill custom attributes 
	for (auto attributes : m_metaObject->GetGenericAttributes()) {
		if (attributes->IsDeleted())
			continue;
		SEng attribute;
		attribute.sName = attributes->GetName();
		attribute.iName = attributes->GetMetaID();
		attribute.sSynonym = wxT("attribute");
		aAttributes.push_back(attribute);
	}

	m_methods->PrepareAttributes(aAttributes.data(), aAttributes.size());
	m_methods->PrepareMethods(aMethods.data(), aMethods.size());
}

#include "frontend/visualView/controls/form.h"

CValue CRecordSetInformationRegister::Method(methodArg_t& aParams)
{
	CValue Ret;

	switch (aParams.GetIndex())
	{
	case recordSet::enAdd:
		return new CRecordSetRegisterReturnLine(this, AppenRow());
	case recordSet::enCount:
		return (unsigned int)m_aObjectValues.size();
	case recordSet::enClear:
		m_aObjectValues.clear();
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

CValue CRecordManagerInformationRegister::Method(methodArg_t& aParams)
{
	switch (aParams.GetIndex())
	{
	case recordManager::enCopyRecordManager:
		return CopyRegister();
	case recordManager::enWriteRecordManager:
		return WriteRegister(
			aParams.GetParamCount() > 0 ?
			aParams[0].GetBoolean() : true
		);
	case recordManager::enDeleteRecordManager:
		return DeleteRegister();
	case recordManager::enModifiedRecordManager:
		return m_recordSet->IsModified();
	case recordManager::enReadRecordManager:
		m_recordSet->Read();
		break;
	case recordManager::enSelectedRecordManager:
		return m_recordSet->Selected();
	case recordManager::enGetFormRecord:
		return GetFormValue();
	case recordManager::enGetMetadataRecordManager:
		return m_metaObject;
	}

	return CValue();
}