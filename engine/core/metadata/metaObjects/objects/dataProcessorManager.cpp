////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : dataProcessor - manager
////////////////////////////////////////////////////////////////////////////

#include "dataProcessorManager.h"
#include "metadata/metadata.h"
#include "compiler/methods.h"
#include "baseObject.h"

wxIMPLEMENT_DYNAMIC_CLASS(CDataProcessorManager, CValue);

CDataProcessorManager::CDataProcessorManager(CMetaObjectDataProcessor *metaObject) : CValue(eValueTypes::TYPE_VALUE, true),
m_methods(new CMethods()), m_metaObject(metaObject)
{
}

CDataProcessorManager::~CDataProcessorManager()
{
	wxDELETE(m_methods);
}

CMetaCommonModuleObject *CDataProcessorManager::GetModuleManager() const { return m_metaObject->GetModuleManager(); }

#include "metadata/singleMetaTypes.h"

CLASS_ID CDataProcessorManager::GetClassType() const
{
	IMetaTypeObjectValueSingle *clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString CDataProcessorManager::GetTypeString() const
{
	IMetaTypeObjectValueSingle *clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString CDataProcessorManager::GetString() const
{
	IMetaTypeObjectValueSingle *clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

//////////////////////////////////////////////////////////////////////////////////////////////////////

wxIMPLEMENT_DYNAMIC_CLASS(CManagerExternalDataProcessorValue, CValue);

CManagerExternalDataProcessorValue::CManagerExternalDataProcessorValue() : CValue(eValueTypes::TYPE_VALUE, true),
m_methods(new CMethods())
{
}

CManagerExternalDataProcessorValue::~CManagerExternalDataProcessorValue()
{
	wxDELETE(m_methods);
}

wxString CManagerExternalDataProcessorValue::GetTypeString() const
{
	return wxT("extenalManagerDataProcessor");
}

wxString CManagerExternalDataProcessorValue::GetString() const
{
	return wxT("extenalManagerDataProcessor");
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

SO_VALUE_REGISTER(CManagerExternalDataProcessorValue, "extenalManagerDataProcessor", CManagerExternalDataProcessorValue, TEXT2CLSID("MG_EXTD"));