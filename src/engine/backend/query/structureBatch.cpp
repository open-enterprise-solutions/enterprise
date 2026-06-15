#include "backend/query/structureBatch.h"

#include "backend/query/schemaBuilder.h"                  // ibSchemaBuilder — Execute / RunOrDefer / barrier
#include "backend/query/queryable.h"                      // ibBackendQueryable — GetQueryTableName / GetMetaData
#include "backend/query/queryColumn.h"                    // ibBackendQueryColumn — GetTypeDesc / GetPhysicalName
#include "backend/databaseLayer/databaseQueryBuilder.h"   // L2 door — ibUpdate / ibDelete / Execute (the type-removal data cleanups)
#include "backend/databaseLayer/databaseErrorCodes.h"     // DATABASE_LAYER_QUERY_RESULT_ERROR

#include <set>

ibStructureBatch::ibStructureBatch(const ibBackendQueryable* queryable)
	: m_queryable(queryable), m_table(queryable->GetQueryTableName())
{
}

const ibMetaData* ibStructureBatch::GetMetaData() const
{
	return m_queryable != nullptr ? m_queryable->GetMetaData() : nullptr;
}

namespace {

// One physical field (layout slot) -> the L2 column it renders as.
ibDdlColumn ColumnOf(const ibColumnSlot& slot)
{
	ibDdlColumn c;
	c.m_name    = slot.m_name;
	c.m_type    = slot.m_type;
	c.m_notNull = slot.m_notNull;
	c.m_default = slot.m_default;
	return c;
}

} // namespace

void ibStructureBatch::AddColumn(const ibBackendQueryColumn* column)
{
	for (const ibColumnSlot& slot : DescribeColumnLayout(column, GetMetaData()))
		AddField(slot);
}

void ibStructureBatch::DropColumn(const ibBackendQueryColumn* column)
{
	for (const ibColumnSlot& slot : DescribeColumnLayout(column, GetMetaData()))
		DropField(slot.m_name);
}

void ibStructureBatch::AddField(const ibColumnSlot& slot)
{
	m_steps.push_back(ibAddColumn(m_table, ColumnOf(slot)));
}

void ibStructureBatch::DropField(const wxString& fieldName)
{
	m_steps.push_back(ibDropColumn(m_table, fieldName));
}

void ibStructureBatch::AlterField(const ibColumnSlot& slot)
{
	m_steps.push_back(ibAlterColumn(m_table, ColumnOf(slot)));
}

void ibStructureBatch::CreateTable(std::vector<const ibBackendQueryColumn*> columns)
{
	// Expand each logical column to its physical fields (a raw scaffold column = one typed field).
	// No table-level PRIMARY KEY — identity is an index concern (see CreateIndex).
	std::vector<ibDdlColumn> ddlCols;
	for (const ibBackendQueryColumn* col : columns)
		for (const ibColumnSlot& slot : DescribeColumnLayout(col, GetMetaData()))
			ddlCols.push_back(ColumnOf(slot));
	m_steps.push_back(ibCreateTable(m_table, std::move(ddlCols)));
}

void ibStructureBatch::DropTable()
{
	m_steps.push_back(ibDropTable(m_table));
}

void ibStructureBatch::CreateIndex(const wxString& indexName, std::vector<const ibBackendQueryColumn*> columns, bool unique)
{
	// Expand every logical column to its physical field names — the index covers those.
	std::vector<wxString> fields;
	for (const ibBackendQueryColumn* col : columns)
		for (const wxString& f : ColumnFieldNames(col, GetMetaData()))
			fields.push_back(f);
	if (fields.empty())   // a column with no physical fields => no index (the old explicit guard)
		return;
	m_steps.push_back(ibCreateIndex(m_table, indexName, std::move(fields), unique));
}

void ibStructureBatch::Ddl(const ibDdlStatement& ddl)
{
	m_steps.push_back(ddl);
}

int ibStructureBatch::Flush(ibSchemaBuilder& schema)
{
	int retCode = 1;
	const bool multiClause = schema.AlterTableMultiClause();

	// A pending run of consecutive same-op column clauses, folded into one ALTER on flush.
	std::vector<ibAlterClause> run;
	ibAlterOp                  runOp = ibAlterOp::Add;

	auto flushRun = [&]() -> bool {
		if (run.empty())
			return true;
		if (multiClause) {
			retCode = schema.Execute(ibAlterTable(m_table, run));
			run.clear();
			return retCode != DATABASE_LAYER_QUERY_RESULT_ERROR;
		}
		// No multi-clause ALTER (SQLite): one statement per clause.
		for (const ibAlterClause& clause : run) {
			retCode = schema.Execute(clause.m_op == ibAlterOp::Add
				? ibAddColumn(m_table, clause.m_column)
				: ibDropColumn(m_table, clause.m_column.m_name));
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR) {
				run.clear();
				return false;
			}
		}
		run.clear();
		return true;
	};

	for (const ibDdlStatement& step : m_steps) {
		if (step.m_kind == ibDdlKind::AddColumn || step.m_kind == ibDdlKind::DropColumn) {
			const ibAlterOp op = (step.m_kind == ibDdlKind::AddColumn) ? ibAlterOp::Add : ibAlterOp::Drop;
			if (!run.empty() && op != runOp && !flushRun())   // a different op breaks the run
				return retCode;
			runOp = op;
			ibAlterClause clause;
			clause.m_op     = op;
			clause.m_column = step.m_columns.empty() ? ibDdlColumn() : step.m_columns.front();
			run.push_back(std::move(clause));
		}
		else {
			if (!flushRun())                                  // emit the pending run before create/index/etc
				return retCode;
			retCode = schema.Execute(step);
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return retCode;
		}
	}
	if (!flushRun())
		return retCode;

	// Data seeds. RunOrDefer parks them past the DDL commit when this batch's table was just created
	// on a barrier dialect (the create step above recorded it), and runs them now otherwise.
	for (std::function<bool()>& write : m_inserts) {
		if (!schema.RunOrDefer(m_table, write)) {
			retCode = DATABASE_LAYER_QUERY_RESULT_ERROR;
			return retCode;
		}
	}

	m_steps.clear();
	m_inserts.clear();
	return retCode;
}

// ============================================================================
// DiffColumnInto — the column-creation diff, lifted off the metaobject attribute.
// ============================================================================
namespace {

const ibColumnSlot* SlotOfRole(const std::vector<ibColumnSlot>& layout, ibColumnRole role)
{
	for (const ibColumnSlot& s : layout)
		if (s.m_role == role) return &s;
	return nullptr;
}

const ibColumnSlot* SlotByName(const std::vector<ibColumnSlot>& layout, const wxString& name)
{
	for (const ibColumnSlot& s : layout)
		if (s.m_name == name) return &s;
	return nullptr;
}

// Two physical slots render to the same SQL type? (qualifier change -> ALTER). Compares the canonical
// L2 type, so an alter fires only when the column's physical type actually changes.
bool SameSlotType(const ibColumnType& a, const ibColumnType& b)
{
	return a.m_kind == b.m_kind && a.m_length == b.m_length && a.m_precision == b.m_precision
	    && a.m_scale == b.m_scale && a.m_datePrec == b.m_datePrec && a.m_fixed == b.m_fixed;
}

bool DescContainsClsid(const ibTypeDescription& td, const ibClassID& clsid)
{
	for (const auto& c : td.GetClsidList())
		if (c == clsid) return true;
	return false;
}

// UPDATE <table> SET <fld>_TYPE = 0 WHERE <fld>_TYPE = <tag> — clear the variant tag of rows whose
// stored type was just removed (so they read back as undefined, not as a now-missing sub-field).
int ClearRowsOfType(const wxString& tableName, const wxString& fieldName, int typeTag)
{
	const wxString typeCol = fieldName + ibFieldSuffix(ibColumnRole::Discriminator);
	ibDatabaseQueryBuilder q;
	return q.Execute(ibUpdate(tableName,
		{ { typeCol, ibConst(ibValue(0)) } },
		ibBinOp(ibQueryBinOp::Eq, ibCol(typeCol), ibConst(ibValue(typeTag)))));
}

} // namespace

int DiffColumnInto(ibStructureBatch& batch, const ibBackendQueryColumn* srcCol, const ibBackendQueryColumn* dstCol)
{
	int retCode = 1;

	// CREATE — pour the whole new column (the batch expands its layout: TYPE + per-type data + ref pair).
	if (dstCol == nullptr) {
		batch.AddColumn(srcCol);
		return retCode;
	}
	// DELETE — drop the whole removed column (every physical field).
	if (srcCol == nullptr) {
		batch.DropColumn(dstCol);
		return retCode;
	}
	// UPDATE — only if the type set changed.
	if (srcCol->GetTypeDesc() == dstCol->GetTypeDesc())
		return retCode;

	const wxString&    tableName = batch.GetTable();
	const ibMetaData*  metaData  = batch.GetMetaData();
	const ibTypeDescription& srcTypeDesc = srcCol->GetTypeDesc();
	const ibTypeDescription& dstTypeDesc = dstCol->GetTypeDesc();
	const wxString fieldName = srcCol->GetPhysicalName();
	const std::vector<ibColumnSlot> srcLayout = DescribeColumnLayout(srcCol, metaData);
	const std::vector<ibColumnSlot> dstLayout = DescribeColumnLayout(dstCol, metaData);

	// Primitive / discriminator fields: a pure SLOT diff by name. _TYPE is always present, so it is
	// never added/dropped; only the per-type data columns move. The reference pair is per-clsid below.
	auto isRefSlot = [](const ibColumnSlot& s) {
		return s.m_role == ibColumnRole::ReferenceType || s.m_role == ibColumnRole::ReferenceId;
	};
	for (const ibColumnSlot& s : srcLayout)
		if (!isRefSlot(s) && SlotByName(dstLayout, s.m_name) == nullptr)
			batch.AddField(s);
	for (const ibColumnSlot& d : dstLayout) {
		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return retCode;
		if (isRefSlot(d))
			continue;
		const ibColumnSlot* s = SlotByName(srcLayout, d.m_name);
		if (s == nullptr) {
			// gone -> DROP the field; clear the stale _TYPE tag of rows that held this type.
			batch.DropField(d.m_name);
			if (d.m_role != ibColumnRole::Discriminator)
				retCode = ClearRowsOfType(tableName, fieldName, ibPersistedTypeTag(d.m_role));
		}
		else if (!SameSlotType(s->m_type, d.m_type)) {
			// A date narrowing from Time cannot ALTER in place -> drop + re-add; else ALTER.
			if (s->m_role == ibColumnRole::Date
			    && s->m_type.m_datePrec != ibDatePrec::Time && d.m_type.m_datePrec == ibDatePrec::Time) {
				batch.DropField(d.m_name);
				batch.AddField(*s);
			}
			else {
				batch.AlterField(*s);
			}
		}
	}

	// Reference pair (per-clsid): which reference targets are new / still present / removed.
	std::set<ibClassID> createdRef, currentRef, removedRef;
	for (auto clsid : srcTypeDesc.GetClsidList())
		if (!DescContainsClsid(dstTypeDesc, clsid) && IsReferenceClsid(clsid, metaData))
			createdRef.insert(clsid);
	for (auto clsid : dstTypeDesc.GetClsidList()) {
		if (!IsReferenceClsid(clsid, metaData))
			continue;
		if (DescContainsClsid(srcTypeDesc, clsid))
			currentRef.insert(clsid);
		else
			removedRef.insert(clsid);
	}

	if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
		return retCode;

	// newly needed -> ADD the pair; some targets removed while others remain -> clear those rows' tag;
	// all targets gone -> clear + DROP the pair (never a row DELETE — that wipes records).
	if (!createdRef.empty() && currentRef.empty() && removedRef.empty()) {
		if (const ibColumnSlot* rt = SlotOfRole(srcLayout, ibColumnRole::ReferenceType))
			batch.AddField(*rt);
		if (const ibColumnSlot* rr = SlotOfRole(srcLayout, ibColumnRole::ReferenceId))
			batch.AddField(*rr);
	}
	if (!currentRef.empty()) {
		const wxString typeCol = fieldName + ibFieldSuffix(ibColumnRole::Discriminator);
		const wxString refCol  = fieldName + ibFieldSuffix(ibColumnRole::ReferenceType);
		ibDatabaseQueryBuilder q;
		for (auto clsid : removedRef) {
			// clsid is a 64-bit ibClassID — bind through ibNumber, NOT ibValue(wxLongLong_t) (that ctor is Date).
			retCode = q.Execute(ibUpdate(tableName,
				{ { typeCol, ibConst(ibValue(0)) } },
				ibBinOp(ibQueryBinOp::Eq, ibCol(refCol), ibConst(ibValue(ibNumber(clsid))))));
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return retCode;
		}
	}
	if (!removedRef.empty() && currentRef.empty()) {
		retCode = ClearRowsOfType(tableName, fieldName, ibPersistedTypeTag(ibColumnRole::ReferenceType));
		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return retCode;
		batch.DropField(fieldName + ibFieldSuffix(ibColumnRole::ReferenceType));
		batch.DropField(fieldName + ibFieldSuffix(ibColumnRole::ReferenceId));
	}
	return retCode;
}
