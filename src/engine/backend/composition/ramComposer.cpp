////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : ibDataRamComposer — the in-place RAM composition engine (L5-2)
////////////////////////////////////////////////////////////////////////////
//
// The RAM realisation of ibDataComposer, defined next to its declaration (ramComposer.h). It reads the RAM
// VALUE-STORAGE's live nodes DIRECTLY — never a query text / parse / lowering / queryable / ComputeRows copy —
// and produces the display ORDER (filtered + stable multi-key sorted storage indices). Grouping + the windowing
// of this order into a page live on the model side (ibValueModelStorage::RunComposerPage, modelRam.cpp); the
// per-row field resolution (incl. dot-walk over references) lives on the storage (ibRamValueStorage::SplitField
// / ResolveField). slice: filter + sort, flat order; dot-walk up to references.

#include "backend/composition/ramComposer.h"   // ibDataRamComposer
#include "backend/model.h"                  // ibRamValueStorage — RowCount / SplitField / ResolveField

// The DISPLAY-order source: filter + stable multi-key sort the storage's rows → their STORAGE indices in display
// order (index i ↔ storage node i). A field path is split into a HEAD storage column + a dotted TAIL walked over
// references per row (ibRamValueStorage::ResolveField). A path whose head is not a storage column is skipped: an
// unevaluable filter passes (never hides a row), an unevaluable sort drops out.
// ---------------------------------------------------------------------------
// The filter TREE, evaluated over a RAM row
// ---------------------------------------------------------------------------
//
// A DB source hands the condition to the engine and the engine lowers it. RAM
// has no engine — the rows are right here — so the same AST is evaluated
// directly against them. Same tree, same meaning; only the machinery differs.
//
// Without this the tree would be applied on a DB list and SILENTLY IGNORED on a
// RAM one — the same filter narrowing one list and not the other, which reads as
// "the filter is broken" and cannot be told apart from an empty result.

namespace {

// A side of a comparison, as a value: a Column reads the row, a Param and a
// Literal read themselves. Anything else (an arithmetic node, a function) is a
// shape the RAM path does not evaluate yet — the caller treats that as "cannot
// answer" rather than guessing.
bool RamSideValue(const ibQueryAstExpr& side, const ibRamValueStorage* storage, long row,
	const std::map<wxString, ibValue>& params, ibValue& out)
{
	switch (side.m_kind) {
	case ibQueryAstExprKind::Column: {
		wxString path;
		for (const wxString& seg : side.m_path)
			path += path.IsEmpty() ? seg : wxT(".") + seg;
		ibMetaID col; std::vector<wxString> tail;
		if (storage == nullptr || !storage->SplitField(path, col, tail))
			return false;
		out = storage->ResolveField(row, col, tail);
		return true;
	}
	case ibQueryAstExprKind::Param: {
		const auto it = params.find(side.m_paramName);
		if (it == params.end())
			return false;
		out = it->second;
		return true;
	}
	case ibQueryAstExprKind::Literal:
		out = side.m_literal;
		return true;
	default:
		return false;
	}
}

// Evaluate the condition for one row. `unknown` means the shape was not one this
// path understands — the row is KEPT, because hiding rows on the strength of a
// condition nobody evaluated is the one outcome a user cannot debug.
bool RamEvalCondition(const ibQueryAstExpr& expr, const ibRamValueStorage* storage, long row,
	const std::map<wxString, ibValue>& params, bool& unknown)
{
	switch (expr.m_kind) {
	case ibQueryAstExprKind::Logical: {
		if (!expr.m_lhs || !expr.m_rhs) { unknown = true; return true; }
		const bool lhs = RamEvalCondition(*expr.m_lhs, storage, row, params, unknown);
		const bool rhs = RamEvalCondition(*expr.m_rhs, storage, row, params, unknown);
		return expr.m_isOr ? (lhs || rhs) : (lhs && rhs);
	}
	case ibQueryAstExprKind::Not: {
		if (!expr.m_lhs) { unknown = true; return true; }
		return !RamEvalCondition(*expr.m_lhs, storage, row, params, unknown);
	}
	case ibQueryAstExprKind::Compare: {
		ibValue lhs, rhs;
		if (!expr.m_lhs || !expr.m_rhs
		 || !RamSideValue(*expr.m_lhs, storage, row, params, lhs)
		 || !RamSideValue(*expr.m_rhs, storage, row, params, rhs)) {
			unknown = true; return true;
		}
		switch (expr.m_cmp) {
		case ibQueryCompareOp::Ne: return lhs != rhs;
		case ibQueryCompareOp::Lt: return lhs <  rhs;
		case ibQueryCompareOp::Le: return lhs <= rhs;
		case ibQueryCompareOp::Gt: return lhs >  rhs;
		case ibQueryCompareOp::Ge: return lhs >= rhs;
		default:                   return lhs == rhs;
		}
	}
	case ibQueryAstExprKind::Like: {
		ibValue lhs, rhs;
		if (!expr.m_lhs || !expr.m_rhs
		 || !RamSideValue(*expr.m_lhs, storage, row, params, lhs)
		 || !RamSideValue(*expr.m_rhs, storage, row, params, rhs)) {
			unknown = true; return true;
		}
		// Same translation the flat RAM path uses — SQL wildcards to wx ones.
		wxString pat = rhs.GetString();
		pat.Replace(wxT("%"), wxT("*")); pat.Replace(wxT("_"), wxT("?"));
		const bool matched = lhs.GetString().Lower().Matches(pat.Lower());
		return expr.m_negated ? !matched : matched;
	}
	default:
		unknown = true;
		return true;
	}
}

} // namespace

std::vector<long> ibDataRamComposer::ComputeOrder() const
{
	if (m_storage == nullptr)
		return {};

	struct RamFilter { ibMetaID m_col; std::vector<wxString> m_tail; wxString m_op; ibValue m_value; };
	std::vector<RamFilter> filters;
	for (size_t i = 0; i < FilterCount(); ++i) {
		wxString path, op; ibValue value;
		if (!GetFilterAt(i, path, op, value)) continue;
		ibMetaID col; std::vector<wxString> tail;
		if (!m_storage->SplitField(path, col, tail)) continue;
		filters.push_back({ col, std::move(tail), op, value });
	}
	struct RamSort { ibMetaID m_col; std::vector<wxString> m_tail; bool m_ascending; };
	std::vector<RamSort> sorts;
	for (size_t i = 0; i < SortCount(); ++i) {
		wxString path; bool asc = true;
		if (!GetSortAt(i, path, asc)) continue;
		ibMetaID col; std::vector<wxString> tail;
		if (!m_storage->SplitField(path, col, tail)) continue;
		sorts.push_back({ col, std::move(tail), asc });
	}

	// One pass over the rows: evaluate the filters, and stash the sort keys (so the sort reads each row's key
	// ONCE up front, not per comparison). Reads cells straight off the storage's nodes (dot-tail hops references).
	const long n = m_storage->RowCount();
	struct Row { long m_idx; std::vector<ibValue> m_sortKeys; };
	std::vector<Row> rows;
	rows.reserve(static_cast<size_t>(n));
	for (long r = 0; r < n; ++r) {
		bool pass = true;

		// THE TREE, when there is one — evaluated first because it is the whole
		// condition, not one line of it.
		if (m_filterAst) {
			bool unknown = false;
			if (!RamEvalCondition(*m_filterAst, m_storage, r, m_params, unknown))
				continue;
		}

		for (const RamFilter& f : filters) {
			const ibValue cell = m_storage->ResolveField(r, f.m_col, f.m_tail);
			bool ok;
			if      (f.m_op == wxT("="))                          ok = (cell == f.m_value);
			else if (f.m_op == wxT("<>") || f.m_op == wxT("!="))  ok = (cell != f.m_value);
			else if (f.m_op == wxT(">"))                          ok = (cell >  f.m_value);
			else if (f.m_op == wxT(">="))                         ok = (cell >= f.m_value);
			else if (f.m_op == wxT("<"))                          ok = (cell <  f.m_value);
			else if (f.m_op == wxT("<="))                         ok = (cell <= f.m_value);
			else if (f.m_op.CmpNoCase(wxT("LIKE")) == 0) {
				wxString pat = f.m_value.GetString();
				pat.Replace(wxT("%"), wxT("*")); pat.Replace(wxT("_"), wxT("?"));
				ok = cell.GetString().Lower().Matches(pat.Lower());
			}
			else                                                  ok = true;   // unknown op → do not hide the row
			if (!ok) { pass = false; break; }
		}
		if (!pass)
			continue;
		Row row;
		row.m_idx = r;
		row.m_sortKeys.reserve(sorts.size());
		for (const RamSort& s : sorts)
			row.m_sortKeys.push_back(m_storage->ResolveField(r, s.m_col, s.m_tail));
		rows.push_back(std::move(row));
	}

	if (!sorts.empty()) {
		std::stable_sort(rows.begin(), rows.end(), [&](const Row& a, const Row& b) {
			for (size_t i = 0; i < sorts.size(); ++i) {
				const int c = a.m_sortKeys[i].CompareValueLS(b.m_sortKeys[i]);
				if (c != 0) return sorts[i].m_ascending ? (c < 0) : (c > 0);
			}
			return false;   // equal on all keys — stable_sort keeps the storage order
		});
	}

	std::vector<long> order;
	order.reserve(rows.size());
	for (const Row& row : rows)
		order.push_back(row.m_idx);
	return order;
}

// (ibDataRamComposer has NO Run override — the base no-op default is inherited. The RAM display path is
//  ComputeOrder + the live nodes windowed on the model side, so the driver-walk seam is unused for RAM; L5-2 is
//  fully self-contained.)
