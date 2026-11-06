#ifndef __FULL_TEXT_INDEX_H__
#define __FULL_TEXT_INDEX_H__

#include "backend/backend_core.h"
#include "backend/system/systemEnum.h"   // ibFullTextSearchMode

#include <vector>

class ibMetaData;
class ibValueMetaObjectAttributeBase;
class ibValueMetaObjectRecordDataMutableRef;
class ibValueRecordDataObjectRef;

// ⭐⭐ THE BASE'S FULL-TEXT INDEX - which records hold which words, kept in the base beside the records themselves.
//
// WHAT IS INDEXED is the metadata's answer, asked when the index is written: an attribute whose *Full-text search*
// property is on (ibValueMetaObjectAttributeBase::IsFullTextSearch), of a catalog, a document or a chart, or of one
// of their tabular sections - and only the strings it holds. Nothing else keeps a list of them.
//
// WHERE: three tables of the base, made through the query builder, so the same on every driver -
//   sys_search_word   the dictionary: every word the index holds, and its length (the fuzzy match reads by it);
//   sys_search_entry  one row per word, record and attribute, with how many times the word stands there;
//   sys_search_state  one row per metaobject the index was BUILT for: with which attributes, whether the build is
//                     complete, how far it got, and when it completed.
// The mode - whether the base has a full-text search at all - is one row of sys_settings (the Search category).
//
// WHEN: inside the record's own write. A write of a catalog item or a document replaces that record's rows in the
// write's transaction (IndexRecord, from ibValueRecordDataObjectRef::SaveData); a delete drops them (DropRecord). So
// the index is never behind its data and there is no second record of what changed. With the mode switched off
// nothing is written, and switching it off empties the index.
//
// THE BUILD is what a write cannot do: index the records written before a flag was switched on, before the mode was,
// or since the index was cleared. Update reads them in portions; IsIndexTrue says whether anything is left to build.
class BACKEND_API ibFullTextIndex {
public:

	// How far a build is: records indexed and records to index, over every metaobject that has indexed attributes.
	struct ibProgress {
		long long m_indexed = 0;
		long long m_total = 0;
	};

	// The tables, made when a base opens (appData.cpp) - idempotent.
	static void CreateTables();

	// THE MODE, the base's. Nothing saved reads as Disable. Switching it needs the Data administration right; switching
	// it off empties the index (no write keeps it from then on), switching it on starts the index empty - Update builds it.
	static ibFullTextSearchMode GetMode();
	static void SetMode(ibFullTextSearchMode mode);

	// The attributes of a metaobject the index holds: its own and its tabular sections', in that order.
	static std::vector<const ibValueMetaObjectAttributeBase*> GetIndexedAttributes(const ibValueMetaObjectRecordDataMutableRef* metaObject);

	// THE WRITE: the record's rows replaced by what it holds now - inside the caller's transaction. Does nothing for a
	// metaobject with no indexed attribute, or with the mode switched off.
	static void IndexRecord(const ibValueRecordDataObjectRef& object);
	// …and the delete: the record's rows dropped.
	static void DropRecord(const ibValueRecordDataObjectRef& object);
	static void DropRecord(const ibValueMetaObjectRecordDataMutableRef* metaObject, const ibGuid& key);
	// A record's rows replaced by what the base stores for it now - read without running its module, as the build
	// reads every record. One no longer stored has its rows dropped.
	static void IndexStoredRecord(const ibValueMetaObjectRecordDataMutableRef* metaObject, const ibGuid& key);

	// THE BUILD. True when every metaobject with indexed attributes is built with exactly those attributes, and
	// nothing is indexed for one that has none any more.
	static bool IsIndexTrue(const ibMetaData* metaData);
	// One portion of records, or every record left - each portion in a transaction of its own. Refused while the mode
	// is off. Answers how far the build is after it.
	static ibProgress Update(const ibMetaData* metaData, bool portion);
	// …how far it is, without building.
	static ibProgress GetProgress(const ibMetaData* metaData);
	// Everything dropped; every metaobject is built again by the next Update.
	static void Clear();
	// When the build last completed - the empty date while it never has, or while it is not complete now.
	static ibDateTime GetUpdateDate(const ibMetaData* metaData);

	// The records a portion of Update reads.
	static constexpr int kPortionSize = 500;

	// ⭐ THE SEARCH. Plain words, no operators: a record is found when it holds every word of the text - each as the
	// beginning of an indexed word (`whe` finds `wheel`) or within the fuzzy threshold of one (`wheell`, `wehel`).
	// The threshold is a share of the word's letters, in percent: at the default a five-letter word may be one edit away.
	// Ranked by how many times the words stand in the record, an exact or prefix match counting twice a fuzzy one.
	//
	// WHAT A PERSON MAY SEE: only metaobjects they may Read, and of their records only those the row-level access
	// lets them read - so the hits are counted AFTER that filter, and the total is the person's. Refused while the
	// mode is off, and for a text with no word in it.
	struct ibHit {
		const ibValueMetaObjectRecordDataMutableRef* m_metaObject = nullptr;
		ibGuid m_key;
		long long m_rank = 0;
	};
	static constexpr int kDefaultFuzzyThreshold = 20;
	static std::vector<ibHit> Search(const ibMetaData* metaData, const ibString& text, int fuzzyThreshold = kDefaultFuzzyThreshold);
};

#endif // !__FULL_TEXT_INDEX_H__
