////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : report - manager
////////////////////////////////////////////////////////////////////////////

#include "dataReportManager.h"
#include "metadata/metadata.h"
#include "compiler/methods.h"
#include "baseObject.h"

wxIMPLEMENT_DYNAMIC_CLASS(CReportManager, CValue);

CReportManager::CReportManager(CMetaObjectReport *metaObject) : CValue(eValueTypes::TYPE_VALUE, true),
m_methods(new CMethods()), m_metaObject(metaObject)
{
}

CReportManager::~CReportManager()
{
	wxDELETE(m_methods);
}

CMetaCommonModuleObject *CReportManager::GetModuleManager()  const { return m_metaObject->GetModuleManager(); }

#include "metadata/singleMetaTypes.h"

CLASS_ID CReportManager::GetClassType() const
{
	IMetaTypeObjectValueSingle *clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString CReportManager::GetTypeString() const
{
	IMetaTypeObjectValueSingle *clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString CReportManager::GetString() const
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