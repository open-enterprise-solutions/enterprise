////////////////////////////////////////////////////////////////////////////
//	Description : The query constructor - AST -> tabs (which statement, and what each pane shows)
//	              (queryConstructor.h)
////////////////////////////////////////////////////////////////////////////

#include "queryConstructorInternal.h"

using namespace queryctor;

// ===========================================================================
//  The AST being edited
// ===========================================================================

// THE STATEMENT'S OWN SELECT — the one that owns the union branches, the INTO, the FOR UPDATE.
ibQuerySelect* ibDialogQueryConstructor::StatementSelect() const
{
	if (m_statement >= m_package.m_statements.size())
		return nullptr;
	const ibQueryAstStatement& statement = m_package.m_statements[m_statement];
	if (!statement.m_select)
		return nullptr;   // a DROP statement has no select — the tabs show nothing, correctly
	return statement.m_select.get();
}

// WHAT THE TABS EDIT. Normally the statement itself; when a union BRANCH is selected on the strip
// down the right edge, that branch — because a branch is a query with its own tables, conditions
// and ordering, and the only sane way to edit one is with the same tabs that edit any other query.
ibQuerySelect* ibDialogQueryConstructor::Current() const
{
	ibQuerySelect* select = StatementSelect();
	if (select == nullptr || m_unionBranch < 0)
		return select;
	return static_cast<size_t>(m_unionBranch) < select->m_unions.size()
		? select->m_unions[static_cast<size_t>(m_unionBranch)].get()
		: select;
}

std::vector<ibQuerySource*> ibDialogQueryConstructor::CurrentSources() const
{
	std::vector<ibQuerySource*> out;
	ibQuerySelect* select = Current();
	if (select == nullptr)
		return out;
	out.push_back(&select->m_from);
	for (ibQueryAstJoin& join : select->m_joins)
		out.push_back(&join.m_source);
	return out;
}

std::vector<ibQueryConstructorField> ibDialogQueryConstructor::AvailableFields() const
{
	std::vector<ibQueryConstructorField> out;
	const std::vector<ibQuerySource*> sources = CurrentSources();
	// ⚠ ALWAYS QUALIFIED — `Catalog1.Code`, never a bare `Code`, even with one table in the query.
	//
	// A bare name is unambiguous only until a SECOND table joins, and at that moment every field
	// written before it becomes ambiguous at once — the engine refuses the query and the author is
	// shown an error about fields they did not touch, for a reason that happened elsewhere. The
	// alias exists to be used; writing the full path from the first field on means adding a table
	// never invalidates what is already there.
	const bool qualify = true;

	for (const ibQuerySource* source : sources) {
		if (source == nullptr)
			continue;
		if (source->m_name.empty() && !source->m_subquery)
			continue;
		// ONE table needs no prefix; several do, and the prefix is what the query writes.
		std::vector<ibQueryConstructorField> fields = qualify
			? m_model.GetQualifiedFields(*source, m_package, m_statement)
			: m_model.GetFields(*source, m_package, m_statement);
		out.insert(out.end(), fields.begin(), fields.end());
	}
	return out;
}

wxString ibDialogQueryConstructor::ChooseField(const wxString& title)
{
	const std::vector<ibQueryConstructorField> fields = AvailableFields();
	if (fields.empty()) {
		wxMessageBox(_("Choose a table first: the fields come from the tables the query reads."),
			title, wxOK | wxICON_INFORMATION, this);
		return wxEmptyString;
	}

	wxArrayString labels;
	for (const ibQueryConstructorField& field : fields)
		labels.Add(field.m_presentation);

	const int index = wxGetSingleChoiceIndex(_("Field"), title, labels, this);
	return index >= 0 ? fields[static_cast<size_t>(index)].m_name : wxString();
}

// ===========================================================================
//  Fill — AST -> tabs
// ===========================================================================

void ibDialogQueryConstructor::FillAll()
{
	// ONE FRAME. Every edit refills every tab — that is what keeps the views from drifting — but
	// without this the user watches nine lists empty and refill one after another, and the window
	// looks like it is thinking rather than answering. Freeze holds the paint until it is all done.
	//
	// The event handler goes quiet with it: filling a list raises selection events, and a handler
	// that runs mid-refill reads half-rebuilt state. (m_filling guards the same thing from the
	// other side — belt and braces, because a control that ignores Freeze still cannot slip past
	// the flag.)
	Freeze();
	SetEvtHandlerEnabled(false);

	// ⚠ NOTHING THAT DOES NOT RESOLVE IS THROWN AWAY HERE ANY MORE.
	//
	// This used to run PruneUnresolved before anything was shown, so a field whose table had been
	// deleted — or a name with a typo in it — simply vanished from the query. Worse, the verdict
	// line under the text is filled AFTERWARDS, by asking the engine about the already-tidied
	// package: it therefore always answered "the engine reads this query", while the work had
	// quietly lost a column. Silence plus a clean bill of health is the one combination that
	// teaches a person to trust a window that is wrong.
	//
	// The rule now: WHAT IS WRITTEN STAYS WRITTEN, and the engine SAYS what is wrong with it —
	// its own words, its own position (AskEngine → CheckNames, inside the metadata scope). A
	// broken query must read as broken.
	//
	// ⭐ AND NOTHING IS REMOVED AT ALL ANY MORE. A table with no link and no field of its own used to
	// be swept away on OK as an unfinished gesture. That reading died with the decision that a
	// PRODUCT is a sentence: `FROM A, B` says "multiply these", and "I added a table and changed my
	// mind" is the SAME AST. The two cannot be told apart, so guessing meant silently changing how
	// many rows a query returns — the one thing this window must never do. Deleting a table the
	// author no longer wants is one click; a result quietly divided by the row count of another
	// table is found a week later, on the numbers.

	// ⭐ EVERY GRID KEEPS ITS PLACE.
	//
	// A refill Resets every model, and a Reset clears the control's selection — so after ANY edit
	// there was no selected row, and the verb that came next had nothing to work on. From outside
	// that is "I cannot delete the condition": the Delete button was fine, the row under it was gone.
	//
	// A virtual list model's item IS its row (id = row + 1), so keeping the item and putting it back
	// is the whole of it — no bookkeeping, no per-grid code. A row that the edit removed is not
	// re-selected: the count is asked first, because selecting a row that no longer exists is how a
	// grid starts answering questions about nothing.
	ibDataViewCtrl* const grids[] = {
		m_fields, m_links, m_conditions, m_grouping, m_aggregates, m_order,
		m_indexFields, m_totalsAggregates, m_totalsDimensions, m_unions, m_unionFields,
	};
	std::vector<ibDataViewItem> kept;
	kept.reserve(WXSIZEOF(grids));
	for (ibDataViewCtrl* grid : grids)
		kept.push_back(grid != nullptr ? grid->GetSelection() : ibDataViewItem());

	m_filling = true;
	FillPackage();
	FillTables();
	// AFTER the tables, because what there is to choose from depends on which tables are in.
	FillFieldSources();
	FillLinks();
	FillConditions();
	FillOrder();
	FillIndex();
	FillGrouping();
	FillTotals();
	FillUnions();
	FillAdvanced();
	m_filling = false;
	FillPreview();

	for (size_t i = 0; i < WXSIZEOF(grids); ++i) {
		if (grids[i] == nullptr || !kept[i].IsOk())
			continue;
		const ibDataViewVirtualListModel* model =
			dynamic_cast<const ibDataViewVirtualListModel*>(grids[i]->GetModel());
		if (model == nullptr || model->GetRow(kept[i]) >= model->GetCount())
			continue;   // the row this was standing on is gone — leave nothing selected rather than something else
		grids[i]->Select(kept[i]);
	}

	SetEvtHandlerEnabled(true);
	Thaw();
}

void ibDialogQueryConstructor::FillPackage()
{
	// THE MODEL IS THE FILL — it reads the package on every paint, so telling it how many rows there
	// are IS refreshing the list. Nothing is copied out of the AST into the control.
	m_statementModel->SetRowCount(static_cast<unsigned int>(m_package.m_statements.size()));
	if (m_statement < m_package.m_statements.size())
		m_statements->Select(m_statementModel->GetItem(static_cast<unsigned int>(m_statement)));

	// THE BATCH STRIP — one tab per statement, rebuilt only when the count changed (a notebook
	// re-made on every refill flickers, and its own selection event would come back round as an
	// edit — the same rule the branch strip and the tab set both hold to).
	if (m_batchStrip != nullptr) {
		const size_t statements = m_package.m_statements.size();
		if (m_batchStrip->GetPageCount() != statements) {
			m_batchStrip->DeleteAllPages();
			for (size_t i = 0; i < statements; ++i)
				m_batchStrip->AddPage(new wxPanel(m_batchStrip),
					wxString::Format(_("Query %u"), static_cast<unsigned int>(i + 1)));
		}
		if (m_statement < statements && m_batchStrip->GetSelection() != static_cast<int>(m_statement))
			m_batchStrip->SetSelection(static_cast<int>(m_statement));
	}

	const ibQueryAstStatement* statement = m_statement < m_package.m_statements.size()
		? &m_package.m_statements[m_statement] : nullptr;

	// The KIND, read back off the statement — which is where it lives; the control is a view of it.
	const int kind = statement == nullptr ? 0
		: statement->IsDrop() ? 2
		: (statement->m_select && !statement->m_select->m_intoTemp.IsEmpty()) ? 1 : 0;

	if (m_queryKind != nullptr)
		m_queryKind->SetSelection(kind);
	if (m_tempName != nullptr) {
		m_tempName->ChangeValue(statement == nullptr ? wxString()
			: statement->IsDrop() ? statement->m_dropTemp
			: (statement->m_select ? statement->m_select->m_intoTemp : wxString()));
		m_tempName->Enable(kind != 0);   // the name is asked only by the kinds that need one
	}

	SyncNotebookPages();
}

// The tab set follows what the statement IS. A drop has no query, so the query tabs are not there
// — its select is KEPT behind the scenes, so switching the kind back brings the query with it
// rather than a blank.
void ibDialogQueryConstructor::SyncNotebookPages()
{
	if (m_notebook == nullptr)
		return;

	const ibQueryAstStatement* statement = m_statement < m_package.m_statements.size()
		? &m_package.m_statements[m_statement] : nullptr;
	const bool isDrop = statement != nullptr && statement->IsDrop();

	// How many tables the query actually reads — the Links tab exists only from the second one.
	// Only a statement that MATERIALISES its result has a table to index.
	const bool makesTempTable = statement != nullptr && !statement->IsDrop()
		&& statement->m_select && !statement->m_select->m_intoTemp.IsEmpty();

	size_t tableCount = 0;
	for (const ibQuerySource* source : CurrentSources())
		if (source != nullptr && (!source->m_name.empty() || source->m_subquery))
			++tableCount;

	// WHICH TABS THIS STATEMENT SHOULD HAVE.
	std::vector<const Page*> wanted;
	for (const Page& page : m_pages) {
		if (page.m_queryPage && isDrop)
			continue;
		if (page.m_needsTwoTables && tableCount < 2)
			continue;   // a join is a relation BETWEEN two sources
		if (page.m_needsTempTable && !makesTempTable)
			continue;   // only a table this statement makes can be indexed
		if (page.m_refusedByTempTable && makesTempTable)
			continue;   // a temp table is flat; TOTALS yields a tree, and the parser refuses the pair
		if (page.m_title == _("Query batch") && m_subQuery)
			continue;   // a sub-query is not a package
		wanted.push_back(&page);
	}

	// ⚠⚠ A PAGE THAT IS NOT IN THE NOTEBOOK MUST BE HIDDEN. This is THE `Fielbles and fields` bug,
	// and it was never the font and never the repaint.
	//
	// Every page window is built in the constructor as a CHILD of the notebook. A child that was
	// never added — or that `RemovePage` detached, because RemovePage detaches without hiding —
	// stays a plain visible child sitting at its default position, (0, 0). That is the top-left
	// corner of the notebook, which is exactly where the TAB STRIP is. So the Index page (normally
	// absent: only a statement that makes a temp table can be indexed) was painting its own left
	// label, `Fields`, straight over the first tab's caption, `Tables and fields`. Overlay the two
	// and you get `Fielbles and fields` — letter for letter.
	//
	// Hiding is therefore not tidiness. It is the difference between "this page is not shown" and
	// "this page is drawn on top of the window's chrome".
	for (const Page& page : m_pages) {
		const bool keep = std::find(wanted.begin(), wanted.end(), &page) != wanted.end();
		if (!keep && page.m_window != nullptr)
			page.m_window->Hide();
	}

	// REBUILD ONLY WHEN THE SET ACTUALLY CHANGED. This runs on every refill — that is, on every
	// edit — and tearing the notebook down each time cost two visible bugs: the selected tab jumped
	// back to wherever the restore landed, and the strip flickered. Comparing first makes the common
	// case do nothing, which is what "the tab set follows the statement" should mean. (The hiding
	// above happens BEFORE this early-out, because a stray page must be hidden on the very first
	// pass — when the notebook already holds exactly the wanted set and nothing else would run.)
	bool same = wanted.size() == m_notebook->GetPageCount();
	for (size_t i = 0; same && i < wanted.size(); ++i)
		same = m_notebook->GetPage(static_cast<size_t>(i)) == wanted[i]->m_window;
	if (same)
		return;

	// ⭐ THE TAB THE AUTHOR CHOSE, not merely the one showing.
	//
	// Switching branches refills, and a branch with one table has no Links tab — so the set changes,
	// the title is not found, and the selection drops to the first page. From outside that is the
	// window jumping about while you flip between Query 1 and Query 2, and switching back does not
	// bring you home either, because by then "the one showing" is the first one.
	//
	// So the LAST DELIBERATE choice is remembered (m_wantedTab, set by the page-changed handler when
	// a person moved it) and restored the moment that tab exists again. What is showing right now is
	// the fallback, for the first pass when nothing has been chosen yet.
	const wxString selected = !m_wantedTab.IsEmpty() ? m_wantedTab
		: (m_notebook->GetPageCount() > 0 ? m_notebook->GetPageText(m_notebook->GetSelection()) : wxString());

	// ⚠ ONLY THE DIFFERENCE. Tearing the whole strip down and building it again is what made the
	// window FLICKER every time the query kind changed — a kind switch adds one tab and removes
	// another, and paying for ten teardowns to do it is visible. The page order is fixed (it is
	// m_pages' order, and `wanted` is a subset in the same order), so pages only ever appear or
	// disappear — never move — which is exactly the case an insert/remove walk handles.
	{
		wxWindowUpdateLocker hold(m_notebook);

		size_t at = 0;
		for (const Page* page : wanted) {
			if (at < m_notebook->GetPageCount() && m_notebook->GetPage(at) == page->m_window) {
				++at;
				continue;   // already in place
			}
			page->m_window->Show();
			m_notebook->InsertPage(at, page->m_window, page->m_title);
			++at;
		}
		while (m_notebook->GetPageCount() > at)
			m_notebook->RemovePage(m_notebook->GetPageCount() - 1);
	}

	// The tab that was showing, if it is still one of them — by TITLE, because the page window a
	// title belongs to is the same object either way.
	for (size_t i = 0; i < m_notebook->GetPageCount(); ++i)
		if (m_notebook->GetPageText(i) == selected) {
			m_notebook->SetSelection(i);
			break;
		}
}

// EVERY TREE IN THIS WINDOW IS DRESSED THE SAME WAY. Written once because "the catalogue has
// pictures and the chosen tables do not" is exactly the sort of difference that reads as a bug:
// the same table is the same thing on both sides of the window.
//
// A METATYPE'S OWN PICTURE, taken from the type registry — the namespace a source registers under
// IS the metatype's registered name ("Catalog", "Document", "Constant"), so this needs no table of
// its own and a metatype added tomorrow is dressed the day it registers.
ibDialogQueryConstructor::TreeIcons ibDialogQueryConstructor::PrepareIcons(wxTreeCtrl* tree) const
{
	TreeIcons icons;
	if (tree == nullptr)
		return icons;

	// A FRESH LIST WITH THE TREE. The list is indexed by position, and a stale one would put a
	// document's icon on a catalog.
	icons.m_images = new wxImageList(16, 16);
	icons.m_field = icons.m_images->Add(ibValue::GetIconGroup());
	tree->AssignImageList(icons.m_images);
	return icons;
}

// THE PICTURE FOR ONE EXPRESSION, wherever an expression is listed — fields, groupings, order,
// index, totals. A COLUMN wears its own picture (a dimension, a resource, a plain attribute); a
// dot-walk wears the picture of what it ENDS on, because `Producer.Region.Name` is that field seen
// from here; and anything that is not a column — `23 + 3456`, an aggregate call — is not a field at
// all, so it takes the grid's plain one.
//
// One answer, asked by every grid, so a row cannot look like one kind of thing in the tree and
// another kind three inches to the right.
wxIcon ibDialogQueryConstructor::IconOfExpr(const ibQueryAstExprPtr& expr) const
{
	if (!expr || expr->m_kind != ibQueryAstExprKind::Column || expr->m_path.empty())
		return wxNullIcon;

	const ibQuerySelect* select = Current();
	if (select == nullptr)
		return wxNullIcon;

	const ibSourceMetaDataScope resolveAgainst(m_metaData);
	const ibQueryConstructorField field =
		m_model.FieldOfPath(*select, expr->m_path, m_package, m_statement);
	return field.m_icon;
}


// THE MIRROR OF THE COLLECT ABOVE: rewrite the source a path starts on. Same walk, same rule about
// which paths name a source (a Column's, and only a Column's) — so the two can never disagree about
// what counts as a reference.
static void ibQueryRenameMentioned(const ibQueryAstExprPtr& expr, const wxString& from, const wxString& to)
{
	if (!expr)
		return;

	if (expr->m_kind == ibQueryAstExprKind::Column && !expr->m_path.empty()
	    && expr->m_path.front().IsSameAs(from, false))
		expr->m_path.front() = to;

	ibQueryRenameMentioned(expr->m_arg,  from, to);
	ibQueryRenameMentioned(expr->m_lhs,  from, to);
	ibQueryRenameMentioned(expr->m_rhs,  from, to);
	ibQueryRenameMentioned(expr->m_low,  from, to);
	ibQueryRenameMentioned(expr->m_high, from, to);
	ibQueryRenameMentioned(expr->m_else, from, to);
	for (const ibQueryAstExprPtr& item : expr->m_list)
		ibQueryRenameMentioned(item, from, to);
	for (const auto& branch : expr->m_cases) {
		ibQueryRenameMentioned(branch.first,  from, to);
		ibQueryRenameMentioned(branch.second, from, to);
	}
}

// RENAMING A TABLE CARRIES ITS REFERENCES. An alias is what the rest of the query calls that table
// by, so changing it without touching the paths written against it turns every one of them into a
// name that resolves to nothing — the author renames one thing and breaks ten, none of which they
// were looking at.
//
// ⚠ THIS SELECT ONLY. An alias is scoped to the query it is declared in: a union branch and a
// nested subquery have their own tables, and a branch that happens to use the same word means its
// own table by it. Descending would rename somebody else's reference to somebody else's table.
void queryctor::ibQueryRenameSourceReferences(ibQuerySelect& select, const wxString& from, const wxString& to)
{
	if (from.IsEmpty() || to.IsEmpty() || from.IsSameAs(to, false))
		return;

	for (ibQueryProjection& projection : select.m_projections)
		ibQueryRenameMentioned(projection.m_expr, from, to);
	ibQueryRenameMentioned(select.m_where,  from, to);
	ibQueryRenameMentioned(select.m_having, from, to);
	for (ibQueryAstExprPtr& key : select.m_groupBy)
		ibQueryRenameMentioned(key, from, to);
	for (ibQueryOrderItem& order : select.m_orderBy)
		ibQueryRenameMentioned(order.m_expr, from, to);
	for (ibQueryAstExprPtr& key : select.m_indexBy)
		ibQueryRenameMentioned(key, from, to);
	for (ibQueryAstExprPtr& aggregate : select.m_totalsAggregates)
		ibQueryRenameMentioned(aggregate, from, to);
	for (ibQueryTotalDim& dimension : select.m_totalsBy)
		ibQueryRenameMentioned(dimension.m_expr, from, to);
	// The JOIN CONDITIONS name both sides — renaming one of them is exactly what this is for.
	for (ibQueryAstJoin& join : select.m_joins)
		ibQueryRenameMentioned(join.m_on, from, to);
}

// ⭐⭐ A TABLE IS GONE, AND SO IS EVERYTHING WRITTEN AGAINST IT.
//
// Removing a table used to leave its fields in the selection, its links in the list and its columns
// in the conditions — on the reasoning that a broken path is simply not RESOLVED and therefore not
// shown. That reasoning had a mechanism under it (a prune on every refill) and the mechanism is
// gone: what does not resolve now STAYS and is spoken about, which is right for a typo and wrong
// here. The result was a query full of `SliceLast.Dimension2` naming a table nobody could see, and a
// red line about an attribute the author had already removed the source of.
//
// So removal cascades EXPLICITLY, from the verb that removed. Only what NAMES the departed table
// goes; everything else is left exactly as written — including things that are broken for their own
// reasons, which remain the engine's to talk about.
//
// EVERY SOURCE THE QUERY NAMES — the first segment of every column path, wherever an expression can
// stand. Only a Column's path names a source: a Cast's path is a TYPE and a Value's is a meta-path,
// so both are walked through (their m_arg) without their own path being read. A subquery's sources
// are its own business and are not collected here.
static void ibQueryCollectMentioned(const ibQueryAstExprPtr& expr, std::set<wxString>& out)
{
	if (!expr)
		return;

	if (expr->m_kind == ibQueryAstExprKind::Column && !expr->m_path.empty())
		out.insert(expr->m_path.front().Lower());

	ibQueryCollectMentioned(expr->m_arg,  out);
	ibQueryCollectMentioned(expr->m_lhs,  out);
	ibQueryCollectMentioned(expr->m_rhs,  out);
	ibQueryCollectMentioned(expr->m_low,  out);
	ibQueryCollectMentioned(expr->m_high, out);
	ibQueryCollectMentioned(expr->m_else, out);
	for (const ibQueryAstExprPtr& item : expr->m_list)
		ibQueryCollectMentioned(item, out);
	for (const auto& branch : expr->m_cases) {
		ibQueryCollectMentioned(branch.first,  out);
		ibQueryCollectMentioned(branch.second, out);
	}
}

// Same walk as the rename above, so the two cannot disagree about what counts as a reference.
static bool ibQueryMentions(const ibQueryAstExprPtr& expr, const wxString& source)
{
	std::set<wxString> named;
	ibQueryCollectMentioned(expr, named);
	return named.find(source.Lower()) != named.end();
}

void queryctor::ibQueryDropSourceReferences(ibQuerySelect& select, const wxString& source)
{
	if (source.IsEmpty())
		return;

	const auto mentions = [&source](const ibQueryAstExprPtr& expr) { return ibQueryMentions(expr, source); };

	select.m_projections.erase(
		std::remove_if(select.m_projections.begin(), select.m_projections.end(),
			[&](const ibQueryProjection& p) { return mentions(p.m_expr); }),
		select.m_projections.end());
	if (select.m_projections.empty())
		select.m_selectAll = true;   // a query that selects nothing reads everything, as it did at birth

	// THE CONDITIONS ARE A LIST, and only the terms that named the table go — the rest of the AND
	// chain is somebody's work and has nothing to do with this removal.
	const auto dropTerms = [&](ibQueryAstExprPtr& clause) {
		std::vector<ibQueryAstExprPtr> terms;
		ibQueryFlattenAnd(clause, terms);
		terms.erase(std::remove_if(terms.begin(), terms.end(), mentions), terms.end());
		clause = ibQueryFoldAnd(terms);
	};
	dropTerms(select.m_where);
	dropTerms(select.m_having);

	const auto dropExprs = [&](std::vector<ibQueryAstExprPtr>& list) {
		list.erase(std::remove_if(list.begin(), list.end(), mentions), list.end());
	};
	dropExprs(select.m_groupBy);
	dropExprs(select.m_indexBy);
	dropExprs(select.m_totalsAggregates);

	select.m_orderBy.erase(
		std::remove_if(select.m_orderBy.begin(), select.m_orderBy.end(),
			[&](const ibQueryOrderItem& o) { return mentions(o.m_expr); }),
		select.m_orderBy.end());
	select.m_totalsBy.erase(
		std::remove_if(select.m_totalsBy.begin(), select.m_totalsBy.end(),
			[&](const ibQueryTotalDim& d) { return mentions(d.m_expr); }),
		select.m_totalsBy.end());

	// AND THE LINKS THAT NAMED IT. The join ENTRY of the departed table is erased by the verb (it IS
	// the table); what is cleared here is a link written on ANOTHER table that mentioned this one —
	// a sentence about two tables, one of which is no longer in the query.
	for (ibQueryAstJoin& join : select.m_joins)
		if (mentions(join.m_on)) {
			join.m_on = nullptr;
			join.m_kind = ibQueryJoinKindAst::Inner;   // the kind belongs to the link
		}
}

// ⛔ THE SWEEP THAT USED TO STAND HERE IS GONE, and the reason is a decision rather than a cleanup.
//
// It removed, on OK, a table with no link and no field of its own — reading that as "the author
// added it and never got round to using it". Then the PRODUCT became a sentence in this language:
// `FROM A, B` is how you say "multiply these", written with the comma that has always meant it.
//
// And "I meant a product" and "I changed my mind" are the SAME AST. Nothing in the text tells them
// apart, so the sweep was guessing — and guessing wrong meant silently changing HOW MANY ROWS the
// query returns, which is the one edit no window may make behind an author's back. (The sweep knew
// this rule: it exempted a table named by a neighbouring `ON` for exactly that reason, and then
// broke it for the case that had no `ON` at all.)
//
// A table nobody wants is one click to delete. A result quietly multiplied — or quietly not — is
// found a week later, on the numbers.

// THE PICTURE FOR ONE FIELD. The column already answered what it looks like
// (ibBackendSourceColumn::GetColumnIcon — an attribute by default, a dimension / a resource when
// the metaobject says so), so this only has to put that picture in THIS tree's list and remember
// where. Nothing here knows a metatype name, and nothing switches over a kind: a new kind of
// column arrives dressed on the day it overrides the icon, with this file untouched.
int ibDialogQueryConstructor::FieldIcon(TreeIcons& icons, const ibQueryConstructorField& field) const
{
	if (icons.m_images == nullptr || !field.m_icon.IsOk())
		return icons.m_field;

	for (const std::pair<wxIcon, int>& known : icons.m_byIcon)
		if (known.first.IsSameAs(field.m_icon))
			return known.second;

	const int index = icons.m_images->Add(field.m_icon);
	icons.m_byIcon.emplace_back(field.m_icon, index);
	return index;
}

// The plain-field picture in a tree's own list. Index 0 BY CONSTRUCTION — PrepareIcons adds it
// first to a fresh list — which is what lets a lazily-expanded node draw the same icon as the rows
// that were there from the start, without rebuilding the list and restyling every row.
static int FieldIconOf(wxTreeCtrl* tree)
{
	return tree != nullptr && tree->GetImageList() != nullptr ? 0 : wxNOT_FOUND;
}

// SEVEN TREES, ONE HANDLER. Every field tree in this window unfolds a reference the same way, and
// which tree raised the event is read off the event itself — so a pane added tomorrow gets the walk
// by binding this, with nothing added here.
void ibDialogQueryConstructor::OnFieldTreeExpanding(wxTreeEvent& event)
{
	event.Skip();

	wxTreeCtrl* tree = dynamic_cast<wxTreeCtrl*>(event.GetEventObject());
	ibQueryExpandFieldNode(tree, event.GetItem(), m_model, FieldIconOf(tree));
}

// A NESTED QUERY and a TEMP TABLE have no metatype, so there is no registered class icon for them
// to borrow. They are not "no kind" though: they are two particular kinds of source, each with its
// own picture, and these sentinels name them so ONE function answers "which icon does this row get"
// for all three cases. A row with no picture beside rows that have one is what makes a tree read as
// half-finished.
//
// ⚠ They start with '@', which no metatype name can, so they cannot collide with a real kind in the
// same cache.
static const wxChar* const kKindNestedQuery = wxT("@nested");
static const wxChar* const kKindTempTable   = wxT("@temp");

int ibDialogQueryConstructor::KindIcon(TreeIcons& icons, const wxString& kind) const
{
	if (icons.m_images == nullptr || kind.IsEmpty())
		return wxNOT_FOUND;

	const auto cached = icons.m_byKind.find(kind);
	if (cached != icons.m_byKind.end())
		return cached->second;

	int index = wxNOT_FOUND;

	// THE TWO SOURCES WITH NO METATYPE come from the art provider; everything else wears the icon
	// its metaobject registered, which is why a metatype added tomorrow is dressed the day it
	// registers and nothing here is edited.
	if (kind == kKindNestedQuery || kind == kKindTempTable) {
		const wxBitmap picture = wxArtProvider::GetBitmap(
			kind == kKindNestedQuery ? wxART_NESTED_QUERY : wxART_TEMP_TABLE,
			wxART_FRONTEND, FromDIP(wxSize(16, 16)));
		if (picture.IsOk())
			index = icons.m_images->Add(picture);
	}
	else if (m_metaData != nullptr) {
		if (const ibCtorAbstractType* ctor = m_metaData->GetAvailableCtor(kind)) {
			const wxIcon icon = ctor->GetClassIcon();
			if (icon.IsOk())
				index = icons.m_images->Add(icon);
		}
	}

	icons.m_byKind[kind] = index;
	return index;
}

// The metatype a chosen source belongs to — its first path segment.
//
// A NESTED QUERY and a TEMP TABLE have no metatype at all, so there is no registered class icon for
// them to borrow. They are not "no kind" though: they are two particular kinds of source, and each
// has its own picture. The sentinels below name them so ONE function answers "which icon does this
// row get" for all three cases — a row with no picture beside rows that have one is what makes a
// tree read as half-finished.
//
static wxString KindOfSource(const ibQuerySource& source)
{
	if (source.m_subquery)
		return kKindNestedQuery;
	if (source.m_name.size() == 1)
		return kKindTempTable;   // a bare name is what a temp table is
	return source.m_name.size() > 1 ? source.m_name[0] : wxString();
}

void ibDialogQueryConstructor::FillSourceTree()
{
	// The catalogue is the biggest list in the window and the one rebuilt most often (it changes
	// whenever a statement is added, moved or re-kinded). Rebuilding it visibly is most of the
	// flicker there is.
	wxWindowUpdateLocker hold(m_sourceTree);
	m_sourceTree->DeleteAllItems();
	TreeIcons icons = PrepareIcons(m_sourceTree);

	const wxTreeItemId root = m_sourceTree->AddRoot(wxEmptyString);

	// THE CATALOGUE IS A WALK. The top level is whatever kinds the factory actually holds, so a
	// metatype registered tomorrow appears here with nothing edited in this file.
	std::map<wxString, wxTreeItemId> groups;
	for (const ibQueryConstructorSource& source : m_model.GetSources()) {
		if (source.m_path.empty())
			continue;
		const wxString kind = source.m_path[0];
		auto it = groups.find(kind);
		if (it == groups.end()) {
			const int icon = KindIcon(icons, kind);
			it = groups.emplace(kind, m_sourceTree->AppendItem(root, kind, icon, icon)).first;
		}

		wxString leaf;
		for (size_t i = 1; i < source.m_path.size(); ++i)
			leaf += (i > 1 ? wxT(".") : wxT("")) + source.m_path[i];
		// The TABLE carries its metatype's icon too — that is what tells a catalog from a document
		// at a glance, which is the whole reason a picture is here.
		const int icon = KindIcon(icons, kind);
		const wxTreeItemId table = m_sourceTree->AppendItem(root == it->second ? root : it->second,
			leaf, icon, icon, new ibQueryTreeNode(source.m_path, false));

		// AND ITS FIELDS, so a field can be dragged straight from the catalogue into the query —
		// which is what "you can drag any field you can see" means. Filled now rather than lazily:
		// the model answers from the descriptor without touching the database.
		ibQuerySource asSource;
		asSource.m_name = source.m_path;
		// The node carries BOTH: its own field path and the TABLE it came out of — dragging a field
		// out of the catalogue means "read this table and select this field", so the row has to know
		// which table that is, at every level of the unfold.
		for (const ibQueryConstructorField& field : m_model.GetFields(asSource, m_package, m_statement))
			ibQueryAddFieldNode(m_sourceTree, table, field, FieldIcon(icons, field), -1, wxEmptyString, source.m_path);
	}

	// AND THE PACKAGE'S OWN TEMP TABLES, at the bottom beside the metaobject sources — because a
	// created temp table IS a source from the next statement's point of view, and a package is only
	// readable when step 3 can see what step 2 left. The tree is fed from two places: the factory
	// (permanent) and the package being edited (temporary).
	const std::vector<ibQueryConstructorSource> temps =
		ibQueryConstructorModel::GetTempSources(m_package, m_statement);
	if (!temps.empty()) {
		// A TEMP TABLE HAS A PICTURE TOO — asked for the same way every other row asks, so this loop
		// does not know where the picture came from.
		const int tempIcon = KindIcon(icons, kKindTempTable);

		const wxTreeItemId group = m_sourceTree->AppendItem(root, _("Temporary tables"),
			tempIcon, tempIcon);
		for (const ibQueryConstructorSource& temp : temps) {
			const wxTreeItemId table = m_sourceTree->AppendItem(group, temp.m_presentation,
				tempIcon, tempIcon, new ibQueryTreeNode(temp.m_path, true));

			// AND ITS FIELDS, exactly as a permanent table gets them above. A temp table was the
			// one source in this tree that stood as a childless leaf, so it could be added to the
			// query but nothing could be selected FROM it and no field could be dragged out of it —
			// which reads as "temp tables do not work". The model already answers the question:
			// GetFields resolves a temp NAME to the projection of the statement that creates it.
			ibQuerySource asSource;
			asSource.m_name = temp.m_path;
			for (const ibQueryConstructorField& field : m_model.GetFields(asSource, m_package, m_statement))
				ibQueryAddFieldNode(m_sourceTree, table, field, FieldIcon(icons, field), -1, wxEmptyString, temp.m_path);
		}
		m_sourceTree->Expand(group);
	}
}

void ibDialogQueryConstructor::FillTables()
{
	m_tables->DeleteAllItems();
	TreeIcons icons = PrepareIcons(m_tables);
	const wxTreeItemId root = m_tables->AddRoot(wxEmptyString);

	ibQuerySelect* select = Current();
	if (m_fieldModel != nullptr)
		m_fieldModel->SetRowCount(0);
	if (select == nullptr)
		return;

	const std::vector<ibQuerySource*> sources = CurrentSources();
	for (size_t i = 0; i < sources.size(); ++i) {
		const ibQuerySource& source = *sources[i];
		if (source.m_name.empty() && !source.m_subquery)
			continue;   // the FROM slot of a query that has no table yet

		const int tableIcon = KindIcon(icons, KindOfSource(source));
		const wxTreeItemId node = m_tables->AppendItem(root, ibQuerySourceLabel(source), tableIcon, tableIcon,
			new ibQueryTreeNode(static_cast<int>(i), wxEmptyString));

		// ASK THE SOURCE. A real table answers through its descriptor, a nested one through its
		// projections, a temp one through the statement that made it — and this loop does not know
		// which of the three it is talking to.
		for (const ibQueryConstructorField& field : m_model.GetFields(source, m_package, m_statement))
			ibQueryAddFieldNode(m_tables, node, field, FieldIcon(icons, field), static_cast<int>(i), wxEmptyString);
		m_tables->Expand(node);
	}

	// The field grid reads the projections itself — a row count is the whole refresh.
	if (m_fieldModel != nullptr)
		m_fieldModel->SetRowCount(static_cast<unsigned int>(select->m_projections.size()));
}

void ibDialogQueryConstructor::FillLinks()
{
	ibQuerySelect* select = Current();

	// The names each source is known by, in the order the constructor indexes them — the grid and
	// the diagram are handed the SAME list, so a table cannot be called one thing in one and
	// another in the other.
	std::vector<wxString> tableNames;
	if (select != nullptr) {
		for (const ibQuerySource* source : CurrentSources()) {
			if (source == nullptr)
				continue;
			if (source->m_name.empty() && !source->m_subquery) {
				tableNames.push_back(wxString());
				continue;
			}
			tableNames.push_back(ibQuerySourceLabel(*source));
		}
	}
	if (m_linkModel != nullptr)
		m_linkModel->SetContent(select, tableNames);

}

// ONE ANSWER, SHOWN FOUR TIMES. Grouping, Conditions, Order and Index all ask "which fields does
// this query have", so all four show the same tree: the chosen tables, each expanding into its
// fields. A flat list of qualified names was the first shape and it hid the thing a person is
// actually looking for — which table a field belongs to.

// (`SeededAggregate` stood here — a fixed `SUM(Field)` for the modal that opened when a field was
//  moved into the aggregates. Both callers now ASK the engine which folds the field's type takes and
//  add the row without a window: SeededAggregateFor, in queryConstructorEdit.cpp.)

// ⭐ THE STAR, WRITTEN OUT. Every select of the package — and every union branch of each — that says
// `*` and lists nothing gets one projection per field its tables offer.
//
// The fields are asked of the same model everything else asks, so a nested table answers with its
// own projections and a virtual table with what its arguments make of it. Qualified, because a bare
// name breaks the moment a second table joins.
//
// ⚠ ONLY WHERE THERE IS A TABLE TO ASK. `SELECT 1` has no source and its star means nothing to
// expand; leaving it alone is what keeps such a query openable.
void ibDialogQueryConstructor::ExpandStars()
{
	const ibSourceMetaDataScope resolveAgainst(m_metaData);

	const std::function<void(ibQuerySelect&)> expand = [&](ibQuerySelect& select) {
		for (const ibQuerySelectPtr& branch : select.m_unions)
			if (branch)
				expand(*branch);

		if (!select.m_selectAll || !select.m_projections.empty())
			return;

		std::vector<const ibQuerySource*> sources{ &select.m_from };
		for (const ibQueryAstJoin& join : select.m_joins)
			sources.push_back(&join.m_source);

		for (const ibQuerySource* source : sources) {
			if (source == nullptr || (source->m_name.empty() && !source->m_subquery))
				continue;
			for (const ibQueryConstructorField& field :
			         m_model.GetQualifiedFields(*source, m_package, m_statement)) {
				ibQueryProjection projection;
				try {
					ibQueryParser parser;
					projection.m_expr = parser.ParseExpression(field.m_name);
				}
				catch (const ibBackendException&) { continue; }
				if (projection.m_expr)
					select.m_projections.push_back(projection);
			}
		}
		if (!select.m_projections.empty())
			select.m_selectAll = false;   // it lists them now; the star has been spent
	};

	for (ibQueryAstStatement& statement : m_package.m_statements)
		if (statement.m_select)
			expand(*statement.m_select);
}

void ibDialogQueryConstructor::FillFieldSources()
{
	for (wxTreeCtrl* tree : { m_groupingSource, m_conditionSource, m_orderSource, m_indexSource,
	                          m_totalsSource }) {
		if (tree == nullptr)
			continue;

		wxWindowUpdateLocker hold(tree);
		tree->DeleteAllItems();
		TreeIcons icons = PrepareIcons(tree);
		const wxTreeItemId root = tree->AddRoot(wxEmptyString);

		// ⭐⭐ TOTALS ARE TAKEN OVER THE RESULT, so the Totals tab offers the RESULT'S fields — the
		// aliases, the very table the Unions tab shows.
		//
		// Every other tab here narrows or arranges the rows as they are READ, so it asks about the
		// source tables. Totals happen after all of that, over one relation: with a union, over every
		// branch at once. Showing source fields there was wrong twice over — it offered columns of
		// table 2 that the result does not carry under that name, and it hid the output field that IS
		// what a total is counted by.
		// ⚠ THE RESULT'S FIELDS STAND FIRST AND FLAT — no heading over them. They are what a total is
		// taken by, so they are the answer to this tab's question, and putting them inside a folder
		// made them one option among others instead of the obvious one.
		//
		// The tables go BELOW, under "All fields" — because they are still reachable (a total may be
		// taken over something the result does not carry under its own name), just not the first
		// thing offered.
		wxTreeItemId tablesUnder = root;
		if (tree == m_totalsSource) {
			const ibQuerySelect* select = Current();
			// ⭐ AND EACH RESULT FIELD KEEPS WHAT IT IS. Built from a name alone, these rows carried
			// no type and no reference — so the picker had nothing to unfold and drew every one of
			// them as a plain field, including the references. The description is taken from the
			// SOURCE field it stands for, which is the same answer the other tabs show.
			const std::vector<ibQueryConstructorField> available = AvailableFields();
			if (select != nullptr)
				for (const ibQueryProjection& projection : select->m_projections) {
					ibQueryConstructorField field;
					const wxString written = projection.m_expr ? ibRenderQueryExpr(*projection.m_expr) : wxString();
					wxString shown = ibQueryOutputName(projection);
					if (shown.IsEmpty())
						shown = written;
					if (shown.IsEmpty())
						continue;
					// ⚠ THE LABEL IS THE ALIAS; THE PATH IS THE SOURCE. The row shows what the result
					// calls this field, and carries what the query has to WRITE to reach it — a total
					// over `Catalog1.Parent.Description` is one the engine resolves, over
					// `Parent.Description` it is not. (The node's path comes from m_name, its caption
					// from m_presentation; that split is what makes both true at once.)
					field.m_name         = written.IsEmpty() ? shown : written;
					field.m_presentation = shown;

					// A plain column stands for a field of a table, and that field knows its type,
					// its picture and whether it can be walked into. An expression stands for nothing
					// but itself and stays a leaf.
					if (projection.m_expr && projection.m_expr->m_kind == ibQueryAstExprKind::Column)
						for (const ibQueryConstructorField& source : available)
							if (source.m_name.IsSameAs(written, false)) {
								field.m_reference       = source.m_reference;
								field.m_referenceClsid  = source.m_referenceClsid;
								field.m_type            = source.m_type;
								field.m_icon            = source.m_icon;
								break;
							}
					ibQueryAddFieldNode(tree, root, field, FieldIcon(icons, field), 0, wxEmptyString);
				}
			const int allIcon = (select != nullptr) ? KindIcon(icons, KindOfSource(select->m_from)) : -1;
			tablesUnder = tree->AppendItem(root, _("All fields"), allIcon, allIcon,
				new ibQueryTreeNode(0, wxEmptyString));
		}

		const std::vector<ibQuerySource*> sources = CurrentSources();
		const bool qualify = true;   // a bare name breaks the moment a table joins
		for (size_t i = 0; i < sources.size(); ++i) {
			const ibQuerySource& source = *sources[i];
			if (source.m_name.empty() && !source.m_subquery)
				continue;

			// THE GROUP HEADER IS THE TABLE, by the name the query calls it — `Catalog1`, not
			// `Catalog.Catalog1 (Catalog1)`. It is a heading over its own fields, and the path it
			// came from is the catalogue's business, one tab away.
			// ⚠ UNDER `tablesUnder`, NOT UNDER THE ROOT. On the Totals tab that is the "All fields"
			// node — I created it and then went on appending the tables beside it, so the node stood
			// empty while the tables sat at the top level next to the result fields. Everywhere else
			// `tablesUnder` IS the root, and nothing changes.
			const int tableIcon = KindIcon(icons, KindOfSource(source));
			const wxTreeItemId table = tree->AppendItem(tablesUnder, ibQuerySourceLabel(source), tableIcon, tableIcon,
				new ibQueryTreeNode(static_cast<int>(i), wxEmptyString));
			// The name a CONDITION writes: qualified when more than one table is in play, because
			// that is what the query text must carry to be unambiguous.
			const std::vector<ibQueryConstructorField> fields = qualify
				? m_model.GetQualifiedFields(source, m_package, m_statement)
				: m_model.GetFields(source, m_package, m_statement);
			for (const ibQueryConstructorField& field : fields)
				ibQueryAddFieldNode(tree, table, field, FieldIcon(icons, field), static_cast<int>(i), wxEmptyString);
			// Expanded on the tabs where the tables ARE the answer; folded under "All fields", where
			// they are the second place to look and opening all of them buries the result's own list.
			if (tablesUnder == root)
				tree->Expand(table);
		}
	}
}

// The technical name behind the selected node of one of those trees, or empty when the selection
// is a table row rather than a field.
wxString ibDialogQueryConstructor::SelectedFieldOf(wxTreeCtrl* tree) const
{
	if (tree == nullptr)
		return wxEmptyString;
	const wxTreeItemId item = tree->GetSelection();
	if (!item.IsOk())
		return wxEmptyString;
	const ibQueryTreeNode* node = dynamic_cast<ibQueryTreeNode*>(tree->GetItemData(item));
	return node != nullptr ? node->m_field : wxString();
}

// THE SCOPE RULE, in one place. Grouping, Order, Index and Totals all act on a field tree, and all
// four mean the same thing by a selection — so the question "which fields did they point at" is
// answered here rather than four times.
std::vector<wxString> ibDialogQueryConstructor::SelectedFieldsOf(wxTreeCtrl* tree) const
{
	std::vector<wxString> fields;
	if (tree == nullptr)
		return fields;
	const wxTreeItemId item = tree->GetSelection();
	if (!item.IsOk())
		return fields;

	const ibQueryTreeNode* node = dynamic_cast<ibQueryTreeNode*>(tree->GetItemData(item));
	if (node == nullptr)
		return fields;

	// ⚠ A FIELD MOVES AS A FIELD — including a reference one. Unfolding a reference and taking
	// everything behind it in one go was tried and it is too wide an answer: a reference IS a
	// field (it is selectable, orderable, groupable on its own), so acting on it has to mean it,
	// and taking five columns when one was pointed at is the window deciding on the author's
	// behalf. Everything behind it is right there, unfolded, to be taken one at a time.
	//
	// Only the TABLE row is a container, and that is the one place where "all of these" is a thing
	// you can genuinely point at.
	if (!node->m_field.IsEmpty()) {
		fields.push_back(node->m_field);
		return fields;
	}

	// A TABLE row: every field under it (its direct children — not the whole reference graph).
	wxTreeItemIdValue cookie;
	for (wxTreeItemId child = tree->GetFirstChild(item, cookie); child.IsOk();
	     child = tree->GetNextChild(item, cookie)) {
		const ibQueryTreeNode* leaf = dynamic_cast<ibQueryTreeNode*>(tree->GetItemData(child));
		if (leaf != nullptr && !leaf->m_field.IsEmpty())
			fields.push_back(leaf->m_field);
	}
	return fields;
}
// WHICH projections the aggregate pane is about. One place decides it — the grid's model, its
// verbs and its selection all index through here, so they cannot disagree about which projection
// a row stands for.
std::vector<size_t> ibDialogQueryConstructor::AggregateRows() const
{
	std::vector<size_t> rows;
	const ibQuerySelect* select = Current();
	if (select == nullptr)
		return rows;
	for (size_t i = 0; i < select->m_projections.size(); ++i)
		if (IsAggregateProjection(select->m_projections[i]))
			rows.push_back(i);
	return rows;
}

long ibDialogQueryConstructor::SelectedRow(ibDataViewCtrl* grid, ibQueryGridModel* model) const
{
	if (grid == nullptr || model == nullptr)
		return -1;
	const ibDataViewItem item = grid->GetSelection();
	return item.IsOk() ? static_cast<long>(model->GetRow(item)) : -1;
}

void ibDialogQueryConstructor::FillGrouping()
{
	// THE MODELS ARE THE FILL. Each reads the select on every paint, so a refresh is a row count —
	// there is no second copy of the AST inside a control to fall out of step with it.
	const ibQuerySelect* select = Current();
	if (m_groupingModel != nullptr)
		m_groupingModel->SetRowCount(select == nullptr ? 0u : static_cast<unsigned int>(select->m_groupBy.size()));
	if (m_aggregateModel != nullptr)
		m_aggregateModel->SetRowCount(static_cast<unsigned int>(AggregateRows().size()));
}

void ibDialogQueryConstructor::FillConditions()
{
	// THE MODEL IS THE FILL. With a dataview there is no row-by-row loop to keep in step with the
	// AST — the model reads the AND chain out of the select on every paint, so pointing it at the
	// select IS refreshing the grid.
	if (m_conditionModel != nullptr)
		m_conditionModel->SetContent(Current());
}
void ibDialogQueryConstructor::FillIndex()
{
	if (m_indexModel == nullptr)
		return;
	const ibQuerySelect* select = Current();
	m_indexModel->SetRowCount(select == nullptr ? 0u : static_cast<unsigned int>(select->m_indexBy.size()));
}

void ibDialogQueryConstructor::OnAddIndexField(wxCommandEvent&)
{
	if (!CanEdit())
		return;
	ibQuerySelect* select = Current();
	if (select == nullptr)
		return;

	const std::vector<wxString> fields = SelectedFieldsOf(m_indexSource);
	if (fields.empty())
		return;

	for (const wxString& field : fields) {
		// The LAST segment: an index is over a column of the temp table, and the temp table's
		// columns are the projection's output names, not the paths they were read from.
		select->m_indexBy.push_back(ibQueryColumnFromPath(field.AfterLast(wxT('.'))));
	}
	FillAll();
}

void ibDialogQueryConstructor::OnRemoveIndexField(wxCommandEvent&)
{
	if (!CanEdit())
		return;
	ibQuerySelect* select = Current();
	if (select == nullptr)
		return;
	const long index = SelectedRow(m_indexFields, m_indexModel);
	if (index < 0 || static_cast<size_t>(index) >= select->m_indexBy.size())
		return;
	select->m_indexBy.erase(select->m_indexBy.begin() + index);
	FillAll();
}

void ibDialogQueryConstructor::FillOrder()
{
	if (m_orderModel == nullptr)
		return;
	const ibQuerySelect* select = Current();
	m_orderModel->SetRowCount(select == nullptr ? 0u : static_cast<unsigned int>(select->m_orderBy.size()));
}

void ibDialogQueryConstructor::FillTotals()
{
	const ibQuerySelect* select = Current();
	if (m_totalsAggregateModel != nullptr)
		m_totalsAggregateModel->SetRowCount(select == nullptr ? 0u
			: static_cast<unsigned int>(select->m_totalsAggregates.size()));
	if (m_totalsDimensionModel != nullptr)
		m_totalsDimensionModel->SetRowCount(select == nullptr ? 0u
			: static_cast<unsigned int>(select->m_totalsBy.size()));
	// THE BOX SAYS WHAT THE QUERY SAYS — including when the query arrived as TEXT. A query typed
	// with `BY OVERALL` has to show its box ticked, or the tab is describing a different query.
	if (m_grandTotals != nullptr) {
		m_grandTotals->SetValue(select != nullptr && select->m_totalsOverall);
		m_grandTotals->Enable(!m_readOnly && select != nullptr);
	}
}

void ibDialogQueryConstructor::FillUnions()
{
	// THE BRANCH LIST BELONGS TO THE STATEMENT, not to the branch being edited — otherwise
	// selecting branch 2 would show branch 2's own (empty) union list and the strip would vanish
	// under the person using it.
	ibQuerySelect* select = StatementSelect();

	// The TABS down the right edge: one per branch, the first being this query. Rebuilt only when the
	// count changed — a notebook re-made on every refill flickers, and its own selection event would
	// come back round as an edit (the lesson SyncNotebookPages already carries).
	if (m_branchStrip != nullptr) {
		const size_t branches = select != nullptr ? select->m_unions.size() + 1 : 0;
		if (m_branchStrip->GetPageCount() != branches) {
			m_branchStrip->DeleteAllPages();
			for (size_t i = 0; i < branches; ++i)
				m_branchStrip->AddPage(new wxPanel(m_branchStrip),
					wxString::Format(_("Query %u"), static_cast<unsigned int>(i + 1)));
		}
		const int wanted = m_unionBranch + 1;
		if (wanted >= 0 && static_cast<size_t>(wanted) < m_branchStrip->GetPageCount()
		    && m_branchStrip->GetSelection() != wanted)
			m_branchStrip->SetSelection(wanted);
		ShowBranchStrip();
	}

	if (m_unionModel != nullptr)
		m_unionModel->SetContent(select);
	if (m_unionFieldModel == nullptr || m_unionFields == nullptr)
		return;

	m_unionFieldModel->SetContent(select);

	// ONE COLUMN PER BRANCH, rebuilt only when the count changed. The map's shape follows the
	// query; rebuilding it on every refill would flicker for nothing (the same lesson the tab set
	// taught — see SyncNotebookPages).
	const unsigned int wanted = 1 + m_unionFieldModel->BranchCount();   // the name column + a branch each
	if (m_unions != nullptr && m_unionFields->GetColumnCount() != wanted) {
		m_unionFields->ClearColumns();
		// CALLED WHAT IT IS. The header said "Field name", so a person looking for the aliases saw a
		// column of names identical to the branch columns and concluded there were none. It is the
		// ALIAS — the output field's name — and it is typed into.
		// ⚠ ICON-TEXT, AND STILL EDITABLE. The model hands this cell a picture with its text (these
		// rows are the output FIELDS), and a plain text renderer cannot read that value — it drew an
		// EMPTY cell for every row whose field resolved to an icon, which is the worst possible way
		// to fail: the ones that stayed visible were exactly the ones nothing was found for.
		// Editable because this is the one place an output field is named.
		m_unionFields->AppendColumn(new ibDataViewColumn(_("Alias"),
			new ibDataViewIconTextRenderer(ibDataViewIconTextRenderer::GetDefaultType(),
				wxDATAVIEW_CELL_EDITABLE),
			kUnionColName, FromDIP(220), wxAlignment::wxALIGN_LEFT));
		// ⭐ EACH BRANCH CELL IS A CHOICE OF THAT BRANCH'S OWN FIELDS. A union is lined up BY HAND —
		// two branches over different tables rarely spell the same field the same way, and reading
		// the line-up off by name is right often enough to be misleading. The empty entry is an
		// answer too: this branch supplies nothing here, and the column is NULL for its rows.
		for (unsigned int branch = 0; branch < m_unionFieldModel->BranchCount(); ++branch)
			m_unionFields->AppendColumn(new ibDataViewColumn(
				wxString::Format(_("Query %u"), branch + 1),
				new ibRowChoiceRenderer([this, branch]() -> wxArrayString {
					wxArrayString words;
					words.Add(wxEmptyString);   // supplies nothing -> NULL
					const wxArrayString fields = m_unionFieldModel->FieldsOfBranch(branch);
					WX_APPEND_ARRAY(words, fields);
					return words;
				}, m_readOnly ? wxDATAVIEW_CELL_INERT : wxDATAVIEW_CELL_EDITABLE),
				kUnionColName + 1 + branch, FromDIP(220), wxAlignment::wxALIGN_LEFT));
	}
}
void ibDialogQueryConstructor::FillAdvanced()
{
	ibQuerySelect* select = Current();
	const bool has = select != nullptr;

	m_distinct ->SetValue(has && select->m_distinct);
	m_allowed  ->SetValue(has && select->m_allowed);
	m_forUpdate->SetValue(has && select->m_forUpdate);
	m_useTop   ->SetValue(has && select->m_top > 0);
	m_topCount ->SetValue(has && select->m_top > 0 ? static_cast<int>(select->m_top) : 10);
	m_topCount ->Enable(has && select->m_top > 0);
}

void ibDialogQueryConstructor::FillPreview()
{
	if (m_preview == nullptr)
		return;
	const bool wasFilling = m_filling;
	m_filling = true;
	m_preview->SetText(ibRenderQueryPackage(m_package));
	ibMarkQueryParameters(m_preview);   // the marks go with the text they marked
	m_filling = wasFilling;
	ShowEngineVerdict();
}

// ONE GATE FOR EVERY NAME THE USER TYPES. The language decides what a name can be — asked of the
// LEXER, whose definition of an identifier is the only one that matters. `Reference f 3` is not one,
// and the constructor used to accept it, write it into the text and then show its own engine's
// lexical error back: a window arguing with itself about text it produced.
bool ibDialogQueryConstructor::AcceptName(const wxString& name, const wxString& what)
{
	if (ibQueryLexer::IsIdentifier(name))
		return true;

	// ⚠ TOLD, NOT MENTIONED. A refused name used to leave a sentence on the verdict line at the
	// bottom of the window while the cell quietly kept its old text — and a person watching the
	// cell has no reason to look at the far edge of the dialog. The rejection has to arrive where
	// the eyes already are, and only then does the old value come back.
	wxMessageBox(wxString::Format(
			_("'%s' cannot be a %s.\n\nA name is one word: letters, digits and underscores, "
			  "with no spaces or punctuation."), name, what),
		_("Query constructor"), wxOK | wxICON_WARNING, this);
	return false;
}

// ⭐⭐ ONE VERDICT, FOR THE LINE AND FOR THE BUTTON.
//
// "Is this query sound" has TWO sources, and the line under the tabs shows both: what the ENGINE says
// about the query, and the one thing only the WINDOW knows — a link the author started and left
// empty. The engine cannot see that one: two tables with no link is a complete query (they are
// multiplied), so "no link" and "a link I have not finished" are the same AST, and the difference
// lives in this window's own state.
//
// It was asked in two places with two different definitions: the line showed both, OK consulted only
// the engine. So a query glowing red closed without a word — which is the shape that teaches people
// the red line is decoration. Asked once here, both places get the same answer by construction.
bool ibDialogQueryConstructor::Verdict(wxString& message) const
{
	const wxString unfinished = m_linkModel != nullptr ? m_linkModel->UnfinishedLink() : wxString();
	if (!unfinished.IsEmpty()) {
		// ⚠ ASCII ONLY IN A STRING LITERAL. The sources carry no BOM, so MSVC reads them in the
		// system codepage: an em dash typed here reached the screen as "вЂ”". Dashes belong in
		// comments, where nobody renders them.
		message = wxString::Format(
			_("the link on '%s' has no condition: write one, or delete the link. Two tables with no "
			  "link between them are simply multiplied."), unfinished);
		return false;
	}
	return AskEngine(message);
}

bool ibDialogQueryConstructor::AskEngine(wxString& message) const
{
	message.clear();
	try {
		ibQueryParser parser;
		const ibQueryPackage read = parser.ParsePackage(ibRenderQueryPackage(m_package));

		// ⚠ PARSING IS NOT THE WHOLE QUESTION. In this language names are resolved against metadata
		// at EXECUTION, so the parser cannot tell `Code` from `DataVersionPredefinedName` — both are
		// legal identifiers. A verdict line that said "the engine reads this query" after parsing
		// alone was true about the grammar and false about the query, which is the worst shape a
		// reassurance can have.
		//
		// So the names are resolved too, by the SAME resolver the execution uses, and its words come
		// back verbatim. It stays silent where it cannot verify (a temp table outside its package, a
		// nested query's own tables) rather than inventing an error.
		//
		// ⚠ INSIDE THE METADATA SCOPE, or it verifies nothing. Sources register on the CONFIG'S own
		// factory, and the resolver reads that config from ibSourceMetaDataScope — without the scope
		// it knocks on the global factory, fails to find the metaclass at all, and the check falls
		// silent by its own "never a false error" rule. Which is exactly how it managed to pass a
		// query naming a field nothing has. The config was handed to this dialog for precisely this.
		const ibSourceMetaDataScope resolveAgainst(m_metaData);
		ibQueryLowering::CheckNames(read, std::map<wxString, ibValue>());
		return true;
	}
	catch (const ibBackendException& e) {
		message = e.GetErrorDescription();   // VERBATIM. The core said it; the dialog only carries it.
		return false;
	}
}

void ibDialogQueryConstructor::ShowEngineVerdict()
{
	if (m_status == nullptr)
		return;

	wxString message;
	const bool valid = Verdict(message);
	m_status->SetForegroundColour(valid
		? wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT)
		: wxColour(0xC0, 0x30, 0x30));
	m_status->SetLabelText(valid ? wxString(_("The query engine reads this query.")) : message);
	m_status->Refresh();
}

