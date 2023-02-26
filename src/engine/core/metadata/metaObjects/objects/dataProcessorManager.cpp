////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : dataProcessor - manager
////////////////////////////////////////////////////////////////////////////

#include "dataProcessorManager.h"
#include "core/metadata/metadata.h"
#include "object.h"

wxIMPLEMENT_DYNAMIC_CLASS(CDataProcessorManager, CValue);

CDataProcessorManager::CDataProcessorManager(CMetaObjectDataProcessor* metaObject) : CValue(eValueTypes::TYPE_VALUE, true),
m_methodHelper(new CMethodHelper()), m_metaObject(metaObject)
{
}

CDataProcessorManager::~CDataProcessorManager()
{
	wxDELETE(m_methodHelper);
}

CMetaCommonModuleObject* CDataProcessorManager::GetModuleManager() const { return m_metaObject->GetModuleManager(); }

#include "core/metadata/singleClass.h"

CLASS_ID CDataProcessorManager::GetTypeClass() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetTypeClass();
}

wxString CDataProcessorManager::GetTypeString() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString CDataProcessorManager::GetString() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

//////////////////////////////////////////////////////////////////////////////////////////////////////

wxIMPLEMENT_DYNAMIC_CLASS(CManagerExternalDataProcessorValue, CValue);

CManagerExternalDataProcessorValue::CManagerExternalDataProcessorValue() : CValue(eValueTypes::TYPE_VALUE, true),
m_methodHelper(new CMethodHelper())
{
}

CManagerExternalDataProcessorValue::~CManagerExternalDataProcessorValue()
{
	wxDELETE(m_methodHelper);
}

wxString CManagerExternalDataProcessorValue::GetTypeString() const
{
	return wxT("extenalManagerDataProcessor");
}

wxString CManagerExternalDataProcessorValue::GetString() const
{
	return wxT("extenalManagerDataProcessor");
}

//////////////////////////////////////////////////////////////////////////////////////////////////////

enum Func {
	eCreate = 0,
	eGetForm
};

#include "core/metadata/metadata.h"

void CDataProcessorManager::PrepareNames() const
{
	IMetadata* metaData = m_metaObject->GetMetadata();
	wxASSERT(metaData);
	IModuleManager* moduleManager = metaData->GetModuleManager();
	wxASSERT(moduleManager);

	m_methodHelper->ClearHelper();
	m_methodHelper->AppendFunc("create", "create()");
	m_methodHelper->AppendFunc("getForm", "getForm(string, owner, guid)");

	CValue* pRefData = moduleManager->FindCommonModule(m_metaObject->GetModuleManager());
	if (pRefData != NULL) {
		//добавляем методы из контекста
		for (long idx = 0; idx < pRefData->GetNMethods(); idx++) {
			m_methodHelper->CopyMethod(pRefData->GetPMethods(), idx);
		}
	}
}

#include "frontend/visualView/controls/form.h"

bool CDataProcessorManager::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case eCreate:
		pvarRetValue = m_metaObject->CreateObjectValue();
		return true;
	case eGetForm:
	{
		CValueGuid* guidVal = lSizeArray > 2 ? paParams[2]->ConvertToType<CValueGuid>() : NULL;
		pvarRetValue = m_metaObject->GetGenericForm(paParams[0]->GetString(),
			lSizeArray > 1 ? paParams[1]->ConvertToType<IValueFrame>() : NULL,
			guidVal ? ((Guid)*guidVal) : Guid());
		return true;
	}
	}

	IMetadata* metaData = m_metaObject->GetMetadata();
	wxASSERT(metaData);
	IModuleManager* moduleManager = metaData->GetModuleManager();
	wxASSERT(moduleManager);

	CValue* pRefData =
		moduleManager->FindCommonModule(m_metaObject->GetModuleManager());
	if (pRefData != NULL)
		return pRefData->CallAsFunc(lMethodNum, pvarRetValue, paParams, lSizeArray);
	return false;
}

void CManagerExternalDataProcessorValue::PrepareNames() const
{
	m_methodHelper->ClearHelper();
	m_methodHelper->AppendFunc("create", "create(fullPath)");
}

#include "core/compiler/systemObjects.h"
#include "core/metadata/external/metadataDataProcessor.h"

bool CManagerExternalDataProcessorValue::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case eCreate:
	{
		CMetadataDataProcessor* metaDataProcessor = new CMetadataDataProcessor();
		if (metaDataProcessor->LoadFromFile(paParams[0]->GetString())) {
			IModuleManager* moduleManager = metaDataProcessor->GetModuleManager();
			pvarRetValue = moduleManager->GetObjectValue();
			return true; 
		}
		wxDELETE(metaDataProcessor);
		CSystemObjects::Raise(
			wxString::Format("Failed to load data processor '%s'", paParams[0]->GetString())
		);
		return false;
	}
	}

	return false;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

SO_VALUE_REGISTER(CManagerExternalDataProcessorValue, "extenalManagerDataProcessor", TEXT2CLSID("MG_EXTD"));