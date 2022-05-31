////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : acc register manager
////////////////////////////////////////////////////////////////////////////

#include "accumulationRegisterManager.h"
#include "metadata/metadata.h"
#include "compiler/methods.h"
#include "baseObject.h"

wxIMPLEMENT_DYNAMIC_CLASS(CAccumulationRegisterManager, CValue);

CAccumulationRegisterManager::CAccumulationRegisterManager(CMetaObjectAccumulationRegister* metaObject) : CValue(eValueTypes::TYPE_VALUE, true),
m_methods(new CMethods()), m_metaObject(metaObject)
{
}

CAccumulationRegisterManager::~CAccumulationRegisterManager()
{
	wxDELETE(m_methods);
}

CMetaCommonModuleObject* CAccumulationRegisterManager::GetModuleManager() const { 
	return m_metaObject->GetModuleManager(); 
}

#include "metadata/singleMetaTypes.h"

CLASS_ID CAccumulationRegisterManager::GetClassType() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString CAccumulationRegisterManager::GetTypeString() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString CAccumulationRegisterManager::GetString() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}