////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : report - manager
////////////////////////////////////////////////////////////////////////////

#include "dataReportManager.h"
#include "core/metadata/metadata.h"
#include "object.h"

wxIMPLEMENT_DYNAMIC_CLASS(CReportManager, CValue);

CReportManager::CReportManager(CMetaObjectReport* metaObject) : CValue(eValueTypes::TYPE_VALUE, true),
m_methodHelper(new CMethodHelper()), m_metaObject(metaObject)
{
}

CReportManager::~CReportManager()
{
	wxDELETE(m_methodHelper);
}

CMetaCommonModuleObject* CReportManager::GetModuleManager()  const { return m_metaObject->GetModuleManager(); }

#include "core/metadata/singleClass.h"

CLASS_ID CReportManager::GetTypeClass() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetTypeClass();
}

wxString CReportManager::GetTypeString() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString CReportManager::GetString() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

//////////////////////////////////////////////////////////////////////////////////////////////////////

wxIMPLEMENT_DYNAMIC_CLASS(CManagerExternalReport, CValue);

CManagerExternalReport::CManagerExternalReport() : CValue(eValueTypes::TYPE_VALUE, true),
m_methodHelper(new CMethodHelper())
{
}

CManagerExternalReport::~CManagerExternalReport()
{
	wxDELETE(m_methodHelper);
}

wxString CManagerExternalReport::GetTypeString() const
{
	return wxT("extenalManagerReport");
}

wxString CManagerExternalReport::GetString() const
{
	return wxT("extenalManagerReport");
}

//////////////////////////////////////////////////////////////////////////////////////////////////////

enum Func {
	eCreate = 0,
	eGetForm
};

#include "core/metadata/metadata.h"

void CReportManager::PrepareNames() const
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

bool CReportManager::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case eCreate:
		pvarRetValue = m_metaObject->CreateObjectValue();
		return true;
	case eGetForm: {
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

	CValue* pRefData = moduleManager->FindCommonModule(m_metaObject->GetModuleManager());
	if (pRefData != NULL)
		return pRefData->CallAsFunc(lMethodNum, pvarRetValue, paParams, lSizeArray);
	return false;
}

void CManagerExternalReport::PrepareNames() const
{
	m_methodHelper->ClearHelper();
	m_methodHelper->AppendFunc("create", "create(fullPath)");
}

#include "core/compiler/systemObjects.h"
#include "core/metadata/external/metadataReport.h"

bool CManagerExternalReport::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
{
	CValue ret;
	switch (lMethodNum)
	{
	case eCreate:
	{
		CMetadataReport* metaReport = new CMetadataReport();
		if (metaReport->LoadFromFile(paParams[0]->GetString())) {
			IModuleManager* moduleManager = metaReport->GetModuleManager();
			pvarRetValue = moduleManager->GetObjectValue();
			return true;
		}
		wxDELETE(metaReport);
		CSystemObjects::Raise(
			wxString::Format("Failed to load report '%s'", paParams[0]->GetString())
		);
	}
	}
	return false;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

SO_VALUE_REGISTER(CManagerExternalReport, "extenalManagerReport", TEXT2CLSID("MG_EXTR"));