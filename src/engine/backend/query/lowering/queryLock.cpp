////////////////////////////////////////////////////////////////////////////
//	ibLowering::ExecuteLock — a package's LOCK where it stands, the facade whole: the tables it names,
//	the keys its WHERE, its ON and a virtual table's condition give each of them, handed through the
//	door to each table's provider (ibDataQueryBuilder::Lock → ExecuteLock), which holds them its own
//	way — for the transaction itself, or given back for a TempDataManager alone to keep.
//	Speaks to the translation through queryLoweringInternal.h.
//
//	The rules are the user's: docs/private/record-locks.md, "Which records are locked".
////////////////////////////////////////////////////////////////////////////

#include "queryLoweringInternal.h"

#include "backend/lock/lockTypes.h"                // ibLockItem — the keys a table's provider is handed
#include "backend/query/queryable.h"               // ibSourceMetaDataScope::GetFactory
#include "backend/query/queryHierarchy.h"          // ibQueryHierarchyScope — the subtree an IN HIERARCHY names
#include "backend/query/queryTempStore.h"          // ibQueryTempDataStore — the LOCK kept under its name
#include "backend/query/queryableFactory.h"        // DescribeParameters — which argument of a virtual table is its condition
#include "core/query/queryRender.h"                // ibQuerySourceName, ibRenderQueryExpr
#include "core/query/queryRewrite.h"               // ibQueryFlattenAnd / ibQueryFoldAnd — a condition, one conjunct at a time

#include <algorithm>   // std::min — a source's table, its first two names; std::any_of, std::find_if
#include <set>

using namespace ibLowering;

namespace {

constexpr size_t kNone = static_cast<size_t>(-1);   // no table, no place

// ONE TABLE THE LOCK HOLDS — as written, and the table behind it.
struct ibHeldTable
{
	ibQuerySource                             m_source;      // as written: a virtual table keeps its arguments
	ibString                                  m_name;        // what the statement calls it — its alias, else its last name
	ibString                                  m_written;     // `<Kind>.<Object>` — what a refusal names
	std::shared_ptr<const ibBackendQueryable> m_table;       // that table — its provider holds the lock: a virtual table's is its table's
	std::shared_ptr<const ibBackendQueryable> m_q;           // the source as written — the fields a key may name
};

// A FIELD OF ONE OF THE LOCK'S TABLES, named as the table names it.
struct ibKeyField
{
	size_t   m_table = kNone;
	ibString m_name;
};

// ONE CONDITION, read as a key says it: `field = value`, `field IN (…)`, `(fields) IN (SELECT …)`, a range
// `field BETWEEN a AND b` / `field >= value`, or a tie `field = <other table>.field` — or as no key holds it
// (ReadTerm).
struct ibKeyTerm
{
	ibQueryAstExprPtr              m_written;              // as written — what a read without a table keeps
	std::vector<ibKeyField>        m_fields;               // one, a row's, a tie's two sides — or every field it reads
	std::vector<bool>              m_keys;                 // per field: whether this condition keys its table
	std::vector<ibQueryAstExprPtr> m_values;               // `= value` / `IN (list)`
	ibQueryAstExprPtr              m_from, m_to;           // a range: `>= from`, `< to`, BETWEEN both; null — open
	bool                           m_fromExcluded = false; // `>` — the bound itself is not in it
	bool                           m_toExcluded   = false; // `<`
	bool                           m_in = false;           // IN — a value that is a collection stands for its elements
	ibQueryDimUnfold               m_unfold = ibQueryDimUnfold::Elements;   // IN HIERARCHY — a value, for its subtree
	std::shared_ptr<ibQuerySelect> m_select;               // `IN (SELECT …)`
	bool                           m_tie = false;
	bool                           m_unkeyed = false;      // no key holds it: it narrows nothing, its names checked as a query's
	int                            m_join = -1;            // the join whose ON said it; -1 = the WHERE
	size_t                         m_argumentOf = kNone; // a virtual table's own condition — it stays in that table's call
};

// THE VALUES OF SOME FIELDS OF ONE TABLE — what one condition says of it. A table's keys are its sets met (Meet).
// Values are told apart as a key tells them, by their hash.
struct ibKeySet
{
	std::vector<ibString>             m_fields;
	std::vector<std::vector<ibValue>> m_tuples;   // one per key, its values in m_fields' order
};

// A FIELD A KEY CAN NAME — a field of one of the lock's tables, as the table names it: `Item`, `s.Item`. A walk through
// a reference (`Item.Code`) reads another table's row, and keys nothing here.
bool IsKeyField(const std::vector<ibHeldTable>& tables, const ibQueryAstExpr& e)
{
	if (e.m_kind != ibQueryAstExprKind::Column || e.m_arg || e.m_path.empty() || e.m_path.size() > 2)
		return false;
	if (e.m_path.size() == 1)
		return true;
	for (const ibHeldTable& table : tables)
		if (table.m_name.IsSameAs(e.m_path[0], false))
			return true;
	return false;
}

// A VALUE — an expression with no field of any table in it.
bool IsValue(const ibQueryAstExpr& e)
{
	if (e.m_kind == ibQueryAstExprKind::Column || e.m_subquery)
		return false;
	bool value = true;
	ibQueryForEachOperand(e, [&value](const ibQueryAstExprPtr& child) { if (child && !IsValue(*child)) value = false; });
	return value;
}

// Which table a column names, and the field it starts from — the field itself for a key field (IsKeyField), the one a
// walk starts from (`Item` of `Item.Code`) otherwise. `own` — a virtual table's condition, which reads its own fields only.
ibKeyField FieldOf(const std::vector<ibHeldTable>& tables, const ibQueryAstExpr& e, size_t own)
{
	ibKeyField field;
	if (e.m_path.size() >= 2)
		for (size_t t = 0; t < tables.size(); ++t)
			if (tables[t].m_name.IsSameAs(e.m_path[0], false)) {
				field.m_table = t;
				field.m_name  = e.m_path[1];
			}
	if (field.m_table == kNone) {
		field.m_name = e.m_path[0];
		if (own != kNone)
			field.m_table = own;
		else if (tables.size() == 1)
			field.m_table = 0;
		else
			for (size_t t = 0; t < tables.size(); ++t)
				if (tables[t].m_q->ResolveColumnByName(field.m_name) != nullptr) {
					if (field.m_table != kNone)
						ThrowQueryException(e.m_line, e.m_col,
							_("'%s' is a field of more than one table of this LOCK: name the table"), field.m_name);
					field.m_table = t;
				}
	}
	if (field.m_table == kNone)
		ThrowQueryException(e.m_line, e.m_col, _("no table of this LOCK has the field '%s'"), field.m_name);
	if (own != kNone && field.m_table != own)
		ThrowQueryException(e.m_line, e.m_col,
			_("the condition of '%s' reads its own fields, not '%s'"), tables[own].m_name, ibRenderQueryExpr(e));

	const std::shared_ptr<const ibBackendQueryColumn> column = tables[field.m_table].m_q->ResolveColumnByName(field.m_name);
	if (column == nullptr)
		ThrowQueryException(e.m_line, e.m_col, _("'%s' has no field '%s'"), tables[field.m_table].m_written, field.m_name);
	field.m_name = column->GetName();
	return field;
}

// THE FIELDS A CONDITION READS, each of the table it names (FieldOf). A subquery may read any table of the statement,
// so a condition holding one reads them all — each table, no field named.
std::vector<ibKeyField> FieldsOf(const std::vector<ibHeldTable>& tables, const ibQueryAstExpr& e, size_t own)
{
	std::vector<ibKeyField> fields;
	if (e.m_subquery) {
		for (size_t t = 0; t < tables.size(); ++t)
			fields.push_back({ t, ibString() });
		return fields;
	}
	if (e.m_kind == ibQueryAstExprKind::Column && !e.m_arg && !e.m_path.empty())
		fields.push_back(FieldOf(tables, e, own));
	ibQueryForEachOperand(e, [&tables, own, &fields](const ibQueryAstExprPtr& child) {
		if (child)
			for (ibKeyField& field : FieldsOf(tables, *child, own))
				fields.push_back(std::move(field));
	});
	return fields;
}

// One conjunct, read as a key — or as a condition no key holds.
ibKeyTerm ReadTerm(const std::vector<ibHeldTable>& tables, const ibQueryAstExprPtr& written, size_t own)
{
	// ⭐ ANY OTHER CONDITION IS A QUERY'S, written as in any query (Max, 2026-11-26) — `<>`, OR, LIKE, a walk through a
	// reference, a field against a field of its own table. It narrows nothing: the fields it reads are held whole, by
	// whatever else keys their table; and it stays in every read of the statement that keeps the tables it reads
	// (TiedSet). A range keys its field (Max, 2026-11-27: "I will just give the parameters of what to lock").
	const auto unkeyed = [&tables, &written, own] {
		ibKeyTerm term;
		term.m_written = written;
		term.m_fields  = FieldsOf(tables, *written, own);
		term.m_keys.assign(term.m_fields.size(), false);
		term.m_unkeyed = true;
		return term;
	};

	const ibQueryAstExpr& e = *written;
	ibKeyTerm term;
	term.m_written = written;

	if (e.m_kind == ibQueryAstExprKind::Compare && e.m_cmp == ibQueryCompareOp::Eq && e.m_lhs && e.m_rhs) {
		const bool lhsField = IsKeyField(tables, *e.m_lhs);
		const bool rhsField = IsKeyField(tables, *e.m_rhs);
		if (lhsField && rhsField) {
			term.m_tie = true;
			term.m_fields = { FieldOf(tables, *e.m_lhs, own), FieldOf(tables, *e.m_rhs, own) };
			// A field against a field of its own table keys nothing — it says which rows, not which values.
			if (term.m_fields[0].m_table == term.m_fields[1].m_table)
				return unkeyed();
		}
		else if (lhsField && IsValue(*e.m_rhs)) {
			term.m_fields = { FieldOf(tables, *e.m_lhs, own) };
			term.m_values = { e.m_rhs };
		}
		else if (rhsField && IsValue(*e.m_lhs)) {
			term.m_fields = { FieldOf(tables, *e.m_rhs, own) };
			term.m_values = { e.m_lhs };
		}
		else
			return unkeyed();
	}
	else if (e.m_kind == ibQueryAstExprKind::Compare && e.m_cmp != ibQueryCompareOp::Ne && e.m_lhs && e.m_rhs) {
		// ONE SIDE OF A RANGE — `field >= value`, `value < field`: the field's side whichever it is written on.
		const bool lhsField = IsKeyField(tables, *e.m_lhs) && IsValue(*e.m_rhs);
		if (!lhsField && !(IsKeyField(tables, *e.m_rhs) && IsValue(*e.m_lhs)))
			return unkeyed();
		term.m_fields = { FieldOf(tables, lhsField ? *e.m_lhs : *e.m_rhs, own) };
		const ibQueryAstExprPtr& value = lhsField ? e.m_rhs : e.m_lhs;
		const bool excluded = e.m_cmp == ibQueryCompareOp::Gt || e.m_cmp == ibQueryCompareOp::Lt;
		if ((e.m_cmp == ibQueryCompareOp::Gt || e.m_cmp == ibQueryCompareOp::Ge) == lhsField) {   // the field above it
			term.m_from         = value;
			term.m_fromExcluded = excluded;
		}
		else {
			term.m_to         = value;
			term.m_toExcluded = excluded;
		}
	}
	else if (e.m_kind == ibQueryAstExprKind::Between && !e.m_negated && e.m_lhs && e.m_low && e.m_high) {
		if (!IsKeyField(tables, *e.m_lhs) || !IsValue(*e.m_low) || !IsValue(*e.m_high))
			return unkeyed();
		term.m_fields = { FieldOf(tables, *e.m_lhs, own) };
		term.m_from   = e.m_low;
		term.m_to     = e.m_high;
	}
	else if (e.m_kind == ibQueryAstExprKind::In && !e.m_negated && e.m_lhs) {
		term.m_in     = true;
		term.m_unfold = e.m_unfold;   // IN HIERARCHY names its subtree by a field and a value, never a row or a select
		if (e.m_unfold != ibQueryDimUnfold::Elements && (e.m_subquery || e.m_lhs->m_kind == ibQueryAstExprKind::Row))
			return unkeyed();
		if (e.m_lhs->m_kind == ibQueryAstExprKind::Row) {
			if (!e.m_subquery)
				return unkeyed();
			for (const ibQueryAstExprPtr& element : e.m_lhs->m_args) {
				if (!element || !IsKeyField(tables, *element))
					return unkeyed();
				term.m_fields.push_back(FieldOf(tables, *element, own));
			}
			// A ROW IS ONE TABLE'S — its values are that table's keys together.
			for (const ibKeyField& field : term.m_fields)
				if (field.m_table != term.m_fields.front().m_table)
					return unkeyed();
		}
		else if (IsKeyField(tables, *e.m_lhs))
			term.m_fields = { FieldOf(tables, *e.m_lhs, own) };
		else
			return unkeyed();

		if (e.m_subquery)
			term.m_select = e.m_subquery;
		else {
			for (const ibQueryAstExprPtr& item : e.m_list)
				if (!item || !IsValue(*item))
					return unkeyed();
			term.m_values = e.m_list;
		}
	}
	else
		return unkeyed();

	term.m_keys.assign(term.m_fields.size(), true);
	return term;
}

// WHETHER A JOIN'S ON KEYS A TABLE. An outer join keeps the rows of its preserved side whatever ON says, so ON keys
// only the side it filters: LEFT — the joined table, RIGHT — the tables before it, FULL — neither.
bool OnKeys(ibQueryJoinKindAst kind, size_t table, size_t joined)
{
	switch (kind) {
	case ibQueryJoinKindAst::Inner: return true;
	case ibQueryJoinKindAst::Left:  return table == joined;
	case ibQueryJoinKindAst::Right: return table != joined;
	case ibQueryJoinKindAst::Full:  return false;
	}
	return true;
}

// A table of the LOCK: the source as written, and the table it stands for — `<Kind>.<Object>`, whatever a virtual
// table or a tabular section adds after it; a temporary table is its own. A virtual table's provider is the road it
// is read by, so the lock is that table's provider's — which may hold nothing (ExecuteLock below).
ibHeldTable HeldTable(const ibQuerySource& source, const std::map<ibString, ibValue>& params)
{
	ibHeldTable table;
	table.m_source = source;
	table.m_name   = ibQuerySourceName(source);

	ibQuerySource owner;
	owner.m_name.assign(source.m_name.begin(), source.m_name.begin() + std::min<size_t>(source.m_name.size(), 2));
	owner.m_line = source.m_line;
	owner.m_col  = source.m_col;
	for (const ibString& part : owner.m_name)
		table.m_written += (table.m_written.IsEmpty() ? ibString() : ibString(ibT("."))) + part;
	table.m_table = ResolveSource(owner, params);

	std::vector<ibQueryAstExprPtr> conditions;   // asked of the reading as written; the keys read them below
	table.m_q = source.m_name.size() <= 2 ? table.m_table : ResolveSource(source, params, &conditions);
	return table;
}

// A VIRTUAL TABLE'S CONDITION — the argument its source declares a predicate. Its other arguments (a period, a date)
// key nothing: `SliceLast(&Date, Product = &P)` locks every period of that product.
std::vector<ibQueryAstExprPtr> ConditionArguments(const ibQuerySource& source)
{
	std::vector<ibQueryAstExprPtr> conditions;
	if (source.m_args.empty() || source.m_name.size() < 2)
		return conditions;

	ibString name = source.m_name[1];
	for (size_t i = 2; i < source.m_name.size(); ++i)
		name += ibT(".") + source.m_name[i];
	ibQueryableFactory* factory = ibSourceMetaDataScope::GetFactory();
	ibQueryableSourceDescriptor* descriptor = factory != nullptr ? factory->FindDescriptor(source.m_name[0], name) : nullptr;
	std::vector<ibQuerySourceParameter> declared;
	if (descriptor != nullptr)
		descriptor->DescribeParameters(declared);

	for (size_t i = 0; i < source.m_args.size() && i < declared.size(); ++i)
		if (declared[i].m_condition && source.m_args[i])
			conditions.push_back(source.m_args[i]);
	return conditions;
}

// A selection drained — its rows, a column per output in its order.
std::vector<std::vector<ibValue>> RowsOf(const ibQuerySelect& select, const std::map<ibString, ibValue>& params,
                                         size_t& columns)
{
	std::vector<ibQueryOutputColumn> schema;
	ibDataQueryResult result = ibQueryLowering::Execute(select, params, schema);
	const ibQueryRamTable rows = DrainIntoSnapshot(result, schema);
	columns = schema.size();

	std::vector<std::vector<ibValue>> out;
	out.reserve(static_cast<size_t>(rows.RowCount()));
	for (long r = 0; r < rows.RowCount(); ++r) {
		std::vector<ibValue> row;
		row.reserve(columns);
		for (size_t c = 0; c < columns; ++c)
			row.push_back(rows.GetCell(r, static_cast<ibMetaID>(c + 1)));   // DrainIntoSnapshot numbers them 1..n
		out.push_back(std::move(row));
	}
	return out;
}

// A set made of its fields and tuples: a tuple holding NULL stands for no record (an outer join's missing side) and
// keys nothing; a field named twice keeps the tuples that agree on it, once.
ibKeySet MakeSet(std::vector<ibString> fields, std::vector<std::vector<ibValue>> tuples)
{
	ibKeySet set;
	std::vector<size_t> kept;      // the first place of each field
	std::vector<size_t> sameAs(fields.size(), kNone);
	for (size_t i = 0; i < fields.size(); ++i) {
		for (size_t k = 0; k < kept.size(); ++k)
			if (fields[kept[k]].IsSameAs(fields[i], false))
				sameAs[i] = kept[k];
		if (sameAs[i] == kNone) {
			kept.push_back(i);
			set.m_fields.push_back(fields[i]);
		}
	}
	for (std::vector<ibValue>& tuple : tuples) {
		bool keys = true;
		for (size_t i = 0; i < tuple.size() && keys; ++i)
			keys = tuple[i].GetType() != ibValueTypes::TYPE_NULL
				&& (sameAs[i] == kNone || tuple[i].GetValueHash() == tuple[sameAs[i]].GetValueHash());
		if (!keys)
			continue;
		std::vector<ibValue> key;
		key.reserve(kept.size());
		for (size_t i : kept)
			key.push_back(std::move(tuple[i]));
		set.m_tuples.push_back(std::move(key));
	}
	return set;
}

// TWO SETS OF ONE TABLE MET — a key of both is a key of each that agrees on the fields they share. Conditions joined
// by AND, said as keys.
ibKeySet Meet(const ibKeySet& a, const ibKeySet& b)
{
	std::vector<std::pair<size_t, size_t>> shared;   // (place in a, place in b)
	std::vector<size_t> added;                       // b's fields a has not
	for (size_t j = 0; j < b.m_fields.size(); ++j) {
		size_t in = kNone;
		for (size_t i = 0; i < a.m_fields.size(); ++i)
			if (a.m_fields[i].IsSameAs(b.m_fields[j], false))
				in = i;
		if (in != kNone)
			shared.emplace_back(in, j);
		else
			added.push_back(j);
	}

	ibKeySet met;
	met.m_fields = a.m_fields;
	for (size_t j : added)
		met.m_fields.push_back(b.m_fields[j]);
	for (const std::vector<ibValue>& x : a.m_tuples)
		for (const std::vector<ibValue>& y : b.m_tuples) {
			bool agree = true;
			for (const auto& [i, j] : shared)
				if (x[i].GetValueHash() != y[j].GetValueHash()) { agree = false; break; }
			if (!agree)
				continue;
			std::vector<ibValue> key = x;
			for (size_t j : added)
				key.push_back(y[j]);
			met.m_tuples.push_back(std::move(key));
		}
	return met;
}

// TWO RANGES OF ONE FIELD MET — the later from, the earlier to: `Period >= &A AND Period < &B`. A bound neither a date
// nor a number is open (ibLockRange); two of no order between them (a date against a number) keep the first's, which
// holds their meeting and more.
ibLockRange Meet(const ibLockRange& a, const ibLockRange& b)
{
	const auto ordered = [](const ibValue& v) {
		return v.GetType() == ibValueTypes::TYPE_DATE || v.GetType() == ibValueTypes::TYPE_NUMBER;
	};
	const auto order = [](const ibValue& x, const ibValue& y) {   // y against x; 0 — one, or no order between them
		if (x.GetType() != y.GetType())
			return 0;
		if (x.GetType() == ibValueTypes::TYPE_NUMBER)
			return y.GetNumber().Compare(x.GetNumber());
		return y.GetDate() < x.GetDate() ? -1 : x.GetDate() < y.GetDate() ? 1 : 0;
	};

	ibLockRange met = a;
	if (!ordered(met.from) || (ordered(b.from) && order(met.from, b.from) > 0))
		met.from = b.from;
	if (!ordered(met.to) || (ordered(b.to) && order(met.to, b.to) < 0))
		met.to = b.to;
	return met;
}

// THE VALUES A CONDITION GIVES ITS TABLE — written ones, the subtrees they name, or the rows of its select.
ibKeySet SetOf(const std::vector<ibHeldTable>& tables, const ibKeyTerm& term, const std::map<ibString, ibValue>& params)
{
	std::vector<ibString> fields;
	for (const ibKeyField& field : term.m_fields)
		fields.push_back(field.m_name);

	std::vector<std::vector<ibValue>> tuples;
	if (term.m_select) {
		size_t columns = 0;
		tuples = RowsOf(*term.m_select, params, columns);
		if (columns != fields.size())
			ThrowQueryException(term.m_written->m_line, term.m_written->m_col,
				_("IN (SELECT ...) in a LOCK selects as many fields as it is compared with: '%s'"), ibRenderQueryExpr(*term.m_written));
	}
	else if (term.m_unfold != ibQueryDimUnfold::Elements) {
		// IN HIERARCHY — the named values and everything under them, read through the field as a query reads it, as
		// the subtree stands now: an item put into the group later is not held.
		const std::shared_ptr<const ibBackendQueryable>& table = tables[term.m_fields.front().m_table].m_q;
		const ibQueryHierarchyScope subtree(table, table->ResolveColumnByName(fields.front()),
			ibQueryHierarchyNamedValues(term.m_values.empty() ? ibValue() : EvalValue(*term.m_values.front(), params)),
			term.m_unfold);
		for (const ibValue& value : subtree.Accepted())
			tuples.push_back({ value });
	}
	else
		for (const ibQueryAstExprPtr& written : term.m_values) {
			ibValue value = EvalValue(*written, params);
			// …and under IN a collection stands for its elements, as it does in a WHERE.
			std::shared_ptr<ibValueIteratorState> elements = term.m_in ? value.CreateIterator() : nullptr;
			if (elements == nullptr) {
				tuples.push_back({ value });
				continue;
			}
			ibValue element;
			while (elements->MoveNext(element))
				tuples.push_back({ element });
		}
	return MakeSet(std::move(fields), std::move(tuples));
}

// THE VALUES A RANGE GIVES ITS FIELD — both bounds in it, as the lock file keeps a range. A date's bound a `>` or `<`
// leaves out moves a millisecond inward, the date's own step; a number's stays in, the lock one value wider than asked.
ibLockRange RangeOf(const ibKeyTerm& term, const std::map<ibString, ibValue>& params)
{
	const auto bound = [&params](const ibQueryAstExprPtr& written, bool excluded, long long step) {
		if (!written)
			return ibValue();
		ibValue value = EvalValue(*written, params);
		if (excluded && value.GetType() == ibValueTypes::TYPE_DATE)
			value = ibValue(value.GetDate().AddMilliseconds(step));
		return value;
	};

	ibLockRange range;
	range.field = term.m_fields.front().m_name;
	range.from  = bound(term.m_from, term.m_fromExcluded, 1);
	range.to    = bound(term.m_to, term.m_toExcluded, -1);
	return range;
}

// THE STATEMENT READ WITHOUT ONE OF ITS TABLES — where the values a tie gives that table come from, so a value the
// table has no record of yet (a product with no price) is still locked. Every condition that names the table goes
// with it; whatever the rest keeps, the read keeps.
ibKeySet TiedSet(const std::vector<ibHeldTable>& tables, const ibQueryAstLock& lock, const std::vector<ibKeyTerm>& terms,
                 size_t without, const std::vector<ibKeyField>& tied, const std::vector<ibKeyField>& asked,
                 const std::map<ibString, ibValue>& params)
{
	const auto names = [without](const ibKeyTerm& term) {
		for (const ibKeyField& field : term.m_fields)
			if (field.m_table == without)
				return true;
		return false;
	};

	ibQuerySelect read;
	read.m_distinct = true;
	for (size_t i = 0; i < asked.size(); ++i) {
		ibQueryProjection projection;
		projection.m_expr = ibQueryAstExpr::Make(ibQueryAstExprKind::Column);
		projection.m_expr->m_path = { tables[asked[i].m_table].m_name, asked[i].m_name };
		projection.m_alias = ibString::Format(ibT("Key%u"), static_cast<unsigned>(i + 1));
		read.m_projections.push_back(std::move(projection));
	}

	std::vector<ibQueryAstExprPtr> where;
	bool first = true;
	for (size_t t = 0; t < tables.size(); ++t) {
		if (t == without)
			continue;
		std::vector<ibQueryAstExprPtr> on;
		for (const ibKeyTerm& term : terms)
			if (t > 0 && term.m_join == static_cast<int>(t - 1) && !names(term))
				on.push_back(term.m_written);
		if (first) {
			read.m_from = tables[t].m_source;
			where.insert(where.end(), on.begin(), on.end());   // the first table read has no join to hold it
			first = false;
			continue;
		}
		ibQueryAstJoin join;
		join.m_kind   = t > 0 ? lock.m_select->m_joins[t - 1].m_kind : ibQueryJoinKindAst::Inner;
		join.m_source = tables[t].m_source;
		join.m_on     = ibQueryFoldAnd(on);   // nothing left of it — every combination (a comma)
		read.m_joins.push_back(std::move(join));
	}
	for (const ibKeyTerm& term : terms)
		if (term.m_join < 0 && term.m_argumentOf == kNone && !names(term))
			where.push_back(term.m_written);
	read.m_where = ibQueryFoldAnd(where);

	std::vector<ibString> fields;
	for (const ibKeyField& field : tied)
		fields.push_back(field.m_name);
	size_t columns = 0;
	return MakeSet(std::move(fields), RowsOf(read, params, columns));
}

} // namespace

// ⭐⭐ WHO HOLDS IT is decided here, first: the `manager` the package was handed, alone — past the commit, with no
// transaction at all, until it is closed or gone, or an UNLOCK; without one, the transaction its statements run in, to
// its end — given to it by the provider, which refuses when none is open (the lock would end with the statement). Kept
// in the store under its name either way, where an UNLOCK further down (or, in a manager's store, in a later query)
// finds it: a manager's lets go then, the transaction's at its end.
long ibLowering::ExecuteLock(const ibQueryAstLock& lock, const std::map<ibString, ibValue>& params,
	ibQueryTempDataStore& temps, const ibQueryTempDataStore* manager)
{
	const ibQuerySelect& held = *lock.m_select;
	const ibQuerySource& at = held.m_from;
	if (!lock.m_name.IsEmpty() && temps.Has(lock.m_name))
		ThrowQueryException(at.m_line, at.m_col, _("'%s' already names a temporary table or a lock"), lock.m_name);

	// The tables, as written: the first, then each one joined.
	std::vector<ibHeldTable> tables;
	tables.push_back(HeldTable(held.m_from, params));
	for (const ibQueryAstJoin& join : held.m_joins)
		tables.push_back(HeldTable(join.m_source, params));

	// The conditions, read as keys — a virtual table's own, each join's ON, the WHERE.
	std::vector<ibKeyTerm> terms;
	std::vector<ibQueryAstExprPtr> conjuncts;
	for (size_t t = 0; t < tables.size(); ++t)
		for (const ibQueryAstExprPtr& condition : ConditionArguments(tables[t].m_source)) {
			conjuncts.clear();
			ibQueryFlattenAnd(condition, conjuncts);
			for (const ibQueryAstExprPtr& conjunct : conjuncts) {
				terms.push_back(ReadTerm(tables, conjunct, t));
				terms.back().m_argumentOf = t;
			}
		}
	for (size_t j = 0; j < held.m_joins.size(); ++j) {
		conjuncts.clear();
		ibQueryFlattenAnd(held.m_joins[j].m_on, conjuncts);
		for (const ibQueryAstExprPtr& conjunct : conjuncts) {
			ibKeyTerm term = ReadTerm(tables, conjunct, kNone);
			term.m_join = static_cast<int>(j);
			for (size_t f = 0; f < term.m_fields.size(); ++f)
				term.m_keys[f] = term.m_keys[f] && OnKeys(held.m_joins[j].m_kind, term.m_fields[f].m_table, j + 1);
			terms.push_back(std::move(term));
		}
	}
	conjuncts.clear();
	ibQueryFlattenAnd(held.m_where, conjuncts);
	for (const ibQueryAstExprPtr& conjunct : conjuncts)
		terms.push_back(ReadTerm(tables, conjunct, kNone));

	// A condition no key holds is a query's, and its names are checked as a query's: a field misspelt there would
	// otherwise narrow nothing in silence. The keys' own names FieldOf has resolved.
	if (std::any_of(terms.begin(), terms.end(), [](const ibKeyTerm& term) { return term.m_unkeyed; })) {
		ibQueryPackage named;
		named.m_statements.emplace_back().m_lock = std::make_shared<ibQueryAstLock>(lock);
		ibQueryLowering::CheckNames(named, params);
	}

	// EACH TABLE'S KEYS — its sets met, its ranges with them; a table no condition keys is locked whole — handed to its
	// provider through the door, which holds them its own way. A table whose provider holds nothing (a temporary table:
	// no other session writes it) is refused.
	const ibLockMode mode = lock.m_mode == ibQueryLockModeAst::Shared ? ibLockMode::Shared : ibLockMode::Exclusive;
	const auto holds = std::make_shared<std::vector<std::shared_ptr<void>>>();
	long keys = 0;
	for (size_t t = 0; t < tables.size(); ++t) {
		std::vector<ibLockItem> items;
		std::set<ibString> taken;   // one key once, however many ways the text said it
		const auto take = [&items, &taken](ibLockItem item) {
			if (taken.insert(item.KeyData()).second)
				items.push_back(std::move(item));
		};

		std::vector<ibKeySet> sets;
		std::vector<ibLockRange> ranges;   // a field each, its conditions met
		std::vector<ibKeyField> tied, asked;
		for (const ibKeyTerm& term : terms) {
			if (term.m_unkeyed)
				continue;
			if (term.m_tie) {
				for (size_t side = 0; side < 2; ++side)
					if (term.m_fields[side].m_table == t && term.m_keys[side]) {
						tied.push_back(term.m_fields[side]);
						asked.push_back(term.m_fields[1 - side]);
					}
				continue;
			}
			if (term.m_fields.front().m_table != t || !term.m_keys.front())
				continue;
			if (!term.m_from && !term.m_to) {
				sets.push_back(SetOf(tables, term, params));
				continue;
			}
			const ibLockRange range = RangeOf(term, params);
			const auto same = std::find_if(ranges.begin(), ranges.end(),
				[&range](const ibLockRange& other) { return other.field.IsSameAs(range.field, false); });
			if (same == ranges.end())
				ranges.push_back(range);
			else
				*same = Meet(*same, range);
		}
		if (!tied.empty())
			sets.push_back(TiedSet(tables, lock, terms, t, tied, asked, params));

		// No set — one key of no field, the ranges its own; a field a value keys has no range, the value being the
		// narrower. Neither — the table whole.
		ibKeySet key;
		key.m_tuples.emplace_back();
		if (!sets.empty()) {
			key = sets.front();
			for (size_t s = 1; s < sets.size(); ++s)
				key = Meet(key, sets[s]);
		}
		std::erase_if(ranges, [&key](const ibLockRange& range) {
			return std::any_of(key.m_fields.begin(), key.m_fields.end(),
				[&range](const ibString& field) { return field.IsSameAs(range.field, false); });
		});
		for (const std::vector<ibValue>& tuple : key.m_tuples) {
			ibLockItem item = ibLockItem::ForNamespace(ibEmptyString, mode);
			for (size_t f = 0; f < key.m_fields.size(); ++f)
				item.keyFields.emplace_back(key.m_fields[f], tuple[f]);
			item.keyRanges = ranges;
			take(std::move(item));
		}

		// Taken — waited out while another session holds a key they share, as the provider waits; without a manager the
		// provider gives them to the transaction itself (WithLock), or refuses with none open.
		ibDataQueryBuilder door;
		door.From(tables[t].m_table);
		if (manager == nullptr)
			door.WithLock(mode);
		std::shared_ptr<void> hold = door.Lock(items);
		if (hold == nullptr)
			ThrowQueryException(tables[t].m_source.m_line, tables[t].m_source.m_col,
				_("LOCK holds tables of the base: '%s' is not one"), tables[t].m_written);
		holds->push_back(std::move(hold));
		keys += static_cast<long>(items.size());
	}

	// The holds mean nothing to whoever keeps them: emptied, the locks go.
	ibJournalInfo(ibT("query"), ibT("lock: %ld key(s) of %u table(s)"), keys, static_cast<unsigned>(tables.size()));
	temps.Put(lock.m_name, holds);
	return keys;
}
