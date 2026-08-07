////////////////////////////////////////////////////////////////////////////
//	Description : The links grid's model over m_joins (queryLinkModel.h)
////////////////////////////////////////////////////////////////////////////

#include "queryLinkModel.h"

#include "backend/query/queryRender.h"
#include "backend/query/queryParser.h"   // a condition typed into a cell is read by the ENGINE

namespace {

// The two boxes, read off the kind. Written out both ways in one place so the four states and the
// four kinds cannot drift apart.
bool AllFromLeft(ibQueryJoinKindAst kind)
{
	return kind == ibQueryJoinKindAst::Left || kind == ibQueryJoinKindAst::Full;
}

bool AllFromRight(ibQueryJoinKindAst kind)
{
	return kind == ibQueryJoinKindAst::Right || kind == ibQueryJoinKindAst::Full;
}

ibQueryJoinKindAst KindOf(bool allLeft, bool allRight)
{
	if (allLeft && allRight) return ibQueryJoinKindAst::Full;
	if (allLeft)             return ibQueryJoinKindAst::Left;
	if (allRight)            return ibQueryJoinKindAst::Right;
	return ibQueryJoinKindAst::Inner;   // neither box: only the rows that match
}

} // namespace

void ibQueryLinkModel::SetContent(ibQuerySelect* select, std::vector<wxString> tableNames)
{
	m_select = select;
	m_tableNames = std::move(tableNames);
	Reset(m_select != nullptr ? static_cast<unsigned int>(m_select->m_joins.size()) : 0u);
}

ibQueryAstJoin* ibQueryLinkModel::JoinAt(unsigned int row) const
{
	if (m_select == nullptr || row >= m_select->m_joins.size())
		return nullptr;
	return &m_select->m_joins[row];
}

wxString ibQueryLinkModel::LeftName(unsigned int /*row*/) const
{
	// EVERY JOIN ATTACHES TO THE QUERY'S PRIMARY SOURCE as the AST models it — a left-deep chain,
	// so the left-hand side a reader sees is the FROM. Saying anything else here would be inventing
	// a shape the query does not have.
	return !m_tableNames.empty() ? m_tableNames[0] : wxString();
}

wxString ibQueryLinkModel::RightName(unsigned int row) const
{
	const size_t index = static_cast<size_t>(row) + 1;   // joins[row] is source index row+1
	return index < m_tableNames.size() ? m_tableNames[index] : wxString();
}

void ibQueryLinkModel::GetValueByRow(wxVariant& variant, unsigned row, unsigned col) const
{
	const ibQueryAstJoin* join = JoinAt(row);
	if (join == nullptr)
		return;   // the view can paint a row the list no longer has (a Reset lands after the paint)

	switch (col) {
	case kLinkColLeftTable:
		variant = LeftName(row);
		break;
	case kLinkColAllLeft:
		variant = AllFromLeft(join->m_kind);
		break;
	case kLinkColRightTable:
		variant = RightName(row);
		break;
	case kLinkColAllRight:
		variant = AllFromRight(join->m_kind);
		break;
	case kLinkColCondition:
		// AN EMPTY CONDITION IS NOT "nothing" — it means the join follows the reference between the
		// tables, which is a different statement from `ON TRUE`. The cell says so rather than
		// leaving a blank that reads as unfinished.
		variant = join->m_on ? ibRenderQueryExpr(*join->m_on) : wxString(_("(by reference)"));
		break;
	default:
		break;
	}
}

bool ibQueryLinkModel::SetValueByRow(const wxVariant& variant, unsigned row, unsigned col)
{
	ibQueryAstJoin* join = JoinAt(row);
	if (join == nullptr)
		return false;

	// The table cells describe the query's SHAPE — changing one means adding or removing a table,
	// which is the Tables tab's verb, not a cell edit.
	switch (col) {
	case kLinkColAllLeft:
		join->m_kind = KindOf(variant.GetBool(), AllFromRight(join->m_kind));
		break;
	case kLinkColAllRight:
		join->m_kind = KindOf(AllFromLeft(join->m_kind), variant.GetBool());
		break;
	case kLinkColCondition: {
		// TYPED WHERE IT STANDS, and read by the ENGINE. The expression editor is still a
		// double-click away for anything longer; emptying the cell puts the join back to "by
		// reference", which is what a null condition MEANS here (not `ON TRUE`).
		wxString text = variant.GetString();
		text.Trim(true).Trim(false);
		if (text.IsEmpty() || text == _("(by reference)")) {
			join->m_on = nullptr;
			break;
		}
		try {
			ibQueryParser parser;
			join->m_on = parser.ParseExpression(text);
		}
		catch (const ibBackendException& error) {
			if (m_onError)
				m_onError(error.GetErrorDescription());
			return false;
		}
		break;
	}
	default:
		return false;
	}

	if (m_onChanged)
		m_onChanged();
	return true;
}
