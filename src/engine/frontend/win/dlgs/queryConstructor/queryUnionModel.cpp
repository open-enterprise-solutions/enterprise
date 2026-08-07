////////////////////////////////////////////////////////////////////////////
//	Description : The unions tab's models — branches, and the field map
//	              (queryUnionModel.h)
////////////////////////////////////////////////////////////////////////////

#include "queryUnionModel.h"

#include "backend/query/queryRender.h"
#include "backend/query/queryLexer.h"   // IsIdentifier — the language decides what a name can be

// ---------------------------------------------------------------------------
//  The branches
// ---------------------------------------------------------------------------

void ibQueryUnionModel::SetContent(ibQuerySelect* select)
{
	m_select = select;
	// Row 0 is THIS query. A union's first branch is the query itself, and hiding that would make
	// the numbering lie about which branch a field column belongs to.
	Reset(m_select != nullptr ? static_cast<unsigned int>(m_select->m_unions.size() + 1) : 0u);
}

ibQuerySelect* ibQueryUnionModel::BranchAt(unsigned int row) const
{
	if (m_select == nullptr || row == 0 || row > m_select->m_unions.size())
		return nullptr;
	return m_select->m_unions[row - 1].get();
}

void ibQueryUnionModel::GetValueByRow(wxVariant& variant, unsigned row, unsigned col) const
{
	if (m_select == nullptr)
		return;

	switch (col) {
	case kUnionColName:
		variant = wxString::Format(_("Query %u"), row + 1);
		break;
	case kUnionColKeepDuplicates: {
		// THE COLUMN SAYS "No duplicates", AND THAT IS THE NEGATION OF WHAT THE AST STORES.
		// `m_unionAll` means UNION ALL — keep them. Returning it raw put a box labelled "no
		// duplicates" that ticked when duplicates were KEPT, which is a control that lies.
		//
		// The flag belongs to the branch being ATTACHED (the operator sits between two branches),
		// so row 0 has none — its box is off and disabled rather than pretending to be a setting.
		const ibQuerySelect* branch = BranchAt(row);
		variant = branch != nullptr && !branch->m_unionAll;
		break;
	}
	default:
		break;
	}
}

bool ibQueryUnionModel::SetValueByRow(const wxVariant& variant, unsigned row, unsigned col)
{
	if (col != kUnionColKeepDuplicates)
		return false;
	ibQuerySelect* branch = BranchAt(row);
	if (branch == nullptr)
		return false;

	branch->m_unionAll = !variant.GetBool();   // the box says "no duplicates"; the AST stores "all"
	if (m_onChanged)
		m_onChanged();
	return true;
}

bool ibQueryUnionModel::IsEnabledByRow(unsigned row, unsigned col) const
{
	if (col == kUnionColKeepDuplicates)
		return BranchAt(row) != nullptr;   // nothing to keep duplicates OF on the first branch
	return true;
}

// ---------------------------------------------------------------------------
//  The field map
// ---------------------------------------------------------------------------

void ibQueryUnionFieldModel::SetContent(ibQuerySelect* select)
{
	m_select = select;
	m_names.clear();
	m_branchCount = 0;

	if (m_select == nullptr) {
		Reset(0);
		return;
	}

	// THE OUTPUT FIELDS ARE THE FIRST BRANCH'S. Not a merge of every branch's columns: the union's
	// result IS the first branch's shape, and a row for a name only the third branch has would
	// promise a column the query does not produce.
	//
	// ⚠ ONE ROW PER PROJECTION — including the ones with NO name yet. This grid used to be built by
	// collecting NAMES, so a projection that had none (`SELECT 1`, any expression, an aggregate
	// before it is named) was silently absent from the only place an alias can be typed. The fields
	// that most need a name were exactly the fields you could not name. Rows are projections now,
	// and the name is what the row SHOWS, not what makes it exist.
	for (const ibQueryProjection& projection : m_select->m_projections)
		m_names.push_back(ibQueryOutputName(projection));

	m_branchCount = static_cast<unsigned int>(m_select->m_unions.size() + 1);
	Reset(static_cast<unsigned int>(m_names.size()));
}

// (The name a branch's table is known by is ibQuerySourceName — this file spelled it out again,
//  a survivor of the same collapse that took the other seven copies.)

wxString ibQueryUnionFieldModel::ColumnOf(const ibQuerySelect& branch, const wxString& name) const
{
	// SELECT * offers everything it reads, and by name — so it lines up, and saying so is more
	// honest than showing "none" for a branch that will in fact supply the column.
	if (branch.m_selectAll && branch.m_projections.empty())
		return wxT("*");

	for (const ibQueryProjection& projection : branch.m_projections) {
		if (!ibQueryOutputName(projection).IsSameAs(name, false))
			continue;
		if (!projection.m_expr)
			return name;

		const wxString text = ibRenderQueryExpr(*projection.m_expr);
		// ⚠ QUALIFY A BARE COLUMN WITH ITS TABLE. Without this every column of this grid showed the
		// same word — the alias on the left and the source on the right, spelled identically — and
		// the tab read as though it had no aliases in it at all. A branch cell answers "where does
		// this field come from", so it says the table; the first column answers "what is it called",
		// so it says the name alone. The difference has to be VISIBLE or the grid teaches nothing.
		const wxString table = ibQuerySourceName(branch.m_from);
		if (!table.IsEmpty() && projection.m_expr->m_kind == ibQueryAstExprKind::Column
		    && projection.m_expr->m_path.size() == 1)
			return table + wxT(".") + text;
		return text;
	}
	return wxEmptyString;
}

void ibQueryUnionFieldModel::GetValueByRow(wxVariant& variant, unsigned row, unsigned col) const
{
	if (m_select == nullptr || row >= m_names.size())
		return;

	if (col == kUnionColName) {
		// A NAMED FIELD SHOWS ITS NAME. An unnamed one shows what it IS — the expression — because a
		// blank cell in a column called "Alias" reads as "already filled in with nothing", and the
		// author has no way to tell which row is the `1` they just added.
		if (!m_names[row].IsEmpty())
			variant = m_names[row];
		else if (row < m_select->m_projections.size() && m_select->m_projections[row].m_expr)
			variant = ibRenderQueryExpr(*m_select->m_projections[row].m_expr);
		return;
	}

	// Column N (from 2) is branch N-2: the first branch is this query, the rest are the unions.
	const unsigned int branchIndex = col - kUnionColName - 1;
	if (branchIndex >= m_branchCount)
		return;

	// BRANCH 0 IS THIS QUERY, and its cell is the projection standing on this row — no lookup, no
	// name needed. Looking it up by name showed `<none>` beside a field the query plainly selects,
	// for the only reason that the field had not been given a name yet.
	if (branchIndex == 0) {
		const ibQueryProjection& projection = m_select->m_projections[row];
		const wxString text = projection.m_expr ? ibRenderQueryExpr(*projection.m_expr) : wxString();
		const wxString table = ibQuerySourceName(m_select->m_from);
		variant = (!table.IsEmpty() && projection.m_expr
		           && projection.m_expr->m_kind == ibQueryAstExprKind::Column
		           && projection.m_expr->m_path.size() == 1)
			? table + wxT(".") + text : text;
		return;
	}

	const ibQuerySelect* branch = branchIndex - 1 < m_select->m_unions.size()
		? m_select->m_unions[branchIndex - 1].get() : nullptr;
	if (branch == nullptr)
		return;

	const wxString column = ColumnOf(*branch, m_names[row]);
	// `<none>` IS THE ANSWER, not a blank: a branch with no column of that name contributes nothing
	// to it, and an empty cell would read as "not filled in yet".
	variant = column.IsEmpty() ? wxString(_("<none>")) : column;
}

// THE FIELD NAME IS THE ALIAS, and this is where a union asks for it. The output name of a query is
// what its first branch calls the column, and what every other branch is lined up against — so
// renaming it here is not a label change, it is naming the field of the RESULT. Writing it sets the
// alias on the projection it stands for; the expression is untouched.
bool ibQueryUnionFieldModel::SetValueByRow(const wxVariant& variant, unsigned row, unsigned col)
{
	if (col != kUnionColName || m_select == nullptr || row >= m_select->m_projections.size())
		return false;

	wxString name = variant.GetString();
	name.Trim(true).Trim(false);
	if (name.IsSameAs(m_names[row], false))
		return false;   // not a change

	auto refuse = [this](const wxString& why) {
		if (m_onError)
			m_onError(why);
		return false;
	};

	if (name.IsEmpty())
		return refuse(_("a field of the result has to have a name"));

	// ⚠ CHECKED THE MOMENT IT IS TYPED, and by the LANGUAGE. An output name goes into the query text
	// after AS, so what can be one is the lexer's definition and nobody else's — accepting
	// `Total sum` here meant writing it out and then showing the engine's lexical error about text
	// the window itself produced.
	if (!ibQueryLexer::IsIdentifier(name))
		return refuse(wxString::Format(
			_("'%s' cannot be a field name: one word, letters, digits and underscores, "
			  "no spaces or punctuation."), name));

	// A NAME ALREADY TAKEN IS REFUSED, not silently numbered. Numbering is right when the
	// constructor ADDS a duplicate on the author's behalf; here the author typed a specific name,
	// and quietly storing a different one would be answering a question they did not ask.
	//
	// And it is not cosmetic: the result is read back BY NAME — a union lines its branches up by it,
	// a temp table's columns and index are these names — so two the same are ambiguous in earnest.
	for (size_t i = 0; i < m_names.size(); ++i)
		if (i != row && m_names[i].IsSameAs(name, false))
			return refuse(wxString::Format(
				_("another field of the result is already called '%s'"), name));

	// ⚠ BY ROW, NOT BY NAME. The row IS the projection — which is the whole point of the change:
	// looking the projection up by its old name could not find one that had no name, so the fields
	// that needed an alias were exactly the ones this refused to give one to.
	m_select->m_projections[row].m_alias = name;
	m_names[row] = name;
	if (m_onChanged)
		m_onChanged();
	return true;
}
