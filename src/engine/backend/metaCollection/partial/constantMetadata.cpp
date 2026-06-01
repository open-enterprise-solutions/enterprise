////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : constants
////////////////////////////////////////////////////////////////////////////

#include "constant.h"
#include "backend/metaData.h"

#define objectModule wxT("objectModule")

//***********************************************************************
//*                         metaData                                    * 
//***********************************************************************


//***********************************************************************
//*                         Attributes                                  * 
//***********************************************************************

ibValueMetaObjectConstant::ibValueMetaObjectConstant() : ibValueMetaObjectAttribute()
{
	//set default proc
	m_propertyModule->GetMetaObject()->SetDefaultProcedure(wxT("BeforeWrite"), ibContentHelper::eProcedureHelper, { wxT("Cancel") });
	m_propertyModule->GetMetaObject()->SetDefaultProcedure(wxT("OnWrite"), ibContentHelper::eProcedureHelper, { wxT("Cancel") });
}

ibValueMetaObjectConstant::~ibValueMetaObjectConstant()
{
}

bool ibValueMetaObjectConstant::LoadData(ibReaderMemory& dataReader)
{
	//load object module
	m_propertyModule->GetMetaObject()->LoadMeta(dataReader);

	return ibValueMetaObjectAttribute::LoadData(dataReader);
}

bool ibValueMetaObjectConstant::SaveData(ibWriterMemory& dataWritter)
{
	//save object module
	m_propertyModule->GetMetaObject()->SaveMeta(dataWritter);

	return ibValueMetaObjectAttribute::SaveData(dataWritter);
}

bool ibValueMetaObjectConstant::DeleteData()
{
	return ibValueMetaObjectAttribute::DeleteData();
}

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

#include "backend/appData.h"

bool ibValueMetaObjectConstant::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	if (!ibValueMetaObjectAttribute::OnCreateMetaObject(metaData, flags))
		return false;

	return m_propertyModule->GetMetaObject()->OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectConstant::OnLoadMetaObject(ibMetaData* metaData)
{

	if (!m_propertyModule->GetMetaObject()->OnLoadMetaObject(metaData))
		return false;

	return ibValueMetaObjectAttribute::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectConstant::OnSaveMetaObject(int flags)
{
	if (!m_propertyModule->GetMetaObject()->OnSaveMetaObject(flags))
		return false;

	return ibValueMetaObjectAttribute::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectConstant::OnDeleteMetaObject()
{
	if (!m_propertyModule->GetMetaObject()->OnDeleteMetaObject())
		return false;

	return ibValueMetaObjectAttribute::OnDeleteMetaObject();
}

#include "backend/constantCtor.h"

bool ibValueMetaObjectConstant::OnBeforeRunMetaObject(int flags)
{
	if (!m_propertyModule->GetMetaObject()->OnBeforeRunMetaObject(flags))
		return false;

	registerConstObject();
	registerConstManager();

	return ibValueMetaObjectAttribute::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectConstant::OnAfterRunMetaObject(int flags)
{
	if (!m_propertyModule->GetMetaObject()->OnAfterRunMetaObject(flags))
		return false;


	if (auto* cc = m_metaData->GetCompileCache()) {

		if (ibValueMetaObjectAttribute::OnAfterRunMetaObject(flags))
			return cc->AddCompileModule(m_propertyModule->GetMetaObject(), CreateRecordDataObjectValue());

		return false;
	}

	return ibValueMetaObjectAttribute::OnAfterRunMetaObject(flags);
}

bool ibValueMetaObjectConstant::OnBeforeCloseMetaObject()
{
	if (!m_propertyModule->GetMetaObject()->OnBeforeCloseMetaObject())
		return false;


	if (auto* cc = m_metaData->GetCompileCache()) {

		// Run the base BEFORE-close hook in the before phase, then drop the
		// compile-cache entry — mirror of ibValueMetaObjectCatalog and the other
		// business types. Was OnAfterCloseMetaObject (a pre-phase-split legacy
		// copy/paste) which fired the after-hook + metaTree->CloseMetaObject in the
		// before phase, then again in OnAfterCloseMetaObject — double close.
		if (ibValueMetaObjectAttribute::OnBeforeCloseMetaObject())
			return cc->RemoveCompileModule(m_propertyModule->GetMetaObject());

		return false;
	}

	return ibValueMetaObjectAttribute::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectConstant::OnAfterCloseMetaObject()
{
	if (!m_propertyModule->GetMetaObject()->OnAfterCloseMetaObject())
		return false;

	unregisterConstObject();
	unregisterConstManager();

	return ibValueMetaObjectAttribute::OnAfterCloseMetaObject();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ibBackendValueForm* ibValueMetaObjectConstant::GetObjectForm() const
{
	ibBackendValueForm* const foundedForm = ibBackendValueForm::FindFormByUniqueKey(nullptr, nullptr, m_metaGuid);
	if (foundedForm == nullptr)
		return ibValueMetaObjectFormBase::CreateAndBuildForm(nullptr, nullptr, CreateRecordDataObjectValue(), m_metaGuid);
	return foundedForm;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectConstant, "Constant", g_metaConstantCLSID);
