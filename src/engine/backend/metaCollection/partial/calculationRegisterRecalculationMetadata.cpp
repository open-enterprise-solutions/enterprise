////////////////////////////////////////////////////////////////////////////
//	Description : Recalculation — the metaobject: ctor, lifecycle
//	              events, ReadData/WriteData, plus the vended L4 queryable and its
//	              source descriptor. Shaped after the DB-backed tabular section
//	              (metaTableObject.cpp): a subordinate that vends a queryable +
//	              a parent-qualified physical table and registers as a source on run.
////////////////////////////////////////////////////////////////////////////

#include "calculationRegister.h"               // the recalculation is the register's; its standard columns take their TYPE from it
#include "backend/serialize/dataBuilder.h"
#include "backend/metaData.h"
#include "backend/appData.h"                   // DesignerMode — which copy stamps the month's column
#include "backend/databaseLayer/databaseMaterializeBuilder.h"   // KeyHashColumnName
#include "backend/query/schemaSnapshot.h"      // ibDerivedKeyNeedsHash — the schema's own answer about the key
#include "backend/objCtor.h"

//***********************************************************************
//*                  ibRecalculationStandardColumn                      *
//***********************************************************************

const ibValueMetaObjectAttributeBase* ibRecalculationStandardColumn::Identity() const
{
	if (m_meta == nullptr)
		return nullptr;
	switch (m_role) {
	case Role::RecalculationObject: return m_meta->GetRecalculationObject();
	case Role::CalculationType:     return m_meta->GetCalculationType();
	case Role::ActionPeriod:        return m_meta->GetActionPeriod();
	}
	return nullptr;
}

wxString ibRecalculationStandardColumn::GetName() const
{
	switch (m_role) {
	case Role::RecalculationObject: return wxT("RecalculationObject");
	case Role::CalculationType:     return wxT("CalculationType");
	case Role::ActionPeriod:        return wxT("ActionPeriod");
	}
	return wxEmptyString;
}

wxString ibRecalculationStandardColumn::GetSynonym() const
{
	const ibValueMetaObjectAttributeBase* identity = Identity();
	return identity != nullptr ? identity->GetSynonym() : GetName();
}

wxString ibRecalculationStandardColumn::GetPhysicalName() const
{
	const ibValueMetaObjectAttributeBase* identity = Identity();
	return identity != nullptr ? identity->GetPhysicalName() : wxString();
}

ibMetaID ibRecalculationStandardColumn::GetColumnId() const
{
	const ibValueMetaObjectAttributeBase* identity = Identity();
	return identity != nullptr ? identity->GetColumnId() : 0;
}

// Asked of the register at the moment of asking — see the class note. A recalculation that is not (or
// no longer) under a calculation register has nothing to lend it a type, and answers with an empty one.
ibTypeDescription& ibRecalculationStandardColumn::GetTypeDesc() const
{
	const ibValueMetaObjectCalculationRegister* reg = m_meta != nullptr ? m_meta->GetRegister() : nullptr;
	const ibValueMetaObjectAttributeBase* lender = nullptr;
	if (reg != nullptr) {
		switch (m_role) {
		case Role::RecalculationObject: lender = reg->GetRegisterRecorder(); break;
		case Role::CalculationType:     lender = reg->GetCalculationType(); break;
		case Role::ActionPeriod:        lender = reg->GetActionPeriod(); break;
		}
	}
	if (lender != nullptr)
		return lender->GetTypeDesc();
	static ibTypeDescription s_nothingLent;   // nothing may write through it, as through any column's type
	return s_nothingLent;
}

//***********************************************************************
//*                   ibRecalculationQueryable                          *
//***********************************************************************

const ibBackendQueryColumn* ibRecalculationQueryable::RecalculationObjectColumn() const
{
	return m_recalcObject.GetColumnId() != 0 ? &m_recalcObject : nullptr;
}

const ibBackendQueryColumn* ibRecalculationQueryable::CalculationTypeColumn() const
{
	return m_calcType.GetColumnId() != 0 ? &m_calcType : nullptr;
}

const ibBackendQueryColumn* ibRecalculationQueryable::ActionPeriodColumn() const
{
	return m_meta->KeepsActionPeriod() ? &m_actionPeriod : nullptr;
}

const ibBackendQueryColumn* ibRecalculationQueryable::ResolveColumnByName(const wxString& name) const
{
	if (name.IsSameAs(wxT("RecalculationObject"), false))
		return RecalculationObjectColumn();
	if (name.IsSameAs(wxT("CalculationType"), false))
		return CalculationTypeColumn();
	if (name.IsSameAs(wxT("ActionPeriod"), false))
		return ActionPeriodColumn();
	// A dimension HOLDS a query column rather than being one, so the metaobject is found first and
	// asked for its column second — and a name that names nothing answers null rather than dereferencing.
	const ibValueMetaObjectDimension* dimension =
		m_meta->FindObjectByFilter<ibValueMetaObjectDimension>(name, { g_metaDimensionCLSID });
	return dimension != nullptr ? dimension->GetQueryColumn() : nullptr;
}

std::vector<const ibBackendQueryColumn*> ibRecalculationQueryable::GetColumns() const
{
	std::vector<const ibBackendQueryColumn*> columns;
	for (const ibBackendQueryColumn* standard : { RecalculationObjectColumn(), CalculationTypeColumn(), ActionPeriodColumn() })
		if (standard != nullptr)
			columns.push_back(standard);
	for (const auto dimension : m_meta->GetDimensionArrayObject())
		columns.push_back(dimension->GetQueryColumn());
	return columns;
}

wxString ibRecalculationQueryable::GetQueryTableName() const { return m_meta->GetPhysicalTableName(); }
const ibUniqueKey& ibRecalculationQueryable::GetQueryTableGuid() const { return m_meta->GetGuid(); }
wxString ibRecalculationQueryable::GetQueryName()      const { return m_meta->GetName(); }
ibMetaID ibRecalculationQueryable::GetQueryTableId()   const { return m_meta->GetMetaID(); }
const ibMetaData* ibRecalculationQueryable::GetMetaData() const { return m_meta->GetMetaData(); }
// Identity = the recalculation object, the calculation type, then the dimension columns — the row's
// natural key. No line number and no uuid.
//
// 🛑 THE CALCULATION TYPE IS PART OF IT. The import keyed a row by (object, dimensions) alone, so one
// document holding a stale bonus AND a stale allowance for the same employee was one row: the second
// mark overwrote the first (the UPSERT matches on this key), and the unique index would have refused a
// plain insert of it. Two stale records of two types are two things to recalculate.
//
// …AND THE MONTH, LAST, where the register keeps one: two positions of one type for one employee in one
// run are two things to recalculate (see ibRecalculationStandardColumn). Last, so the leading columns —
// the ones a lookup by recorder and employee rides — stay where they were.
std::vector<const ibBackendQueryColumn*> ibRecalculationQueryable::GetPrimaryKeyColumns() const
{
	std::vector<const ibBackendQueryColumn*> keys;
	for (const ibBackendQueryColumn* standard : { RecalculationObjectColumn(), CalculationTypeColumn() })
		if (standard != nullptr)
			keys.push_back(standard);
	for (const auto dimension : m_meta->GetDimensionArrayObject())
		keys.push_back(dimension->GetQueryColumn());
	if (const ibBackendQueryColumn* month = ActionPeriodColumn())
		keys.push_back(month);
	return keys;
}

//***********************************************************************
//*                 ibRecalculationSourceDescriptor                     *
//***********************************************************************

ibRecalculationSourceDescriptor::ibRecalculationSourceDescriptor(ibValueMetaObjectCalculationRegister::ibValueMetaObjectRecalculation* meta)
	: m_meta(meta), m_queryable(meta)
{
}

wxString ibRecalculationSourceDescriptor::GetNamespace() const
{
	// parent-qualified: the recalculation's namespace is its parent calculation register's kind.
	ibValueMetaObject* parent = m_meta->GetParent();
	return parent != nullptr ? ibValue::GetNameObjectFromID(parent->GetClassType()) : wxString();
}

wxString ibRecalculationSourceDescriptor::GetName() const
{
	// "<Register>.<Recalculation>" — reached as the 3-segment source.
	ibValueMetaObject* parent = m_meta->GetParent();
	return parent != nullptr ? (parent->GetName() + wxT(".") + m_meta->GetName()) : m_meta->GetName();
}

const ibBackendQueryable* ibRecalculationSourceDescriptor::CreateQueryable(ibValue** /*paParams*/, long /*lSizeArray*/)
{
	return &m_queryable;
}

void ibRecalculationSourceDescriptor::FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const
{
	if (m_meta == nullptr)
		return;
	// The record being recalculated + the calculation type (+ the month), then the recalculation's own dimensions.
	for (const ibBackendQueryColumn* standard : { m_queryable.RecalculationObjectColumn(), m_queryable.CalculationTypeColumn(),
			m_queryable.ActionPeriodColumn() })
		if (standard != nullptr)
			explorer.AppendColumn(standard, /*enabled*/ true, /*visible*/ true);
	for (const ibValueMetaObjectDimension* dimension : m_meta->GetDimensionArrayObject())
		if (dimension != nullptr)
			explorer.AppendColumn(dimension->GetQueryColumn(), /*enabled*/ true, /*visible*/ true);
}

//***********************************************************************
//*                 ibValueMetaObjectRecalculation                      *
//***********************************************************************

ibValueMetaObjectCalculationRegister::ibValueMetaObjectRecalculation::ibValueMetaObjectRecalculation() : ibValueMetaObjectCompositeData()
{
}

ibValueMetaObjectCalculationRegister::ibValueMetaObjectRecalculation::~ibValueMetaObjectRecalculation()
{
}

const ibValueMetaObjectCalculationRegister* ibValueMetaObjectCalculationRegister::ibValueMetaObjectRecalculation::GetRegister() const
{
	return GetParentAsType<ibValueMetaObjectCalculationRegister>();
}

bool ibValueMetaObjectCalculationRegister::ibValueMetaObjectRecalculation::KeepsActionPeriod() const
{
	const ibValueMetaObjectCalculationRegister* reg = GetRegister();
	return reg != nullptr && reg->IsUseActionPeriod() && (*m_propertyActionPeriod)->GetMetaID() != 0;
}

wxString ibValueMetaObjectCalculationRegister::ibValueMetaObjectRecalculation::GetKeyHashColumn() const
{
	return ibDerivedKeyNeedsHash(GetQueryable()->GetPrimaryKeyColumns()) ? wxString(KeyHashColumnName()) : wxString();
}

bool ibValueMetaObjectCalculationRegister::ibValueMetaObjectRecalculation::StampActionPeriodIfNeverSaved(int flags)
{
	if ((*m_propertyActionPeriod)->GetMetaID() != 0)
		return true;
	// The copy that mirrors the database — the designer's baseline, run with loadConfigFlag, and the running
	// application, which is not the designer — goes on not having it until the edited one is applied (the
	// chart's sections ask the same pair, chartOfCalculationTypesMetadata.cpp).
	if ((flags & loadConfigFlag) != 0 || !appData->DesignerMode())
		return true;
	return (*m_propertyActionPeriod)->OnCreateMetaObject(m_metaData, 0);
}

//***************************************************************************
//*                       Save & load metaData                              *
//***************************************************************************

// Beyond Name/Synonym (held by the base) and the dimension children (their own metaobjects), a
// recalculation carries the IDENTITIES of its standard columns — saved, because a column's number is its
// field's name and must be the same number the next time the configuration is read.
bool ibValueMetaObjectCalculationRegister::ibValueMetaObjectRecalculation::ReadData(const ibDataNode& node)
{
	m_propertyRecalculationObject->SetNodeValue(node.GetProperty(m_propertyRecalculationObject->GetName()));
	m_propertyCalculationType->SetNodeValue(node.GetProperty(m_propertyCalculationType->GetName()));
	m_propertyActionPeriod->SetNodeValue(node.GetProperty(m_propertyActionPeriod->GetName()));   // absent: id 0, stamped at run
	return ibValueMetaObjectCompositeData::ReadData(node);
}

bool ibValueMetaObjectCalculationRegister::ibValueMetaObjectRecalculation::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyRecalculationObject->GetName(), m_propertyRecalculationObject->GetNodeValue());
	node.SetProperty(m_propertyCalculationType->GetName(), m_propertyCalculationType->GetNodeValue());
	node.SetProperty(m_propertyActionPeriod->GetName(), m_propertyActionPeriod->GetNodeValue());
	return ibValueMetaObjectCompositeData::WriteData(node);
}

//***********************************************************************
//*								Events								    *
//***********************************************************************

// (The month's attribute takes part in the events only once it has a number — NumberedActionPeriod.)

bool ibValueMetaObjectCalculationRegister::ibValueMetaObjectRecalculation::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	if (!ibValueMetaObjectCompositeData::OnCreateMetaObject(metaData, flags))
		return false;
	// The numbers are handed out HERE, by the create event, like every predefined child's.
	return (*m_propertyRecalculationObject)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyCalculationType)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyActionPeriod)->OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectCalculationRegister::ibValueMetaObjectRecalculation::OnLoadMetaObject(ibMetaData* metaData)
{
	ibValueMetaObjectAttributeBase* month = NumberedActionPeriod();
	if (!(*m_propertyRecalculationObject)->OnLoadMetaObject(metaData) ||
		!(*m_propertyCalculationType)->OnLoadMetaObject(metaData) ||
		(month != nullptr && !month->OnLoadMetaObject(metaData)))
		return false;
	return ibValueMetaObjectCompositeData::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectCalculationRegister::ibValueMetaObjectRecalculation::OnSaveMetaObject(int flags)
{
	ibValueMetaObjectAttributeBase* month = NumberedActionPeriod();
	if (!(*m_propertyRecalculationObject)->OnSaveMetaObject(flags) ||
		!(*m_propertyCalculationType)->OnSaveMetaObject(flags) ||
		(month != nullptr && !month->OnSaveMetaObject(flags)))
		return false;
	return ibValueMetaObjectCompositeData::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectCalculationRegister::ibValueMetaObjectRecalculation::OnDeleteMetaObject()
{
	ibValueMetaObjectAttributeBase* month = NumberedActionPeriod();
	if (!(*m_propertyRecalculationObject)->OnDeleteMetaObject() ||
		!(*m_propertyCalculationType)->OnDeleteMetaObject() ||
		(month != nullptr && !month->OnDeleteMetaObject()))
		return false;
	return ibValueMetaObjectCompositeData::OnDeleteMetaObject();
}

bool ibValueMetaObjectCalculationRegister::ibValueMetaObjectRecalculation::OnReloadMetaObject()
{
	ibValueMetaObject* metaObject = GetParent();
	wxASSERT(metaObject);
	if (metaObject != nullptr && metaObject->OnReloadMetaObject())
		return ibValueMetaObjectCompositeData::OnReloadMetaObject();
	return false;
}

bool ibValueMetaObjectCalculationRegister::ibValueMetaObjectRecalculation::OnBeforeRunMetaObject(int flags)
{
	// Stamped BEFORE it runs — the counter is seeded by now (before-run follows the whole load), and
	// the attribute registers under its number.
	if (!StampActionPeriodIfNeverSaved(flags))
		return false;
	ibValueMetaObjectAttributeBase* month = NumberedActionPeriod();
	if (!(*m_propertyRecalculationObject)->OnBeforeRunMetaObject(flags) ||
		!(*m_propertyCalculationType)->OnBeforeRunMetaObject(flags) ||
		(month != nullptr && !month->OnBeforeRunMetaObject(flags)))
		return false;
	return ibValueMetaObjectCompositeData::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectCalculationRegister::ibValueMetaObjectRecalculation::OnAfterRunMetaObject(int flags)
{
	ibValueMetaObjectAttributeBase* month = NumberedActionPeriod();
	if (!(*m_propertyRecalculationObject)->OnAfterRunMetaObject(flags) ||
		!(*m_propertyCalculationType)->OnAfterRunMetaObject(flags) ||
		(month != nullptr && !month->OnAfterRunMetaObject(flags)))
		return false;
	// Register the recalculation as an L4 query source (parent-qualified "<Register>.<Recalculation>").
	// Register ALWAYS — the factory is PER-CONFIG (in the metadata), so a read-only DB load still
	// registers its OWN sources into its OWN factory or the recalculation can't resolve on that config.
	m_metaData->RegisterSource(&m_queryable);
	return ibValueMetaObjectCompositeData::OnAfterRunMetaObject(flags);
}

bool ibValueMetaObjectCalculationRegister::ibValueMetaObjectRecalculation::OnBeforeCloseMetaObject()   // un-resolve — mirror of OnRun's RegisterSource
{
	m_metaData->UnregisterSource(&m_queryable);
	ibValueMetaObjectAttributeBase* month = NumberedActionPeriod();
	if (!(*m_propertyRecalculationObject)->OnBeforeCloseMetaObject() ||
		!(*m_propertyCalculationType)->OnBeforeCloseMetaObject() ||
		(month != nullptr && !month->OnBeforeCloseMetaObject()))
		return false;
	return ibValueMetaObjectCompositeData::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectCalculationRegister::ibValueMetaObjectRecalculation::OnAfterCloseMetaObject()
{
	ibValueMetaObjectAttributeBase* month = NumberedActionPeriod();
	if (!(*m_propertyRecalculationObject)->OnAfterCloseMetaObject() ||
		!(*m_propertyCalculationType)->OnAfterCloseMetaObject() ||
		(month != nullptr && !month->OnAfterCloseMetaObject()))
		return false;
	return ibValueMetaObjectCompositeData::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectCalculationRegister::ibValueMetaObjectRecalculation, "Recalculation", g_metaRecalculationCLSID);
