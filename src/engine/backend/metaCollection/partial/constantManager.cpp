////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : constants manager
////////////////////////////////////////////////////////////////////////////

#include "constantManager.h"

wxIMPLEMENT_DYNAMIC_CLASS(CConstantManager, CValue);

CConstantManager::CConstantManager(CMetaObjectConstant *metaConst) :
	CValue(eValueTypes::TYPE_VALUE, true), m_metaConst(metaConst)
{
}

CConstantManager::~CConstantManager()
{
}

#include "backend/metaData.h"
#include "backend/objCtor.h"

class_identifier_t CConstantManager::GetClassType() const
{
	IMetaData *metaData = m_metaConst->GetMetaData();
	wxASSERT(metaData);
	IMetaValueTypeCtor *clsFactory =
		metaData->GetTypeCtor(m_metaConst, eCtorMetaType::eCtorMetaType_Manager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString CConstantManager::GetClassName() const
{
	IMetaData *metaData = m_metaConst->GetMetaData();
	wxASSERT(metaData);
	IMetaValueTypeCtor *clsFactory =
		metaData->GetTypeCtor(m_metaConst, eCtorMetaType::eCtorMetaType_Manager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString CConstantManager::GetString() const
{
	IMetaData *metaData = m_metaConst->GetMetaData();
	wxASSERT(metaData);
	IMetaValueTypeCtor *clsFactory =
		metaData->GetTypeCtor(m_metaConst, eCtorMetaType::eCtorMetaType_Manager);
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
	CRecordDataObjectConstant* constantValue =
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