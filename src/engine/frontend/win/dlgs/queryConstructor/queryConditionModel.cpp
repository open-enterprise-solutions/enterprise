////////////////////////////////////////////////////////////////////////////
//	Description : The conditions grid's model over the WHERE chain
//	              (queryConditionModel.h)
////////////////////////////////////////////////////////////////////////////

#include "queryConditionModel.h"

#include "backend/query/queryRender.h"
#include "backend/query/queryRewrite.h"   // ibQueryFlattenAnd / ibQueryFoldAnd — the AND chain, in one place
#include "backend/query/queryParser.h"   // a condition typed into a cell is read by the ENGINE


void ibQueryConditionModel::SetContent(ibQuerySelect* select)
{
	m_select = select;
	const size_t count = Rows().size();
	// ⚠ GROWN, NOT REBUILT. `assign` here wiped the switches on every fill — and a fill follows every
	// edit, including the one that SET a switch, so ticking "Arbitrary" turned itself back off in the
	// same breath. Whether a row is written by hand is the author's answer and outlives the refill.
	m_freehand.resize(count, false);
	Reset(static_cast<unsigned int>(count));
}

bool ibQueryConditionModel::IsArbitrary(unsigned int row) const
{
	// THE OBSERVATION FIRST. A condition that cannot be read as field · comparison · value IS
	// arbitrary whoever ticks what — the switch can only ever ADD freedom, never take away a shape
	// the condition does not have.
	if (!IsSimple(RowAt(row)))
		return true;
	return row < m_freehand.size() && m_freehand[row];
}

std::vector<ibQueryAstExprPtr> ibQueryConditionModel::Rows() const
{
	// ⚠ BOTH CLAUSES. This tab is "the conditions of this query", not "the contents of WHERE" —
	// which clause a condition lands in is the ENGINE's call (a condition over a folded value is a
	// HAVING), and it is answered in the query TEXT. Reading WHERE alone meant the row an author had
	// just written disappeared from the grid the moment the rule moved it, leaving a condition that
	// is plainly in the query and nowhere to be edited or removed.
	std::vector<ibQueryAstExprPtr> rows;
	if (m_select != nullptr) {
		ibQueryFlattenAnd(m_select->m_where, rows);
		ibQueryFlattenAnd(m_select->m_having, rows);
	}
	return rows;
}

void ibQueryConditionModel::SetRows(const std::vector<ibQueryAstExprPtr>& rows)
{
	if (m_select == nullptr)
		return;
	// EVERY ROW GOES BACK AS ONE SET, then the rule splits it again — so a condition that stops
	// being an aggregate (edited from `SUM(Qty) > 1` to `Qty > 1`) comes back to WHERE by the same
	// door it left by, instead of being stranded in HAVING.
	m_select->m_having = nullptr;
	m_select->m_where = ibQueryFoldAnd(rows);
	// ⭐ A CONDITION OVER A FOLDED VALUE IS A HAVING, and it moves NOW rather than at execution.
	// The tab offers aggregate fields beside plain ones, so a condition over SUM(x) is written here
	// like any other; the engine's rewrite would have moved it anyway, but on a clone — leaving the
	// author looking at a WHERE while the engine ran a HAVING. Same rule, applied where the AST is
	// actually held, so the text says what the query is.
	ibQueryMoveAggregateConditionsToHaving(*m_select);
	// The switch belongs to the ROW, and the rows are these — so the flags follow the list's length.
	// Growing keeps what was set (an edit does not un-ask for free text); shrinking drops the tail.
	m_freehand.resize(rows.size(), false);
	Reset(static_cast<unsigned int>(rows.size()));
	if (m_onChanged)
		m_onChanged();
}

ibQueryAstExprPtr ibQueryConditionModel::RowAt(unsigned int row) const
{
	const std::vector<ibQueryAstExprPtr> rows = Rows();
	return row < rows.size() ? rows[row] : nullptr;
}

bool ibQueryConditionModel::IsSimple(const ibQueryAstExprPtr& condition)
{
	if (!condition)
		return false;
	// The left side has to be a plain column for the row to read as *field · comparison · value*.
	const bool leftIsColumn = condition->m_lhs && condition->m_lhs->m_kind == ibQueryAstExprKind::Column;
	switch (condition->m_kind) {
	case ibQueryAstExprKind::Compare: return leftIsColumn;
	case ibQueryAstExprKind::Like:    return leftIsColumn && !condition->m_negated;
	case ibQueryAstExprKind::IsNull:  return leftIsColumn;
	// ⚠ AN UNFOLD WORD MAKES IT NOT SIMPLE. `x IN HIERARCHY (&p)` reads as field · comparison · value
	// and is not one: the drop-down has no such comparison, so offering it would mean rebuilding the
	// row without the word the author wrote — a filter silently widened from a subtree to a list.
	// Free text is the honest editor until the choices learn the two words.
	case ibQueryAstExprKind::In:      return leftIsColumn && !condition->m_negated && !condition->m_subquery
	                                      && condition->m_unfold == ibQueryDimUnfold::Elements;
	default:                          return false;
	}
}

void ibQueryConditionModel::GetValueByRow(wxVariant& variant, unsigned row, unsigned col) const
{
	const ibQueryAstExprPtr condition = RowAt(row);
	if (!condition)
		return;   // the view can paint a row the chain no longer has (a Reset lands after the paint)

	switch (col) {
	case kConditionColNumber:
		variant = wxString::Format(wxT("%u"), row + 1);
		break;
	case kConditionColArbitrary:
		variant = IsArbitrary(row);
		break;
	case kConditionColText:
		variant = ibRenderQueryExpr(*condition);
		break;
	default:
		break;
	}
}

bool ibQueryConditionModel::SetValueByRow(const wxVariant& variant, unsigned row, unsigned col)
{
	// ⭐ "ARBITRARY" SWITCHES THE EDITOR, not the condition. Ticked, the cell beside it is free text
	// with the "..." into the expression editor; cleared, it is a drop-down of the conditions the
	// engine can build over this query's fields. The query text is the same either way — which is
	// what makes it a presentation flag and not a property of the query.
	//
	// Clearing it on a condition that CANNOT be read as field · comparison · value is refused rather
	// than obeyed: obeying could only mean rewriting the author's `CASE …` or `a.x = b.y` into a
	// shape it does not have. The observation is the answer there, and the box says so by going
	// straight back on.
	if (col == kConditionColArbitrary) {
		const bool wanted = variant.GetBool();
		if (!wanted && !IsSimple(RowAt(row))) {
			if (m_onError)
				m_onError(_("this condition is not a field, a comparison and a value: it can only be written by hand"));
			return false;
		}
		if (row >= m_freehand.size())
			m_freehand.resize(row + 1, false);
		m_freehand[row] = wanted;
		// ⚠ THE ROW REDRAWS; THE WORLD DOES NOT. `m_onChanged` is "the AST changed" — it re-renders the
		// text and refills every tab, which resets this grid and takes the SELECTION with it. Ticking a
		// box that changes nothing about the query left nothing selected, so the very next Delete had
		// no row to work on: "the conditions are broken, I cannot delete".
		RowChanged(row);
		return true;
	}

	// The number is the row's position — read, never written.
	if (col != kConditionColText)
		return false;

	std::vector<ibQueryAstExprPtr> rows = Rows();
	if (row >= rows.size())
		return false;

	// THE CONDITION IS TYPED WHERE IT STANDS, and the ENGINE reads it — the same parser the runtime
	// uses. An empty cell DELETES the row, because a condition with no text is not a condition; a
	// text the parser refuses leaves the old one alone and hands the complaint up verbatim.
	wxString text = variant.GetString();
	text.Trim(true).Trim(false);
	if (text.IsEmpty()) {
		rows.erase(rows.begin() + row);
		SetRows(rows);
		return true;
	}

	try {
		ibQueryParser parser;
		rows[row] = parser.ParseExpression(text);
	}
	catch (const ibBackendException& error) {
		if (m_onError)
			m_onError(error.GetErrorDescription());
		return false;
	}
	SetRows(rows);
	return true;
}
