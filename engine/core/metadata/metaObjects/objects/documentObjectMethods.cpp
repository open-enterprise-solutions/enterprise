////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : document object - methods
////////////////////////////////////////////////////////////////////////////

#include "document.h"
#include "compiler/methods.h"
#include "appData.h"

enum
{
	enIsNew = 0,

	enCopy,
	enFill,
	enWrite,
	enDelete,

	enModified,

	enGetForm,
	enGetMetadata
};

//****************************************************************************
//*                              Support methods                             *
//****************************************************************************

CMethods* CObjectDocument::GetPMethods() const
{
	CObjectDocument::PrepareNames();
	return m_methods;
};

void CObjectDocument::PrepareNames() const
{
	std::vector<SEng> aMethods, aAttributes;

	aMethods = {
	{"isNew", "isNew()"},

	{"copy", "copy()"},
	{"fill", "fill(object)"},
	{"write", "write()"},
	{"delete", "delete()"},

	{"modified", "modified()"},

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

	{
		//add reg records 
		SEng attribute;
		attribute.sName = registerRecords;
		attribute.sSynonym = wxT("system");
		aAttributes.push_back(attribute);
	}

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

	//fill custom tables 
	for (auto tables : m_metaObject->GetObjectTables()) {
		if (tables->IsDeleted())
			continue;
		SEng table;
		table.sName = tables->GetName();
		table.iName = tables->GetMetaID();
		table.sSynonym = wxT("table");
		aAttributes.push_back(table);
	}

	if (m_procUnit != NULL) {
		CByteCode* m_byteCode = m_procUnit->GetByteCode();
		for (auto exportFunction : m_byteCode->m_aExportFuncList) {
			SEng methods;
			methods.sName = exportFunction.first;
			methods.sSynonym = wxT("procUnit");
			methods.iName = exportFunction.second;
			aMethods.push_back(methods);
		}

		for (auto exportVariable : m_byteCode->m_aExportVarList) {
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

CValue CObjectDocument::Method(methodArg_t& aParams)
{
	switch (aParams.GetIndex())
	{
	case enIsNew: return m_newObject;
	case enCopy: return CopyObject();
	case enFill: FillObject(aParams[0]); break;
	case enWrite: WriteObject(aParams[0].ConvertToEnumType<eDocumentWriteMode>(), aParams[1].ConvertToEnumType<eDocumentPostingMode>()); break;
	case enDelete: DeleteObject(); break;
	case enModified: return m_objModified;
	case enGetForm: return GetFormValue();
	case enGetMetadata: return m_metaObject;
	}

	return IModuleInfo::ExecuteMethod(aParams);
}

/////////////////////////////////////////////////////////////////////////////////

enum
{
	enWriteRegister = 0
};

CMethods* CObjectDocument::CRecordRegister::GetPMethods() const
{
	CObjectDocument::CRecordRegister::PrepareNames();
	return m_methods;
}

#include "metadata/metadata.h"

void CObjectDocument::CRecordRegister::PrepareNames() const
{
	std::vector<SEng> aMethods, aAttributes;

	aMethods = {
	{"write", "write()"},
	};

	for (auto pair : m_records) {
		
		IRecordSetObject *record = pair.second; 
		wxASSERT(record);
		IMetaObjectRegisterData *metaObject =
			record->GetMetaObject();	
		wxASSERT(metaObject);

		SEng attributes;
		attributes.sName = metaObject->GetName();
		attributes.sSynonym = wxT("record");
		attributes.iName = pair.first;
		aAttributes.push_back(attributes);
	}

	m_methods->PrepareAttributes(aAttributes.data(), aAttributes.size());
	m_methods->PrepareMethods(aMethods.data(), aMethods.size());
}

CValue CObjectDocument::CRecordRegister::Method(methodArg_t& aParams)
{
	switch (aParams.GetIndex())
	{
	case enWriteRegister: 
		WriteRecordSet(); 
		break;
	}

	return CValue();
}