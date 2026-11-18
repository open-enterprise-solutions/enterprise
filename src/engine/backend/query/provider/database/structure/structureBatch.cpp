#include "backend/query/provider/database/structure/structureBatch.h"

#include "backend/query/provider/database/structure/schemaBuilder.h" // ibSchemaBuilder — Execute / RunOrDefer / barrier
#include "backend/query/queryable.h"                                 // ibBackendQueryable — GetQueryTableName / GetMetaData
#include "backend/query/queryColumn.h"                               // ibBackendQueryColumn — GetTypeDesc / GetPhysicalName
#include "backend/query/provider/database/columnCodec.h"             // BindWriteValue — a field's DEFAULT is the column's write of a fresh value
#include "backend/databaseLayer/databaseQueryBuilder.h"              // L2 door — ibUpdate / ibDelete / Execute (the type-removal data cleanups)
#include "backend/restructureInfo.h"                                 // RequireExclusiveForDDL — demanded here, by the code that writes DDL
#include "backend/backend_exception.h"                               // ibBackendCoreException — a refused write stops the apply

#include <set>
#include <algorithm>   // std::find — an index's value fields first, then the rest (CreateIndex)

ibStructureBatch::ibStructureBatch(const std::shared_ptr<const ibBackendQueryable>& queryable)
	// Null-tolerant: not every table in a snapshot stands behind a metaobject. A DERIVED table
	// (a register's totals) is declared by one but IS none, so it carries no queryable — and the
	// name-keyed constructor is the right one for it. Dereferencing here crashed the apply.
	: m_queryable(queryable), m_table(queryable != nullptr ? queryable->GetQueryTableName() : ibString())
{
}

const ibMetaData* ibStructureBatch::GetMetaData() const
{
	return m_queryable != nullptr ? m_queryable->GetMetaData() : nullptr;
}

namespace {

// One physical field (layout slot) -> the L2 column it renders as, with no DEFAULT (ColumnsOf asks for one).
ibDdlColumn ColumnOf(const ibColumnSlot& slot)
{
	ibDdlColumn c;
	c.m_name    = slot.m_name;
	c.m_type    = slot.m_type;
	c.m_notNull = slot.m_notNull;
	return c;
}

// ⭐ A LOGICAL COLUMN'S FIELDS AS THE L2 COLUMNS THEY RENDER AS, in its layout's order — each created holding what the
// column's write puts there for the value a fresh row of it holds: its DEFAULT, so a row a seed fills in part, or one
// already in the table when the field is added, reads as that value written — a functional option nobody switched as
// its initial value, a dimension kind a predefined account's seed left out as the empty value of its type. The value is
// the column's (ibBackendQueryColumn::CreateValue — an attribute's column makes it as its attribute does, any other
// from its type). Asked of the codec, the DEFAULT cannot be a second spelling of a write. A raw field is written as it is by whoever writes it and gets none; nor does a field the write leaves NULL (a
// key a composite has no reference for) or empty (a schedule's absent blob).
std::vector<ibDdlColumn> ColumnsOf(const ibBackendQueryColumn& column, const ibMetaData* metaData)
{
	const std::vector<ibColumnSlot> layout = DescribeColumnLayout(column);
	std::vector<ibDdlColumn> columns;
	columns.reserve(layout.size());
	for (const ibColumnSlot& slot : layout)
		columns.push_back(ColumnOf(slot));
	if (column.AsRawColumn() != nullptr)
		return columns;

	std::vector<ibString> fields;
	fields.reserve(layout.size());
	for (const ibColumnSlot& slot : layout)
		fields.push_back(slot.m_name);
	ibQueryStatement capture(ibQueryStatement::Kind::Delete, ibString(), fields);
	int position = 1;
	BindWriteValue(capture, column, metaData, column.CreateValue(), position);
	const std::vector<ibQueryExprPtr>& values = capture.CapturedValues();
	for (size_t i = 0; i < columns.size() && i < values.size(); ++i) {
		const ibQueryExprPtr& value = values[i];
		if (value && (value->m_blob.GetDataLen() > 0
		              || (value->m_const.GetType() != TYPE_EMPTY && value->m_const.GetType() != TYPE_NULL)))
			columns[i].m_default = value;
	}
	return columns;
}

// …the one of them `slot` is.
ibDdlColumn ColumnOf(const ibBackendQueryColumn& column, const ibColumnSlot& slot, const ibMetaData* metaData)
{
	for (ibDdlColumn& field : ColumnsOf(column, metaData))
		if (field.m_name == slot.m_name)
			return std::move(field);
	return ColumnOf(slot);
}

} // namespace

void ibStructureBatch::AddColumn(const std::shared_ptr<const ibBackendQueryColumn>& column)
{
	for (ibDdlColumn& field : ColumnsOf(*column, GetMetaData()))
		m_steps.push_back(ibAddColumn(m_table, std::move(field)));
}

void ibStructureBatch::DropColumn(const std::shared_ptr<const ibBackendQueryColumn>& column)
{
	for (ibDdlColumn& field : ColumnsOf(*column, GetMetaData()))
		m_steps.push_back(ibDropColumn(m_table, std::move(field)));
}

// Each clause, as it is decided — this is the half that says WHICH column the differ believes in.
// A drop of a field that was never created, or an add of one that exists, is visible here one line
// before the database refuses it.
void ibStructureBatch::AddField(const std::shared_ptr<const ibBackendQueryColumn>& column, const ibColumnSlot& slot)
{
	m_steps.push_back(ibAddColumn(m_table, ColumnOf(*column, slot, GetMetaData())));
}

void ibStructureBatch::DropField(const std::shared_ptr<const ibBackendQueryColumn>& column, const ibColumnSlot& slot)
{
	// The full column rides the statement: the renderer spells only the name, the compensation
	// ledger reads the shape back if the second phase fails and the drop has to be undone (empty).
	m_steps.push_back(ibDropColumn(m_table, ColumnOf(*column, slot, GetMetaData())));
}

void ibStructureBatch::AlterField(const std::shared_ptr<const ibBackendQueryColumn>& column, const ibColumnSlot& slot,
	const ibColumnSlot& prev)
{
	// ⭐⭐ A STRING THAT GAINS OR LOSES ITS LIMIT IS MADE ANEW, ITS VALUES CARRIED OVER (Max, 2026-11-10). A text of no
	// length is not a type an ALTER reaches everywhere: Firebird keeps it as a text blob and turns no column into one or
	// back, SQLite changes no type in place at all. So the column as it stands steps aside under a second name and the
	// field's own name is added anew — both in the DDL phase, so an index rebuilt over the field stands on its new type —
	// and the data phase copies the values across, cut to the length when the limit comes back (as Adjust cuts them),
	// then drops the old column. The ledger undoes the rename and the add in reverse if that phase fails.
	if ((slot.m_type.m_kind == ibCanonicalKind::Text) == (prev.m_type.m_kind == ibCanonicalKind::Text)) {
		m_steps.push_back(ibAlterColumn(m_table, ColumnOf(slot), ColumnOf(prev)));   // the type alone: all an ALTER and its undo read
		return;
	}
	const ibString table = m_table, field = slot.m_name, held = slot.m_name + ibT("_N");
	const ibColumnType type = slot.m_type;
	Ddl(ibRenameColumn(table, field, held));
	AddField(column, slot);
	Insert([table, field, held, type]() {
		const ibQueryExprPtr value = type.m_kind == ibCanonicalKind::Text ? ibCol(held)
			: ibCast(ibSubstring(ibCol(held), ibConst(ibValue(1)), ibConst(ibValue(type.m_length))), ibTypeString(type.m_length));
		ibDatabaseQueryBuilder().Execute(ibUpdate(table, { { field, value } }));
		ibExecuteDdl(&ibSchemaBuilder().Connection(), ibDropColumn(table, held));
		return true;   // a real failure THREW
	});
}

void ibStructureBatch::CreateTable(const std::vector<std::shared_ptr<const ibBackendQueryColumn>>& columns)
{
	// Expand each logical column to its physical fields (a raw scaffold column = one typed field).
	// No table-level PRIMARY KEY — identity is an index concern (see CreateIndex).
	std::vector<ibDdlColumn> ddlCols;
	for (const std::shared_ptr<const ibBackendQueryColumn>& col : columns)
		for (ibDdlColumn& field : ColumnsOf(*col, GetMetaData()))
			ddlCols.push_back(std::move(field));
	m_steps.push_back(ibCreateTable(m_table, std::move(ddlCols)));
}

void ibStructureBatch::DropTable()
{
	m_steps.push_back(ibDropTable(m_table));
}

void ibStructureBatch::DropTable(const std::vector<std::shared_ptr<const ibBackendQueryColumn>>& columns)
{
	// The same expansion CreateTable makes — what the ledger re-creates is the table as it was declared.
	std::vector<ibDdlColumn> ddlCols;
	for (const std::shared_ptr<const ibBackendQueryColumn>& col : columns)
		for (ibDdlColumn& field : ColumnsOf(*col, GetMetaData()))
			ddlCols.push_back(std::move(field));
	m_steps.push_back(ibDropTable(m_table, std::move(ddlCols)));
}

// Expand every logical column to its physical field names — the index covers those.
//
// ⭐⭐ THE VALUE FIRST, the tag after it. A reference is stored as `_TYPE, _RTRef, _RRRef`, and indexed in that
// order its key came LAST: a join that meets it on the key alone — the only way a reference column may be met,
// since its tag can be empty in a row that still matches (the co-located join says why) — could not use the
// index at all, and Firebird read the table through once per row of the other side. `Goods LEFT JOIN Stock ON
// S.Item = G.Ref WHERE S.Item IS NULL` over 3 000 goods took 1.2 s per run with the index present (measured
// 2026-09-29). A value-and-tag index answers the full equality exactly as before, and uniqueness is the same
// set of fields.
//
// ⭐⭐ …AND EVERY COLUMN'S VALUE BEFORE ANY COLUMN'S TAG, in the order a read sorts on (ColumnSortFields). With
// the tags beside their own columns the period's `_TYPE` stood between the date and the recorder, so a list
// ordered by period, recorder and line was not the index's leading fields, and Firebird sorted a million
// movements for every page of the register's list: 1100 ms a page, 235 ms walked by the index (2026-10-01).
static std::vector<ibString> IndexFields(const std::vector<std::shared_ptr<const ibBackendQueryColumn>>& columns)
{
	std::vector<ibString> fields;
	for (const std::shared_ptr<const ibBackendQueryColumn>& col : columns)
		for (const ibString& f : ColumnSortFields(*col))
			fields.push_back(f);
	for (const std::shared_ptr<const ibBackendQueryColumn>& col : columns)
		for (const ibString& f : ColumnFieldNames(*col))
			if (std::find(fields.begin(), fields.end(), f) == fields.end())
				fields.push_back(f);
	return fields;
}

void ibStructureBatch::CreateIndex(const ibString& indexName, const std::vector<std::shared_ptr<const ibBackendQueryColumn>>& columns, bool unique,
	bool descending)
{
	std::vector<ibString> fields = IndexFields(columns);
	if (fields.empty())   // a column with no physical fields => no index (the old explicit guard)
		return;
	m_steps.push_back(ibCreateIndex(m_table, indexName, std::move(fields), unique, descending));
}

void ibStructureBatch::DropIndex(const ibString& indexName, const std::vector<std::shared_ptr<const ibBackendQueryColumn>>& columns, bool unique,
	bool descending)
{
	m_steps.push_back(ibDropIndex(indexName, m_table, IndexFields(columns), unique, descending));
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

const ibColumnSlot* SlotByName(const std::vector<ibColumnSlot>& layout, const ibString& name)
{
	for (const ibColumnSlot& s : layout)
		if (s.m_name == name) return &s;
	return nullptr;
}

// (The "same SQL type?" test moved to the layout tier as ibSameFieldType (columnLayout.h) — the
//  derived-table rebuild decision asks the identical question of a key column and must get the
//  identical answer. An alter still fires here only when the physical type actually changes.)

bool DescContainsClsid(const ibTypeDescription& td, const ibClassID& clsid)
{
	for (const auto& c : td.GetClsidList())
		if (c == clsid) return true;
	return false;
}

// The rows whose stored value a removed type empties: <fld>_TYPE = <tag>. A removed reference TARGET
// adds <fld>_RTRef = <clsid> — and keeps the tag term, because _RTRef outlives the value it named: a
// row that holds a number now may still carry the clsid of the reference it held before, and matched
// on _RTRef alone it lost the number.
//
// ONE predicate for the UPDATE that clears and the COUNT the ledger shows, so the number said before
// the apply is the number the apply empties.
ibQueryExprPtr OfType(const ibString& fieldName, int typeTag)
{
	return ibBinOp(ibQueryBinOp::Eq, ibCol(fieldName + ibFieldSuffix(ibColumnRole::Discriminator)), ibConst(ibValue(typeTag)));
}

ibQueryExprPtr OfTarget(const ibString& fieldName, ibClassID clsid)
{
	// clsid is a 64-bit ibClassID — bind through ibNumber: ibValue has no 64-bit integer ctor.
	return ibBinOp(ibQueryBinOp::And,
		OfType(fieldName, ibPersistedTypeTag(ibColumnRole::ReferenceType)),
		ibBinOp(ibQueryBinOp::Eq, ibCol(fieldName + ibFieldSuffix(ibColumnRole::ReferenceType)), ibConst(ibValue(ibNumber(clsid)))));
}

// UPDATE <table> SET <assignments> WHERE <where> — empty what the type no longer admits: the rows whose stored
// type was just removed (the overload below — the column's empty value, every field of it), a number below zero
// of a type made non-negative (DiffColumnInto). Returns how many rows that empties, counted NOW, while the values
// are still there.
//
// ⭐⭐ POURED INTO THE BATCH, NOT RUN HERE. Every ADD / DROP this diff decides on is ACCUMULATED and
// reaches the database later, at Flush; a cleanup executed on the spot therefore runs against the
// table as it stood BEFORE the whole restructuring — and asked it about a column the restructuring
// has not created yet. On Firebird that is "Column unknown FLDnnnn_TYPE" and, since the refusal now
// stops the apply, the entire restructuring dies on a cleanup for work that had not happened.
//
// The batch is the existing answer to exactly this: seed writes already ride it, and it routes them
// through the DDL barrier so they land after the shape is settled — and the cleanup needs that order: it
// writes every field of the column as it is declared now, one the same restructuring added among them.
// How many rows of the batch's table `where` holds, asked NOW — before the batch changes them.
long long CountRows(const ibStructureBatch& batch, const ibQueryExprPtr& where)
{
	ibQueryIR count;
	count.m_root = ibProject(ibFilter(ibScan(batch.GetTable()), where),
		{ ibQueryProjItem{ ibFunc(ibT("COUNT"), {}), ibT("n") } });
	ibQueryResult rows = ibDatabaseQueryBuilder().ExecuteIR(count);
	return rows.Next() ? rows.GetResultLong(ibT("n")) : 0;
}

long long ClearRows(ibStructureBatch& batch, std::vector<ibDmlAssign> assignments, const ibQueryExprPtr& where)
{
	const ibString& tableName = batch.GetTable();
	const long long cleared = CountRows(batch, where);

	batch.Insert([tableName, assignments = std::move(assignments), where]() {
		ibDatabaseQueryBuilder q;
		q.Execute(ibUpdate(tableName, assignments, where));
		return true;   // a 0-row clear is the ordinary case; a real failure THREW
	});
	return cleared;
}

// …a row whose stored type was removed: it holds the column's empty value (AdjustValue — what the codec reads such a
// row as), every field as the column's write spells it, the one spelling every other row of the table has for that
// value. Its tag alone set to 0 left the other fields as they were: the row read back empty, yet a GROUP BY over its
// fields — which is how the server folds a level — made it a group of its own beside the written empties, one for each
// reference it used to hold.
long long ClearRows(ibStructureBatch& batch, const ibBackendQueryColumn& column, const ibQueryExprPtr& where)
{
	const std::vector<ibColumnSlot> layout = DescribeColumnLayout(column);
	std::vector<ibString> fields;
	fields.reserve(layout.size());
	for (const ibColumnSlot& slot : layout)
		fields.push_back(slot.m_name);
	ibQueryStatement capture(ibQueryStatement::Kind::Delete, ibString(), fields);
	int position = 1;
	BindWriteValue(capture, column, batch.GetMetaData(), column.AdjustValue(), position);
	const std::vector<ibQueryExprPtr>& values = capture.CapturedValues();
	std::vector<ibDmlAssign> assignments;
	assignments.reserve(fields.size());
	for (size_t i = 0; i < fields.size(); ++i)
		assignments.push_back({ fields[i], i < values.size() && values[i] ? values[i] : ibConst(ibValue()) });
	return ClearRows(batch, std::move(assignments), where);
}

} // namespace

long long DiffColumnInto(ibStructureBatch& batch, const std::shared_ptr<const ibBackendQueryColumn>& srcCol,
	const std::shared_ptr<const ibBackendQueryColumn>& dstCol, long long* cut)
{
	// CREATE — pour the whole new column (the batch expands its layout: TYPE + per-type data + ref pair).
	if (dstCol == nullptr) {
		batch.AddColumn(srcCol);
		return 0;
	}
	// DELETE — drop the whole removed column (every physical field).
	if (srcCol == nullptr) {
		batch.DropColumn(dstCol);
		return 0;
	}
	// UPDATE — only if what the column may HOLD changed: GetTypeValueDesc, the answer its layout is built
	// from below. Asked of the DECLARATION, a field declared through a chart of characteristic types never
	// changed at all — its declaration IS the chart — while the chart gaining a reference type grew its
	// layout a reference pair. The apply wrote nothing, and the next read would have asked for columns
	// the table does not have (found 2026-09-24 on a rehearsal: a catalog added to a chart's types,
	// 0 changes).
	const ibTypeDescription& srcTypeDesc = srcCol->GetTypeValueDesc();
	const ibTypeDescription& dstTypeDesc = dstCol->GetTypeValueDesc();
	if (srcTypeDesc == dstTypeDesc)
		return 0;

	const ibString fieldName = srcCol->GetPhysicalName();
	long long cleared = 0;
	const std::vector<ibColumnSlot> srcLayout = DescribeColumnLayout(*srcCol);
	const std::vector<ibColumnSlot> dstLayout = DescribeColumnLayout(*dstCol);

	// Primitive / discriminator fields: a pure SLOT diff by name. _TYPE is always present, so it is
	// never added/dropped; only the per-type data columns move. The reference pair is per-clsid below.
	auto isRefSlot = [](const ibColumnSlot& s) {
		return s.m_role == ibColumnRole::ReferenceType || s.m_role == ibColumnRole::ReferenceId;
	};
	for (const ibColumnSlot& s : srcLayout)
		if (!isRefSlot(s) && SlotByName(dstLayout, s.m_name) == nullptr)
			batch.AddField(srcCol, s);
	for (const ibColumnSlot& d : dstLayout) {
		if (isRefSlot(d))
			continue;
		const ibColumnSlot* s = SlotByName(srcLayout, d.m_name);
		if (s == nullptr) {
			// gone -> DROP the field; the rows that held this type hold the column's empty value.
			batch.DropField(dstCol, d);
			if (d.m_role != ibColumnRole::Discriminator)
				cleared += ClearRows(batch, *srcCol, OfType(fieldName, ibPersistedTypeTag(d.m_role)));
		}
		else if (!ibSameFieldType(s->m_type, d.m_type)) {
			// A date narrowing from Time cannot ALTER in place -> drop + re-add; else ALTER.
			if (s->m_role == ibColumnRole::Date
			    && s->m_type.m_datePrec != ibDatePrec::Time && d.m_type.m_datePrec == ibDatePrec::Time) {
				batch.DropField(dstCol, d);
				batch.AddField(srcCol, *s);
			}
			else {
				// …and a string of no limit given a length keeps what fits it (AlterField cuts the rest): the rows with a
				// character past the length are counted while they still have it
				if (cut != nullptr && d.m_type.m_kind == ibCanonicalKind::Text && s->m_type.m_kind == ibCanonicalKind::String)
					*cut += CountRows(batch, ibBinOp(ibQueryBinOp::Ne,
						ibCast(ibSubstring(ibCol(d.m_name), ibConst(ibValue(s->m_type.m_length + 1)), ibConst(ibValue(1))), ibTypeString(1)),
						ibConst(ibValue(ibString()))));
				batch.AlterField(srcCol, *s, d);
			}
		}
	}

	// ⭐ A NUMBER MADE NON-NEGATIVE KEEPS NO STORED VALUE BELOW ZERO (Max, 2026-11-10). The sign is the type's rule,
	// not the base's — the column stays the NUMERIC it was and no ALTER says anything — so the values already written
	// are brought to it here, as Adjust brings one written now: below zero is 0, and counted for the ledger like any
	// value a change empties.
	if (srcTypeDesc.IsNonNegative() && !dstTypeDesc.IsNonNegative())
		if (const ibColumnSlot* number = SlotOfRole(srcLayout, ibColumnRole::Number))
			if (SlotOfRole(dstLayout, ibColumnRole::Number) != nullptr)
				cleared += ClearRows(batch, { { number->m_name, ibConst(ibValue(0)) } }, ibBinOp(ibQueryBinOp::And,
					OfType(fieldName, ibPersistedTypeTag(ibColumnRole::Number)),
					ibBinOp(ibQueryBinOp::Lt, ibCol(number->m_name), ibConst(ibValue(0)))));

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
			batch.AddField(srcCol, *rt);
		if (const ibColumnSlot* rr = SlotOfRole(srcLayout, ibColumnRole::ReferenceId))
			batch.AddField(srcCol, *rr);
	}
	else if (oldHasRef && !newHasRef) {
		// Field LOST all references -> clear the rows that held one + DROP the shared pair (never a row
		// DELETE — that wipes records). The slots come off the OLD layout — they exist there because
		// oldHasRef is what this branch means.
		cleared += ClearRows(batch, *srcCol, OfType(fieldName, ibPersistedTypeTag(ibColumnRole::ReferenceType)));
		if (const ibColumnSlot* rt = SlotOfRole(dstLayout, ibColumnRole::ReferenceType))
			batch.DropField(dstCol, *rt);
		if (const ibColumnSlot* rr = SlotOfRole(dstLayout, ibColumnRole::ReferenceId))
			batch.DropField(dstCol, *rr);
	}
	else if (!removedRef.empty()) {
		// Still a reference, but SOME targets were dropped (incl. a full retarget) -> KEEP the shared pair;
		// the rows whose stored target (_RTRef) is gone hold the column's empty value, not a dead reference.
		for (auto clsid : removedRef)
			cleared += ClearRows(batch, *srcCol, OfTarget(fieldName, clsid));
	}
	return cleared;   // a real DB error THREW
}
