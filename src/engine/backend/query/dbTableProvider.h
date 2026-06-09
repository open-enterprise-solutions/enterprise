#ifndef __DB_TABLE_PROVIDER_H__
#define __DB_TABLE_PROVIDER_H__

// ibDbTableProvider — the BIG provider: the L3<->L2 connection for a real DB table (vends
// physical paged reads / cached reads / aggregate / write) PLUS the static GET/WRITE
// data-access TEMPLATE — the one place a column's value is lifted from / bound to a DB row.
// It lives in its OWN L2-coupled file (it traffics L2 IR: BuildPageIR -> ibQueryIR), kept off
// the deliberately L2-free queryProvider.h. The lighter providers (RAM / temp) do NOT inherit
// it. (docs/query-language-arc.md §22.4)

#include "queryProvider.h"                                          // ibBackendQueryProvider / ibDataQuerySpec / ibReadPageRequest / ibDataQueryResult
#include "backend/databaseLayer/databaseQueryBuilder.h"             // ibQueryIR / ibQueryResult / ibQuerySortItem / ibRenderedQuery (L2)
#include "backend/metaCollection/attribute/metaAttributeObject.h"   // ibValueMetaObjectAttributeBase::ibFieldTypes (the value-assembly's TYPE-tag enum)

class ibMetaData;   // the metadata context the column-based value-assembly threads through (reference / enum reconstruction)

// ibRenderedPageCache — the build-once page cache (one scroll shape across ticks). OPAQUE at
// the door (dataQueryBuilder.h forward-declares it; the list model holds it via shared_ptr and
// builds it through ibDataQueryBuilder::NewPageCache). Its FULL layout lives here, where L2 is
// in scope (it stores an L2 ibRenderedQuery): the DB provider's ExecuteReadCached fills/reads
// it, and NewPageCache constructs it. (docs/query-language-arc.md §19/§20)
struct ibRenderedPageCache
{
	wxString                     m_sig;             // signature of the SQL-determining inputs
	std::vector<ibQuerySortItem> m_effectiveSort;   // resolved once (identity tail walk)
	ibRenderedQuery              m_rendered;         // SQL + bind plan, rendered once
	bool                         m_valid = false;
};

class ibDbTableProvider : public ibBackendQueryProvider
{
public:
	// --- the L3<->L2 read/write engine (vended by a DB-family queryable) ---
	ibDataQueryResult ExecuteRead(const ibDataQuerySpec& spec, const ibReadPageRequest& req) override;
	ibDataQueryResult ExecuteReadCached(const ibDataQuerySpec& spec, const ibReadPageRequest& req,
	                                    ibRenderedPageCache& cache, const wxString& signature) override;
	ibDataQueryResult ExecuteAggregate(const ibDataQuerySpec& spec) override;
	bool ExecuteWrite(const ibDataQuerySpec& spec, ibDataQueryBuilder::WriteKind kind) override;

	// --- the DB GET data-access TEMPLATE — COLUMN-BASED. Reads the dialect-normalised typed
	//     fields of an L2 result (ibQueryResult) for a query COLUMN and assembles the L3 ibValue
	//     (primitive / enum / reference) off the column's type descriptor + the metadata context
	//     (reference / enum reconstruction) — no attribute. The one place every engine DB read
	//     lifts a value up. (docs §22.4b)
	static bool GetValueColumn(const wxString& fieldName, ibValueMetaObjectAttributeBase::ibFieldTypes fieldType,
	                           const ibBackendQueryColumn* col, const ibMetaData* metaData, ibValue& retValue, ibQueryResult& result, bool createData = true);
	static bool GetValueColumn(const wxString& fieldName, const ibBackendQueryColumn* col, const ibMetaData* metaData,
	                           ibValue& retValue, ibQueryResult& result, bool createData = true);
	static bool GetValueColumn(const ibBackendQueryColumn* col, const ibMetaData* metaData, ibValue& retValue, ibQueryResult& result, bool createData = true);

	// --- the DB WRITE data-access template (symmetric to GetValueColumn) — decompose an ibValue
	//     across the COLUMN's physical fields (TYPE tag + per-contained-type data + reference blob,
	//     derived from the column's type descriptor) and bind them POSITIONALLY into the L2
	//     statement. The provider works at L2, so it binds into ibQueryStatement (the deferred
	//     L2 template that captures each value as an IR const and renders through the dialect),
	//     NEVER a raw L1 driver statement. Column-based: no attribute. (docs §22.4b)
	static void SetValueColumn(const ibBackendQueryColumn* col, const ibMetaData* metaData, const ibValue& cValue, ibQueryStatement* statement, int& position);
	static void SetValueColumn(const ibBackendQueryColumn* col, const ibMetaData* metaData, const ibValue& cValue, ibQueryStatement* statement);

	// --- thin convenience adapters for callers that already hold the metaobject attribute (the
	//     register lowering — recorder / period / dimension / resource attributes). The attribute
	//     IS a column and carries its own metaData, so these forward to the column-based core; the
	//     core itself names no attribute.
	static void SetValueAttribute(const ibValueMetaObjectAttributeBase* attr, const ibValue& cValue, ibQueryStatement* statement, int& position);
	static bool GetValueAttribute(const ibValueMetaObjectAttributeBase* attr, ibValue& retValue, ibQueryResult& result, bool createData = true);
	static bool GetValueAttribute(const wxString& fieldName, ibValueMetaObjectAttributeBase::ibFieldTypes fieldType,
	                              const ibValueMetaObjectAttributeBase* attr, ibValue& retValue, ibQueryResult& result, bool createData = true);

private:
	// Name-substitution lowering — spec -> L2 IR (connection-free Build()).
	static ibQueryIR BuildPageIR(const ibDataQuerySpec& spec, const ibReadPageRequest& req,
	                             const std::vector<ibQuerySortItem>& effective);
	static std::vector<ibValue> BuildExternal(const ibReadPageRequest& req, const std::vector<ibQuerySortItem>& effective);
};

#endif // __DB_TABLE_PROVIDER_H__
