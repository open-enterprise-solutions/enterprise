////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : constants
////////////////////////////////////////////////////////////////////////////

#include "constant.h"
#include "backend/serialize/dataBuilder.h"
#include "backend/metaData.h"   // ibMetaData::RegisterSource — the constant registers its source into its OWN config

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

bool ibValueMetaObjectConstant::ReadData(const ibDataNode& node)
{
	m_propertyModule->ReadNodeValue(node.GetProperty(m_propertyModule->GetName()));

	return ibValueMetaObjectAttribute::ReadData(node);
}

bool ibValueMetaObjectConstant::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyModule->GetName(), m_propertyModule->GetNodeValue());

	return ibValueMetaObjectAttribute::WriteData(node);
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

	// Register the constant as an L4 query source (its descriptor field, holding the single-row sys_const
	// queryable). Register ALWAYS — the factory is PER-CONFIG (in the metadata), so a read-only DB load
	// (onlyLoadFlag) must still register its OWN source into its OWN factory or its forms can't resolve it.
	m_metaData->RegisterSource(&m_queryable);

	if (auto* cc = m_metaData->GetCompileCache()) {

		if (ibValueMetaObjectAttribute::OnAfterRunMetaObject(flags))
			return cc->AddCompileModule(m_propertyModule->GetMetaObject(), CreateRecordDataObjectValue());

		return false;
	}

	return ibValueMetaObjectAttribute::OnAfterRunMetaObject(flags);
}

bool ibValueMetaObjectConstant::OnBeforeCloseMetaObject()
{
	m_metaData->UnregisterSource(&m_queryable);

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
