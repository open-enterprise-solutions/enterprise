////////////////////////////////////////////////////////////////////////////
//	Description : the borders of a sequence — the table, and the reading
////////////////////////////////////////////////////////////////////////////

#include "sequence.h"

#include "backend/metaData.h"
#include "backend/query/schemaSnapshot.h"                            // ibSchemaSnapshot / ibDeclareLookupIndex
#include "backend/metaCollection/dimension/metaDimensionObject.h"    // the dimensions the key leads with

//***********************************************************************
//*        ibValueMetaObjectSequence::ibBackendColumnPointInTime        *
//***********************************************************************

wxString ibValueMetaObjectSequence::ibBackendColumnPointInTime::GetName()         const { return m_seq->GetPointInTime()->GetName(); }
wxString ibValueMetaObjectSequence::ibBackendColumnPointInTime::GetSynonym()      const { return m_seq->GetPointInTime()->GetSynonym(); }
wxString ibValueMetaObjectSequence::ibBackendColumnPointInTime::GetPhysicalName() const { return m_seq->GetPointInTime()->GetPhysicalName(); }
ibMetaID ibValueMetaObjectSequence::ibBackendColumnPointInTime::GetColumnId()     const { return m_seq->GetPointInTime()->GetColumnId(); }
ibTypeDescription& ibValueMetaObjectSequence::ibBackendColumnPointInTime::GetTypeDesc() const { return m_seq->GetPointInTime()->GetTypeDesc(); }

// THE MOMENT LIES IN TWO OTHER COLUMNS — the period first, then the recorder. Each describes itself,
// so this is their layouts one after the other: sorting by the moment IS sorting by the period and
// then by the recorder's own fields, through the machinery that sorts any reference.
std::vector<ibColumnSlot> ibValueMetaObjectSequence::ibBackendColumnPointInTime::DescribeLayout() const
{
	std::vector<ibColumnSlot> slots;
	for (const ibBackendQueryColumn* part : { m_seq->GetRegisterPeriod()->GetQueryColumn(),
	                                          m_seq->GetRegisterRecorder()->GetQueryColumn() }) {
		if (part == nullptr)
			continue;
		for (const ibColumnSlot& slot : DescribeColumnLayout(part)) {
			// ⚠ WITHOUT THE PARTS' TYPE TAGS — the moment has no such question (its period is always a
			// period and its recorder always a reference), and two tags under one prefix collide in the
			// statement. The same reason the document's moment leaves them out.
			if (slot.m_role == ibColumnRole::Discriminator)
				continue;
			slots.push_back(slot);
		}
	}
	return slots;
}

bool ibValueMetaObjectSequence::ibBackendColumnPointInTime::ReadValue(const wxString& fieldName, const ibMetaData* metaData,
	ibValue& retValue, ibQueryResult& result, bool createData) const
{
	const ibBackendQueryColumn* period = m_seq->GetRegisterPeriod()->GetQueryColumn();
	const ibBackendQueryColumn* recorder = m_seq->GetRegisterRecorder()->GetQueryColumn();
	if (period == nullptr || recorder == nullptr)
		return false;

	// BY THE PARTS' OWN FIELD NAMES — the moment has none of its own, and on a plain read those are the
	// fields the select carries.
	//
	// 🛑⭐ …UNLESS THE READER WAS GIVEN A NAME, and then that name is the base. A JOIN projects every
	// output under its ALIAS (`out_Border_D`, `out_Border_RRRef`) precisely because two sources may
	// carry the same physical field — so a moment read by its parts' names found nothing there:
	// `Field 'fld1563_D' not found in the resultset`, on any joined query selecting a moment
	// (2026-09-18). The argument exists for exactly this, and ignoring it is what made the two
	// spellings drift.
	// ⚠ …AND ITS OWN NAME IS NOT A PREFIX. A reader that holds this column asks it by the name the
	// column answers to (`fld1566`), and NOTHING is stored under that name — the moment's fields are
	// the period's and the recorder's. Taking it for a prefix sent the list looking for `fld1566_D`
	// and the application said so in a box (Max, 2026-09-18). A prefix is a name that is NOT ours.
	const bool aliased = !fieldName.IsEmpty() && !fieldName.IsSameAs(GetPhysicalName(), false);
	const wxString periodBase   = aliased ? fieldName : period->GetPhysicalName();
	const wxString recorderBase = aliased ? fieldName : recorder->GetPhysicalName();

	ibValue vPeriod, vRecorder;
	ibColumnCodec::ReadField(periodBase + ibFieldSuffix(ibColumnRole::Date), ibFieldTypes_Date,
		period, metaData, vPeriod, result, createData);
	ibColumnCodec::ReadField(recorderBase, ibFieldTypes_Reference,
		recorder, metaData, vRecorder, result, createData);

	retValue = new ibValuePointInTime(vPeriod.GetDateTime(), vRecorder);
	return true;
}

// The writing side — what makes `WHERE Moment <= &Point` and an ORDER BY over it run: the halves land
// in the order the layout names them, the period and then the recorder's pair.
void ibValueMetaObjectSequence::ibBackendColumnPointInTime::BindValue(ibQueryStatement& statement, const ibMetaData* /*metaData*/,
	const ibValue& value, int& position) const
{
	wxDateTime date;
	ibValue    reference;
	ibValuePointInTime* moment = nullptr;
	if (value.ConvertToValue(moment) && moment != nullptr) {
		date      = moment->m_date;
		reference = moment->m_reference;
	}
	else {
		date = value.GetDateTime();   // a bare date is a legitimate right-hand side: the instant itself
	}

	ibClassID   refClsid = 0;
	const void* refBlob  = nullptr;
	ibValueReferenceDataObject* refData = nullptr;
	// ⚠ THE FREE FUNCTION, SAID SO. Nested in the metatype, the bare name now finds the ENCLOSING
	// class's member (`ibValue::IsReference`) before the global one, and that member takes no argument.
	if (::IsReference(reference.GetClassType()) && reference.ConvertToValue(refData) && refData != nullptr) {
		refClsid = reference.GetClassType();
		refBlob  = refData->GetReferenceData();
	}

	for (const ibColumnSlot& slot : DescribeLayout()) {
		switch (slot.m_role) {
		case ibColumnRole::Date:          statement.SetParamDate(position++, date); break;
		case ibColumnRole::ReferenceType: statement.SetParamNumber(position++, refClsid); break;
		case ibColumnRole::ReferenceId:
			if (refBlob != nullptr) statement.SetParamBlob(position++, refBlob, sizeof(ibReference));
			else                    statement.SetParamNull(position++);
			break;
		default:                          statement.SetParamNull(position++); break;
		}
	}
}

//***********************************************************************
//*                        ibSequenceQueryable                          *
//***********************************************************************

ibSequenceQueryable::ibSequenceQueryable(const ibValueMetaObjectSequence* seq)
	: ibRegisterDataQueryable(seq), m_seq(seq)
{
}

// THE MOMENT ANSWERS FIRST. Its name is the predefined attribute's — that attribute TYPES it and
// stores nothing, so letting the register's surface answer with the attribute's own column handed
// back a column over fields no table has.
const ibBackendQueryColumn* ibSequenceQueryable::ResolveColumnByName(const wxString& name) const
{
	const ibBackendQueryColumn* moment = m_seq->GetMomentColumn();
	if (moment != nullptr && moment->GetName().IsSameAs(name, false))
		return moment;
	return ibRegisterDataQueryable::ResolveColumnByName(name);
}

// …and it is PROJECTED beside the stored columns, so `SELECT *` and the composer's default fields
// carry it too. One surface answers both questions — a column resolvable by name and missing from
// the projection is the half-answer this pair always produces when they are kept apart.
std::vector<const ibBackendQueryColumn*> ibSequenceQueryable::GetColumns() const
{
	std::vector<const ibBackendQueryColumn*> columns = ibRegisterDataQueryable::GetColumns();
	if (const ibBackendQueryColumn* moment = m_seq->GetMomentColumn())
		columns.push_back(moment);
	return columns;
}

// ⭐ WHAT A REGISTRATION IS ASKED ABOUT: the key it stands under, the moment it stands at, and what
// describes it — the recorder, the period, the dimensions and the attributes.
//
// Two of the register's own fields stay OUT OF THE TREE and reachable by name: ACTIVE and the LINE
// NUMBER. They are the set's bookkeeping, not a question anybody puts to a sequence — a registration
// is one row per recorder per key, not a numbered line of a movement set. The MOMENT is hidden for
// the opposite reason: it is asked for constantly in a query and means nothing as a list column
// (Max, 2026-09-18: "it must be there for the border and for the ordinary selection alike; there is
// no need to see it in the list"). The reference system lists exactly the recorder, the period, the dimensions and
// the moment — and nothing else.
void ibValueMetaObjectSequence::FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const
{
	for (const ibValueMetaObjectAttributeBase* attribute : GetGenericAttributeArrayObject()) {
		if (attribute == nullptr)
			continue;
		const bool bookkeeping = attribute == GetRegisterActive() || attribute == GetRegisterLineNumber();
		explorer.AppendColumn(attribute->GetQueryColumn(), /*enabled*/ true, /*visible*/ !bookkeeping);
	}
	// …and the moment, from the QUERYABLE — where a constructed column lives, so the tree offers
	// exactly what a query can resolve.
	if (const ibBackendQueryable* queryable = GetQueryable())
		explorer.AppendColumn(queryable->ResolveColumnByName(GetPointInTime()->GetName()), /*enabled*/ true, /*visible*/ false);
}

//***********************************************************************
//*                      ibSequenceBordersQueryable                     *
//***********************************************************************

// The key, and what the key has got to. A border says the moment the way a registration says it: the
// recorder and its date, because the border IS the last registration everything is done up to.
std::vector<const ibBackendQueryColumn*> ibSequenceBordersQueryable::GetColumns() const
{
	std::vector<const ibBackendQueryColumn*> columns;
	for (const ibValueMetaObjectDimension* dimension : m_seq->GetDimensionArrayObject())
		columns.push_back(dimension->GetQueryColumn());
	columns.push_back(m_seq->GetRegisterRecorder()->GetQueryColumn());
	columns.push_back(m_seq->GetRegisterPeriod()->GetQueryColumn());
	// …and the moment itself, which is what a reader of a border actually wants: one value to compare
	// and to order by, instead of a date and a document to pair up at every caller.
	columns.push_back(m_seq->GetMomentColumn());
	return columns;
}

std::vector<const ibBackendQueryColumn*> ibSequenceBordersQueryable::GetPrimaryKeyColumns() const
{
	std::vector<const ibBackendQueryColumn*> key;
	for (const ibValueMetaObjectDimension* dimension : m_seq->GetDimensionArrayObject())
		key.push_back(dimension->GetQueryColumn());
	return key;   // a sequence with no dimensions keeps exactly one border
}

const ibBackendQueryColumn* ibSequenceBordersQueryable::ResolveColumnByName(const wxString& name) const
{
	for (const ibBackendQueryColumn* column : GetColumns())
		if (column->GetName().IsSameAs(name, false))
			return column;
	return nullptr;
}

wxString ibSequenceBordersQueryable::GetQueryTableName() const { return m_seq->GetBordersTableName(); }
const ibUniqueKey& ibSequenceBordersQueryable::GetQueryTableGuid() const { return m_seq->GetBordersObject()->GetGuid(); }
wxString ibSequenceBordersQueryable::GetQueryName() const { return m_seq->GetBordersObject()->GetName(); }
ibMetaID ibSequenceBordersQueryable::GetQueryTableId() const { return m_seq->GetBordersObject()->GetMetaID(); }
const ibMetaData* ibSequenceBordersQueryable::GetMetaData() const { return m_seq->GetMetaData(); }

//***********************************************************************
//*                 ibSequenceBordersSourceDescriptor                   *
//***********************************************************************

wxString ibSequenceBordersSourceDescriptor::GetNamespace() const
{
	return ibValue::GetNameObjectFromID(m_meta->GetClassType());
}

wxString ibSequenceBordersSourceDescriptor::GetName() const
{
	return m_meta->GetName() + wxT(".") + m_meta->GetBordersObject()->GetName();
}

const ibBackendQueryable* ibSequenceBordersSourceDescriptor::CreateQueryable(ibValue** /*paParams*/, long /*lSizeArray*/)
{
	return &m_queryable;   // no arguments: a border is a fact of the key, not a reading as of a moment
}

void ibSequenceBordersSourceDescriptor::FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const
{
	for (const ibBackendQueryColumn* column : m_queryable.GetColumns())
		explorer.AppendColumn(column);
}

//***********************************************************************
//*                              the tables                             *
//***********************************************************************

void ibValueMetaObjectSequence::ContributeTables(ibSchemaSnapshot& out) const
{
	// The registrations — the base declares the table; the index is this kind's own, because the
	// border's rules look a document's registrations up BY KEY (which key did this document register
	// under, and what stands between the border and it).
	ibValueMetaObjectRegisterData::ContributeTables(out);
	{
		const ibBackendQueryable* own = GetQueryable();
		ibSchemaTable& t = out.Shared(own->GetQueryTableId(), own->GetQueryTableName());
		std::vector<const ibBackendQueryColumn*> byKey;
		for (const ibValueMetaObjectDimension* dimension : GetDimensionArrayObject())
			byKey.push_back(dimension->GetQueryColumn());
		byKey.push_back(GetRegisterPeriod()->GetQueryColumn());
		ibDeclareLookupIndex(t, t.m_name + wxT("_KIX"), byKey);
	}

	// …and the borders, one row per key.
	if (HasBorders()) {
		const ibSequenceBordersQueryable* borders = GetBordersQueryable();
		ibSchemaTable& t = out.CreateSchemaTable(borders);
		for (const ibBackendQueryColumn* column : borders->GetColumns()) {
			// ⚠ A SYNTHETIC COLUMN IS STORED NOWHERE. The moment is the period and the recorder, and
			// asking for a column of its own asks for their fields a second time — which Firebird says
			// plainly: "violation of PRIMARY or UNIQUE KEY … FLD…_D on SEQUENCE…_BORDERS" (2026-09-18).
			if (column->GetColumnKind() == ibBackendQueryColumn::Kind::Synthetic)
				continue;
			t.Add(column);
		}
		ibDeclareLookupIndex(t, t.m_name + wxT("_LIX"), borders->GetPrimaryKeyColumns());
	}
}
