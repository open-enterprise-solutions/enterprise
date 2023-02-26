////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : constants manager
////////////////////////////////////////////////////////////////////////////

#include "constantManager.h"

wxIMPLEMENT_DYNAMIC_CLASS(CConstantManager, CValue);

CConstantManager::CConstantManager(CMetaConstantObject *metaConst) :
	CValue(eValueTypes::TYPE_VALUE, true), m_metaConst(metaConst)
{
}

CConstantManager::~CConstantManager()
{
}

#include "core/metadata/metadata.h"
#include "core/metadata/singleClass.h"

CLASS_ID CConstantManager::GetTypeClass() const
{
	IMetadata *metaData = m_metaConst->GetMetadata();
	wxASSERT(metaData);
	IMetaTypeObjectValueSingle *clsFactory =
		metaData->GetTypeObject(m_metaConst, eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetTypeClass();
}

wxString CConstantManager::GetTypeString() const
{
	IMetadata *metaData = m_metaConst->GetMetadata();
	wxASSERT(metaData);
	IMetaTypeObjectValueSingle *clsFactory =
		metaData->GetTypeObject(m_metaConst, eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString CConstantManager::GetString() const
{
	IMetadata *metaData = m_metaConst->GetMetadata();
	wxASSERT(metaData);
	IMetaTypeObjectValueSingle *clsFactory =
		metaData->GetTypeObject(m_metaConst, eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

CValue::CMethodHelper CConstantManager::m_methodHelper;

enum Func {
	enSet = 0,
	enGet
};

void CConstantManager::PrepareNames() const
{
	m_methodHelper.ClearHelper();
	m_methodHelper.AppendFunc("set", 1, "set(value)");
	m_methodHelper.AppendFunc("get", "get()");
}

bool CConstantManager::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
{
	CConstantObject* constantValue =
		m_metaConst->CreateObjectValue();
	wxASSERT(constantValue);
	switch (lMethodNum)
	{
	case enSet:
		constantValue->SetConstValue(*paParams[0]);
		break;
	case enGet:
		pvarRetValue = constantValue->GetConstValue();
		break;
	}
	wxDELETE(constantValue);
	return true;
}