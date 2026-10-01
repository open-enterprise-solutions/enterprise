////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : a stored value - the base of a constant and a functional option
////////////////////////////////////////////////////////////////////////////

#include "metaStoredValueObject.h"
#include "backend/serialize/dataBuilder.h"
#include "backend/metaData.h"   // ibMetaData::RegisterSource — the constant registers its source into its OWN config

//***********************************************************************
//*                         metaData                                    *
//***********************************************************************

ibValueMetaObjectStoredValue::ibValueMetaObjectStoredValue() : ibValueMetaObjectGenericData()
{
	// The value column — created with the constant and pinned to it for life. It reports the
	// constant's own name, id and type, so sys_const sees the same column it always did.
	m_column = CreateMetaObjectAndSetParent<ibValueMetaObjectConstantColumn>(this);

	//set default proc
	m_propertyModule->GetMetaObject()->SetDefaultProcedure(wxT("BeforeWrite"), ibContentHelper::eProcedureHelper, { wxT("Cancel") });
	m_propertyModule->GetMetaObject()->SetDefaultProcedure(wxT("OnWrite"), ibContentHelper::eProcedureHelper, { wxT("Cancel") });
}

ibValueMetaObjectStoredValue::~ibValueMetaObjectStoredValue()
{
}

bool ibValueMetaObjectStoredValue::ReadData(const ibDataNode& node)
{
	m_propertyModule->SetNodeValue(node.GetProperty(m_propertyModule->GetName()));
	return ibValueMetaObjectGenericData::ReadData(node);
}

bool ibValueMetaObjectStoredValue::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyModule->GetName(), m_propertyModule->GetNodeValue());
	return ibValueMetaObjectGenericData::WriteData(node);
}

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

#include "backend/appData.h"

bool ibValueMetaObjectStoredValue::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	if (!ibValueMetaObjectGenericData::OnCreateMetaObject(metaData, flags))
		return false;

	// The column needs its metadata context (the provider reads values through it); the id it
	// reports is the constant's, so there is nothing of its own to stamp or to save.
	if (!m_column->OnCreateMetaObject(metaData, flags))
		return false;

	return m_propertyModule->GetMetaObject()->OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectStoredValue::OnLoadMetaObject(ibMetaData* metaData)
{

	if (!m_column->OnLoadMetaObject(metaData))
		return false;

	if (!m_propertyModule->GetMetaObject()->OnLoadMetaObject(metaData))
		return false;

	return ibValueMetaObjectGenericData::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectStoredValue::OnSaveMetaObject(int flags)
{
	if (!m_propertyModule->GetMetaObject()->OnSaveMetaObject(flags))
		return false;

	return ibValueMetaObjectGenericData::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectStoredValue::OnDeleteMetaObject()
{
	if (!m_propertyModule->GetMetaObject()->OnDeleteMetaObject())
		return false;

	return ibValueMetaObjectGenericData::OnDeleteMetaObject();
}

#include "backend/constantCtor.h"

bool ibValueMetaObjectStoredValue::OnBeforeRunMetaObject(int flags)
{
	if (!m_propertyModule->GetMetaObject()->OnBeforeRunMetaObject(flags))
		return false;

	registerConstObject();
	registerConstManager();

	return ibValueMetaObjectGenericData::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectStoredValue::OnAfterRunMetaObject(int flags)
{
	if (!m_propertyModule->GetMetaObject()->OnAfterRunMetaObject(flags))
		return false;

	// Register the constant as an L4 query source (its descriptor field, holding the single-row sys_const
	// queryable). Register ALWAYS — the factory is PER-CONFIG (in the metadata), so a read-only DB load
	// (onlyLoadFlag) must still register its OWN source into its OWN factory or its forms can't resolve it.
	m_metaData->RegisterSource(&m_queryable);

	if (auto* cc = m_metaData->GetCompileCache()) {

		if (ibValueMetaObjectGenericData::OnAfterRunMetaObject(flags))
			return cc->AddCompileModule(m_propertyModule->GetMetaObject(), [this]() -> ibValue { return CreateRecordDataObjectValue(); });

		return false;
	}

	return ibValueMetaObjectGenericData::OnAfterRunMetaObject(flags);
}

bool ibValueMetaObjectStoredValue::OnBeforeCloseMetaObject()
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
		//
		// ⚠ THE REMOVAL'S ANSWER IS NOT THE CLOSE'S ANSWER, and returning it was a real defect
		// (this metatype and twelve others were written the same way). RemoveCompileModule says
		// "there was an entry" — a MISS means the registration never happened at OPEN time, which
		// is a bug worth knowing about but says nothing about whether this object can close. Read
		// as a refusal it stopped CloseSubtree, which stopped CloseDatabase, which failed the whole
		// rollback and left the designer half-open — one unregistered module taking the
		// configuration down with it. The miss is traced instead (metaData.cpp), so it is still
		// visible without being fatal.
		if (ibValueMetaObjectGenericData::OnBeforeCloseMetaObject()) {
			cc->RemoveCompileModule(m_propertyModule->GetMetaObject());
			return true;
		}

		return false;
	}

	return ibValueMetaObjectGenericData::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectStoredValue::OnAfterCloseMetaObject()
{
	if (!m_propertyModule->GetMetaObject()->OnAfterCloseMetaObject())
		return false;

	unregisterConstObject();
	unregisterConstManager();

	return ibValueMetaObjectGenericData::OnAfterCloseMetaObject();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ibFormPtr<ibBackendValueForm> ibValueMetaObjectStoredValue::GetObjectForm() const
{
	ibBackendValueForm* const foundedForm = ibBackendValueForm::FindFormByUniqueKey(nullptr, nullptr, m_metaGuid);
	if (foundedForm == nullptr)
		return ibValueMetaObjectFormBase::CreateAndBuildForm(ibFormRequest(wxString(), m_metaGuid), nullptr, nullptr, CreateRecordDataObjectValue());
	return ibFormPtr<ibBackendValueForm>(foundedForm);   // the open one — its window holds it; this is one more reference
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

// The value column's type. Registered because the factory builds every metaobject, predefined
// children included — not because anything asks for one: it is nested in the constant, absent from
// ResolveChild, and reachable only through GetValueColumn().
METADATA_TYPE_REGISTER(ibValueMetaObjectStoredValue::ibValueMetaObjectConstantColumn, "ConstantColumn");
