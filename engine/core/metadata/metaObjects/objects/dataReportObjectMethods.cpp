////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : report - methods
////////////////////////////////////////////////////////////////////////////

#include "dataReport.h"
#include "compiler/methods.h"
#include "appData.h"

enum
{
	enGetForm = 0,
	enGetMetadata
};

//****************************************************************************
//*                              Support methods                             *
//****************************************************************************

CMethods* CObjectReport::GetPMethods() const
{
	CObjectReport::PrepareNames();
	return m_methods;
};

void CObjectReport::PrepareNames() const
{
	std::vector<SEng> aMethods, aAttributes;

	aMethods = {

	{"getFormObject", "getFormObject(string, owner, guid)"},
	{"getMetadata", "getMetadata()"},
	};

	{
		//add self 
		SEng attribute;
		attribute.sName = thisObject;
		attribute.sSynonym = wxT("system");
		aAttributes.push_back(attribute);
	}

	//fill custom attributes 
	for (auto attributes : m_metaObject->GetObjectAttributes()){
		if (attributes->IsDeleted())
			continue;
		SEng attribute;
		attribute.sName = attributes->GetName();
		attribute.iName = attributes->GetMetaID();
		attribute.sSynonym = wxT("attribute");
		aAttributes.push_back(attribute);
	}

	//fill custom tables 
	for (auto tables : m_metaObject->GetObjectTables()){
		if (tables->IsDeleted())
			continue;
		SEng table;
		table.sName = tables->GetName();
		table.iName = tables->GetMetaID();
		table.sSynonym = wxT("table");
		aAttributes.push_back(table);
	}

	if (m_procUnit != NULL)
	{
		CByteCode *m_byteCode = m_procUnit->GetByteCode();

		for (auto exportFunction : m_byteCode->m_aExportFuncList)
		{
			SEng methods;
			methods.sName = exportFunction.first;
			methods.sSynonym = wxT("procUnit");
			methods.iName = exportFunction.second;
			aMethods.push_back(methods);
		}

		for (auto exportVariable : m_byteCode->m_aExportVarList)
		{
			SEng attributes;
			attributes.sName = exportVariable.first;
			attributes.sSynonym = wxT("procUnit");
			attributes.iName = exportVariable.second;
			aAttributes.push_back(attributes);
		}
	}

	m_methods->PrepareAttributes(aAttributes.data(), aAttributes.size());
	m_methods->PrepareMethods(aMethods.data(), aMethods.size());
}

#include "frontend/visualView/controls/form.h"

CValue CObjectReport::Method(methodArg_t &aParams)
{
	switch (aParams.GetIndex())
	{
	case enGetForm: return GetFormValue();
	case enGetMetadata: return m_metaObject;
	}

	return IModuleInfo::ExecuteMethod(aParams);
}