////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : information register manager
////////////////////////////////////////////////////////////////////////////

#include "informationRegisterManager.h"
#include "metadata/metadata.h"
#include "compiler/methods.h"
#include "object.h"

wxIMPLEMENT_DYNAMIC_CLASS(CInformationRegisterManager, CValue);

CInformationRegisterManager::CInformationRegisterManager(CMetaObjectInformationRegister* metaObject) : CValue(eValueTypes::TYPE_VALUE, true),
m_methods(new CMethods()), m_metaObject(metaObject)
{
}

CInformationRegisterManager::~CInformationRegisterManager()
{
	wxDELETE(m_methods);
}

CMetaCommonModuleObject* CInformationRegisterManager::GetModuleManager() const { 
	return m_metaObject->GetModuleManager(); 
}

#include "metadata/singleMetaTypes.h"

CLASS_ID CInformationRegisterManager::GetClassType() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString CInformationRegisterManager::GetTypeString() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString CInformationRegisterManager::GetString() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}