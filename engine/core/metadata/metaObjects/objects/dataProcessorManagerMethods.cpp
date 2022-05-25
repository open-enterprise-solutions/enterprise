////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : dataProcessor - methods
////////////////////////////////////////////////////////////////////////////

#include "dataProcessorManager.h"
#include "compiler/methods.h"

enum
{
	eCreate = 0,
	eGetForm
};

#include "metadata/metadata.h"

void CDataProcessorManager::PrepareNames() const
{
	IMetadata *metaData = m_metaObject->GetMetadata();
	wxASSERT(metaData);
	IModuleManager *moduleManager = metaData->GetModuleManager();
	wxASSERT(moduleManager);

	SEng aMethods[] =
	{
		{"create", "create()"},
		{"getForm", "getForm(string, owner, guid)"},
	};

	int nCountM = sizeof(aMethods) / sizeof(aMethods[0]);
	m_methods->PrepareMethods(aMethods, nCountM);

	CValue *pRefData = moduleManager->FindCommonModule(m_metaObject->GetModuleManager());

	if (pRefData) {

		//добавляем методы из контекста
		for (unsigned int idx = 0; idx < pRefData->GetNMethods(); idx++) {
			m_methods->AppendMethod(pRefData->GetMethodName(idx),
				pRefData->GetMethodDescription(idx),
				wxT("commonModule"));
		}
	}
}

#include "frontend/visualView/controls/form.h"

CValue CDataProcessorManager::Method(methodArg_t &aParams)
{
	CValue ret;

	switch (aParams.GetIndex())
	{
	case eCreate:
	{
		return m_metaObject->CreateObjectValue();
	}
	case eGetForm:
	{
		CValueGuid *guidVal = aParams.GetParamCount() > 2 ? aParams[2].ConvertToType<CValueGuid>() : NULL;
		return m_metaObject->GetGenericForm(aParams[0].GetString(),
			aParams.GetParamCount() > 1 ? aParams[1].ConvertToType<IValueFrame>() : NULL,
			guidVal ? ((Guid)*guidVal) : Guid());
	}
	}

	IMetadata *metaData = m_metaObject->GetMetadata();
	wxASSERT(metaData);
	IModuleManager *moduleManager = metaData->GetModuleManager();
	wxASSERT(moduleManager);

	CValue *pRefData = moduleManager->FindCommonModule(m_metaObject->GetModuleManager());

	if (pRefData) {
		return pRefData->Method(aParams);
	}

	return ret;
}

void CManagerExternalDataProcessorValue::PrepareNames() const
{
	SEng aMethods[] =
	{
		{"create", "create(fullPath)"}
	};

	int nCountM = sizeof(aMethods) / sizeof(aMethods[0]);
	m_methods->PrepareMethods(aMethods, nCountM);
}

#include "compiler/systemObjects.h"
#include "metadata/external/metadataDataProcessor.h"

CValue CManagerExternalDataProcessorValue::Method(methodArg_t &aParams)
{
	CValue ret;

	switch (aParams.GetIndex())
	{
	case eCreate:
	{
		CMetadataDataProcessor *metaDataProcessor = new CMetadataDataProcessor();
		if (metaDataProcessor->LoadFromFile(aParams[0].GetString())) {
			IModuleManager *moduleManager = metaDataProcessor->GetModuleManager();
			return moduleManager->GetObjectValue();
		}
		wxDELETE(metaDataProcessor);
		CSystemObjects::Raise(
			wxString::Format("Failed to load data processor '%s'", aParams[0].GetString())
		);
	}
	}

	return ret;
}