#include "queryLowering.h"

#include "frmclient/backend/compiler/value.h"
#include "core/query/queryKeywords.h"   // ibIsAggregateKeyword

#include <wx/msgdlg.h>

namespace {

// ⚠ NOT HERE YET — said ONCE: the window checks at every edit, and a box at every keystroke would be noise.
void SayLoweringNotHereYet()
{
	static bool s_said = false;
	if (s_said)
		return;
	s_said = true;
	wxMessageBox(_("The query constructor in this client does not check the names and the grouping of a query against "
		"the configuration yet. The query is checked when it runs."), _("Query constructor"), wxOK | wxICON_INFORMATION);
}

} // namespace

// ⚠ NOT HERE YET — the engine's name check runs the lowering over the configuration's tables, which no request carries
// (query-constructor-doors.md, question 4). Unverifiable is silence, as on the desktop.
void ibQueryLowering::CheckNames(const ibQueryPackage& /*package*/, const std::map<wxString, ibValue>& /*params*/)
{
	SayLoweringNotHereYet();
}

// ⚠ NOT HERE YET — the same lowering (query-constructor-doors.md, question 4). Unverifiable is empty, as on the desktop.
std::vector<ibQueryAstExprPtr> ibQueryLowering::UngroupedProjections(const ibQuerySelect& /*ast*/,
	const std::map<wxString, ibValue>& /*params*/)
{
	return {};
}

namespace {

// WHICH COLUMNS A PROJECTION FOLDS — the desktop's CollectFoldedAndFree (backend/query/queryLowering.cpp), the half the
// constructor asks: a column under a GROUP aggregate is folded, however deep; a WINDOW aggregate folds nothing.
void CollectFolded(const ibQueryAstExprPtr& e, bool insideAggregate, std::vector<const ibQueryAstExpr*>& folded)
{
	if (!e)
		return;
	if (e->m_kind == ibQueryAstExprKind::Column) {
		if (insideAggregate)
			folded.push_back(e.get());
		return;
	}
	const bool fold = insideAggregate
		|| (e->m_kind == ibQueryAstExprKind::Func && ibIsAggregateKeyword(e->m_func) && !e->m_over);
	ibQueryForEachOperand(*e, [&](const ibQueryAstExprPtr& child) {
		CollectFolded(child, fold, folded);
	});
	if (e->m_over) {
		for (const ibQueryAstExprPtr& partition : e->m_over->m_partitionBy)
			CollectFolded(partition, insideAggregate, folded);
		for (const ibQueryOrderItem& order : e->m_over->m_orderBy)
			CollectFolded(order.m_expr, insideAggregate, folded);
	}
}

// A COLUMN NODE STANDING ON ITS OWN (the desktop's ColumnCopy).
ibQueryAstExprPtr ColumnCopy(const ibQueryAstExpr& column)
{
	ibQueryAstExprPtr copy = ibQueryAstExpr::Make(ibQueryAstExprKind::Column);
	copy->m_path = column.m_path;
	copy->m_line = column.m_line;
	copy->m_col  = column.m_col;
	return copy;
}

} // namespace

// THE DESKTOP'S CollectAggregated — the written expressions only, nothing of the configuration.
std::vector<ibQueryAstExprPtr> ibQueryLowering::AggregatedColumns(const ibQuerySelect& ast)
{
	std::vector<ibQueryAstExprPtr> out;
	for (const ibQueryProjection& projection : ast.m_projections) {
		std::vector<const ibQueryAstExpr*> folded;
		CollectFolded(projection.m_expr, false, folded);
		for (const ibQueryAstExpr* column : folded)
			if (column != nullptr && !column->m_path.empty())
				out.push_back(ColumnCopy(*column));
	}
	return out;
}
