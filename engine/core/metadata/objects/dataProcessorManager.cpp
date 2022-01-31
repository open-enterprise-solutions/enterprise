////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : dataProcessor - manager
////////////////////////////////////////////////////////////////////////////

#include "dataProcessorManager.h"
#include "metadata/metadata.h"
#include "compiler/methods.h"
#include "baseObject.h"

wxIMPLEMENT_DYNAMIC_CLASS(CManagerDataProcessorValue, CValue);

CManagerDataProcessorValue::CManagerDataProcessorValue(CMetaObjectDataProcessorValue *metaObject) : CValue(eValueTypes::TYPE_VALUE, true),
m_methods(new CMethods()), m_metaObject(metaObject)
{
}

CManagerDataProcessorValue::~CManagerDataProcessorValue()
{
	wxDELETE(m_methods);
}

CMetaCommonModuleObject *CManagerDataProcessorValue::GetModuleManager() const { return m_metaObject->GetModuleManager(); }

#include "metadata/singleMetaTypes.h"

CLASS_ID CManagerDataProcessorValue::GetTypeID() const
{
	IMetaTypeObjectValueSingle *clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetTypeID();
}

wxString CManagerDataProcessorValue::GetTypeString() const
{
	IMetaTypeObjectValueSingle *clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString CManagerDataProcessorValue::GetString() const
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