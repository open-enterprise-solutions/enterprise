////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : meta-tables
////////////////////////////////////////////////////////////////////////////

#include "metaTableObject.h"
#include "backend/serialize/dataBuilder.h"
#include "backend/metaData.h"


//***********************************************************************
//*                         Attributes                                  * 
//***********************************************************************

#include "backend/objCtor.h"
#include "backend/metaCollection/partial/commonObject.h"

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
	return parent != nullptr ? ibValue::GetNameObjectFromID(parent->GetClassType()) : wxString();
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

void ibTabularSourceDescriptor::FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const
{
	if (m_meta == nullptr)
		return;
	// ⭐⭐ THE OWNER, SHOWN AS A FIELD OF THE SECTION.
	//
	// A section's rows already carry their owner — the row key it stores IS the owner's reference
	// key, the same sixteen bytes. That was only ever a physical detail, invisible to anyone writing
	// a query, so a line of a document could not be joined back to the document it belongs to
	// without leaving the query language.
	//
	// Shown FIRST because it is what a reader looks for: `Ref` selects the owner, joins to it, and
	// dot-walks through it exactly as a reference in any other field does. The type is constant —
	// a section belongs to one owner — so nothing about it is read from a row.
	explorer.AppendColumn(m_queryable.OwnerRefColumn(), /*enabled*/ true, /*visible*/ true);

	// THE SECTION'S OWN ATTRIBUTES — the same list its queryable exposes as columns, because an
	// attribute IS a column. Asked of the metaobject rather than restated here, so a column added
	// to the section tomorrow shows up with nothing edited in this file.
	for (const ibValueMetaObjectAttributeBase* attribute : m_meta->GetGenericAttributeArrayObject())
		if (attribute != nullptr)
			explorer.AppendColumn(attribute, /*enabled*/ true, /*visible*/ true);
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

// The per-type byte path now rides the builder: the binary provider decodes the
// stream into a node tree, ReadData applies it. Bytes are just one provider — the
// SAME node tree renders to JSON / XML through the others. WriteData/ReadData are
// the single source of truth for this object's data.

bool ibValueMetaObjectTableData::ReadData(const ibDataNode& node)
{
	m_propertyUse->ReadNodeValue(node.GetProperty(m_propertyUse->GetName()));
	m_propertyNumberLine->ReadNodeValue(node.GetProperty(m_propertyNumberLine->GetName()));

	return true;
}

bool ibValueMetaObjectTableData::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyUse->GetName(), m_propertyUse->GetNodeValue());
	node.SetProperty(m_propertyNumberLine->GetName(), m_propertyNumberLine->GetNodeValue());

	return true;
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
	// Register the tabular section as an L4 query source (parent-qualified "<Parent>.<Section>"). Register
	// ALWAYS — the factory is PER-CONFIG (in the metadata), so a read-only DB load (onlyLoadFlag) still
	// registers its OWN sources into its OWN factory or the section can't resolve on that config's forms.
	m_metaData->RegisterSource(&m_queryable);
	return ibValueMetaObjectTableData::OnAfterRunMetaObject(flags);
}

bool ibValueMetaObjectTableDataRef::OnBeforeCloseMetaObject()   // un-resolve — mirror of OnRun's RegisterSource
{
	m_metaData->UnregisterSource(&m_queryable);
	return ibValueMetaObjectTableData::OnBeforeCloseMetaObject();
}

// Distinct registration name — the global ctor registry requires it (a duplicate name drops the
// registration, so GetAvailableCtor(MD_TBLR) would return null and break AppendGroupItem). Short
// "Ref" form (not "Reference") keeps the user-visible type name compact.
METADATA_TYPE_REGISTER(ibValueMetaObjectTableDataRef, "TabularSectionRef", g_metaTableRefCLSID);