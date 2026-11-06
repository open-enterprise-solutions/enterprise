////////////////////////////////////////////////////////////////////////////
//	Description : the base's full-text index - its tables, its write, its build
////////////////////////////////////////////////////////////////////////////

#include "fullTextIndex.h"
#include "fullTextTokenizer.h"

#include "backend/appData.h"
#include "backend/backend_exception.h"
#include "backend/databaseLayer/connectionScope.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"
#include "backend/metaData.h"
#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/partial/commonObject.h"
#include "backend/metaCollection/partial/tabularSection/tabularSection.h"
#include "backend/metaCollection/partial/reference/reference.h"
#include "backend/query/dataQueryBuilder.h"
#include "backend/session/session.h"
#include "backend/settings/settingsStorage.h"
#include "core/serialize/dataBuilder.h"

#include <algorithm>
#include <map>
#include <set>

namespace {

// THE ROW OF THE MODE - one per base, no user: a well-known object key, as the base's locale has one.
const ibGuid& ModeObjectKey()
{
	static const ibGuid key(ibT("66756c6c-7465-7874-0000-000000000001"));
	return key;
}

const ibString kModeField = ibT("mode");

// How many keys one IN list carries.
constexpr std::size_t kInChunk = 200;

bool IsMode(std::int32_t number)
{
	return number == ibFullTextSearchMode_Enable || number == ibFullTextSearchMode_Disable;
}

ibSettingsStorage& Storage()
{
	ibSettingsStorage* const storage = ibApplicationInstance::GetSettingsStorage();
	if (storage == nullptr)
		ibBackendCoreException::Error(_("FullTextSearch: no base is open, so there is no full-text search mode to read or set"));
	return *storage;
}

ibString RecordKey(const ibValueRecordDataObjectRef& object)
{
	return object.GetGuid().GetGuid().str();
}

// The attributes as the state row keeps them: their ids, sorted, joined by commas.
ibString Signature(const std::vector<const ibValueMetaObjectAttributeBase*>& attributes)
{
	std::vector<ibMetaID> ids;
	ids.reserve(attributes.size());
	for (const ibValueMetaObjectAttributeBase* attribute : attributes)
		ids.push_back(attribute->GetMetaID());
	std::sort(ids.begin(), ids.end());
	ibString signature;
	for (const ibMetaID id : ids) {
		if (!signature.IsEmpty())
			signature << ibT(",");
		signature << ibString::Format(ibT("%d"), id);
	}
	return signature;
}

ibQueryExprPtr RecordPredicate(ibMetaID metaId, const ibString& recordKey)
{
	return ibBinOp(ibQueryBinOp::And,
		ibBinOp(ibQueryBinOp::Eq, ibCol(ibT("metaId")), ibConst(ibValue(static_cast<signed int>(metaId)))),
		ibBinOp(ibQueryBinOp::Eq, ibCol(ibT("recordKey")), ibConst(ibValue(recordKey))));
}

ibQueryExprPtr MetaPredicate(ibMetaID metaId)
{
	return ibBinOp(ibQueryBinOp::Eq, ibCol(ibT("metaId")), ibConst(ibValue(static_cast<signed int>(metaId))));
}

// The words of a text, counted per (word, attribute).
void Collect(const ibValue& value, ibMetaID attributeId, std::map<std::pair<ibString, ibMetaID>, int>& hits)
{
	if (value.GetType() != ibValueTypes::TYPE_STRING)
		return;
	for (const ibString& word : ibFullTextTokenizer::Split(value.GetString()))
		++hits[{ word, attributeId }];
}

// The words the dictionary does not hold yet go in. An upsert, not an insert: two writes that bring the same new
// word at once must both succeed, and PostgreSQL ends the whole transaction at a refused key.
void AddWords(const std::set<ibString>& words)
{
	if (words.empty())
		return;
	std::set<ibString> known;
	std::vector<ibQueryExprPtr> chunk;
	auto flushKnown = [&] {
		if (chunk.empty())
			return;
		ibDatabaseQueryBuilder q;   // a builder keeps what it was told: one per query
		ibQueryResult rs = q.From(search_word_table)
			.Select({ ibT("word") })
			.Where(ibIn(ibCol(ibT("word")), chunk))
			.Execute();
		while (rs.Next())
			known.insert(rs.GetValue(1).GetString());
		chunk.clear();
	};
	for (const ibString& word : words) {
		chunk.push_back(ibConst(ibValue(word)));
		if (chunk.size() == kInChunk)
			flushKnown();
	}
	flushKnown();

	ibDatabaseQueryBuilder q;
	for (const ibString& word : words) {
		if (known.count(word) != 0)
			continue;
		q.Execute(ibUpsert(search_word_table, {
			{ ibT("word"),       ibConst(ibValue(word)) },
			{ ibT("wordLength"), ibConst(ibValue(static_cast<signed int>(word.Len()))) },
		}, { ibT("word") }));
	}
}

struct ibStateRow {
	ibString  m_attributes;
	bool      m_complete = false;
	ibString  m_cursorKey;
	long long m_indexed = 0;
	ibDateTime m_changed;
};

std::map<ibMetaID, ibStateRow> ReadStates()
{
	std::map<ibMetaID, ibStateRow> states;
	ibDatabaseQueryBuilder q;
	ibQueryResult rs = q.From(search_state_table)
		.Select({ ibT("metaId"), ibT("attributes"), ibT("complete"), ibT("cursorKey"), ibT("indexed"), ibT("changed") })
		.Execute();
	while (rs.Next()) {
		ibStateRow row;
		row.m_attributes = rs.GetValue(2).GetString();
		row.m_complete = rs.GetValue(3).GetInteger() != 0;
		row.m_cursorKey = rs.GetValue(4).GetString();
		row.m_indexed = rs.GetValue(5).GetInteger();
		const ibValue changed = rs.GetValue(6);
		if (!changed.IsNull() && !changed.IsEmpty())
			row.m_changed = changed.GetDate();
		states[static_cast<ibMetaID>(rs.GetValue(1).GetInteger())] = row;
	}
	return states;
}

void WriteState(ibMetaID metaId, const ibStateRow& row)
{
	ibDatabaseQueryBuilder q;
	q.Execute(ibUpsert(search_state_table, {
		{ ibT("metaId"),     ibConst(ibValue(static_cast<signed int>(metaId))) },
		{ ibT("attributes"), ibConst(ibValue(row.m_attributes)) },
		{ ibT("complete"),   ibConst(ibValue(static_cast<signed int>(row.m_complete ? 1 : 0))) },
		{ ibT("cursorKey"),  ibConst(ibValue(row.m_cursorKey)) },
		{ ibT("indexed"),    ibConst(ibValue(static_cast<signed int>(row.m_indexed))) },
		{ ibT("changed"),    ibConst(row.m_changed.IsEmpty() ? ibValue() : ibValue(row.m_changed)) },
	}, { ibT("metaId") }));
}

void DropMeta(ibMetaID metaId)
{
	ibDatabaseQueryBuilder q;
	q.Execute(ibDelete(search_entry_table, MetaPredicate(metaId)));
	q.Execute(ibDelete(search_state_table, MetaPredicate(metaId)));
}

// The metaobjects the index is for - every one with an indexed attribute - with their attributes.
std::vector<std::pair<const ibValueMetaObjectRecordDataMutableRef*, std::vector<const ibValueMetaObjectAttributeBase*>>>
IndexedMetaObjects(const ibMetaData* metaData)
{
	std::vector<std::pair<const ibValueMetaObjectRecordDataMutableRef*, std::vector<const ibValueMetaObjectAttributeBase*>>> result;
	if (metaData == nullptr)
		return result;
	for (const ibValueMetaObjectRecordDataMutableRef* metaObject : metaData->GetAnyArrayObject<ibValueMetaObjectRecordDataMutableRef>()) {
		auto attributes = ibFullTextIndex::GetIndexedAttributes(metaObject);
		if (!attributes.empty())
			result.emplace_back(metaObject, std::move(attributes));
	}
	return result;
}

long long CountRecords(const ibValueMetaObjectRecordDataMutableRef* metaObject)
{
	ibDataQueryBuilder b;
	b.WithAccessPolicy(nullptr).From(metaObject->GetQueryable());
	b.Count(ibT("cnt"));
	ibDataQueryResult r = b.SelectAggregate();
	return r.Next() ? r.GetColumn(ibT("cnt")).GetInteger() : 0;
}

} // namespace

///////////////////////////////////////////////////////////////////////////////

void ibFullTextIndex::CreateTables()
{
	ibDatabaseQueryBuilder q;
	if (!q.TableExists(search_word_table)) {
		q.Execute(ibCreateTable(search_word_table, {
			{ ibT("word"),       ibTypeString(static_cast<int>(ibFullTextTokenizer::kMaxWordLength)), true, true,  ibEmptyString },
			{ ibT("wordLength"), ibTypeInteger(), true, false, ibEmptyString },   // the fuzzy match reads by it
		}));
		q.Execute(ibCreateIndex(search_word_table, ibT("search_word_index_1"), { ibT("wordLength") }));
	}
	if (!q.TableExists(search_entry_table)) {
		q.Execute(ibCreateTable(search_entry_table, {
			{ ibT("word"),        ibTypeString(static_cast<int>(ibFullTextTokenizer::kMaxWordLength)), true, true, ibEmptyString },
			{ ibT("metaId"),      ibTypeInteger(),    true, true,  ibEmptyString },
			{ ibT("recordKey"),   ibTypeString(36),   true, true,  ibEmptyString },   // the record's guid
			{ ibT("attributeId"), ibTypeInteger(),    true, true,  ibEmptyString },
			{ ibT("hits"),        ibTypeInteger(),    true, false, ibEmptyString },   // how many times the word stands there
		}));
		// A write replaces one record's rows: found by the record, not by the word the key starts with.
		q.Execute(ibCreateIndex(search_entry_table, ibT("search_entry_index_1"), { ibT("metaId"), ibT("recordKey") }));
	}
	if (!q.TableExists(search_state_table)) {
		q.Execute(ibCreateTable(search_state_table, {
			{ ibT("metaId"),     ibTypeInteger(),    true,  true,  ibEmptyString },
			{ ibT("attributes"), ibTypeString(1024), true,  false, ibEmptyString },   // the attribute ids it was built with
			{ ibT("complete"),   ibTypeInteger(),    true,  false, ibEmptyString },
			{ ibT("cursorKey"),  ibTypeString(36),   true,  false, ibT("''") },       // the last record a build indexed
			{ ibT("indexed"),    ibTypeInteger(),    true,  false, ibEmptyString },
			{ ibT("changed"),    ibTypeDate(),       false, false, ibEmptyString },   // when the build completed
		}));
	}
}

///////////////////////////////////////////////////////////////////////////////

ibFullTextSearchMode ibFullTextIndex::GetMode()
{
	ibDataNode row;
	if (!Storage().Restore(ibSettingsKey(ibSettingsCategory::Search, ModeObjectKey()), row))
		return ibFullTextSearchMode_Disable;   // nobody switched it on: the base has no search

	std::int32_t number = 0;
	row.GetValue(kModeField, number);
	if (!IsMode(number))
		ibBackendCoreException::Error(_("FullTextSearch: the base keeps a full-text search mode this build does not know (%d)"), number);
	return static_cast<ibFullTextSearchMode>(number);
}

void ibFullTextIndex::SetMode(ibFullTextSearchMode mode)
{
	if (!IsMode(mode))
		ibBackendCoreException::Error(_("FullTextSearch.SetFullTextSearchMode: the mode is a FullTextSearchMode value - FullTextSearchMode.Enable or FullTextSearchMode.Disable"));

	ibDataNode row;
	row.SetValue(kModeField, static_cast<std::int32_t>(mode));
	if (!Storage().Save(ibSettingsKey(ibSettingsCategory::Search, ModeObjectKey()), row))
		ibBackendCoreException::Error(_("FullTextSearch.SetFullTextSearchMode: the mode was not saved in the base (the technology journal says why)"));

	// Switched off, no write keeps the index from now on - so what it holds would only grow wrong.
	if (mode == ibFullTextSearchMode_Disable)
		Clear();
}

///////////////////////////////////////////////////////////////////////////////

std::vector<const ibValueMetaObjectAttributeBase*> ibFullTextIndex::GetIndexedAttributes(const ibValueMetaObjectRecordDataMutableRef* metaObject)
{
	std::vector<const ibValueMetaObjectAttributeBase*> attributes;
	if (metaObject == nullptr)
		return attributes;
	for (const ibValueMetaObjectAttributeBase* attribute : metaObject->GetGenericAttributeArrayObject()) {
		if (attribute->IsFullTextSearch())
			attributes.push_back(attribute);
	}
	for (const ibValueMetaObjectTableData* table : metaObject->GetGenericTableArrayObject()) {
		for (const ibValueMetaObjectAttributeBase* attribute : table->GetGenericAttributeArrayObject()) {
			if (attribute->IsFullTextSearch())
				attributes.push_back(attribute);
		}
	}
	return attributes;
}

void ibFullTextIndex::IndexRecord(const ibValueRecordDataObjectRef& object)
{
	const ibValueMetaObjectRecordDataMutableRef* const metaObject = object.GetMetaObject();
	if (metaObject == nullptr)
		return;

	// The own attributes and the tabular sections', apart: a section's value is read once for all of its own.
	std::vector<const ibValueMetaObjectAttributeBase*> own;
	std::vector<std::pair<const ibValueMetaObjectTableData*, std::vector<const ibValueMetaObjectAttributeBase*>>> tables;
	for (const ibValueMetaObjectAttributeBase* attribute : metaObject->GetGenericAttributeArrayObject()) {
		if (attribute->IsFullTextSearch())
			own.push_back(attribute);
	}
	for (const ibValueMetaObjectTableData* table : metaObject->GetGenericTableArrayObject()) {
		std::vector<const ibValueMetaObjectAttributeBase*> columns;
		for (const ibValueMetaObjectAttributeBase* attribute : table->GetGenericAttributeArrayObject()) {
			if (attribute->IsFullTextSearch())
				columns.push_back(attribute);
		}
		if (!columns.empty())
			tables.emplace_back(table, std::move(columns));
	}
	if (own.empty() && tables.empty())
		return;
	if (GetMode() != ibFullTextSearchMode_Enable)
		return;

	std::map<std::pair<ibString, ibMetaID>, int> hits;
	for (const ibValueMetaObjectAttributeBase* attribute : own)
		Collect(object.GetValueByMetaID(attribute->GetMetaID()), attribute->GetMetaID(), hits);
	for (const auto& [table, columns] : tables) {
		ibValueTabularSectionDataObjectBase* section = nullptr;
		if (!object.GetValueByMetaID(table->GetMetaID()).ConvertToValue(section) || section == nullptr)
			continue;
		for (long row = 0; row < section->GetRowCount(); ++row) {
			const ibComposerNode* node = section->GetViewData<ibComposerNode>(section->GetItem(row));
			if (node == nullptr)
				continue;
			for (const ibValueMetaObjectAttributeBase* attribute : columns)
				Collect(node->GetTableValue(attribute->GetMetaID()), attribute->GetMetaID(), hits);
		}
	}

	const ibMetaID metaId = metaObject->GetMetaID();
	const ibString recordKey = RecordKey(object);

	ibDatabaseQueryBuilder q;
	q.Execute(ibDelete(search_entry_table, RecordPredicate(metaId, recordKey)));
	if (hits.empty())
		return;

	std::set<ibString> words;
	for (const auto& entry : hits)
		words.insert(entry.first.first);
	AddWords(words);

	ibBatchInsert batch(search_entry_table, { ibT("word"), ibT("metaId"), ibT("recordKey"), ibT("attributeId"), ibT("hits") });
	for (const auto& [key, count] : hits) {
		batch.SetParamString(1, key.first);
		batch.SetParamInt(2, metaId);
		batch.SetParamString(3, recordKey);
		batch.SetParamInt(4, key.second);
		batch.SetParamInt(5, count);
		if (batch.RunQuery() < 0)
			ibBackendCoreException::Error(_("%s: the full-text index of the record was not written"), object.GetSourceCaption());
	}
	if (batch.Flush() < 0)
		ibBackendCoreException::Error(_("%s: the full-text index of the record was not written"), object.GetSourceCaption());
}

void ibFullTextIndex::IndexStoredRecord(const ibValueMetaObjectRecordDataMutableRef* metaObject, const ibGuid& key)
{
	// Read as it is stored, its module not run: a build reads data, it runs no code of the configuration. A record
	// that is not there any more (deleted since its key was read) has nothing to index, and its rows are dropped.
	const ibValuePtr<ibValueRecordDataObjectRef> object = metaObject->CreateObjectRefValue(key);
	if (!object)
		ibBackendCoreException::Error(_("FullTextSearch.UpdateIndex: %s %s could not be made to read its words"),
			metaObject->GetFullName(), key.str());
	if (object->ReadData())
		IndexRecord(*object);
	else
		DropRecord(metaObject, key);
}

void ibFullTextIndex::DropRecord(const ibValueRecordDataObjectRef& object)
{
	if (object.GetMetaObject() != nullptr)
		DropRecord(object.GetMetaObject(), object.GetGuid().GetGuid());
}

void ibFullTextIndex::DropRecord(const ibValueMetaObjectRecordDataMutableRef* metaObject, const ibGuid& key)
{
	if (metaObject == nullptr || GetMode() != ibFullTextSearchMode_Enable)
		return;
	// Not asked whether the metaobject has indexed attributes now: rows written while it had are dropped as well.
	ibDatabaseQueryBuilder q;
	q.Execute(ibDelete(search_entry_table, RecordPredicate(metaObject->GetMetaID(), key.str())));
}

///////////////////////////////////////////////////////////////////////////////

bool ibFullTextIndex::IsIndexTrue(const ibMetaData* metaData)
{
	if (GetMode() != ibFullTextSearchMode_Enable)
		return false;
	const auto metaObjects = IndexedMetaObjects(metaData);
	const auto states = ReadStates();
	std::set<ibMetaID> wanted;
	for (const auto& [metaObject, attributes] : metaObjects) {
		wanted.insert(metaObject->GetMetaID());
		const auto it = states.find(metaObject->GetMetaID());
		if (it == states.end() || !it->second.m_complete || it->second.m_attributes != Signature(attributes))
			return false;
	}
	for (const auto& [metaId, row] : states) {
		if (wanted.count(metaId) == 0)
			return false;   // built for attributes that are not indexed any more
	}
	return true;
}

ibFullTextIndex::ibProgress ibFullTextIndex::Update(const ibMetaData* metaData, bool portion)
{
	if (GetMode() != ibFullTextSearchMode_Enable)
		ibBackendCoreException::Error(_("FullTextSearch.UpdateIndex: the full-text search of this base is switched off - there is no index to build"));
	if (metaData == nullptr)
		ibBackendCoreException::Error(_("FullTextSearch.UpdateIndex: no configuration is open, so there is nothing to index"));

	const auto metaObjects = IndexedMetaObjects(metaData);
	auto states = ReadStates();

	// What the index holds for a metaobject that has no indexed attribute any more (or is gone) is dropped.
	{
		ibConnectionScope scope;   // the calling session's connection, as the write's own
		scope.SafeBeginTransaction();
		std::set<ibMetaID> wanted;
		for (const auto& [metaObject, attributes] : metaObjects)
			wanted.insert(metaObject->GetMetaID());
		for (auto it = states.begin(); it != states.end();) {
			if (wanted.count(it->first) == 0) {
				DropMeta(it->first);
				it = states.erase(it);
			}
			else
				++it;
		}
		// …and one built with other attributes starts again.
		for (const auto& [metaObject, attributes] : metaObjects) {
			const ibString signature = Signature(attributes);
			const auto it = states.find(metaObject->GetMetaID());
			if (it != states.end() && it->second.m_attributes == signature)
				continue;
			ibDatabaseQueryBuilder().Execute(ibDelete(search_entry_table, MetaPredicate(metaObject->GetMetaID())));
			ibStateRow row;
			row.m_attributes = signature;
			WriteState(metaObject->GetMetaID(), row);
			states[metaObject->GetMetaID()] = row;
		}
		scope.SafeCommitTransaction();
	}

	for (const auto& [metaObject, attributes] : metaObjects) {
		ibStateRow& state = states[metaObject->GetMetaID()];
		while (!state.m_complete) {
			// THE NEXT PORTION, after the last record indexed - by the record's key, so a record written meanwhile is
			// either still ahead (and read) or behind (and indexed by its own write).
			std::vector<ibGuid> keys;
			{
				ibDataQueryBuilder q;
				q.WithAccessPolicy(nullptr).From(metaObject->GetQueryable()).OrderBy(static_cast<const ibBackendQueryColumn*>(nullptr), true);
				ibReadPageRequest page;
				page.m_count = kPortionSize;
				if (!state.m_cursorKey.IsEmpty()) {
					page.m_hasAnchor = true;
					page.m_anchorSortValues.push_back(ibValue(ibValueReferenceDataObject::Create(metaObject, ibGuid(state.m_cursorKey), ibReferenceLoad::OnDemand)));
				}
				ibDataQueryResult rs = q.Execute(page);
				const ibBackendQueryColumn* const referenceColumn = metaObject->GetDataReference()->GetQueryColumn();
				while (rs.Next() && static_cast<int>(keys.size()) < kPortionSize) {
					ibValueReferenceDataObject* reference = nullptr;
					const ibValue value = rs.GetValue(referenceColumn);
					if (value.ConvertToValue(reference) && reference != nullptr)
						keys.push_back(reference->GetGuid());
				}
			}

			ibConnectionScope scope;   // the calling session's connection, as the write's own
			scope.SafeBeginTransaction();
			for (const ibGuid& key : keys)
				IndexStoredRecord(metaObject, key);
			state.m_indexed += static_cast<long long>(keys.size());
			if (!keys.empty())
				state.m_cursorKey = keys.back().str();
			if (static_cast<int>(keys.size()) < kPortionSize) {
				state.m_complete = true;
				state.m_changed = ibDateTime::Now();
			}
			WriteState(metaObject->GetMetaID(), state);
			scope.SafeCommitTransaction();

			if (portion)
				return GetProgress(metaData);
		}
	}
	return GetProgress(metaData);
}

ibFullTextIndex::ibProgress ibFullTextIndex::GetProgress(const ibMetaData* metaData)
{
	ibProgress progress;
	const auto states = ReadStates();
	for (const auto& [metaObject, attributes] : IndexedMetaObjects(metaData)) {
		const long long total = CountRecords(metaObject);
		progress.m_total += total;
		const auto it = states.find(metaObject->GetMetaID());
		if (it == states.end() || it->second.m_attributes != Signature(attributes))
			continue;
		progress.m_indexed += it->second.m_complete ? total : std::min(total, it->second.m_indexed);
	}
	return progress;
}

void ibFullTextIndex::Clear()
{
	ibConnectionScope scope;   // the calling session's connection, as the write's own
	scope.SafeBeginTransaction();
	ibDatabaseQueryBuilder q;
	q.Execute(ibDelete(search_entry_table));
	q.Execute(ibDelete(search_word_table));
	q.Execute(ibDelete(search_state_table));
	scope.SafeCommitTransaction();
}

ibDateTime ibFullTextIndex::GetUpdateDate(const ibMetaData* metaData)
{
	if (!IsIndexTrue(metaData))
		return ibDateTime();
	ibDateTime latest;
	for (const auto& [metaId, row] : ReadStates()) {
		if (latest.IsEmpty() || row.m_changed > latest)
			latest = row.m_changed;
	}
	return latest;
}

///////////////////////////////////////////////////////////////////////////////

namespace {

// The dictionary's words a query word stands for: those it begins (strict), and those within the threshold of it or of
// their own beginning (fuzzy) - the dictionary is read once for every word of the query.
struct ibWordMatch {
	std::set<ibString> m_strict;
	std::set<ibString> m_fuzzy;
};

// Per record: the hits of one query word.
using ibRecordHits = std::map<std::pair<ibMetaID, ibString>, long long>;

void SumHits(const std::set<ibString>& words, const std::vector<ibQueryExprPtr>& metaIds, long long weight, ibRecordHits& hits)
{
	std::vector<ibQueryExprPtr> chunk;
	auto flush = [&] {
		if (chunk.empty())
			return;
		ibDatabaseQueryBuilder q;
		ibQueryResult rs = q.From(search_entry_table)
			.Project({
				{ ibCol(ibT("metaId")),    ibT("metaId") },
				{ ibCol(ibT("recordKey")), ibT("recordKey") },
				{ ibFunc(ibT("SUM"), { ibCol(ibT("hits")) }), ibT("hits") },
			})
			.Where(ibIn(ibCol(ibT("word")), chunk))
			.Where(ibIn(ibCol(ibT("metaId")), metaIds))
			.GroupBy(ibCol(ibT("metaId")))
			.GroupBy(ibCol(ibT("recordKey")))
			.Execute();
		while (rs.Next()) {
			const std::pair<ibMetaID, ibString> key(static_cast<ibMetaID>(rs.GetValue(1).GetInteger()), rs.GetValue(2).GetString());
			hits[key] += weight * static_cast<long long>(rs.GetValue(3).GetInteger());
		}
		chunk.clear();
	};
	for (const ibString& word : words) {
		chunk.push_back(ibConst(ibValue(word)));
		if (chunk.size() == kInChunk)
			flush();
	}
	flush();
}

} // namespace

std::vector<ibFullTextIndex::ibHit> ibFullTextIndex::Search(const ibMetaData* metaData, const ibString& text, int fuzzyThreshold)
{
	if (GetMode() != ibFullTextSearchMode_Enable)
		ibBackendCoreException::Error(_("FullTextSearch: the full-text search of this base is switched off"));
	if (metaData == nullptr)
		ibBackendCoreException::Error(_("FullTextSearch: no configuration is open, so there is nothing to search in"));
	if (fuzzyThreshold < 0 || fuzzyThreshold > 50)
		ibBackendCoreException::Error(_("FullTextSearch: the fuzzy threshold is a share of a word's letters from 0 to 50 percent, not %d"), fuzzyThreshold);

	std::vector<ibString> query = ibFullTextTokenizer::Split(text);
	std::sort(query.begin(), query.end());
	query.erase(std::unique(query.begin(), query.end()), query.end());
	if (query.empty())
		ibBackendCoreException::Error(_("FullTextSearch: \"%s\" has no word to search by"), text);

	// THE AREA: the metaobjects with indexed attributes the person may read.
	std::map<ibMetaID, const ibValueMetaObjectRecordDataMutableRef*> area;
	std::vector<ibQueryExprPtr> metaIds;
	for (const auto& [metaObject, attributes] : IndexedMetaObjects(metaData)) {
		if (!metaObject->AccessRight_Read())
			continue;
		area[metaObject->GetMetaID()] = metaObject;
		metaIds.push_back(ibConst(ibValue(static_cast<signed int>(metaObject->GetMetaID()))));
	}
	std::vector<ibHit> found;
	if (area.empty())
		return found;

	// THE WORDS each query word stands for.
	std::vector<ibWordMatch> matches(query.size());
	std::vector<std::size_t> limits(query.size());
	std::size_t shortest = ibFullTextTokenizer::kMaxWordLength;
	bool fuzzy = false;
	for (std::size_t i = 0; i < query.size(); ++i) {
		limits[i] = query[i].Len() * static_cast<std::size_t>(fuzzyThreshold) / 100;
		if (limits[i] > 0) {
			fuzzy = true;
			shortest = std::min(shortest, query[i].Len() - limits[i]);
		}
	}
	{
		for (std::size_t i = 0; i < query.size(); ++i) {
			ibDatabaseQueryBuilder q;   // a builder keeps what it was told: one per query
			ibQueryResult rs = q.From(search_word_table)
				.Select({ ibT("word") })
				.Where(ibBinOp(ibQueryBinOp::Like, ibCol(ibT("word")), ibConst(ibValue(query[i] + ibT("%")))))
				.Execute();
			while (rs.Next())
				matches[i].m_strict.insert(rs.GetValue(1).GetString());
		}
		if (fuzzy) {
			ibDatabaseQueryBuilder q;
			ibQueryResult rs = q.From(search_word_table)
				.Select({ ibT("word") })
				.Where(ibBinOp(ibQueryBinOp::Ge, ibCol(ibT("wordLength")), ibConst(ibValue(static_cast<signed int>(shortest)))))
				.Execute();
			while (rs.Next()) {
				const ibString word = rs.GetValue(1).GetString();
				for (std::size_t i = 0; i < query.size(); ++i) {
					if (limits[i] == 0 || matches[i].m_strict.count(word) != 0)
						continue;
					const ibString& wanted = query[i];
					if (ibFullTextTokenizer::Distance(wanted, word, limits[i]) <= limits[i]
						|| (word.Len() > wanted.Len() && ibFullTextTokenizer::Distance(wanted, word.Left(wanted.Len()), limits[i]) <= limits[i]))
						matches[i].m_fuzzy.insert(word);
				}
			}
		}
	}

	// THE RECORDS that hold every word.
	ibRecordHits total;
	for (std::size_t i = 0; i < query.size(); ++i) {
		if (matches[i].m_strict.empty() && matches[i].m_fuzzy.empty())
			return found;   // a word nothing stands for: no record holds them all
		ibRecordHits hits;
		SumHits(matches[i].m_strict, metaIds, 2, hits);
		SumHits(matches[i].m_fuzzy, metaIds, 1, hits);
		if (i == 0) {
			total = std::move(hits);
			continue;
		}
		ibRecordHits both;
		for (const auto& [key, rank] : total) {
			const auto it = hits.find(key);
			if (it != hits.end())
				both[key] = rank + it->second;
		}
		total = std::move(both);
		if (total.empty())
			return found;
	}

	// WHAT THE PERSON MAY READ of them: each metaobject's keys read back through its row-level access.
	std::map<ibMetaID, std::vector<ibGuid>> keys;
	for (const auto& [key, rank] : total)
		keys[key.first].push_back(ibGuid(key.second));
	std::set<std::pair<ibMetaID, ibString>> readable;
	for (const auto& [metaId, guids] : keys) {
		const ibValueMetaObjectRecordDataMutableRef* const metaObject = area[metaId];
		const ibBackendQueryColumn* const referenceColumn = metaObject->GetDataReference()->GetQueryColumn();
		for (std::size_t from = 0; from < guids.size(); from += kInChunk) {
			const std::vector<ibGuid> chunk(guids.begin() + from, guids.begin() + std::min(guids.size(), from + kInChunk));
			ibDataQueryBuilder q;
			q.From(metaObject->GetQueryable()).WhereKeyIn(chunk);
			ibReadPageRequest page;
			page.m_count = static_cast<int>(chunk.size());
			ibDataQueryResult rs = q.Execute(page);
			while (rs.Next()) {
				ibValueReferenceDataObject* reference = nullptr;
				const ibValue value = rs.GetValue(referenceColumn);
				if (value.ConvertToValue(reference) && reference != nullptr)
					readable.insert({ metaId, reference->GetGuid().GetGuid().str() });
			}
		}
	}

	for (const auto& [key, rank] : total) {
		if (readable.count(key) == 0)
			continue;
		ibHit hit;
		hit.m_metaObject = area[key.first];
		hit.m_key = ibGuid(key.second);
		hit.m_rank = rank;
		found.push_back(hit);
	}
	std::stable_sort(found.begin(), found.end(), [](const ibHit& lhs, const ibHit& rhs) { return lhs.m_rank > rhs.m_rank; });
	return found;
}
