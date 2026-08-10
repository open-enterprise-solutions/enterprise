////////////////////////////////////////////////////////////////////////////
//	Description : The unions tab's models — branches, and the field map
//	              (queryUnionModel.h)
////////////////////////////////////////////////////////////////////////////

#include "queryUnionModel.h"

#include "backend/query/queryRender.h"
#include "backend/query/queryLexer.h"   // IsIdentifier — the language decides what a name can be
#include "backend/query/queryParser.h"  // a field picked for a branch is read by the ENGINE

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

// ⭐ THE FIELD KEEPS ITS OWN NAME, and only a collision adds a number — `Code`, then `Code1`,
// `Code2`. A field unlined from a row is still that field; naming it `Field7` throws away the one
// piece of information the author had about it, and then the new row at the bottom is a puzzle.
//
// Asked across EVERY branch, not just the result: a name free in the first one but taken in the
// third would line those two up by accident, which is the very thing unlining was undoing.
wxString ibQueryUnionFieldModel::FreeOutputName(const wxString& wanted) const
{
	const auto taken = [this](const wxString& name) {
		if (m_select == nullptr)
			return false;
		for (const ibQueryProjection& projection : m_select->m_projections)
			if (ibQueryOutputName(projection).IsSameAs(name, false)) return true;
		for (const ibQuerySelectPtr& branch : m_select->m_unions)
			if (branch)
				for (const ibQueryProjection& projection : branch->m_projections)
					if (ibQueryOutputName(projection).IsSameAs(name, false)) return true;
		return false;
	};
	const wxString base = wanted.IsEmpty() ? wxString(wxT("Field")) : wanted;
	if (!taken(base))
		return base;
	for (unsigned int n = 1; ; ++n) {
		const wxString candidate = base + wxString::Format(wxT("%u"), n);
		if (!taken(candidate))
			return candidate;
	}
}

ibQuerySelect* ibQueryUnionFieldModel::BranchSelectAt(unsigned int branch) const
{
	if (m_select == nullptr)
		return nullptr;
	if (branch == 0)
		return m_select;                       // branch 0 IS this query
	const size_t index = static_cast<size_t>(branch) - 1;
	return index < m_select->m_unions.size() ? m_select->m_unions[index].get() : nullptr;
}

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
		const wxString text = !m_names[row].IsEmpty()
			? m_names[row]
			: (row < m_select->m_projections.size() && m_select->m_projections[row].m_expr
				? ibRenderQueryExpr(*m_select->m_projections[row].m_expr) : wxString());

		// AND ITS PICTURE, like every other field list in this window. These rows ARE fields — the
		// output of the union — so a plain text column made them read as something else than the
		// same fields three tabs away. The icon is asked of the column (GetColumnIcon) through the
		// host's resolver; unresolved (a free expression, a name nothing answers to) falls back to
		// the plain field picture, which is what such a row is.
		// ⚠ ALWAYS AN ICON-TEXT VALUE, even with no icon. The column is drawn by an icon-text
		// renderer, and handing it a plain string leaves the cell EMPTY — the mirror of the bug this
		// replaced, where a text renderer was handed an icon-text and drew nothing. A value's shape
		// has to match the renderer unconditionally, not "when there is a picture": the rows without
		// one (a hidden system field, a free expression) are exactly the rows that then vanish.
		variant << ibDataViewIconText(text, m_iconReader ? m_iconReader(row) : wxNullIcon);
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
// ⭐⭐ A BRANCH CELL IS THE LINE-UP, AND IT IS EDITED BY HAND.
//
// The union's result is the first branch's shape; every other branch has to say WHICH of its own
// fields stands under each output field. Reading that off by name is right most of the time and
// impossible the rest of it — two branches over different tables rarely agree on spelling — so the
// cell offers that branch's fields and the author points.
//
// AN EMPTY CHOICE IS AN ANSWER: this branch supplies nothing here, and the column is NULL for its
// rows. That is a real thing to want (a branch that has no such field at all), and it is the state
// the cell already showed as `<none>`.
//
// The mapping is stored where the engine reads it — as a PROJECTION of that branch named after the
// output field. Nothing new is invented to hold it: the lowering lines branches up by name, so
// naming the projection IS the mapping.
bool ibQueryUnionFieldModel::SetBranchColumn(unsigned int row, unsigned int branch, const wxString& field)
{
	ibQuerySelect* target = BranchSelectAt(branch);
	if (target == nullptr || row >= m_names.size())
		return false;

	const wxString outputName = m_names[row];

	// BRANCH 0 IS THIS QUERY, and its cell is the projection standing on this row — no lookup by
	// name, because the row IS that projection (and it may not have a name yet).
	if (branch == 0) {
		if (row >= target->m_projections.size())
			return false;

		// ⭐⭐ CLEARING THE FIRST BRANCH UNLINES IT TOO — and it takes one more step than the others,
		// because the ROWS ARE this branch's projections. Rename it away and the row goes with it,
		// taking the other branches' fields off the list with nothing to stand under.
		//
		// So the row is kept and emptied: a NULL projection under the SAME output name stays where it
		// was (branch 1 now reads NULL there, the other branches keep their fields), and the field
		// that was cleared moves to a row of its own at the bottom under its own name.
		//
		// (It was refused outright here, on the reasoning that the result's field is Delete's
		// business. That confused two acts: unlining a branch from a name, and deleting the field.)
		if (field.IsEmpty()) {
			const wxString keptName = m_names[row];
			ibQueryProjection moved = target->m_projections[row];

			ibQueryProjection none;
			none.m_expr = ibQueryAstExpr::Make(ibQueryAstExprKind::Literal);
			none.m_expr->m_literal.SetType(ibValueTypes::TYPE_NULL);
			none.m_alias = keptName;
			target->m_projections[row] = none;

			wxString wanted;
			if (moved.m_expr && moved.m_expr->m_kind == ibQueryAstExprKind::Column && !moved.m_expr->m_path.empty())
				wanted = moved.m_expr->m_path.back();
			moved.m_alias = FreeOutputName(wanted);
			target->m_projections.push_back(moved);

			if (m_onChanged) m_onChanged();
			return true;
		}
		try {
			ibQueryParser parser;
			target->m_projections[row].m_expr = parser.ParseExpression(field);
		}
		catch (const ibBackendException& error) {
			if (m_onError) m_onError(error.GetErrorDescription());
			return false;
		}
		if (m_onChanged) m_onChanged();
		return true;
	}

	if (outputName.IsEmpty()) {
		if (m_onError)
			m_onError(_("name the result's field first: a branch is lined up against that name"));
		return false;
	}

	// Find what this branch already supplies under that name.
	size_t at = target->m_projections.size();
	for (size_t i = 0; i < target->m_projections.size(); ++i)
		if (ibQueryOutputName(target->m_projections[i]).IsSameAs(outputName, false)) { at = i; break; }

	// ⭐⭐ CLEARING A CELL DOES NOT THROW THE FIELD AWAY — IT UNLINES IT.
	//
	// Emptying this cell says "this branch's field does not belong under THAT output name", not
	// "delete it". So the field keeps its expression and gets an output name of its own: a new row
	// appears at the bottom, where it stands alone. The row it left now reads NULL for this branch,
	// and the new row reads NULL for every other one — which is exactly what the two states mean.
	//
	// Deleting it for good is then a separate act, on the row where it now lives. That is the whole
	// difference between unlining and destroying, and a cell that did the second when asked for the
	// first is a cell nobody dares to touch.
	if (field.IsEmpty()) {
		if (at >= target->m_projections.size())
			return false;   // nothing stood here — nothing to unline

		// ITS OWN NAME FIRST — the field's last path segment (`Catalog2.Code` → `Code`), or what the
		// expression renders as when it is not a plain column.
		wxString wanted;
		const ibQueryAstExprPtr& held = target->m_projections[at].m_expr;
		if (held && held->m_kind == ibQueryAstExprKind::Column && !held->m_path.empty())
			wanted = held->m_path.back();
		else if (held)
			wanted = ibQueryOutputName(target->m_projections[at]);

		const wxString fresh = FreeOutputName(wanted);
		target->m_projections[at].m_alias = fresh;

		// THE RESULT'S SHAPE IS THE FIRST BRANCH'S, so the new field has to exist there for the row
		// to exist at all — as NULL, which is precisely "this branch supplies nothing here".
		if (branch != 0 && m_select != nullptr) {
			ibQueryProjection none;
			none.m_expr = ibQueryAstExpr::Make(ibQueryAstExprKind::Literal);
			none.m_expr->m_literal.SetType(ibValueTypes::TYPE_NULL);
			none.m_alias = fresh;
			m_select->m_selectAll = false;
			m_select->m_projections.push_back(none);
		}
		if (m_onChanged) m_onChanged();
		return true;
	}

	ibQueryAstExprPtr expr;
	try {
		ibQueryParser parser;
		expr = parser.ParseExpression(field);
	}
	catch (const ibBackendException& error) {
		if (m_onError) m_onError(error.GetErrorDescription());
		return false;
	}

	if (at < target->m_projections.size()) {
		target->m_projections[at].m_expr = expr;
		target->m_projections[at].m_alias = outputName;
	}
	else {
		ibQueryProjection projection;
		projection.m_expr  = expr;
		projection.m_alias = outputName;   // named after the output field — that IS the line-up
		target->m_selectAll = false;       // it selects specific fields now, not everything
		target->m_projections.push_back(projection);
	}
	if (m_onChanged) m_onChanged();
	return true;
}

bool ibQueryUnionFieldModel::SetValueByRow(const wxVariant& variant, unsigned row, unsigned col)
{
	// A BRANCH COLUMN — column N + 1 is branch N.
	if (col > kUnionColName)
		return SetBranchColumn(row, col - kUnionColName - 1, variant.GetString());

	if (col != kUnionColName || m_select == nullptr || row >= m_select->m_projections.size())
		return false;

	// ⚠ THE VALUE COMES BACK IN THE SHAPE THE RENDERER USES, and this column is drawn by the icon-text
	// one — so what arrives is an `ibDataViewIconText`, whose `GetString()` is EMPTY. Reading it as a
	// plain string meant every rename committed nothing: first it raised "a field of the result has to
	// have a name" over a name that had been typed, and once that modal was quietened, it renamed
	// silently to nothing at all.
	//
	// A cell that is drawn one way and read another is the same bug this window has had three times
	// now (the condition list, the alias icon, this). ASK THE VARIANT WHAT IT IS.
	wxString name;
	if (variant.GetType() == ibDataViewIconTextRenderer::GetDefaultType()) {
		ibDataViewIconText iconText;
		iconText << variant;
		name = iconText.GetText();
	}
	else {
		name = variant.GetString();
	}
	name.Trim(true).Trim(false);
	if (name.IsSameAs(m_names[row], false))
		return false;   // not a change

	auto refuse = [this](const wxString& why) {
		if (m_onError)
			m_onError(why);
		return false;
	};

	// ⚠ AN EMPTY COMMIT IS NOT AN EDIT, AND CERTAINLY NOT AN ERROR.
	//
	// There is no gesture here that means "take this field's name away" — a result field always has
	// one — so an empty string can only have come from the editor handing back nothing, which a cell
	// does whenever it is opened and closed without typing. A double-click on a row therefore raised
	// a modal complaining about something the author had not done.
	//
	// (It said the right thing about the wrong event. The rule stands: the name is never stored
	// empty. It is enforced by refusing to WRITE it, not by scolding whoever opened the cell.)
	if (name.IsEmpty())
		return false;

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
