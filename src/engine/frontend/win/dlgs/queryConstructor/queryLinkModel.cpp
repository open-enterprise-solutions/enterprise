////////////////////////////////////////////////////////////////////////////
//	Description : The links grid's model over m_joins (queryLinkModel.h)
////////////////////////////////////////////////////////////////////////////

#include "queryLinkModel.h"

#include "backend/query/queryRender.h"
#include "backend/query/queryParser.h"   // a condition typed into a cell is read by the ENGINE
#include "backend/query/queryRewrite.h"  // ibQueryTrueLiteral / ibQueryIsTrueLiteral — "no link", spelled

namespace {

// The two boxes ↔ the kind now live in the HEADER (ibJoinAllFromLeft / ibJoinAllFromRight /
// ibJoinKindOf), because the package's own link grid folds them the same way — and one fold written
// twice is two places where a ticked box could stop meaning the same thing.
constexpr auto AllFromLeft  = &ibJoinAllFromLeft;
constexpr auto AllFromRight = &ibJoinAllFromRight;
constexpr auto KindOf       = &ibJoinKindOf;

} // namespace

void ibQueryLinkModel::SetContent(ibQuerySelect* select, std::vector<wxString> tableNames)
{
	m_select = select;
	m_tableNames = std::move(tableNames);
	const size_t joins = m_select != nullptr ? m_select->m_joins.size() : 0u;
	// GROWN, NOT REBUILT: a fill happens after every edit, and throwing these away on each one would
	// take the row with them the moment anything else in the query was touched.
	m_pending.resize(joins, false);
	m_freehand.resize(joins, false);

	// THE ROWS ARE THE LINKS — a join that carries one, or one the author has started.
	//
	// ⚠ `ON TRUE` IS NOT A LINK. It is how the query text SAYS there is none (the product), because a
	// JOIN with nothing after it reads as unfinished and the engine refuses it. Here it reads as what
	// it means: a table with no link, and so no row.
	m_rows.clear();
	for (size_t i = 0; i < joins; ++i)
		if ((m_select->m_joins[i].m_on && !ibQueryIsTrueLiteral(m_select->m_joins[i].m_on)) || m_pending[i])
			m_rows.push_back(i);
	Reset(static_cast<unsigned int>(m_rows.size()));
}

size_t ibQueryLinkModel::JoinIndexOf(unsigned int row) const
{
	return row < m_rows.size() ? m_rows[row] : static_cast<size_t>(-1);
}

size_t ibQueryLinkModel::FirstUnlinked() const
{
	if (m_select == nullptr)
		return static_cast<size_t>(-1);
	for (size_t i = 0; i < m_select->m_joins.size(); ++i) {
		const bool linked = m_select->m_joins[i].m_on
			&& !ibQueryIsTrueLiteral(m_select->m_joins[i].m_on);
		if (!linked && (i >= m_pending.size() || !m_pending[i]))
			return i;
	}
	return static_cast<size_t>(-1);
}

wxString ibQueryLinkModel::UnfinishedLink() const
{
	if (m_select == nullptr)
		return wxString();
	for (size_t i = 0; i < m_select->m_joins.size(); ++i) {
		if (i >= m_pending.size() || !m_pending[i])
			continue;   // no link was asked for on this table — nothing to finish
		const ibQueryAstExprPtr& on = m_select->m_joins[i].m_on;
		if (!on || ibQueryIsTrueLiteral(on))
			return ibQuerySourceName(m_select->m_joins[i].m_source);
	}
	return wxString();
}

void ibQueryLinkModel::BeginLink(size_t joinIndex)
{
	if (m_select == nullptr || joinIndex >= m_select->m_joins.size())
		return;
	if (joinIndex >= m_pending.size())
		m_pending.resize(joinIndex + 1, false);
	m_pending[joinIndex] = true;
	SetContent(m_select, m_tableNames);   // the row appears
}

void ibQueryLinkModel::RemoveLink(unsigned int row)
{
	const size_t index = JoinIndexOf(row);
	if (m_select == nullptr || index >= m_select->m_joins.size())
		return;
	// THE LINK GOES AND THE TABLE STAYS — back to no condition at all, which is the product. (A
	// hand-written `ON TRUE` says the same thing and is read the same way by the row list above.)
	m_select->m_joins[index].m_on = nullptr;
	// ⚠ AND THE KIND GOES WITH IT. "All rows of X" is a sentence about a LINK — with none, there is
	// nothing for a row to fail to match, and every row already stands against every other. Leaving
	// `Left` behind on a table whose link was deleted left the query saying something nobody meant,
	// invisibly: the Links tab has no row for it, so there is not even a box to untick.
	m_select->m_joins[index].m_kind = ibQueryJoinKindAst::Inner;
	if (index < m_pending.size())
		m_pending[index] = false;
	if (index < m_freehand.size())
		m_freehand[index] = false;
	// The join ENTRY stays: it is the table, and removing a table is the Tables tab's verb.
	if (m_onChanged)
		m_onChanged();
}

bool ibQueryLinkModel::CopyLink(unsigned int row)
{
	const ibQueryAstJoin* from = JoinAt(row);
	if (m_select == nullptr || from == nullptr || !from->m_on || ibQueryIsTrueLiteral(from->m_on))
		return false;   // an empty row has nothing to copy

	const size_t target = FirstUnlinked();
	if (target == static_cast<size_t>(-1))
		return false;   // every table already carries a link

	// THE CONDITION IS SHARED, NOT CLONED — the AST node is refcounted and nothing here mutates one
	// in place: writing the cell PARSES a new expression and replaces the pointer. Copying the
	// pointer is therefore a copy of the text, which is what "copy the link" means.
	m_select->m_joins[target].m_on   = from->m_on;
	m_select->m_joins[target].m_kind = from->m_kind;   // "all rows of…" is part of the link, so it comes too
	if (target < m_pending.size())
		m_pending[target] = false;   // it carries a condition now — it is a link on its own account
	if (m_onChanged)
		m_onChanged();
	return true;
}

bool ibQueryLinkModel::IsSimple(const ibQueryAstJoin& join)
{
	// NO CONDITION IS A LINK — "by reference", which the list offers as its first entry. It is the
	// simplest link there is, not the one case only free text can write.
	if (!join.m_on)
		return true;
	// Anything else has to be ONE equality between two plain columns for the drop-down to be able to
	// stand for it: `a.x = b.y`. An AND chain, a comparison against a value, a CASE — those are what
	// the switch exists to let a person write.
	return join.m_on->m_kind == ibQueryAstExprKind::Compare
		&& join.m_on->m_cmp == ibQueryCompareOp::Eq
		&& join.m_on->m_lhs && join.m_on->m_lhs->m_kind == ibQueryAstExprKind::Column
		&& join.m_on->m_rhs && join.m_on->m_rhs->m_kind == ibQueryAstExprKind::Column;
}

bool ibQueryLinkModel::IsArbitrary(unsigned int row) const
{
	const ibQueryAstJoin* join = JoinAt(row);
	if (join == nullptr)
		return true;
	if (!IsSimple(*join))
		return true;   // the observation wins — a switch cannot take away a shape the link does not have
	const size_t index = JoinIndexOf(row);
	return index < m_freehand.size() && m_freehand[index];
}

ibQueryAstJoin* ibQueryLinkModel::JoinAt(unsigned int row) const
{
	const size_t index = JoinIndexOf(row);
	if (m_select == nullptr || index >= m_select->m_joins.size())
		return nullptr;
	return &m_select->m_joins[index];
}

// The table a column path stands on, or empty when it names none of this query's tables.
static wxString ibQualifierOf(const ibQueryAstExprPtr& e, const std::vector<wxString>& tables)
{
	if (!e || e->m_kind != ibQueryAstExprKind::Column || e->m_path.size() < 2)
		return wxString();
	for (const wxString& table : tables)
		if (!table.IsEmpty() && table.IsSameAs(e->m_path[0], false))
			return table;
	return wxString();
}

wxString ibQueryLinkModel::LeftName(unsigned int row) const
{
	// ⭐ THE OTHER SIDE IS WHATEVER THE CONDITION SAYS IT IS — read off the link, not assumed.
	//
	// This used to answer with the FROM every time, on the reasoning that the AST is a left-deep
	// chain so the left-hand side "is" the primary source. The chain is real, but the SENTENCE is
	// not about it: `Catalog1.Attribute7 = SliceLast.Dimension3` relates those two tables, and a
	// link may perfectly well be written against a table joined earlier rather than against the
	// first one. Showing the first regardless meant the cell contradicted the condition beside it.
	const ibQueryAstJoin* join = JoinAt(row);
	if (join != nullptr && join->m_on
	    && join->m_on->m_kind == ibQueryAstExprKind::Compare) {
		const wxString self = RightName(row);
		const wxString lhs = ibQualifierOf(join->m_on->m_lhs, m_tableNames);
		const wxString rhs = ibQualifierOf(join->m_on->m_rhs, m_tableNames);
		if (!lhs.IsEmpty() && !lhs.IsSameAs(self, false)) return lhs;
		if (!rhs.IsEmpty() && !rhs.IsSameAs(self, false)) return rhs;
	}
	// Nothing written yet, or a condition no single pair describes: the primary source is the
	// honest default — it is the one table every join is guaranteed to have behind it.
	return !m_tableNames.empty() ? m_tableNames[0] : wxString();
}

wxString ibQueryLinkModel::RightName(unsigned int row) const
{
	const size_t join = JoinIndexOf(row);
	if (join == static_cast<size_t>(-1))
		return wxString();
	const size_t index = join + 1;   // joins[i] is source index i+1
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
		// ⚠ AN EMPTY CONDITION SHOWS AS EMPTY — it is a link that has NOT BEEN SET, and that is an
		// ordinary state: the tab becomes available as soon as there are two tables, and the links on
		// it are chosen by hand, one field against another.
		//
		// It said "(by reference)" here, which announced a link nobody had asked for and turned an
		// empty row into a statement about the query. Blank is the truth: nothing has been picked yet.
		variant = (join->m_on && !ibQueryIsTrueLiteral(join->m_on))
			? ibRenderQueryExpr(*join->m_on) : wxString();
		break;
	case kLinkColArbitrary:
		variant = IsArbitrary(row);
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

	switch (col) {
	// ⭐ THE TABLE THIS LINK IS WRITTEN ON — changing it MOVES the link, it does not add or remove a
	// table. That is the whole difference the new rows made: a row is a LINK, so re-pointing it is an
	// ordinary edit of that link. Before this the cell offered a drop-down and then refused every
	// choice in it, which is the worst of both — the window promised something it would not do.
	case kLinkColRightTable: {
		const wxString wanted = variant.GetString();
		const size_t from = JoinIndexOf(row);
		size_t to = static_cast<size_t>(-1);
		for (size_t i = 0; i < m_select->m_joins.size(); ++i)
			if (ibQuerySourceName(m_select->m_joins[i].m_source).IsSameAs(wanted, false)) { to = i; break; }

		if (to == static_cast<size_t>(-1) || to == from)
			return false;   // the same table, or a name this query does not read
		if (m_select->m_joins[to].m_on && !ibQueryIsTrueLiteral(m_select->m_joins[to].m_on)) {
			if (m_onError)
				m_onError(wxString::Format(
					_("'%s' already carries a link: delete it first, or write this one on another table"),
					wanted));
			return false;
		}

		m_select->m_joins[to].m_on   = m_select->m_joins[from].m_on;
		m_select->m_joins[to].m_kind = m_select->m_joins[from].m_kind;
		m_select->m_joins[from].m_on   = nullptr;
		m_select->m_joins[from].m_kind = ibQueryJoinKindAst::Inner;   // the kind belongs to the link
		if (from < m_pending.size()) m_pending[from] = false;
		if (to   < m_pending.size()) m_pending[to]   = m_select->m_joins[to].m_on == nullptr;
		break;
	}
	// AND THE OTHER SIDE IS EDITED BY REWRITING THE CONDITION, because that is where it lives — the
	// AST has no "left table" field. Only the simple shape (`a.x = b.y`) can be re-pointed this way;
	// anything else is the author's own sentence and is left alone rather than half-rewritten.
	case kLinkColLeftTable: {
		const wxString wanted = variant.GetString();
		if (wanted.IsSameAs(LeftName(row), false))
			return false;
		if (!join->m_on || join->m_on->m_kind != ibQueryAstExprKind::Compare) {
			if (m_onError)
				m_onError(_("write the link first: the table on this side is read from the condition"));
			return false;
		}
		const wxString self = RightName(row);
		ibQueryAstExprPtr side =
			!ibQualifierOf(join->m_on->m_lhs, m_tableNames).IsSameAs(self, false)
				? join->m_on->m_lhs : join->m_on->m_rhs;
		if (!side || side->m_kind != ibQueryAstExprKind::Column || side->m_path.size() < 2) {
			if (m_onError)
				m_onError(_("this side of the link is not a plain field: change it in the condition"));
			return false;
		}
		// ⚠ A COPY, NOT AN EDIT IN PLACE. The expression is shared (a copied link hands the same node
		// to two rows), so writing through the pointer would change somebody else's link too.
		ibQueryAstExprPtr moved = std::make_shared<ibQueryAstExpr>(*side);
		moved->m_path[0] = wanted;
		if (side == join->m_on->m_lhs) join->m_on->m_lhs = moved;
		else                           join->m_on->m_rhs = moved;
		break;
	}
	case kLinkColAllLeft:
		join->m_kind = KindOf(variant.GetBool(), AllFromRight(join->m_kind));
		break;
	case kLinkColAllRight:
		join->m_kind = KindOf(AllFromLeft(join->m_kind), variant.GetBool());
		break;
	case kLinkColCondition: {
		// TYPED WHERE IT STANDS, and read by the ENGINE. The expression editor is behind the "..."
		// for anything longer; emptying the cell UNSETS the link and leaves the table where it is.
		wxString text = variant.GetString();
		text.Trim(true).Trim(false);
		if (text.IsEmpty()) {
			join->m_on = nullptr;
			break;
		}
		ibQueryAstExprPtr written;
		try {
			ibQueryParser parser;
			written = parser.ParseExpression(text);
		}
		catch (const ibBackendException& error) {
			if (m_onError)
				m_onError(error.GetErrorDescription());
			return false;
		}
		// WHETHER THE LINKS CONTRADICT ONE ANOTHER IS ASKED OF THE ENGINE, not decided here — it
		// re-reads the whole query after this edit and answers on the verdict line.
		join->m_on = written;
		break;
	}
	case kLinkColArbitrary: {
		// ⭐ THE SWITCH CHANGES THE EDITOR, NOT THE LINK. Ticked, the condition cell is free text with
		// the "..."; cleared, it is a closed list of the field pairs the two tables offer. The join
		// itself — its kind, its ON, the query text — is untouched either way.
		//
		// Clearing it on a link that is not one equality between two columns is refused: obeying could
		// only mean rewriting the author's `a.x = b.y AND a.z = b.w` into something the list can hold,
		// which is not an edit anybody asked for.
		const bool wanted = variant.GetBool();
		if (!wanted && !IsSimple(*join)) {
			if (m_onError)
				m_onError(_("this link is not a single field-to-field equality: it can only be written by hand"));
			return false;
		}
		const size_t index = JoinIndexOf(row);
		if (index >= m_freehand.size())
			m_freehand.resize(index + 1, false);
		m_freehand[index] = wanted;
		// THE ROW REDRAWS AND NOTHING ELSE — the query did not change, so the text is not re-rendered
		// and the tabs are not refilled (which would reset this grid and lose the selection).
		RowChanged(row);
		return true;
	}
	default:
		return false;
	}

	if (m_onChanged)
		m_onChanged();
	return true;
}
