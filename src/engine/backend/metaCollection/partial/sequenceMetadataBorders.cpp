////////////////////////////////////////////////////////////////////////////
//	Description : the borders of a sequence — the table, and the reading
////////////////////////////////////////////////////////////////////////////

#include "sequence.h"

#include "backend/metaData.h"
#include "backend/query/schemaSnapshot.h"                            // ibSchemaSnapshot / ibDeclareLookupIndex
#include "backend/metaCollection/dimension/metaDimensionObject.h"    // the dimensions the key leads with

//***********************************************************************
//*                        ibSequenceQueryable                          *
//***********************************************************************

ibSequenceQueryable::ibSequenceQueryable(const ibValueMetaObjectSequence* seq)
	: ibRegisterDataQueryable(seq), m_seq(seq)
{
}

// THE MOMENT IS PROJECTED beside the stored columns, so `SELECT *`, the composer's default fields and a
// name carry it. Its name is the predefined attribute's — that attribute TYPES it and stores nothing,
// and it is in no list (FillArrayObjectByPredefinedAttribute), so the name finds this column and not a
// column over fields no table has.
std::vector<const ibBackendQueryColumn*> ibSequenceQueryable::GetQueryColumns() const
{
	std::vector<const ibBackendQueryColumn*> columns = ibRegisterDataQueryable::GetQueryColumns();
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
// the moment — and nothing else. The MOMENT is a column of the QUERY (the queryable constructs it), so it is
// offered where the query is written and not here: a source lists the fields that are.
void ibValueMetaObjectSequence::FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const
{
	for (const ibValueMetaObjectAttributeBase* attribute : GetGenericAttributeArrayObject()) {
		if (attribute == nullptr)
			continue;
		const bool bookkeeping = attribute == GetRegisterActive() || attribute == GetRegisterLineNumber();
		explorer.AppendColumn(attribute, attribute->GetColumnId(), /*enabled*/ true, /*visible*/ !bookkeeping);
	}
}

//***********************************************************************
//*                      ibSequenceBordersQueryable                     *
//***********************************************************************

// The key, and what the key has got to. A border says the moment the way a registration says it: the
// recorder and its date, because the border IS the last registration everything is done up to.
std::vector<const ibBackendQueryColumn*> ibSequenceBordersQueryable::GetQueryColumns() const
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

ibString ibSequenceBordersQueryable::GetQueryTableName() const { return m_seq->GetBordersTableName(); }
const ibUniqueKey& ibSequenceBordersQueryable::GetQueryTableGuid() const { return m_seq->GetBordersObject()->GetGuid(); }
ibString ibSequenceBordersQueryable::GetQueryName() const { return m_seq->GetBordersObject()->GetName(); }
ibMetaID ibSequenceBordersQueryable::GetQueryTableId() const { return m_seq->GetBordersObject()->GetMetaID(); }
const ibMetaData* ibSequenceBordersQueryable::GetMetaData() const { return m_seq->GetMetaData(); }

//***********************************************************************
//*                 ibSequenceBordersSourceDescriptor                   *
//***********************************************************************

ibString ibSequenceBordersSourceDescriptor::GetNamespace() const
{
	return ibValue::GetNameObjectFromID(m_meta->GetClassType());
}

ibString ibSequenceBordersSourceDescriptor::GetName() const
{
	return m_meta->GetName() + ibT(".") + m_meta->GetBordersObject()->GetName();
}

const ibBackendQueryable* ibSequenceBordersSourceDescriptor::CreateQueryable(ibValue** /*paParams*/, long /*lSizeArray*/)
{
	return &m_queryable;   // no arguments: a border is a fact of the key, not a reading as of a moment
}

std::vector<const ibBackendQueryColumn*> ibSequenceBordersSourceDescriptor::GetQueryColumns(const std::vector<ibValue>& /*args*/) const
{
	return m_queryable.GetQueryColumns();
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
		// The period CLOSES the index whatever is cut: what stands between the border and a registration is a
		// RANGE on it, served only while it stands right after the key's equalities - the totals' `_DL` twin.
		ibDeclareLookupIndex(t, t.m_name + ibT("_KIX"), byKey, /*closing*/ GetRegisterPeriod()->GetQueryColumn());
	}

	// …and the borders, one row per key.
	if (HasBorders()) {
		const ibSequenceBordersQueryable* borders = GetBordersQueryable();
		ibSchemaTable& t = out.CreateSchemaTable(borders);
		for (const ibBackendQueryColumn* column : borders->GetQueryColumns()) {
			// ⚠ A SYNTHETIC COLUMN IS STORED NOWHERE. The moment is the period and the recorder, and
			// asking for a column of its own asks for their fields a second time — which Firebird says
			// plainly: "violation of PRIMARY or UNIQUE KEY … FLD…_D on SEQUENCE…_BORDERS" (2026-09-18).
			if (column->GetColumnKind() == ibBackendQueryColumn::Kind::Synthetic)
				continue;
			t.Add(column);
		}
		ibDeclareLookupIndex(t, t.m_name + ibT("_LIX"), borders->GetPrimaryKeyColumns());
	}
}
