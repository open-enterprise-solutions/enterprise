////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : report - manager
////////////////////////////////////////////////////////////////////////////

#include "dataReportManager.h"
#include "metadata/metadata.h"
#include "compiler/methods.h"
#include "baseObject.h"

wxIMPLEMENT_DYNAMIC_CLASS(CManagerReportValue, CValue);

CManagerReportValue::CManagerReportValue(CMetaObjectReportValue *metaObject) : CValue(eValueTypes::TYPE_VALUE, true),
m_methods(new CMethods()), m_metaObject(metaObject)
{
}

CManagerReportValue::~CManagerReportValue()
{
	wxDELETE(m_methods);
}

CMetaCommonModuleObject *CManagerReportValue::GetModuleManager()  const { return m_metaObject->GetModuleManager(); }

#include "metadata/singleMetaTypes.h"

CLASS_ID CManagerReportValue::GetTypeID() const
{
	IMetaTypeObjectValueSingle *clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetTypeID();
}

wxString CManagerReportValue::GetTypeString() const
{
	IMetaTypeObjectValueSingle *clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString CManagerReportValue::GetString() const
{
	IMetaTypeObjectValueSingle *clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

//////////////////////////////////////////////////////////////////////////////////////////////////////

wxIMPLEMENT_DYNAMIC_CLASS(CManagerExternalReport, CValue);

CManagerExternalReport::CManagerExternalReport() : CValue(eValueTypes::TYPE_VALUE, true),
m_methods(new CMethods())
{
}

CManagerExternalReport::~CManagerExternalReport()
{
	wxDELETE(m_methods);
}

wxString CManagerExternalReport::GetTypeString() const
{
	return wxT("extenalManagerReport");
}

wxString CManagerExternalReport::GetString() const
{
	return wxT("extenalManagerReport");
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

SO_VALUE_REGISTER(CManagerExternalReport, "extenalManagerReport", CManagerExternalReport, TEXT2CLSID("MG_EXTR"));