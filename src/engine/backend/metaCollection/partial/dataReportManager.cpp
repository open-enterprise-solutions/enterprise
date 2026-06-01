////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : report - manager
////////////////////////////////////////////////////////////////////////////

#include "dataReportManager.h"
#include "backend/metaData.h"
#include "commonObject.h"


const ibValueMetaObjectCommonModule* ibValueManagerDataObjectReport::GetManagerModule() const { return m_metaObject->GetManagerModule(); }

//////////////////////////////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////////////////////////////

enum Func {
	eCreate = 0,
	eGetForm,
	eGetTemplate,
};

void ibValueManagerDataObjectReport::PrepareNames() const
{
	ibValueManagerDataObject::PrepareNames();

	m_methodHelper->AppendFunc(wxT("Create"), wxT("Create()"));
	m_methodHelper->AppendFunc(wxT("GetForm"), wxT("GetForm(name : string, owner : any, id : guid)"));
	m_methodHelper->AppendFunc(wxT("GetTemplate"), 1, wxT("GetTemplate(name : string)"));
}

bool ibValueManagerDataObjectReport::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	ibMetaData* metaData = m_metaObject->GetMetaData();
	wxASSERT(metaData);

	switch (lMethodNum)
	{
	case eCreate:
		pvarRetValue = m_metaObject->CreateObjectValue();
		return true;
	case eGetForm: {
		ibValueGuid* guidVal = lSizeArray > 2 ? paParams[2]->ConvertToType<ibValueGuid>() : nullptr;
		pvarRetValue = m_metaObject->GetGenericForm(paParams[0]->GetString(),
			lSizeArray > 1 ? paParams[1]->ConvertToType<ibBackendControlFrame>() : nullptr,
			guidVal ? ((ibGuid)*guidVal) : ibGuid());
		return true;
	}
	case eGetTemplate:
		pvarRetValue = m_metaObject->GetTemplate(paParams[0]->GetString());
		return true;
	}

	return ibValueManagerDataObject::CallAsFunc(lMethodNum, pvarRetValue, paParams, lSizeArray);
}

ibValue::ibValueMethodHelper ibValueManagerDataObjectExternalReport::m_methodHelper;

void ibValueManagerDataObjectExternalReport::PrepareNames() const
{
	m_methodHelper.ClearHelper();
	m_methodHelper.AppendFunc(wxT("Create"), 1, wxT("Create(fullPath : string)"));
}

#include "backend/system/systemManager.h"
#include "backend/metadataReport.h"

bool ibValueManagerDataObjectExternalReport::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	ibValue ret;
	switch (lMethodNum)
	{
	case eCreate:
	{
		ibMetaDataReport* metaReport = new ibMetaDataReport();
		if (metaReport->LoadFromFile(paParams[0]->GetString())) {
			ibValueModuleRuntimeManagerExternalReport* moduleManager = metaReport->GetManagerModule();
			pvarRetValue = moduleManager->GetObjectValue();
			return true;
		}
		wxDELETE(metaReport);
		ibBackendCoreException::Error(_("Failed to load report '%s'"), paParams[0]->GetString());
	}
	}
	return false;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

SYSTEM_TYPE_REGISTER(ibValueManagerDataObjectExternalReport, "ExternalManagerReport", string_to_clsid("MG_EXTR"));