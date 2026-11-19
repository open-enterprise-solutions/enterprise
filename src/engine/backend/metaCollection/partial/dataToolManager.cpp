////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : dataTool - manager
////////////////////////////////////////////////////////////////////////////

#include "dataToolManager.h"
#include "backend/metaData.h"
#include "commonObject.h"


const ibValueMetaObjectSharedModule* ibValueManagerDataObjectDataTool::GetManagerModule() const { return m_metaObject->GetManagerModule(); }

//////////////////////////////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////////////////////////////

enum Func {
	eCreate = 0,
	eGetForm,
	eGetTemplate,
};

void ibValueManagerDataObjectDataTool::FillManagerMethods(ibMemberTable& helper) const
{
	helper.AppendFunc(ibT("Create"), ibT("Create()"));
	helper.AppendFunc(ibT("GetForm"), 3, ibT("GetForm(name : string, owner : any, id : guid)"));   // the COUNT, not only the text - see Data.From
	helper.AppendFunc(ibT("GetTemplate"), 1, ibT("GetTemplate(name : string)"));
}

bool ibValueManagerDataObjectDataTool::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	// Our own ordinal, not the table index — ibValueManagerDataObject::BuiltinMethodNum says why.
	switch (BuiltinMethodNum(lMethodNum))
	{
	case eCreate:
		pvarRetValue = m_metaObject->CreateObjectValue();
		return true;
	case eGetForm:
	{
		ibValueGuid* guidVal = lSizeArray > 2 ? paParams[2]->ConvertToType<ibValueGuid>() : nullptr;
		pvarRetValue = m_metaObject->GetGenericForm(ibFormRequest(paParams[0]->GetString(), guidVal ? ((ibGuid)*guidVal) : ibGuid()),
			lSizeArray > 1 ? paParams[1]->ConvertToType<ibBackendControlFrame>() : nullptr);
		return true;
	}
	case eGetTemplate:
		pvarRetValue = m_metaObject->GetTemplate(paParams[0]->GetString());
		return true;
	}

	return ibValueManagerDataObject::CallAsFunc(lMethodNum, pvarRetValue, paParams, lSizeArray);
}

void ibValueManagerDataObjectExternalDataTool::FillManagerMethods(ibMemberTable& helper) const
{
	helper.AppendFunc(ibT("Create"), 1, ibT("Create(fullPath : string)"));
}

#include "backend/system/systemManager.h"
#include "backend/metadataDataTool.h"

bool ibValueManagerDataObjectExternalDataTool::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case eCreate:
	{
		// Held from birth; the object it loads holds it from its load on (ibExternalOwnerHelper), so this
		// reference may go at the end of the call.
		const std::shared_ptr<ibMetaDataDataTool> metaDataTool = ibMetaData::MakeShared<ibMetaDataDataTool>();
		if (metaDataTool->LoadFromFile(paParams[0]->GetString())) {
			ibValueModuleRuntimeManagerExternalDataTool* moduleManager = metaDataTool->GetManagerModule();
			pvarRetValue = moduleManager->GetObjectValue();
			return true;
		}
		ibBackendCoreException::Error(_("Failed to load data tool '%s'"), paParams[0]->GetString());
		return false;
	}
	}

	return false;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

SYSTEM_TYPE_REGISTER(ibValueManagerDataObjectExternalDataTool, "externalManagerDataTool", system_to_clsid("MG_EXTD"));
