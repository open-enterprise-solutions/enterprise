////////////////////////////////////////////////////////////////////////////
//	Description : document journal - the rows it is read as
////////////////////////////////////////////////////////////////////////////

#include "documentJournal.h"
#include "document.h"

#include "backend/choiceRequest.h"                                // ibRequestChoice — which document Add makes
#include "backend/metaData.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"           // L2 — the journal's fields over the union
#include "backend/databaseLayer/databaseLayerException.h"         // ibDatabaseLayerException — a type read from no field
#include "backend/databaseLayer/databaseErrorCodes.h"             // DATABASE_LAYER_FIELD_NOT_IN_RESULTSET
#include "backend/query/columnLayout.h"                         // DescribeColumnLayout, ibSqlAliasOf, ibFieldSuffix — the fields a column lies in
#include "backend/query/dataQueryBuilder.h"                       // the union of the documents' reads, each under the policy
#include "backend/query/queryException.h"                         // ibBackendQueryException — documents no statement reads together
#include "backend/system/value/valueType.h"                       // ibValueType — the document's type, a Type
#include "backend/metaCollection/partial/reference/reference.h"   // ibValueReferenceDataObject — a row is a document

#include <algorithm>
#include <iterator>

namespace {

// The journal's columns. The ids are synthetic: nothing declared these columns, the journal publishes them.
enum ibJournalColumn : ibMetaID {
	eJournalRef = 1,
	eJournalDate,
	eJournalNumber,
	eJournalPosted,
	eJournalDeletionMark,
	// …and the one no document has: which document the row is (GetSourceRelation).
	eJournalDocumentType,
};

// …in the order a row lists them - the order of GetQueryColumns.
const ibJournalColumn s_columns[] = { eJournalRef, eJournalDate, eJournalNumber, eJournalDocumentType, eJournalPosted, eJournalDeletionMark };

ibMetaID JournalColumnId(ibJournalColumn which)
{
	return ibBackendQueryColumn::SyntheticId(ibBackendQueryColumn::SyntheticKind::Derived, which);
}

// The attribute of one document that a journal column stands for - none for the document's type.
const ibValueMetaObjectAttributePredefined* DocumentColumn(const ibValueMetaObjectDocument* doc, ibJournalColumn which)
{
	switch (which) {
	case eJournalRef:          return doc->GetDataReference();
	case eJournalDate:         return doc->GetDocumentDate();
	case eJournalNumber:       return doc->GetDocumentNumber();
	case eJournalPosted:       return doc->GetDocumentPosted();
	case eJournalDeletionMark: return doc->GetDataDeletionMark();
	case eJournalDocumentType: return nullptr;
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
	const ibTypeDescription::ibTypeData was = into.GetTypeData();

	into.AppendMetaType(from.GetClsidList(), from.GetTypeData());

	ibTypeDescription::ibTypeData wide = into.GetTypeData();
	if (hadNumber && from.ContainType(ibValueTypes::TYPE_NUMBER)) {
		// As many digits before the point as the wider has, as many after it as the finer.
		const int scale = std::max<int>(was.GetScale(), from.GetScale());
		const int whole = std::max<int>(was.GetPrecision() - was.GetScale(), from.GetPrecision() - from.GetScale());
		wide.SetNumber(static_cast<unsigned char>(whole + scale), static_cast<unsigned char>(scale));
	}
	if (hadString && from.ContainType(ibValueTypes::TYPE_STRING)) {
		// A length of 0 is no limit, and outlasts any other.
		const unsigned short length = was.GetLength() == 0 || from.GetLength() == 0 ? 0 : std::max(was.GetLength(), from.GetLength());
		const bool same = was.GetLength() == from.GetLength() && was.GetAllowedLength() == from.GetAllowedLength();
		wide.SetString(length, same ? was.GetAllowedLength() : ibAllowedLength::ibAllowedLength_Variable);
	}
	if (hadDate && from.ContainType(ibValueTypes::TYPE_DATE))
		wide.SetDate(was.GetDateFraction() == from.GetDateFraction() ? was.GetDateFraction() : ibDateFractions::ibDateFractions_DateTime);
	into.SetTypeData(wide);
}

} // namespace

//***********************************************************************
//*                            the rows                                 *
//***********************************************************************

ibDocumentJournalQueryable::ibDocumentJournalQueryable(const ibValueMetaObjectDocumentJournal* meta)
	: m_meta(meta), m_documents(meta->GetDocuments())
{
	// Named as the documents name them, so a query reads `Ref`, `Date`, `Number` over a journal exactly as
	// it does over any one document - and captioned as a document's list captions them.
	const auto make = [this](const ibString& name, const ibString& caption, ibJournalColumn which) {
		ibTypeDescription type;
		for (const ibValueMetaObjectDocument* doc : m_documents)
			Widen(type, DocumentColumn(doc, which)->GetTypeDesc());
		return std::make_shared<ibTempColumn>(name, name, type, JournalColumnId(which), m_meta->GetMetaData(), caption);
	};
	m_ref          = make(ibT("Ref"), _("Ref"), eJournalRef);
	m_date         = make(ibT("Date"), _("Date"), eJournalDate);
	m_number       = make(ibT("Number"), ibGETTEXT_IN_CONTEXT("document attribute", "Number"), eJournalNumber);
	m_posted       = make(ibT("Posted"), _("Posted"), eJournalPosted);
	m_deletionMark = make(ibT("DeletionMark"), _("Deletion mark"), eJournalDeletionMark);

	// ⭐ WHICH DOCUMENT THE ROW IS, said in a column of its own: the type of its reference, as code compares it
	// (`WHERE DocumentType = Type("DocumentRef.Invoice")`) and a list shows it - made of the reference's type field.
	m_documentType = std::make_shared<ibTypeColumn>(m_meta, m_ref, JournalColumnId(eJournalDocumentType));
}

//***********************************************************************
//*                the document's type, a Type                          *
//***********************************************************************

ibDocumentJournalQueryable::ibTypeColumn::ibTypeColumn(const ibValueMetaObjectDocumentJournal* meta,
	std::shared_ptr<const ibBackendQueryColumn> ref, ibMetaID id)
	: m_meta(meta), m_ref(std::move(ref)), m_id(id)
{
	m_type.SetDefaultMetaType(g_valueTypeCLSID);
}

ibString ibDocumentJournalQueryable::ibTypeColumn::GetName() const { return ibT("DocumentType"); }
ibString ibDocumentJournalQueryable::ibTypeColumn::GetSynonym() const { return _("Document type"); }
// The name it answers to. Nothing is stored under it: ReadValue reads the reference's field.
ibString ibDocumentJournalQueryable::ibTypeColumn::GetPhysicalName() const { return ibT("DocumentType"); }
const ibMetaData* ibDocumentJournalQueryable::ibTypeColumn::GetMetaData() const { return m_meta->GetMetaData(); }

// WHERE IT LIES: in the reference's type field and nowhere else - the reference's identifier is no part of a type,
// and a comparison keyed on it too would compare a type against a row.
std::vector<ibColumnSlot> ibDocumentJournalQueryable::ibTypeColumn::DescribeLayout() const
{
	std::vector<ibColumnSlot> slots;
	for (const ibColumnSlot& slot : DescribeColumnLayout(*m_ref))
		if (slot.m_role == ibColumnRole::ReferenceType)
			slots.push_back(slot);
	return slots;
}

// Read by the reference's field name - the only one there is - unless the reader was given a name of its own, as the
// moment is read (commonObjectMetaQuery.cpp). A result without the field is the codec's answer to the same fault: the
// typed empty, and false.
bool ibDocumentJournalQueryable::ibTypeColumn::ReadValue(const ibString& fieldName, const ibMetaData* /*metaData*/,
	ibValue& retValue, ibQueryResult& result, bool /*createData*/) const
{
	const bool aliased = !fieldName.IsEmpty() && !fieldName.IsSameAs(GetPhysicalName(), false);
	const ibString field = (aliased ? fieldName : m_ref->GetPhysicalName()) + ibFieldSuffix(ibColumnRole::ReferenceType);
	try {
		const ibClassID clsid = static_cast<ibClassID>(result.GetResultLong(field));
		if (clsid != 0)
			retValue = new ibValueType(clsid);
		else
			retValue = AdjustValue();
		return true;
	}
	catch (const ibDatabaseLayerException& err) {
		if (err.GetDriverErrorCode() != DATABASE_LAYER_FIELD_NOT_IN_RESULTSET)
			throw;
		retValue = AdjustValue();
		return false;
	}
}

// …and a comparison binds a type's class there: `WHERE DocumentType = &Kind` is the reference's type field against it.
void ibDocumentJournalQueryable::ibTypeColumn::BindValue(ibQueryStatement& statement, const ibMetaData* /*metaData*/,
	const ibValue& value, int& position) const
{
	ibClassID clsid = 0;
	ibValueType* type = nullptr;
	if (value.ConvertToValue(type) && type != nullptr)
		clsid = type->GetOwnerTypeClass();
	for (size_t slot = DescribeLayout().size(); slot > 0; --slot)
		statement.SetParamNumber(position++, clsid);
}

std::vector<std::shared_ptr<const ibBackendQueryColumn>> ibDocumentJournalQueryable::GetQueryColumns() const
{
	return { m_ref, m_date, m_number, m_documentType, m_posted, m_deletionMark };   // as s_columns lists them
}

ibString ibDocumentJournalQueryable::GetQueryTableName() const
{
	return ibString::Format(ibT("%s%i"), m_meta->GetClassName(), m_meta->GetMetaID());
}

const ibUniqueKey& ibDocumentJournalQueryable::GetQueryTableGuid() const { return m_meta->GetGuid(); }
ibString ibDocumentJournalQueryable::GetQueryName() const { return m_meta->GetName(); }
ibMetaID ibDocumentJournalQueryable::GetQueryTableId() const { return m_meta->GetMetaID(); }
const ibMetaData* ibDocumentJournalQueryable::GetMetaData() const { return m_meta->GetMetaData(); }
const ibValueMetaObjectGenericData* ibDocumentJournalQueryable::GetSourceMetaObject() const { return m_meta; }

ibBackendQueryProvider& ibDocumentJournalQueryable::GetProvider() const
{
	return m_documents.empty() ? ibComputedProviderInstance() : ibBackendQueryable::GetProvider();
}

// A journal that lists nothing has no rows, and says so with the columns it would have.
ibQueryRamTable ibDocumentJournalQueryable::ComputeRows(const std::vector<ibQueryCondition>& /*extra*/) const
{
	ibQueryRamTable table;
	for (const std::shared_ptr<const ibBackendQueryColumn>& column : GetQueryColumns())
		table.AddColumn(column->GetColumnId(), column->GetName(), column->GetTypeDesc());
	return table;
}

// ⭐⭐ THE UNION, AS THE QUERY LANGUAGE WRITES `SELECT ALLOWED … UNION ALL …` (Max, 2026-11-24): a branch per document,
// each a read of its own - under the reader's policy, AS ALLOWED - and the union of them one statement (a branch read on
// the server, ibSubqueryQueryable::ReadsOnServer; its policy folded in, ibDataQueryBuilder::BuildRelation). So a role's
// row filter on a document stands inside its branch, and a document the reader may not read is a branch of no row while
// the rest are read: the journal is no road around a document's right, nor does one closed document close it. Its own
// right is the journal's (AccessRight_Show), asked by the policy of the source the query names. It was a union written
// by hand, each branch's rights asked by hand.
//
// FIELD FOR FIELD over that union. Each journal column takes its fields by role from the output of its name: a union's,
// called what the statement calls it (ibSqlAliasOf) and of the kinds of every branch (ibComposedQueryColumn), as the
// journal's type is the documents' kinds joined (Widen); one document's, its own column. The document's type has no
// field to write: it is the reference's type field, read as a Type (ibTypeColumn).
ibQueryRelPtr ibDocumentJournalQueryable::GetSourceRelation(const ibString& alias) const
{
	if (m_documents.empty())
		return nullptr;

	const std::vector<std::shared_ptr<const ibBackendQueryColumn>> columns = GetQueryColumns();

	ibDataQueryBuilder rows;
	rows.WithAccessPolicy(nullptr);   // the policy is each branch's: the union reads nothing of its own
	std::shared_ptr<const ibSubqueryQueryable> first;
	for (const ibValueMetaObjectDocument* doc : m_documents) {
		ibDataQueryBuilder read;   // the session's connection and policy - what any read of the document carries
		read.Allowed().From(doc->GetQueryable());
		for (size_t c = 0; c < columns.size(); ++c) {
			if (columns[c]->IsSyntheticColumn())
				continue;   // the document's type - its field is the reference's, read with it
			const std::shared_ptr<const ibBackendQueryColumn> own = DocumentColumn(doc, s_columns[c])->GetQueryColumn();
			read.Select(own, own->GetName());
		}
		const std::shared_ptr<const ibSubqueryQueryable> branch = std::make_shared<ibSubqueryQueryable>(read);
		if (first == nullptr) {
			rows.From(branch);
			first = branch;
		}
		else
			rows.Union(branch, ibEmptyString, /*keepDuplicates*/ true);
	}
	// The union's outputs are the first branch's columns, under the journal's names.
	const std::vector<std::shared_ptr<const ibBackendQueryColumn>> outputs = first->GetQueryColumns();
	std::vector<std::shared_ptr<const ibBackendQueryColumn>> outputOf(columns.size());
	for (size_t c = 0, at = 0; c < columns.size() && at < outputs.size(); ++c)
		if (!columns[c]->IsSyntheticColumn()) {
			outputOf[c] = outputs[at++];
			rows.Select(outputOf[c], columns[c]->GetName());
		}

	const ibQueryRelPtr united = rows.BuildRelation();
	if (united == nullptr)
		ibBackendQueryException::Throw(ibBackendQueryException::Kind::TranslationFailure, ibString::Format(
			_("the documents of journal '%s' are not read by one statement of its base"), m_meta->GetName()));

	const ibString inner = ibT("u");
	const bool stacked = m_documents.size() > 1;
	ibValue absent;
	absent.SetType(ibValueTypes::TYPE_NULL);
	std::vector<ibQueryProjItem> fields;
	for (size_t c = 0; c < columns.size(); ++c) {
		if (outputOf[c] == nullptr)
			continue;
		const std::vector<ibColumnSlot> from = DescribeColumnLayout(*outputOf[c]);
		for (const ibColumnSlot& slot : DescribeColumnLayout(*columns[c])) {
			if (stacked) {
				fields.push_back({ ibCol(inner, ibSqlAliasOf(columns[c]->GetName()) + ibFieldSuffix(slot.m_role)), slot.m_name });
				continue;
			}
			const auto same = std::find_if(from.begin(), from.end(),
				[&slot](const ibColumnSlot& candidate) { return candidate.m_role == slot.m_role; });
			fields.push_back({ same != from.end() ? ibCol(inner, same->m_name) : ibCast(ibConst(absent), slot.m_type), slot.m_name });
		}
	}
	return ibSubquery(ibProject(ibSubquery(united, inner), std::move(fields)), alias);
}

//***********************************************************************
//*                    what the factory knows it by                     *
//***********************************************************************

// The columns are made with the journal and stay as it does: what each answers is asked of the reading at the time.
ibDocumentJournalSource::ibDocumentJournalSource(const ibValueMetaObjectDocumentJournal* meta) : m_meta(meta)
{
	m_columns.reserve(std::size(s_columns));
	for (const ibJournalColumn which : s_columns)
		m_columns.emplace_back(meta, JournalColumnId(which));
}

ibString ibDocumentJournalSource::GetNamespace() const { return ibValue::GetNameObjectFromID(m_meta->GetClassType()); }
ibString ibDocumentJournalSource::GetName() const { return m_meta->GetName(); }

std::shared_ptr<const ibBackendQueryable> ibDocumentJournalSource::CreateQueryable(ibValue** /*paParams*/, long /*lSizeArray*/,
	const std::vector<ibQueryPredicatePtr>& /*conditions*/, const ibQueryReadColumns& /*read*/)
{
	return m_meta->GetQueryable();
}

std::vector<std::shared_ptr<const ibBackendQueryColumn>> ibDocumentJournalSource::GetQueryColumns(const std::vector<ibValue>& /*args*/) const
{
	return m_meta->GetQueryable()->GetQueryColumns();
}

// The list's columns are the journal's own - named, captioned and typed as its reading publishes them, so the
// reference is a reference to any document listed - the reference and the deletion mark hidden, as a document's
// list hides them.
void ibDocumentJournalSource::FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const
{
	for (const ibColumn& column : m_columns) {
		const bool hidden = column.GetColumnId() == JournalColumnId(eJournalRef) || column.GetColumnId() == JournalColumnId(eJournalDeletionMark);
		explorer.AppendColumn(&column, column.GetColumnId(), /*enabled*/ true, /*visible*/ !hidden);
	}
}

// The reading's column of this id, asked afresh: the journal makes another reading when its documents change.
static std::shared_ptr<const ibBackendQueryColumn> ReadingColumn(const ibValueMetaObjectDocumentJournal* meta, ibMetaID id)
{
	for (const std::shared_ptr<const ibBackendQueryColumn>& column : meta->GetQueryable()->GetQueryColumns())
		if (column->GetColumnId() == id)
			return column;
	ibFAIL_MSG(ibT("a journal column the reading does not publish"));
	return nullptr;
}

ibString ibDocumentJournalSource::ibColumn::GetName() const { return ReadingColumn(m_meta, m_id)->GetName(); }
ibString ibDocumentJournalSource::ibColumn::GetSynonym() const { return ReadingColumn(m_meta, m_id)->GetSynonym(); }
// The column is the journal's reading's, and the journal holds it: the description outlives this call.
ibTypeDescription& ibDocumentJournalSource::ibColumn::GetTypeDesc() const { return ReadingColumn(m_meta, m_id)->GetTypeDesc(); }

// The document a row is: the kind its reference names, and the key that document's own list knows the row by
// (its guid). Null when the reference names nothing.
static const ibValueMetaObjectDocument* DocumentOf(const ibValue& ref, ibUniqueKey& documentKey)
{
	ibValueReferenceDataObject* reference = nullptr;
	if (!ref.ConvertToValue(reference) || reference == nullptr)
		return nullptr;
	const ibValueMetaObjectDocument* document = nullptr;
	if (reference->GetMetaObject() == nullptr || !reference->GetMetaObject()->ConvertToValue(document) || document == nullptr)
		return nullptr;
	documentKey = ibUniqueKey(reference->GetGuid());
	return document;
}

// A row picks the document it is.
ibValue ibDocumentJournalSource::GetSelectValue(const ibRowMetaValues& rowValues) const
{
	const ibValue* ref = rowValues.find_value(JournalColumnId(eJournalRef));
	return ref != nullptr ? *ref : ibValue();
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

// …and is drawn as its document draws it: the document reads its marks by its own ids, so the row's are handed
// over under them.
ibPictureID ibDocumentJournalSource::GetRowPicture(const ibRowMetaValues& rowValues) const
{
	ibUniqueKey documentKey;
	const ibValueMetaObjectDocument* const document = DocumentOf(GetSelectValue(rowValues), documentKey);
	if (document == nullptr)
		return 0;
	ibRowMetaValues own;
	for (const ibJournalColumn which : { eJournalPosted, eJournalDeletionMark })
		if (const ibValue* value = rowValues.find_value(JournalColumnId(which)))
			own[DocumentColumn(document, which)->GetMetaID()] = *value;
	return document->GetRowPicture(own);
}

// A document's list's commands - every document's are the same, so the first one listed says them. Asked with the
// journal's form id, which a document's set does not depend on.
void ibDocumentJournalSource::GetCommandCollection(const ibFormID& formType, std::vector<ibCommandItem>& commands) const
{
	const std::vector<const ibValueMetaObjectDocument*> documents = m_meta->GetDocuments();
	if (!documents.empty())
		documents.front()->GetCommandCollection(formType, commands);
}

// The document a new row is: the one listed, or the one the person picks among them. Null - cancelled.
static const ibValueMetaObjectDocument* ChooseDocument(const std::vector<const ibValueMetaObjectDocument*>& documents)
{
	if (documents.size() < 2)
		return documents.empty() ? nullptr : documents.front();

	std::vector<ibChoiceItem> items;
	for (size_t idx = 0; idx < documents.size(); ++idx)
		items.push_back(ibChoiceItem{ static_cast<std::int32_t>(idx), documents[idx]->GetSynonym(),
			ibPictureDescription(documents[idx]->GetClassType()) });
	std::int32_t chosen = 0;
	return ibRequestChoice(_("Select document type"), items, chosen) ? documents[static_cast<size_t>(chosen)] : nullptr;
}

// ⭐ A ROW'S COMMAND IS RUN BY THE ROW'S DOCUMENT, as its own list runs it - opened, copied, posted, marked or
// deleted there, by its key there. Add has no row yet: the document it adds is asked for first.
void ibDocumentJournalSource::CallAsCommand(ibActionID id, const ibUniqueKey& /*anchor*/, const ibUniqueKey& key, ibBackendValueForm* srcForm) const
{
	if (id == ibValueMetaObjectDocument::eAddValue) {
		if (const ibValueMetaObjectDocument* const document = ChooseDocument(m_meta->GetDocuments()))
			document->CallAsCommand(id, ibUniqueKey(), ibUniqueKey(), srcForm);
		return;
	}
	ibUniqueKey documentKey;
	if (const ibValueMetaObjectDocument* const document = DocumentOf(key.GetKey(JournalColumnId(eJournalRef)), documentKey))
		document->CallAsCommand(id, ibUniqueKey(), documentKey, srcForm);
}

// Opening a row opens the document, by the road that document's own list takes.
void ibDocumentJournalSource::ShowValueByKey(const ibUniqueKey& key, ibBackendValueForm* srcForm) const
{
	ibUniqueKey documentKey;
	if (const ibValueMetaObjectDocument* const document = DocumentOf(key.GetKey(JournalColumnId(eJournalRef)), documentKey))
		document->ShowValueByKey(documentKey, srcForm);
}
