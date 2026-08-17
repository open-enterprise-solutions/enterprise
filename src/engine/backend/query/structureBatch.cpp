#include "backend/query/structureBatch.h"

#include "backend/query/schemaBuilder.h"                  // ibSchemaBuilder — Execute / RunOrDefer / barrier
#include "backend/query/queryable.h"                      // ibBackendQueryable — GetQueryTableName / GetMetaData
#include "backend/query/queryColumn.h"                    // ibBackendQueryColumn — GetTypeDesc / GetPhysicalName
#include "backend/databaseLayer/databaseQueryBuilder.h"   // L2 door — ibUpdate / ibDelete / Execute (the type-removal data cleanups)
#include "backend/restructureInfo.h"                      // RequireExclusiveForDDL — demanded here, by the code that writes DDL
#include "backend/backend_exception.h"                    // ibBackendCoreException — a refused write stops the apply

#include <set>

ibStructureBatch::ibStructureBatch(const ibBackendQueryable* queryable)
	// Null-tolerant: not every table in a snapshot stands behind a metaobject. A DERIVED table
	// (a register's totals) is declared by one but IS none, so it carries no queryable — and the
	// name-keyed constructor is the right one for it. Dereferencing here crashed the apply.
	: m_queryable(queryable), m_table(queryable != nullptr ? queryable->GetQueryTableName() : wxString())
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
	for (const ibColumnSlot& slot : DescribeColumnLayout(column))
		AddField(slot);
}

void ibStructureBatch::DropColumn(const ibBackendQueryColumn* column)
{
	for (const ibColumnSlot& slot : DescribeColumnLayout(column))
		DropField(slot);
}

// Each clause, as it is decided — this is the half that says WHICH column the differ believes in.
// A drop of a field that was never created, or an add of one that exists, is visible here one line
// before the database refuses it.
void ibStructureBatch::AddField(const ibColumnSlot& slot)
{
	m_steps.push_back(ibAddColumn(m_table, ColumnOf(slot)));
}

void ibStructureBatch::DropField(const ibColumnSlot& slot)
{
	// The full column rides the statement: the renderer spells only the name, the compensation
	// ledger reads the shape back if the second phase fails and the drop has to be undone (empty).
	m_steps.push_back(ibDropColumn(m_table, ColumnOf(slot)));
}

void ibStructureBatch::AlterField(const ibColumnSlot& slot, const ibColumnSlot& prev)
{
	m_steps.push_back(ibAlterColumn(m_table, ColumnOf(slot), ColumnOf(prev)));
}

void ibStructureBatch::CreateTable(std::vector<const ibBackendQueryColumn*> columns)
{
	// Expand each logical column to its physical fields (a raw scaffold column = one typed field).
	// No table-level PRIMARY KEY — identity is an index concern (see CreateIndex).
	std::vector<ibDdlColumn> ddlCols;
	for (const ibBackendQueryColumn* col : columns)
		for (const ibColumnSlot& slot : DescribeColumnLayout(col))
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
		for (const wxString& f : ColumnFieldNames(col))
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
	// MONOPOLY IS OWED BY WHOEVER WRITES DDL, NOT BY WHOEVER PRESSED "UPDATE". The gate used to sit at the
	// top of the save (ibStructureBuilder::OnBeforeSave), so editing a module demanded exclusive mode as
	// loudly as adding a dimension — and the error message promised the opposite ("code-only changes can be
	// saved without it"). Here the question answers itself: a batch with no steps has nothing to execute, so
	// nobody asks, and a code-only apply goes through while people work.
	//
	// This also covers the cases where metadata moved but the DATABASE did not — adding another type to a
	// composite attribute, say, which keeps living in the same reference columns. No rule has to know that;
	// the diff simply emits no step, and no step means no demand. The criterion stops being "what did the
	// user change" and becomes "is there anything to run", which cannot drift from the truth because it IS
	// the truth.
	//
	// Idempotent per apply: the gate returns immediately once exclusive is held (ts_acquiredByGate /
	// ExclusiveMode), so the per-table flushes after the first cost nothing. Seed INSERTs are not counted —
	// they are DML, and data writes are what exclusive mode exists to keep OTHERS from doing, not us.
	if (!m_steps.empty())
		ibRestructureInfo::RequireExclusiveForDDL();

	const bool multiClause = schema.AlterTableMultiClause();

	// A pending run of consecutive same-op column clauses, folded into one ALTER on flush.
	std::vector<ibAlterClause> run;
	ibAlterOp                  runOp = ibAlterOp::Add;

	// ⚠ NO EXISTENCE PROBE HERE, ON PURPOSE. A guard that read the table's physical columns and
	// skipped the DROP of an absent one lived here briefly (2026-08-17) and was removed the same
	// day: the base is a function of the configuration BY CONSTRUCTION, and reading it to decide
	// which DDL to emit legalises exactly the drift it papers over. The view / trigger probes are
	// the boundary, not a precedent — those are DERIVED objects whose replacement is the normal
	// path. A drop that meets a missing column is a DEFECT upstream (or an outside edit, which is
	// not covered) and must fail loudly; a base crippled by a past defective apply is repaired by
	// hand, not by a permanent listener in every apply.
	// Errors propagate as EXCEPTIONS now (schema.Execute -> RunQuery throws); the return is an
	// affected-row COUNT — 0 (DDL, or a cleanup matching no rows) is NOT a failure, so it is ignored.
	auto flushRun = [&]() {
		if (run.empty())
			return;
		if (multiClause) {
			schema.Execute(ibAlterTable(m_table, run));
			run.clear();
			return;
		}
		// No multi-clause ALTER (SQLite): one statement per clause.
		for (const ibAlterClause& clause : run)
			schema.Execute(clause.m_op == ibAlterOp::Add
				? ibAddColumn(m_table, clause.m_column)
				: ibDropColumn(m_table, clause.m_column.m_name));
		run.clear();
	};

	for (const ibDdlStatement& step : m_steps) {
		if (step.m_kind == ibDdlKind::AddColumn || step.m_kind == ibDdlKind::DropColumn) {
			const ibAlterOp op = (step.m_kind == ibDdlKind::AddColumn) ? ibAlterOp::Add : ibAlterOp::Drop;
			if (!run.empty() && op != runOp)   // a different op breaks the run
				flushRun();
			runOp = op;
			ibAlterClause clause;
			clause.m_op     = op;
			clause.m_column = step.m_columns.empty() ? ibDdlColumn() : step.m_columns.front();
			run.push_back(std::move(clause));
		}
		else {
			flushRun();                        // emit the pending run before create/index/etc
			schema.Execute(step);
		}
	}
	flushRun();

	// Data seeds. RunOrDefer parks them past the DDL commit when this batch's table was just created
	// on a barrier dialect (the create step above recorded it), and runs them now otherwise.
	//
	// ⭐ AND THE ANSWER IS READ. The DDL above reports failure by THROWING, so reaching this line means
	// the shape is settled; a write that then fails is a real refusal and has to travel the same way.
	// Dropping it let a seed — or a type-tag cleanup, which rides this same queue — fail silently and
	// leave the apply believing it had written what it had not. A PARKED write answers true here and
	// reports for real in the post-commit drain, where the transaction is already gone.
	for (std::function<bool()>& write : m_inserts)
		if (!schema.RunOrDefer(m_table, write))
			ibBackendCoreException::Error(
				_("Failed to write the data of %s - the restructuring was rolled back"), m_table);

	m_steps.clear();
	m_inserts.clear();
	return 1;   // reached the end => success (a real DB error THREW; a 0-row count is not an error)
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
//
// ⭐⭐ POURED INTO THE BATCH, NOT RUN HERE. Every ADD / DROP this diff decides on is ACCUMULATED and
// reaches the database later, at Flush; a cleanup executed on the spot therefore runs against the
// table as it stood BEFORE the whole restructuring — and asked it about a column the restructuring
// has not created yet. On Firebird that is "Column unknown FLDnnnn_TYPE" and, since the refusal now
// stops the apply, the entire restructuring dies on a cleanup for work that had not happened.
//
// The batch is the existing answer to exactly this: seed writes already ride it, and it routes them
// through the DDL barrier so they land after the shape is settled. The order the cleanup needs is
// preserved either way — the discriminator (_TYPE) is never added or dropped, only the per-type data
// fields around it are, so clearing it after the drop reads the same as clearing it before.
void ClearRowsOfType(ibStructureBatch& batch, const wxString& tableName, const wxString& fieldName, int typeTag)
{
	const wxString typeCol = fieldName + ibFieldSuffix(ibColumnRole::Discriminator);
	batch.Insert([tableName, typeCol, typeTag]() {
		ibDatabaseQueryBuilder q;
		q.Execute(ibUpdate(tableName,
			{ { typeCol, ibConst(ibValue(0)) } },
			ibBinOp(ibQueryBinOp::Eq, ibCol(typeCol), ibConst(ibValue(typeTag)))));
		return true;   // a 0-row clear is the ordinary case; a real failure THREW
	});
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
	const ibTypeDescription& srcTypeDesc = srcCol->GetTypeDesc();
	const ibTypeDescription& dstTypeDesc = dstCol->GetTypeDesc();
	const wxString fieldName = srcCol->GetPhysicalName();
	const std::vector<ibColumnSlot> srcLayout = DescribeColumnLayout(srcCol);
	const std::vector<ibColumnSlot> dstLayout = DescribeColumnLayout(dstCol);

	// Primitive / discriminator fields: a pure SLOT diff by name. _TYPE is always present, so it is
	// never added/dropped; only the per-type data columns move. The reference pair is per-clsid below.
	auto isRefSlot = [](const ibColumnSlot& s) {
		return s.m_role == ibColumnRole::ReferenceType || s.m_role == ibColumnRole::ReferenceId;
	};
	for (const ibColumnSlot& s : srcLayout)
		if (!isRefSlot(s) && SlotByName(dstLayout, s.m_name) == nullptr)
			batch.AddField(s);
	for (const ibColumnSlot& d : dstLayout) {
		if (isRefSlot(d))
			continue;
		const ibColumnSlot* s = SlotByName(srcLayout, d.m_name);
		if (s == nullptr) {
			// gone -> DROP the field; clear the stale _TYPE tag of rows that held this type.
			batch.DropField(d);
			if (d.m_role != ibColumnRole::Discriminator)
				ClearRowsOfType(batch, tableName, fieldName, ibPersistedTypeTag(d.m_role));
		}
		else if (!SameSlotType(s->m_type, d.m_type)) {
			// A date narrowing from Time cannot ALTER in place -> drop + re-add; else ALTER.
			if (s->m_role == ibColumnRole::Date
			    && s->m_type.m_datePrec != ibDatePrec::Time && d.m_type.m_datePrec == ibDatePrec::Time) {
				batch.DropField(d);
				batch.AddField(*s);
			}
			else {
				batch.AlterField(*s, d);
			}
		}
	}

	// Reference pair — ONE shared (_RTRef clsid + _RRRef guid) per field that admits ANY reference target
	// (columnLayout: present iff some clsid is a reference). It is therefore added / dropped on the field
	// GAINING / LOSING references AS A WHOLE — NOT per target clsid. (The old per-clsid logic dropped the
	// shared pair when a reference field merely RETARGETED, e.g. ref-A -> ref-B: createdRef={B}, removedRef
	// ={A} hit the "all targets gone" DROP and skipped the "newly needed" ADD, so the next write hit
	// "FLDxxxx_RTRef unknown".) Per-clsid still matters for CLEARING stale rows of a dropped target.
	std::set<ibClassID> createdRef, currentRef, removedRef;
	for (auto clsid : srcTypeDesc.GetClsidList())
		if (!DescContainsClsid(dstTypeDesc, clsid) && IsReference(clsid))
			createdRef.insert(clsid);
	for (auto clsid : dstTypeDesc.GetClsidList()) {
		if (!IsReference(clsid))
			continue;
		if (DescContainsClsid(srcTypeDesc, clsid))
			currentRef.insert(clsid);
		else
			removedRef.insert(clsid);
	}

	const bool newHasRef = !createdRef.empty() || !currentRef.empty();   // the NEW type admits a reference
	const bool oldHasRef = !removedRef.empty() || !currentRef.empty();   // the OLD type admitted a reference

	if (newHasRef && !oldHasRef) {
		// Field GAINED references (had none) -> ADD the shared pair.
		if (const ibColumnSlot* rt = SlotOfRole(srcLayout, ibColumnRole::ReferenceType))
			batch.AddField(*rt);
		if (const ibColumnSlot* rr = SlotOfRole(srcLayout, ibColumnRole::ReferenceId))
			batch.AddField(*rr);
	}
	else if (oldHasRef && !newHasRef) {
		// Field LOST all references -> clear the rows that held one + DROP the shared pair (never a row
		// DELETE — that wipes records). The slots come off the OLD layout — they exist there because
		// oldHasRef is what this branch means.
		ClearRowsOfType(batch, tableName, fieldName, ibPersistedTypeTag(ibColumnRole::ReferenceType));
		if (const ibColumnSlot* rt = SlotOfRole(dstLayout, ibColumnRole::ReferenceType))
			batch.DropField(*rt);
		if (const ibColumnSlot* rr = SlotOfRole(dstLayout, ibColumnRole::ReferenceId))
			batch.DropField(*rr);
	}
	else if (!removedRef.empty()) {
		// Still a reference, but SOME targets were dropped (incl. a full retarget) -> KEEP the shared pair;
		// clear the stale _TYPE of rows whose stored target (_RTRef) is gone so they read undefined, not dead.
		const wxString typeCol = fieldName + ibFieldSuffix(ibColumnRole::Discriminator);
		const wxString refCol  = fieldName + ibFieldSuffix(ibColumnRole::ReferenceType);
		// Batched for the same reason as the two cleanups above — it reads columns this very apply
		// may still be creating.
		for (auto clsid : removedRef)
			batch.Insert([tableName, typeCol, refCol, clsid]() {
				ibDatabaseQueryBuilder q;
				// clsid is a 64-bit ibClassID — bind through ibNumber, NOT ibValue(wxLongLong_t) (that ctor is Date).
				q.Execute(ibUpdate(tableName,
					{ { typeCol, ibConst(ibValue(0)) } },
					ibBinOp(ibQueryBinOp::Eq, ibCol(refCol), ibConst(ibValue(ibNumber(clsid))))));
				return true;
			});
	}
	return retCode;   // 1 — success; a real DB error THREW (the affected-row count is not an error signal)
}
