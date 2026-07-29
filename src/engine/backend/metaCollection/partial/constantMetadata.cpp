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

ibValueMetaObjectConstant::ibValueMetaObjectConstant() : ibValueMetaObjectGenericData()
{
	// The value column — created with the constant and pinned to it for life. It reports the
	// constant's own name, id and type, so sys_const sees the same column it always did.
	m_column = CreateMetaObjectAndSetParent<ibValueMetaObjectConstantColumn>(this);

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

	// The value properties are the constant's own now — the attribute base used to carry them.
	m_propertyType->ReadNodeValue(node.GetProperty(m_propertyType->GetName()));
	m_propertyFillCheck->ReadNodeValue(node.GetProperty(m_propertyFillCheck->GetName()));

	return ibValueMetaObjectGenericData::ReadData(node);
}

bool ibValueMetaObjectConstant::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyModule->GetName(), m_propertyModule->GetNodeValue());

	node.SetProperty(m_propertyType->GetName(), m_propertyType->GetNodeValue());
	node.SetProperty(m_propertyFillCheck->GetName(), m_propertyFillCheck->GetNodeValue());

	return ibValueMetaObjectGenericData::WriteData(node);
}

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

#include "backend/appData.h"

bool ibValueMetaObjectConstant::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	if (!ibValueMetaObjectGenericData::OnCreateMetaObject(metaData, flags))
		return false;

	// The column needs its metadata context (the provider reads values through it); the id it
	// reports is the constant's, so there is nothing of its own to stamp or to save.
	if (!m_column->OnCreateMetaObject(metaData, flags))
		return false;

	return m_propertyModule->GetMetaObject()->OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectConstant::OnLoadMetaObject(ibMetaData* metaData)
{

	if (!m_column->OnLoadMetaObject(metaData))
		return false;

	if (!m_propertyModule->GetMetaObject()->OnLoadMetaObject(metaData))
		return false;

	return ibValueMetaObjectGenericData::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectConstant::OnSaveMetaObject(int flags)
{
	if (!m_propertyModule->GetMetaObject()->OnSaveMetaObject(flags))
		return false;

	return ibValueMetaObjectGenericData::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectConstant::OnDeleteMetaObject()
{
	if (!m_propertyModule->GetMetaObject()->OnDeleteMetaObject())
		return false;

	return ibValueMetaObjectGenericData::OnDeleteMetaObject();
}

#include "backend/constantCtor.h"

bool ibValueMetaObjectConstant::OnBeforeRunMetaObject(int flags)
{
	if (!m_propertyModule->GetMetaObject()->OnBeforeRunMetaObject(flags))
		return false;

	registerConstObject();
	registerConstManager();

	return ibValueMetaObjectGenericData::OnBeforeRunMetaObject(flags);
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

		if (ibValueMetaObjectGenericData::OnAfterRunMetaObject(flags))
			return cc->AddCompileModule(m_propertyModule->GetMetaObject(), [this]() -> ibValue* { return CreateRecordDataObjectValue(); });

		return false;
	}

	return ibValueMetaObjectGenericData::OnAfterRunMetaObject(flags);
}

bool ibValueMetaObjectConstant::OnBeforeCloseMetaObject()
{
	// un-resolve — mirror of OnRun's RegisterSource
	m_metaData->UnregisterSource(&m_queryable);

	if (!m_propertyModule->GetMetaObject()->OnBeforeCloseMetaObject())
		return false;


	if (auto* cc = m_metaData->GetCompileCache()) {

		// Run the base BEFORE-close hook in the before phase, then drop the
		// compile-cache entry — mirror of ibValueMetaObjectCatalog and the other
		// business types. Was OnAfterCloseMetaObject (a pre-phase-split legacy
		// copy/paste) which fired the after-hook + metaTree->CloseMetaObject in the
		// before phase, then again in OnAfterCloseMetaObject — double close.
		if (ibValueMetaObjectGenericData::OnBeforeCloseMetaObject())
			return cc->RemoveCompileModule(m_propertyModule->GetMetaObject());

		return false;
	}

	return ibValueMetaObjectGenericData::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectConstant::OnAfterCloseMetaObject()
{
	if (!m_propertyModule->GetMetaObject()->OnAfterCloseMetaObject())
		return false;

	unregisterConstObject();
	unregisterConstManager();

	return ibValueMetaObjectGenericData::OnAfterCloseMetaObject();
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

// The value column's type. Registered because the factory builds every metaobject, predefined
// children included — not because anything asks for one: it is nested in the constant, absent from
// ResolveChild, and reachable only through GetValueColumn().
METADATA_TYPE_REGISTER(ibValueMetaObjectConstant::ibValueMetaObjectConstantColumn, "ConstantColumn");
