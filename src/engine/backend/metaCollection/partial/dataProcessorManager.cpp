////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : dataProcessor - manager
////////////////////////////////////////////////////////////////////////////

#include "dataProcessorManager.h"
#include "backend/metaData.h"
#include "commonObject.h"


const ibValueMetaObjectCommonModule* ibValueManagerDataObjectDataProcessor::GetManagerModule() const { return m_metaObject->GetManagerModule(); }

//////////////////////////////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////////////////////////////

enum Func {
	eCreate = 0,
	eGetForm,
	eGetTemplate,
};

void ibValueManagerDataObjectDataProcessor::FillManagerMethods(ibMemberTable& helper) const
{
	helper.AppendFunc(wxT("Create"), wxT("Create()"));
	helper.AppendFunc(wxT("GetForm"), wxT("GetForm(name : string, owner : any, id : guid)"));
	helper.AppendFunc(wxT("GetTemplate"), 1, wxT("GetTemplate(name : string)"));
}

bool ibValueManagerDataObjectDataProcessor::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case eCreate:
		pvarRetValue = m_metaObject->CreateObjectValue();
		return true;
	case eGetForm:
	{
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

void ibValueManagerDataObjectExternalDataProcessor::FillManagerMethods(ibMemberTable& helper) const
{
	helper.AppendFunc(wxT("Create"), 1, wxT("Create(fullPath : string)"));
}

#include "backend/system/systemManager.h"
#include "backend/metadataDataProcessor.h"

bool ibValueManagerDataObjectExternalDataProcessor::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case eCreate:
	{
		ibMetaDataDataProcessor* metaDataProcessor = new ibMetaDataDataProcessor();
		if (metaDataProcessor->LoadFromFile(paParams[0]->GetString())) {
			ibValueModuleRuntimeManagerExternalDataProcessor* moduleManager = metaDataProcessor->GetManagerModule();
			pvarRetValue = moduleManager->GetObjectValue();
			return true;
		}
		wxDELETE(metaDataProcessor);
		ibBackendCoreException::Error(_("Failed to load data processor '%s'"), paParams[0]->GetString());
		return false;
	}
	}

	return false;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

SYSTEM_TYPE_REGISTER(ibValueManagerDataObjectExternalDataProcessor, "externalManagerDataProcessor", string_to_clsid("MG_EXTD"));