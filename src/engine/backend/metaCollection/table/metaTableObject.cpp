////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : meta-tables
////////////////////////////////////////////////////////////////////////////

#include "metaTableObject.h"
#include "backend/metaData.h"


//***********************************************************************
//*                         Attributes                                  * 
//***********************************************************************

#include "backend/objCtor.h"
#include "backend/metaCollection/partial/commonObject.h"
#include "backend/query/queryableHooks.h"   // light L4 source registration hooks

//***********************************************************************
//*                  ibTabularSourceDescriptor                          *
//***********************************************************************

ibTabularSourceDescriptor::ibTabularSourceDescriptor(ibValueMetaObjectTableDataRef* meta)
	: m_meta(meta), m_queryable(meta)
{
}

wxString ibTabularSourceDescriptor::GetNamespace() const
{
	// parent-qualified: the tabular section's namespace is its parent record/document's kind.
	ibValueMetaObject* parent = m_meta->GetParent();
	return parent != nullptr ? ibValue::GetNameObjectFromID(parent->GetClassType()) : wxEmptyString;
}

wxString ibTabularSourceDescriptor::GetName() const
{
	// "<Parent>.<Section>" — reached as the 3-segment source Document.Expense.Goods.
	ibValueMetaObject* parent = m_meta->GetParent();
	return parent != nullptr ? (parent->GetName() + wxT(".") + m_meta->GetName()) : m_meta->GetName();
}

const ibBackendQueryable* ibTabularSourceDescriptor::CreateQueryable(ibValue** /*paParams*/, long /*lSizeArray*/)
{
	return &m_queryable;
}

ibTypeDescription ibValueMetaObjectTableData::GetTypeDesc() const
{
	const ibCtorMetaValueType* typeCtor = m_metaData->GetTypeCtor(this, ibCtorObjectMetaType::ibCtorObjectMetaType_TabularSection);
	wxASSERT(typeCtor);
	if (typeCtor != nullptr) return ibTypeDescription(typeCtor->GetClassType());
	return ibTypeDescription();
}

////////////////////////////////////////////////////////////////////////////

ibValueMetaObjectTableData::ibValueMetaObjectTableData() : ibValueMetaObjectCompositeData()
{
}

ibValueMetaObjectTableData::~ibValueMetaObjectTableData()
{
	//wxDELETE(m_numberLine);
}

bool ibValueMetaObjectTableData::LoadData(ibReaderMemory& dataReader)
{
	m_propertyUse->SetValue(dataReader.r_u16());

	//load default attributes:
	return (*m_propertyNumberLine)->LoadMeta(dataReader);
}

bool ibValueMetaObjectTableData::SaveData(ibWriterMemory& dataWritter)
{
	dataWritter.w_u16(m_propertyUse->GetValueAsInteger());

	//save default attributes:
	return (*m_propertyNumberLine)->SaveMeta(dataWritter);;
}

//***********************************************************************
//*								Events								    *
//***********************************************************************

bool ibValueMetaObjectTableData::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	if (!ibValueMetaObject::OnCreateMetaObject(metaData, flags))
		return false;
	if (!(*m_propertyNumberLine)->OnCreateMetaObject(metaData, flags)) {
		return false;
	}
	return true;
}

bool ibValueMetaObjectTableData::OnLoadMetaObject(ibMetaData* metaData)
{
	if (!(*m_propertyNumberLine)->OnLoadMetaObject(metaData))
		return false;

	return ibValueMetaObject::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectTableData::OnSaveMetaObject(int flags)
{
	if (!(*m_propertyNumberLine)->OnSaveMetaObject(flags))
		return false;

	return ibValueMetaObject::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectTableData::OnDeleteMetaObject()
{
	if (!(*m_propertyNumberLine)->OnDeleteMetaObject())
		return false;

	return ibValueMetaObject::OnDeleteMetaObject();
}

bool ibValueMetaObjectTableData::OnReloadMetaObject()
{
	ibValueMetaObject* metaObject = GetParent();
	wxASSERT(metaObject);
	if (metaObject->OnReloadMetaObject())
		return ibValueMetaObject::OnReloadMetaObject();
	return false;
}

// Pure-virtual-with-body: the common run body every leaf chains to (number line + framework).
// The value-ctor registration is each leaf's own job, done in its override before this call.
bool ibValueMetaObjectTableData::OnBeforeRunMetaObject(int flags)
{
	if (!(*m_propertyNumberLine)->OnBeforeRunMetaObject(flags))
		return false;
	return ibValueMetaObject::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectTableData::OnAfterRunMetaObject(int flags)
{
	if ((flags & newObjectFlag) != 0 || (flags & pasteObjectFlag) != 0)
		OnReloadMetaObject();
	return ibValueMetaObject::OnAfterRunMetaObject(flags);
}

bool ibValueMetaObjectTableData::OnBeforeCloseMetaObject()
{
	return ibValueMetaObject::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectTableData::OnAfterCloseMetaObject()
{
	if (!(*m_propertyNumberLine)->OnAfterCloseMetaObject())
		return false;
	ibValueMetaObjectRecordData* metaObject = dynamic_cast<ibValueMetaObjectRecordData*>(GetParent());
	wxASSERT(metaObject);
	if (metaObject != nullptr) {
		unregisterTabularSection();
		unregisterTabularSection_String();
	}
	return ibValueMetaObject::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

//***********************************************************************
//*                  ibValueMetaObjectTableDataRam                      *
//***********************************************************************
// RAM leaf — thin; all behaviour (attributes, plain value-ctor, null queryable) is the base
// default. Only its factory identity (MD_TBL) is its own. The base itself is NOT registered.

ibValueMetaObjectTableDataRam::ibValueMetaObjectTableDataRam() : ibValueMetaObjectTableData()
{
}

ibValueMetaObjectTableDataRam::~ibValueMetaObjectTableDataRam()
{
}

bool ibValueMetaObjectTableDataRam::OnBeforeRunMetaObject(int flags)
{
	ibValueMetaObjectRecordData* metaObject = GetParentAsType<ibValueMetaObjectRecordData>();
	registerTabularSection();
	registerTabularSection_String();
	return ibValueMetaObjectTableData::OnBeforeRunMetaObject(flags);
}

METADATA_TYPE_REGISTER(ibValueMetaObjectTableDataRam, "TabularSection", g_metaTableCLSID);

//***********************************************************************
//*                  ibValueMetaObjectTableDataRef                      *
//***********************************************************************

ibValueMetaObjectTableDataRef::ibValueMetaObjectTableDataRef() : ibValueMetaObjectTableData()
{
}

ibValueMetaObjectTableDataRef::~ibValueMetaObjectTableDataRef()
{
}

bool ibValueMetaObjectTableDataRef::OnBeforeRunMetaObject(int flags)
{
	ibValueMetaObjectRecordData* metaObject = GetParentAsType<ibValueMetaObjectRecordData>();
	registerTabularSectionReference();
	registerTabularSection_String();
	return ibValueMetaObjectTableData::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectTableDataRef::OnAfterRunMetaObject(int flags)
{
	// Register the tabular section as an L4 query source (parent-qualified "<Parent>.<Section>").
	if (!(flags & onlyLoadFlag))
		ibRegisterQueryableSource(&m_queryable);
	return ibValueMetaObjectTableData::OnAfterRunMetaObject(flags);
}

bool ibValueMetaObjectTableDataRef::OnBeforeCloseMetaObject()
{
	ibUnregisterQueryableSource(&m_queryable);
	return ibValueMetaObjectTableData::OnBeforeCloseMetaObject();
}

// Distinct registration name — the global ctor registry requires it (a duplicate name drops the
// registration, so GetAvailableCtor(MD_TBLR) would return null and break AppendGroupItem). Short
// "Ref" form (not "Reference") keeps the user-visible type name compact.
METADATA_TYPE_REGISTER(ibValueMetaObjectTableDataRef, "TabularSectionRef", g_metaTableRefCLSID);