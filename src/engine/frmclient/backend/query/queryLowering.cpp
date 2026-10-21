#include "queryLowering.h"

#include "core/query/queryKeywords.h"   // ibIsAggregateKeyword

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
