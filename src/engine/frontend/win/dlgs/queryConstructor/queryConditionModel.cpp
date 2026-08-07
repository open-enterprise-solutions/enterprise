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
	Reset(static_cast<unsigned int>(Rows().size()));
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
	case ibQueryAstExprKind::In:      return leftIsColumn && !condition->m_negated && !condition->m_subquery;
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
		variant = !IsSimple(condition);
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
	// "Arbitrary" is an OBSERVATION about the condition, not a setting on it — ticking it could only
	// mean "rewrite this into something I cannot decompose", which is not an edit anybody means. The
	// number is the row's position. Neither is written.
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
