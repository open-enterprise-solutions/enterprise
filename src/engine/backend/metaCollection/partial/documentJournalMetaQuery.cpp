////////////////////////////////////////////////////////////////////////////
//	Description : document journal - the rows it is read as
////////////////////////////////////////////////////////////////////////////

#include "documentJournal.h"
#include "document.h"

#include "backend/metaData.h"
#include "backend/query/columnLayout.h"                       // DescribeColumnLayout — the fields a column lies in
#include "backend/metaCollection/partial/reference/reference.h"   // ibValueReferenceDataObject — a row is a document

#include <algorithm>
#include <iterator>

namespace {

// The journal's columns, by the place each takes in a row. The ids are synthetic: nothing declared these
// columns, the journal publishes them.
enum ibJournalColumn : ibMetaID {
	eJournalRef = 1,
	eJournalDate,
	eJournalNumber,
	eJournalPosted,
	eJournalDeletionMark,
};

ibMetaID JournalColumnId(ibJournalColumn which)
{
	return ibBackendQueryColumn::SyntheticId(ibBackendQueryColumn::SyntheticKind::Derived, which);
}

// The column of one document that a journal column stands for.
const ibValueMetaObjectAttributePredefined* DocumentColumn(const ibValueMetaObjectDocument* doc, ibJournalColumn which)
{
	switch (which) {
	case eJournalRef:          return doc->GetDataReference();
	case eJournalDate:         return doc->GetDocumentDate();
	case eJournalNumber:       return doc->GetDocumentNumber();
	case eJournalPosted:       return doc->GetDocumentPosted();
	case eJournalDeletionMark: return doc->GetDataDeletionMark();
	}
	return nullptr;
}

// ⭐ ONE TYPE THAT HOLDS EVERY DOCUMENT'S VALUE. The kinds are joined, and where two documents qualify the
// same kind differently the wider one is kept: a number of 11 characters in one document and of 6 in the
// other is a column of 11, or the longer one is cut on the way into the list.
void Widen(ibTypeDescription& into, const ibTypeDescription& from)
{
	const bool hadNumber = into.ContainType(ibValueTypes::TYPE_NUMBER);
	const bool hadString = into.ContainType(ibValueTypes::TYPE_STRING);
	const bool hadDate   = into.ContainType(ibValueTypes::TYPE_DATE);
	const ibTypeDescription::ibTypeData was = into.m_typeData;

	into.AppendMetaType(from.m_listTypeClass, from.m_typeData);

	if (hadNumber && from.ContainType(ibValueTypes::TYPE_NUMBER))
		into.m_typeData.SetNumber(std::max(was.GetPrecision(), from.m_typeData.GetPrecision()),
			std::max(was.GetScale(), from.m_typeData.GetScale()));
	if (hadString && from.ContainType(ibValueTypes::TYPE_STRING))
		into.m_typeData.SetString(std::max(was.GetLength(), from.m_typeData.GetLength()),
			was.GetLength() == from.m_typeData.GetLength() ? was.GetAllowedLength() : ibAllowedLength::ibAllowedLength_Variable);
	if (hadDate)
		into.m_typeData.SetDate(was.GetDateFraction());
}

const ibJournalColumn s_columns[] = { eJournalRef, eJournalDate, eJournalNumber, eJournalPosted, eJournalDeletionMark };

} // namespace

//***********************************************************************
//*                            the rows                                 *
//***********************************************************************

ibDocumentJournalQueryable::ibDocumentJournalQueryable(const ibValueMetaObjectDocumentJournal* meta) : m_meta(meta)
{
	// Named as the documents name them, so a query reads `Ref`, `Date`, `Number` over a journal exactly as
	// it does over any one document - and captioned as a document's list captions them.
	const auto make = [](const ibString& name, const ibString& caption, ibJournalColumn which) {
		return std::make_unique<ibTempColumn>(name, name, ibTypeDescription(), JournalColumnId(which), caption);
	};
	m_ref          = make(ibT("Ref"), _("Ref"), eJournalRef);
	m_date         = make(ibT("Date"), _("Date"), eJournalDate);
	m_number       = make(ibT("Number"), ibGETTEXT_IN_CONTEXT("document attribute", "Number"), eJournalNumber);
	m_posted       = make(ibT("Posted"), _("Posted"), eJournalPosted);
	m_deletionMark = make(ibT("DeletionMark"), _("Deletion mark"), eJournalDeletionMark);
}

void ibDocumentJournalQueryable::Retype() const
{
	ibTempColumn* const columns[] = { m_ref.get(), m_date.get(), m_number.get(), m_posted.get(), m_deletionMark.get() };

	for (size_t at = 0; at < std::size(s_columns); ++at) {
		ibTypeDescription type;
		for (const ibValueMetaObjectDocument* doc : m_meta->GetDocuments())
			if (const ibValueMetaObjectAttributePredefined* own = DocumentColumn(doc, s_columns[at]))
				Widen(type, own->GetTypeDesc());
		columns[at]->GetTypeDesc() = type;
	}
}

std::vector<const ibBackendQueryColumn*> ibDocumentJournalQueryable::GetColumns() const
{
	Retype();
	return { m_ref.get(), m_date.get(), m_number.get(), m_posted.get(), m_deletionMark.get() };
}

const ibBackendQueryColumn* ibDocumentJournalQueryable::ResolveColumnByName(const ibString& name) const
{
	for (const ibBackendQueryColumn* column : GetColumns())
		if (stringUtils::IsSameAs(column->GetName(), name))
			return column;
	return nullptr;
}

ibString ibDocumentJournalQueryable::GetQueryTableName() const
{
	return ibString::Format(ibT("%s%i"), m_meta->GetClassName(), m_meta->GetMetaID());
}

ibString ibDocumentJournalQueryable::GetQueryName() const { return m_meta->GetName(); }
const ibMetaData* ibDocumentJournalQueryable::GetMetaData() const { return m_meta->GetMetaData(); }
const ibValueMetaObjectGenericData* ibDocumentJournalQueryable::GetSourceMetaObject() const { return m_meta; }

bool ibDocumentJournalQueryable::IsComputedInRam() const { return m_meta->GetDocuments().empty(); }

ibBackendQueryProvider& ibDocumentJournalQueryable::GetProvider() const
{
	return IsComputedInRam() ? ibComputedProviderInstance() : ibBackendQueryable::GetProvider();
}

// A journal that lists nothing has no rows, and says so with the columns it would have.
ibQueryRamTable ibDocumentJournalQueryable::ComputeRows(const std::vector<ibQueryCondition>& /*extra*/) const
{
	ibQueryRamTable table;
	for (const ibBackendQueryColumn* column : GetColumns())
		table.AddColumn(column->GetColumnId(), column->GetName(), column->GetTypeDesc());
	return table;
}

// ⭐⭐ ONE BRANCH PER DOCUMENT, FIELD FOR FIELD. Each journal column is spread into the fields its type
// takes (the tag, a reference's pair, a number's and a string's field when the documents keep both), and
// each branch fills them from the document's own fields of the same role. A field that document does not
// have - a numeric number where this one keeps text - is a typed NULL, never read, since the tag beside
// it says where the value is.
//
// The branches must agree on every field's type, so a field whose type is not the journal's is cast to
// it; one that already is stays a bare column.
ibQueryRelPtr ibDocumentJournalQueryable::GetSourceRelation(const ibString& alias) const
{
	const std::vector<const ibValueMetaObjectDocument*> documents = m_meta->GetDocuments();
	if (documents.empty())
		return nullptr;

	const std::vector<const ibBackendQueryColumn*> columns = GetColumns();
	ibValue absent;
	absent.SetType(ibValueTypes::TYPE_NULL);

	ibQueryRelPtr rows;
	for (const ibValueMetaObjectDocument* doc : documents) {
		std::vector<ibQueryProjItem> fields;
		for (size_t at = 0; at < columns.size(); ++at) {
			const ibValueMetaObjectAttributePredefined* own = DocumentColumn(doc, s_columns[at]);
			const std::vector<ibColumnSlot> ownSlots = own != nullptr
				? DescribeColumnLayout(own->GetQueryColumn()) : std::vector<ibColumnSlot>();

			for (const ibColumnSlot& slot : DescribeColumnLayout(columns[at])) {
				const auto same = std::find_if(ownSlots.begin(), ownSlots.end(),
					[&slot](const ibColumnSlot& candidate) { return candidate.m_role == slot.m_role; });
				ibQueryExprPtr value;
				if (same == ownSlots.end())
					value = ibCast(ibConst(absent), slot.m_type);
				else if (ibSameFieldType(same->m_type, slot.m_type))
					value = ibCol(same->m_name);
				else
					value = ibCast(ibCol(same->m_name), slot.m_type);
				fields.push_back({ value, slot.m_name });
			}
		}

		const ibQueryRelPtr branch = ibProject(ibScan(doc->GetQueryable()->GetQueryTableName()), std::move(fields));
		rows = rows ? ibUnionAll(rows, branch) : branch;
	}

	return ibSubquery(rows, alias);
}

//***********************************************************************
//*                    what the factory knows it by                     *
//***********************************************************************

ibString ibDocumentJournalSource::GetNamespace() const { return ibValue::GetNameObjectFromID(m_meta->GetClassType()); }
ibString ibDocumentJournalSource::GetName() const { return m_meta->GetName(); }

void ibDocumentJournalSource::FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const
{
	for (const ibBackendQueryColumn* column : m_rows.GetColumns())
		explorer.AppendColumn(column);
}

// A row picks the document it is.
ibValue ibDocumentJournalSource::GetSelectValue(const ibRowMetaValues& rowValues) const
{
	const ibRowMetaValues::const_iterator it = rowValues.find(JournalColumnId(eJournalRef));
	return it != rowValues.end() ? it->second : ibValue();
}

// …and is known by it: the reference, kept whole, because its guid alone does not say which document's.
ibUniqueKey ibDocumentJournalSource::GetItemKey(const ibRowMetaValues& rowValues) const
{
	ibUniqueKey key;
	const ibValue ref = GetSelectValue(rowValues);
	if (!ref.IsEmpty()) {
		ibRowMetaValues values;
		values[JournalColumnId(eJournalRef)] = ref;
		key.SetKeyValues(values);
	}
	return key;
}

// Opening a row opens the document, by the road that document's own list takes.
void ibDocumentJournalSource::ShowValueByKey(const ibUniqueKey& key, ibBackendValueForm* srcForm) const
{
	const ibValue ref = key.GetKey(JournalColumnId(eJournalRef));
	ibValueReferenceDataObject* reference = nullptr;
	if (!ref.ConvertToValue(reference) || reference == nullptr)
		return;

	const ibValueMetaObjectRecordDataMutableRef* document = nullptr;
	if (reference->GetMetaObject() != nullptr && reference->GetMetaObject()->ConvertToValue(document) && document != nullptr)
		document->ShowValueByKey(reference->GetGuid(), srcForm);
}
