////////////////////////////////////////////////////////////////////////////
//	Description : The query constructor - the verbs (add / remove / move / rename / edit)
//	              (queryConstructor.h)
////////////////////////////////////////////////////////////////////////////

#include "queryConstructorInternal.h"

using namespace queryctor;

// ===========================================================================
//  The package strip
// ===========================================================================

// ---------------------------------------------------------------------------
//  Drag and drop — the gesture the three panes are arranged for
// ---------------------------------------------------------------------------
//
// Same-process drags: the payload is a placeholder because the thing being moved is whatever the
// SOURCE control has selected, and both trees are in this window. The shape is listSettings' —
// remember nothing, start a drag, and let the drop target raise the verb that was already there.

void ibDialogQueryConstructor::OnSourceBeginDrag(wxTreeEvent& event)
{
	if (!CanEdit())
		return;

	// ⚠⚠ DO NOT `Allow()` THIS EVENT. Allowing it tells wxTreeCtrl to run ITS OWN drag — the native
	// one, with a drag image — and we then start a second, modal drag of our own inside the same
	// notification. MSW refuses the second `ImageList_BeginDrag`, which surfaces as
	//     dragimag.cpp(282): assert "Assert failure" failed in BeginDrag(): BeginDrag failed
	// from inside wxTreeCtrl::MSWOnNotify. Leaving the event vetoed is exactly right: we are not
	// asking the tree to drag anything, we are running the drag ourselves.
	//
	// (An earlier note here claimed the opposite — that the gesture looked broken without Allow().
	// It looked broken for a different reason, and this is the shape listSettings has always used.)
	m_dragTree = m_sourceTree;   // where a drop should read the dragged thing FROM
	m_sourceTree->SelectItem(event.GetItem());   // a drag acts on what it started on, not on the cursor
	wxTextDataObject payload(wxT("table"));
	wxDropSource drag(payload, m_sourceTree);    // the SOURCE window is the tree, not the dialog
	drag.DoDragDrop(wxDrag_CopyOnly);
}

// ANY field tree, one handler. The tree that raised it is the one the drop verb will read, so
// this needs to know nothing about which tab it is on.
void ibDialogQueryConstructor::OnFieldTreeBeginDrag(wxTreeEvent& event)
{
	if (!CanEdit())
		return;

	// NOT `Allow()`ed — see OnSourceBeginDrag. The tree must not start a drag of its own beside ours.
	wxTreeCtrl* tree = dynamic_cast<wxTreeCtrl*>(event.GetEventObject());
	if (tree == nullptr)
		return;
	m_dragTree = tree;
	tree->SelectItem(event.GetItem());

	wxTextDataObject payload(wxT("field"));
	wxDropSource drag(payload, tree);
	drag.DoDragDrop(wxDrag_CopyOnly);
}

void ibDialogQueryConstructor::OnTableBeginDrag(wxTreeEvent& event)
{
	if (!CanEdit())
		return;

	// NOT `Allow()`ed — see OnSourceBeginDrag.
	//
	// Dragging the TABLE row carries all its fields, dragging a field row carries that one — the
	// same "what you act on decides the scope" rule the move buttons follow, because a drag and a
	// button press are the same verb reached two ways.
	m_dragTree = m_tables;
	m_tables->SelectItem(event.GetItem());
	wxTextDataObject payload(wxT("field"));
	wxDropSource drag(payload, m_tables);
	drag.DoDragDrop(wxDrag_CopyOnly);
}

// THE STRIP IS FOR SWITCHING BETWEEN BRANCHES, so it is not there on the two tabs where switching
// makes no sense: on Unions the branches are what you are CONFIGURING (the strip would be a second,
// worse copy of the list you are looking at), and on Query package you are choosing a statement,
// which is the level above branches. With one branch there is nothing to switch between at all.
void ibDialogQueryConstructor::ShowBranchStrip()
{
	if (m_branchStrip == nullptr || m_notebook == nullptr)
		return;

	const wxString tab = m_notebook->GetPageCount() > 0 && m_notebook->GetSelection() != wxNOT_FOUND
		? m_notebook->GetPageText(m_notebook->GetSelection()) : wxString();

	// A STRIP IS FOR MOVING BETWEEN THINGS, so each hides where its things are being CONFIGURED
	// instead — the branches on Unions, the statements on Query batch — and where there is only one
	// of them to move between.
	const bool showBranches = m_branchStrip->GetPageCount() > 1
		&& tab != _("Unions / Aliases") && tab != _("Query batch");
	const bool showBatch = m_batchStrip != nullptr && m_batchStrip->GetPageCount() > 1
		&& tab != _("Query batch");

	bool changed = false;
	if (m_branchStrip->IsShown() != showBranches) { m_branchStrip->Show(showBranches); changed = true; }
	if (m_batchStrip != nullptr && m_batchStrip->IsShown() != showBatch) { m_batchStrip->Show(showBatch); changed = true; }
	if (changed)
		Layout();   // not free, and this runs on every refill
}

// A BRANCH IS A QUERY, so picking one on the strip simply changes what the tabs are showing —
// there is no second window and nothing is copied. Row 0 is the statement itself.
void ibDialogQueryConstructor::OnBranchSelected(wxBookCtrlEvent& event)
{
	if (m_filling) { event.Skip(); return; }
	m_unionBranch = event.GetSelection() - 1;
	FillAll();
}

// A BATCH STATEMENT IS A QUERY TOO, so the same gesture at the level above: picking one changes
// which statement the tabs are showing. Switching statements also changes what the CATALOGUE may
// offer (statement 3 can read a temp table statement 2 made, statement 1 cannot), so the source
// tree is rebuilt with it.
void ibDialogQueryConstructor::OnBatchSelected(wxBookCtrlEvent& event)
{
	if (m_filling) { event.Skip(); return; }
	const int selection = event.GetSelection();
	if (selection < 0 || static_cast<size_t>(selection) >= m_package.m_statements.size())
		return;

	m_statement   = static_cast<size_t>(selection);
	m_unionBranch = -1;   // the branches belong to the statement that was left behind
	FillSourceTree();
	FillAll();
}

void ibDialogQueryConstructor::OnStatementSelected(ibDataViewEvent& event)
{
	if (m_filling || m_statementModel == nullptr) { event.Skip(); return; }
	const long row = SelectedRow(m_statements, m_statementModel);
	if (row < 0)
		return;
	m_statement = static_cast<size_t>(row);
	FillSourceTree();   // the temp tables a statement may read depend on WHERE it stands
	FillAll();
}

void ibDialogQueryConstructor::OnAddStatement(wxCommandEvent&)
{
	if (!CanEdit())
		return;

	ibQueryAstStatement statement;
	statement.m_select = std::make_shared<ibQuerySelect>();
	statement.m_select->m_selectAll = true;
	m_package.m_statements.push_back(statement);
	m_statement = m_package.m_statements.size() - 1;
	FillSourceTree();
	FillAll();
}

// THE KIND IS THE STATEMENT'S OWN. Changing it changes what this statement IS — and the query
// behind it is kept either way, so a kind chosen by mistake costs nothing to undo.
void ibDialogQueryConstructor::OnQueryKindChanged(wxCommandEvent&)
{
	// ⚠ THE FOCUS GRAB BELONGS TO THE RADIO ONLY. The name box applies itself on KILL_FOCUS, so a
	// version that also pulled focus back would trap the caret in it forever: click away, the box
	// applies, the apply takes the focus back, click away… The two callers are therefore split.
	ApplyQueryKind(true);
}

void ibDialogQueryConstructor::ApplyQueryKind(bool focusName)
{
	if (!CanEdit() || m_filling)
		return;
	if (m_statement >= m_package.m_statements.size())
		return;

	ibQueryAstStatement& statement = m_package.m_statements[m_statement];
	if (!statement.m_select) {
		statement.m_select = std::make_shared<ibQuerySelect>();
		statement.m_select->m_selectAll = true;
	}

	const int kind = m_queryKind->GetSelection();
	wxString name = m_tempName->GetValue().Trim(true).Trim(false);

	// ⚠ THE KIND IS THE NAME, in this AST: a statement MAKES a temp table by having an `INTO`, and
	// DROPS one by having a drop name. So picking a kind with the name box empty wrote nothing, the
	// refill read the kind back off the (unchanged) statement, and the radio snapped to `Select` —
	// which is exactly "the switch does not work".
	//
	// A kind that needs a name therefore PROPOSES one. That is what "the kind asks for the name"
	// means: it asks by putting an answer there, not by refusing to change until one is typed.
	if (kind == 1 && name.IsEmpty())
		name = SuggestTempTableName();
	if (kind == 2 && name.IsEmpty()) {
		// A DROP names a table that already exists — so the proposal is the last one this package
		// made before this statement, not a fresh name nobody created.
		const std::vector<ibQueryConstructorSource> temps =
			ibQueryConstructorModel::GetTempSources(m_package, m_statement);
		if (temps.empty()) {
			wxMessageBox(_("There is no temporary table to drop yet: an earlier statement of this "
			               "package has to make one first."),
				_("Query kind"), wxOK | wxICON_INFORMATION, this);
			FillAll();   // put the radio back on what the statement actually is
			return;
		}
		name = temps.back().Text();
	}

	// A TEMP TABLE'S NAME IS A NAME — the same gate as every other. It goes into the text after
	// INTO / DROP, so a space in it is a query nothing can read.
	if (kind != 0 && !AcceptName(name, _("temporary table name"))) {
		FillAll();   // put the controls back on what the statement actually is
		return;
	}

	switch (kind) {
	case 1:   // materialise the result under a name
		statement.m_dropTemp.clear();
		statement.m_select->m_intoTemp = name;
		break;
	case 2:   // drop a name made earlier — the query stays behind it, unrendered
		statement.m_select->m_intoTemp.clear();
		statement.m_dropTemp = name;
		break;
	default:  // a plain select — neither name means anything
		statement.m_dropTemp.clear();
		statement.m_select->m_intoTemp.clear();
		break;
	}

	// ⚠ ONE FRAME. Everything below repaints something — the tab set gains a tab and loses another,
	// the strips re-lay out, every grid refills — and done unlocked the eye sees each of them in
	// turn, which is what "it flickers" is. WM_SETREDRAW off across the whole update (that is what
	// wxWindowUpdateLocker is on MSW) makes the switch one transition instead of a sequence.
	{
		wxWindowUpdateLocker hold(this);
		FillAll();
	}
	Refresh();

	// ⚠ AND NOT FillSourceTree(). The catalogue lists the temp tables declared BEFORE this statement
	// — changing what THIS statement is cannot change that, so rebuilding the whole left tree here
	// was pure repaint, and the most expensive one in the switch. It is rebuilt where it genuinely
	// changes: adding, removing or moving a statement.

	// The name is the thing to correct now, so put the caret in it — but only when the RADIO was
	// what changed (see OnQueryKindChanged).
	if (focusName && kind != 0 && m_tempName != nullptr) {
		m_tempName->SetFocus();
		m_tempName->SetSelection(-1, -1);
	}
}

// A NAME NOBODY IS USING. The counter runs over the temp tables this package already declares, so
// two statements cannot propose the same one and quietly overwrite each other's table.
wxString ibDialogQueryConstructor::SuggestTempTableName() const
{
	auto taken = [this](const wxString& candidate) {
		for (const ibQueryAstStatement& statement : m_package.m_statements) {
			if (statement.m_dropTemp.IsSameAs(candidate, false))
				return true;
			if (statement.m_select && statement.m_select->m_intoTemp.IsSameAs(candidate, false))
				return true;
		}
		return false;
	};

	for (unsigned int n = 1; ; ++n) {
		const wxString candidate = wxString::Format(_("TempTable%u"), n);
		if (!taken(candidate))
			return candidate;
	}
}

void ibDialogQueryConstructor::OnRemoveStatement(wxCommandEvent&)
{
	if (!CanEdit())
		return;

	if (m_package.m_statements.size() <= 1)
		return;   // a package always has a statement to edit
	m_package.m_statements.erase(m_package.m_statements.begin() + static_cast<long>(m_statement));
	if (m_statement >= m_package.m_statements.size())
		m_statement = m_package.m_statements.size() - 1;
	FillSourceTree();
	FillAll();
}

void ibDialogQueryConstructor::OnMoveStatement(int delta)
{
	if (!CanEdit())
		return;

	const long target = static_cast<long>(m_statement) + delta;
	if (target < 0 || target >= static_cast<long>(m_package.m_statements.size()))
		return;
	// ORDER IS SEMANTICS HERE, not presentation: moving a statement changes which temp tables the
	// ones after it can see.
	std::swap(m_package.m_statements[m_statement], m_package.m_statements[static_cast<size_t>(target)]);
	m_statement = static_cast<size_t>(target);
	FillSourceTree();
	FillAll();
}

// ===========================================================================
//  Tables and fields
// ===========================================================================

// THE SAME VERBS AS THE TOOLBAR, on the thing itself. A rename is reached ON the table, and the
// menu item raises the tree's own label editor — one renaming mechanism, not a dialog beside it.
void ibDialogQueryConstructor::OnTableContextMenu(wxTreeEvent& event)
{
	if (!CanEdit())
		return;

	const wxTreeItemId item = event.GetItem();
	if (item.IsOk())
		m_tables->SelectItem(item);

	const ibQueryTreeNode* node = dynamic_cast<ibQueryTreeNode*>(m_tables->GetItemData(item));
	const bool isTable = node != nullptr && node->m_sourceIndex >= 0 && node->m_field.IsEmpty();

	enum { kAdd = wxID_HIGHEST + 1, kRename, kNested, kRemove };
	wxMenu menu;
	menu.Append(kAdd, _("Add table"));
	menu.Append(kNested, _("Nested table"));
	menu.AppendSeparator();
	menu.Append(kRename, _("Rename table..."))->Enable(isTable);
	menu.Append(kRemove, _("Delete"))->Enable(isTable);

	menu.Bind(wxEVT_MENU, [this, item](wxCommandEvent& e) {
		switch (e.GetId()) {
		case kAdd:    { wxCommandEvent unused; OnAddTable(unused); break; }
		case kNested: { wxCommandEvent unused; OnAddNestedTable(unused); break; }
		case kRemove: { wxCommandEvent unused; OnRemoveTable(unused); break; }
		case kRename: if (item.IsOk()) m_tables->EditLabel(item); break;
		default: break;
		}
	});
	PopupMenu(&menu);
}

// ONLY A TABLE ROW HAS AN ALIAS. A field's name is the metadata's; renaming it here would promise
// something the query cannot do.
void ibDialogQueryConstructor::OnTableAliasEditBegin(wxTreeEvent& event)
{
	const ibQueryTreeNode* node = dynamic_cast<ibQueryTreeNode*>(m_tables->GetItemData(event.GetItem()));
	if (!CanEdit() || node == nullptr || node->m_sourceIndex < 0 || !node->m_field.IsEmpty())
		event.Veto();
}

void ibDialogQueryConstructor::OnTableAliasEditEnd(wxTreeEvent& event)
{
	if (event.IsEditCancelled())
		return;

	const ibQueryTreeNode* node = dynamic_cast<ibQueryTreeNode*>(m_tables->GetItemData(event.GetItem()));
	const std::vector<ibQuerySource*> sources = CurrentSources();
	if (node == nullptr || node->m_sourceIndex < 0
	    || static_cast<size_t>(node->m_sourceIndex) >= sources.size()) {
		event.Veto();
		return;
	}

	wxString alias = event.GetLabel();
	alias.Trim(true).Trim(false);

	// TYPED BACK TO THE TABLE'S OWN NAME MEANS "no alias". The label shows `Catalog.Products (p)`
	// when there is one, so a person clearing it types the plain name — and an alias equal to the
	// table's own last segment is what having none already renders as.
	ibQuerySource* source = sources[static_cast<size_t>(node->m_sourceIndex)];
	const wxString ownName = !source->m_name.empty() ? source->m_name.back() : wxString();
	if (alias.IsSameAs(ownName, false))
		alias.clear();   // typed back to the table's own name = no alias

	// THE LANGUAGE DECIDES WHAT A NAME CAN BE. A rename with a space in it used to go straight into
	// the text and come back as a lexical error the author had no way to connect to what they typed.
	if (!alias.IsEmpty() && !AcceptName(alias, _("table name"))) {
		event.Veto();
		return;
	}

	// A NAME ALREADY TAKEN IS NUMBERED, the way a duplicate output column is — not refused. The
	// author asked for a name; giving them `Products1` keeps the query valid and says what happened,
	// while a refusal leaves them holding a rename that did nothing.
	ibQuerySelect* select = Current();
	if (!alias.IsEmpty() && select != nullptr)
		alias = ibQueryUniqueSourceAlias(*select, alias, source);

	// EVERY PATH WRITTEN AGAINST THIS TABLE FOLLOWS IT. The name is taken BEFORE the change and the
	// new one after, because "the name of a source" is the alias when there is one and the table's
	// own last segment when there is not — clearing an alias is a rename too, in the other
	// direction, and the references have to come back with it.
	const wxString oldName = ibQuerySourceName(*source);
	source->m_alias = alias;
	if (select != nullptr)
		ibQueryRenameSourceReferences(*select, oldName, ibQuerySourceName(*source));
	// The label is rebuilt from the AST by the refill — letting the tree keep the typed text would
	// leave `p` where `Catalog.Products (p)` belongs.
	event.Veto();
	FillAll();
}

void ibDialogQueryConstructor::OnAddTable(wxCommandEvent&)
{
	if (!CanEdit())
		return;

	ibQuerySelect* select = Current();
	if (select == nullptr)
		return;

	const wxTreeItemId item = m_sourceTree->GetSelection();
	if (!item.IsOk())
		return;

	// THE THING YOU ACT ON DECIDES THE SCOPE. Standing on a table moves that table; standing on the
	// KIND above it moves every table under it. That is why there is no separate "move all" button:
	// "all of these" is already a thing you can point at.
	//
	// ⚠ AND A FIELD IS A FIELD. A field row carries the path of the table it came out of (it has to —
	// that is how a dragged field knows which table to read), and taking that path as "the thing
	// pointed at" made the move arrow ADD A SECOND COPY of a table already in the query, the moment
	// somebody selected a field and pressed it. Two copies of one table make every unqualified field
	// ambiguous at once, which is how one press emptied a whole field list. Standing on a field means
	// the field — the same answer the drag gives, because it is the same gesture.
	const ibQueryTreeNode* node = dynamic_cast<ibQueryTreeNode*>(m_sourceTree->GetItemData(item));
	if (node != nullptr && !node->m_field.IsEmpty()) {
		AddCatalogueFieldToSelect();
		return;
	}

	std::vector<std::vector<wxString>> paths;
	if (node != nullptr && !node->m_path.empty()) {
		paths.push_back(node->m_path);
	}
	else {
		wxTreeItemIdValue cookie;
		for (wxTreeItemId child = m_sourceTree->GetFirstChild(item, cookie); child.IsOk();
		     child = m_sourceTree->GetNextChild(item, cookie)) {
			const ibQueryTreeNode* leaf = dynamic_cast<ibQueryTreeNode*>(m_sourceTree->GetItemData(child));
			if (leaf != nullptr && !leaf->m_path.empty())
				paths.push_back(leaf->m_path);
		}
	}
	AddTableSources(paths);
	FillAll();
}

// THE ONE PLACE THAT KNOWS HOW A TABLE JOINS THE QUERY — and it is a plain function, not a command
// handler.
//
// ⚠ It used to be the handler, and a second verb reached it by synthesising a wxCommandEvent and
// calling OnAddTable. That worked exactly until OnAddTable grew a case that called BACK into the
// verb ("standing on a field means the field") — mutual recursion with no bottom, which arrives as a
// stack overflow rather than as anything readable. A handler reads the window and decides WHAT;
// this does it. Two verbs can share the second half; neither can call the other's first half.
void ibDialogQueryConstructor::AddTableSources(const std::vector<std::vector<wxString>>& paths)
{
	ibQuerySelect* select = Current();
	if (select == nullptr)
		return;

	for (const std::vector<wxString>& path : paths) {
		ibQuerySource source;
		source.m_name = path;
		// A TABLE IS NAMED AS IT IS ADDED: `FROM Catalog.Nomenclature AS Nomenclature`. The dotted
		// path says where it came from; the alias is what the rest of the query calls it, and every
		// qualified field is written against it. Generating it means the text reads the way it will
		// be read, and a second copy of the same table is numbered instead of clashing.
		source.m_alias = ibQueryUniqueSourceAlias(*select, path.empty() ? wxString() : path.back(), nullptr);

		// The FIRST table is the FROM; every one after it is a join whose ON is left empty, which
		// means "follow the reference between them" — not a cross join.
		if (select->m_from.m_name.empty() && !select->m_from.m_subquery) {
			select->m_from = source;
		}
		else {
			ibQueryAstJoin join;
			join.m_source = source;
			join.m_kind = ibQueryJoinKindAst::Inner;
			select->m_joins.push_back(join);
		}
	}
}

void ibDialogQueryConstructor::OnAddNestedTable(wxCommandEvent&)
{
	if (!CanEdit())
		return;

	ibQuerySelect* select = Current();
	if (select == nullptr)
		return;

	// A nested table is a query of its own sitting where a table would — so it is authored in
	// THIS window, one level down. That is the whole re-entrancy requirement, and it is met by
	// opening the same class over the inner select.
	ibQuerySelectPtr inner = std::make_shared<ibQuerySelect>();
	inner->m_selectAll = true;
	if (!ibShowQueryConstructorFor(this, inner, m_metaData, m_readOnly))
		return;

	ibQuerySource source;
	source.m_subquery = inner;
	// A NESTED TABLE MUST BE NAMED, and it cannot borrow a name from anywhere: a real table is
	// called after its path, a nested one has no path at all. Without a name it renders as
	// `FROM (SELECT …)` with nothing to qualify its columns with, which breaks the moment a second
	// table joins it — and the field lists beside it had nothing to prefix either.
	//
	// ⚠ THE BASE STAYS ASCII AND UNTRANSLATED. This is not a label, it goes into the QUERY TEXT as
	// an identifier, and a localized one would be a name the lexer may not read.
	// ALWAYS NUMBERED, from 1 — `NestedQuery1`, `NestedQuery2`. A bare `NestedQuery` beside a
	// `NestedQuery1` reads as two different kinds of thing; numbering the first one costs nothing
	// and makes the series obvious the moment a second appears.
	for (unsigned int n = 1; ; ++n) {
		const wxString candidate = wxString::Format(wxT("NestedQuery%u"), n);
		if (ibQueryUniqueSourceAlias(*select, candidate, nullptr).IsSameAs(candidate, false)) {
			source.m_alias = candidate;
			break;
		}
	}

	if (select->m_from.m_name.empty() && !select->m_from.m_subquery) {
		select->m_from = source;
	}
	else {
		ibQueryAstJoin join;
		join.m_source = source;
		join.m_kind = ibQueryJoinKindAst::Inner;
		select->m_joins.push_back(join);
	}
	FillAll();
}

void ibDialogQueryConstructor::OnEditNestedTable(wxCommandEvent&)
{
	const wxTreeItemId item = m_tables->GetSelection();
	if (!item.IsOk())
		return;
	const ibQueryTreeNode* node = dynamic_cast<ibQueryTreeNode*>(m_tables->GetItemData(item));
	if (node == nullptr || node->m_sourceIndex < 0)
		return;

	const std::vector<ibQuerySource*> sources = CurrentSources();
	if (static_cast<size_t>(node->m_sourceIndex) >= sources.size())
		return;
	ibQuerySource* source = sources[static_cast<size_t>(node->m_sourceIndex)];
	if (source == nullptr || !source->m_subquery) {
		wxMessageBox(_("This table is not a nested one: only a nested table has a query to edit."),
			_("Edit nested table"), wxOK | wxICON_INFORMATION, this);
		return;
	}

	ibQuerySelectPtr inner = source->m_subquery;
	if (ibShowQueryConstructorFor(this, inner, m_metaData, m_readOnly)) {
		source->m_subquery = inner;
		FillAll();
	}
}

void ibDialogQueryConstructor::OnRemoveTable(wxCommandEvent&)
{
	if (!CanEdit())
		return;

	ibQuerySelect* select = Current();
	if (select == nullptr)
		return;

	const wxTreeItemId item = m_tables->GetSelection();
	if (!item.IsOk())
		return;
	const ibQueryTreeNode* node = dynamic_cast<ibQueryTreeNode*>(m_tables->GetItemData(item));
	if (node == nullptr || node->m_sourceIndex < 0)
		return;

	if (node->m_sourceIndex == 0) {
		// Removing the FROM promotes the first join into its place — a query with joins but no
		// primary source is not a query, and silently keeping one would produce text nothing reads.
		if (!select->m_joins.empty()) {
			select->m_from = select->m_joins.front().m_source;
			select->m_joins.erase(select->m_joins.begin());
		}
		else {
			select->m_from = ibQuerySource();
		}
	}
	else {
		const size_t index = static_cast<size_t>(node->m_sourceIndex) - 1;
		if (index < select->m_joins.size())
			select->m_joins.erase(select->m_joins.begin() + static_cast<long>(index));
	}

	// ⚠ NOTHING IS CHASED DOWN AND DELETED HERE, deliberately. Removing the table BREAKS the paths
	// that led through it — that is simply what removal is — and the pass that recomputes what the
	// query HAS (ibQueryLowering::PruneUnresolved, run on every refill) then does not reach them,
	// so they are not shown and not written. The cascade falls out of resolution instead of being
	// re-implemented per verb.
	//
	// The difference matters: a path can break for reasons this verb never sees — an attribute
	// renamed in the configuration, a metaobject deleted, a temp table no longer made. One rule
	// driven by RESOLUTION covers all of them; a cleanup hung off the delete button covers one.
	FillAll();
}



// STRAIGHT FROM THE CATALOGUE TO THE SELECT LIST. Dragging a field out of the left-hand tree onto
// the Fields pane means "select this" — and if the query does not read that table yet, reading it
// is part of what was meant. Making the author drop the table first and the field second is the
// window asking them to do its bookkeeping; the two steps are one gesture.
//
// The same shape covers a DOT-WALKED field (`Reference.Description`): the node carries the whole
// path, the table it belongs to is the one at the top of that branch, and nothing else changes.
bool ibDialogQueryConstructor::AddCatalogueFieldToSelect()
{
	if (!CanEdit() || m_sourceTree == nullptr)
		return false;

	ibQuerySelect* select = Current();
	const wxTreeItemId item = m_sourceTree->GetSelection();
	if (select == nullptr || !item.IsOk())
		return false;

	const ibQueryTreeNode* node = dynamic_cast<ibQueryTreeNode*>(m_sourceTree->GetItemData(item));
	if (node == nullptr || node->m_path.empty())
		return false;   // a metaclass group ("Catalog") — there is no one table to read

	// WHAT YOU ACT ON DECIDES THE SCOPE — and only the TABLE row is a container. A field moves as a
	// field, reference or not: everything behind a reference is right there, unfolded, to be taken
	// one at a time, and taking five columns when one was pointed at is the window deciding on the
	// author's behalf.
	std::vector<wxString> fields;
	if (node->m_field.IsEmpty()) {
		ibQuerySource asSource;
		asSource.m_name = node->m_path;
		for (const ibQueryConstructorField& field : m_model.GetFields(asSource, m_package, m_statement))
			fields.push_back(field.m_name);
	}
	else {
		fields.push_back(node->m_field);
	}
	if (fields.empty())
		return false;

	// IS THE TABLE ALREADY IN? Compared by the path a query names it by, which is the only identity
	// a source has here — two entries for one table would make every field of it ambiguous.
	bool present = false;
	for (const ibQuerySource* source : CurrentSources())
		if (source != nullptr && source->m_name == node->m_path)
			present = true;

	if (!present)
		AddTableSources({ node->m_path });   // the one place that knows how a table joins the query

	// ⚠ ALWAYS QUALIFIED, and by the source's OWN NAME — the alias it was given when it joined the
	// query, not the last segment of its catalogue path. Two copies of one table are `Catalog1` and
	// `Catalog1_1`; the path's last segment is `Catalog1` for both, so writing that would name the
	// wrong one. (This site was the last of three still qualifying only when a second table was
	// already there — which is exactly the case that breaks: every field written while there was
	// one table becomes ambiguous the moment the second arrives.)
	wxString prefix;
	for (const ibQuerySource* source : CurrentSources()) {
		if (source == nullptr || source->m_name != node->m_path)
			continue;
		prefix = ibQuerySourceName(*source);
		break;
	}
	if (!prefix.IsEmpty())
		prefix += wxT(".");

	for (const wxString& field : fields) {
		const wxString path = prefix + field;

		ibQueryProjection projection;
		projection.m_expr = ibQueryColumnFromPath(path);

		select->m_selectAll = false;
		ibQueryEnsureUniqueName(*select, projection);
		select->m_projections.push_back(projection);
	}
	FillAll();
	return true;
}

void ibDialogQueryConstructor::OnAddField(wxCommandEvent&)
{
	if (!CanEdit())
		return;

	ibQuerySelect* select = Current();
	if (select == nullptr)
		return;

	const wxTreeItemId item = m_tables->GetSelection();
	if (!item.IsOk())
		return;
	const ibQueryTreeNode* node = dynamic_cast<ibQueryTreeNode*>(m_tables->GetItemData(item));
	if (node == nullptr || node->m_sourceIndex < 0)
		return;

	// SAME RULE AS THE TABLES: what you stand on decides the scope. A field row moves that field;
	// a REFERENCE row moves everything behind it, as dot-walk paths; the TABLE row above them moves
	// every field it has.
	std::vector<wxString> fields;
	if (!node->m_field.IsEmpty()) {
		fields.push_back(node->m_field);   // a field moves as a field, reference or not
	}
	else {
		wxTreeItemIdValue cookie;
		for (wxTreeItemId child = m_tables->GetFirstChild(item, cookie); child.IsOk();
		     child = m_tables->GetNextChild(item, cookie)) {
			const ibQueryTreeNode* leaf = dynamic_cast<ibQueryTreeNode*>(m_tables->GetItemData(child));
			if (leaf != nullptr && !leaf->m_field.IsEmpty())
				fields.push_back(leaf->m_field);
		}
	}
	if (fields.empty())
		return;

	const std::vector<ibQuerySource*> sources = CurrentSources();
	wxString prefix;
	if (static_cast<size_t>(node->m_sourceIndex) < sources.size())   // ALWAYS qualified
		prefix = ibQuerySourceName(*sources[static_cast<size_t>(node->m_sourceIndex)]);

	for (const wxString& field : fields) {
		const wxString path = prefix.IsEmpty() ? field : prefix + wxT(".") + field;

		ibQueryProjection projection;
		projection.m_expr = ibQueryColumnFromPath(path);

		// Naming a field turns SELECT * into a select list — the star and an explicit list are two
		// answers to the same question and the AST keeps only one.
		select->m_selectAll = false;
		ibQueryEnsureUniqueName(*select, projection);
		select->m_projections.push_back(projection);
	}
	FillAll();
}

// A SELECT LIST TAKES EXPRESSIONS, not only columns — `1`, `Price * Qty`, a CASE. The AST has
// always allowed it (a projection holds an expression); this is the gesture that was missing, and
// it opens the one expression editor like everything else.
void ibDialogQueryConstructor::OnAddFieldExpression(wxCommandEvent&)
{
	if (!CanEdit())
		return;
	ibQuerySelect* select = Current();
	if (select == nullptr)
		return;

	ibDialogQueryExpression dialog(this, _("Field expression"), AvailableFields(), nullptr,
		m_metaData, m_readOnly);
	if (dialog.ShowModal() != wxID_OK)
		return;

	ibQueryProjection projection;
	projection.m_expr = dialog.GetExpression();
	if (!projection.m_expr)
		return;

	select->m_selectAll = false;
	ibQueryEnsureUniqueName(*select, projection);
	select->m_projections.push_back(projection);
	FillAll();
}

void ibDialogQueryConstructor::OnRemoveField(wxCommandEvent&)
{
	if (!CanEdit())
		return;

	ibQuerySelect* select = Current();
	if (select == nullptr)
		return;
	const long index = SelectedRow(m_fields, m_fieldModel);
	if (index < 0 || static_cast<size_t>(index) >= select->m_projections.size())
		return;
	select->m_projections.erase(select->m_projections.begin() + index);
	if (select->m_projections.empty())
		select->m_selectAll = true;   // a query with no named fields reads every one of them
	FillAll();
}

void ibDialogQueryConstructor::OnMoveField(int delta)
{
	if (!CanEdit())
		return;

	ibQuerySelect* select = Current();
	if (select == nullptr)
		return;
	const long index = SelectedRow(m_fields, m_fieldModel);
	const long target = index + delta;
	if (index < 0 || target < 0 || static_cast<size_t>(index) >= select->m_projections.size()
	    || static_cast<size_t>(target) >= select->m_projections.size())
		return;
	std::swap(select->m_projections[index], select->m_projections[target]);
	FillAll();
	m_fields->Select(m_fieldModel->GetItem(static_cast<unsigned int>(target)));
}

// EDIT THE FIELD STANDING HERE — the arbitrary-expression editor, over the projection's own
// expression. A projection IS an expression (a column is the simplest one), so this is not a
// separate feature from "write an expression": it is the same window, opened on what is there.
void ibDialogQueryConstructor::OnEditFieldExpression(wxCommandEvent&)
{
	if (!CanEdit())
		return;

	ibQuerySelect* select = Current();
	if (select == nullptr)
		return;
	const long index = SelectedRow(m_fields, m_fieldModel);
	if (index < 0 || static_cast<size_t>(index) >= select->m_projections.size())
		return;

	ibQueryProjection& projection = select->m_projections[index];
	ibDialogQueryExpression dialog(this, _("Field expression"), AvailableFields(), projection.m_expr,
		m_metaData, m_readOnly);
	if (dialog.ShowModal() != wxID_OK)
		return;

	const ibQueryAstExprPtr expression = dialog.GetExpression();
	if (!expression)
		return;   // an empty expression is not a field — the old one stays
	projection.m_expr = expression;
	select->m_selectAll = false;
	// AND IT GETS A NAME. Editing a column into an expression takes its natural name away, and a
	// field with no name is one nothing else can refer to — the same reason an ADDED field is
	// named. This path was the one that did not do it.
	ibQueryEnsureUniqueName(*select, projection);
	FillAll();
	m_fields->Select(m_fieldModel->GetItem(static_cast<unsigned int>(index)));
}

// (An output field's NAME is typed on the Unions / Aliases tab and nowhere else — the handler that
// asked for it in a little dialog is gone rather than left unreachable. A link, likewise, EXISTS
// because a table was added; the "add link" signpost was a message box no button opened.)

// ===========================================================================
//  Links
// ===========================================================================

// ONE EDITOR, reached from either view — the grid's row and the diagram's line are the same join,
// and the condition is written in THE expression editor, the one every other expression uses.
//
// The KIND is not asked here any more: it is the two checkboxes in the grid, edited in place.
void ibDialogQueryConstructor::EditJoinAt(size_t index)
{
	if (!CanEdit())
		return;
	ibQuerySelect* select = Current();
	if (select == nullptr || index >= select->m_joins.size())
		return;

	ibQueryAstJoin& join = select->m_joins[index];
	ibDialogQueryExpression dialog(this, _("Link condition"), AvailableFields(), join.m_on,
		m_metaData, m_readOnly);
	if (dialog.ShowModal() != wxID_OK)
		return;

	// AN EMPTY CONDITION IS NOT `ON TRUE`. It means "join by the reference between the tables", and
	// the editor hands back a null expression for empty text — which is exactly that.
	join.m_on = dialog.GetExpression();
	FillAll();
}

void ibDialogQueryConstructor::OnEditLink(wxCommandEvent&)
{
	if (m_linkModel == nullptr || m_links == nullptr)
		return;
	const unsigned int row = m_linkModel->GetRow(m_links->GetSelection());
	if (row == static_cast<unsigned int>(-1))
		return;
	EditJoinAt(static_cast<size_t>(row));
}

void ibDialogQueryConstructor::OnRemoveLink(wxCommandEvent&)
{
	if (!CanEdit())
		return;

	ibQuerySelect* select = Current();
	if (select == nullptr || m_linkModel == nullptr || m_links == nullptr)
		return;
	const long row = static_cast<long>(m_linkModel->GetRow(m_links->GetSelection()));
	if (row < 0 || static_cast<size_t>(row) >= select->m_joins.size())
		return;
	// Deleting a link deletes the TABLE it joined: a joined table with no join is not a thing the
	// query can express, and leaving one behind would be leaving the query in a state its own text
	// cannot describe.
	select->m_joins.erase(select->m_joins.begin() + row);
	FillAll();
}

// ===========================================================================
//  Grouping
// ===========================================================================

void ibDialogQueryConstructor::OnAddGrouping(wxCommandEvent&)
{
	if (!CanEdit())
		return;

	ibQuerySelect* select = Current();
	if (select == nullptr)
		return;
	// THE SCOPE RULE: a field is itself, a reference is everything behind it, a table is all of it.
	const std::vector<wxString> fields = SelectedFieldsOf(m_groupingSource);
	if (fields.empty())
		return;

	// ⚠ A FIELD THE QUERY ALREADY FOLDS IS NOT A GROUP KEY — asked of the engine, the same call
	// CheckNames refuses by. Without this the same field could stand in both lists at once, and the
	// query that came out grouped by the very column it summed.
	// ASKED ONCE, not once per field: the answer is about the QUERY, and it does not change while
	// this loop runs. (Inside the loop it walked every projection again for every field selected —
	// picking a whole table meant a full re-walk per column of it.)
	std::vector<wxString> foldedText;
	for (const ibQueryAstExprPtr& folded : ibQueryLowering::AggregatedColumns(*select))
		if (folded)
			foldedText.push_back(ibRenderQueryExpr(*folded));

	for (const wxString& field : fields) {
		bool alreadyFolded = false;
		for (const wxString& text : foldedText)
			if (text.IsSameAs(field, false)) { alreadyFolded = true; break; }
		if (!alreadyFolded)
			select->m_groupBy.push_back(ibQueryColumnFromPath(field));
	}

	// ⭐ AND THE REST OF THE SELECTED FIELDS COME WITH IT — ASKED OF THE ENGINE.
	//
	// A grouping is not a property of one column, it is the shape of the whole query: once a query
	// groups, every projected column must be a group key or live inside an aggregate. So grouping by
	// one field of five means the other four are grouped too — there is no reading in which the
	// author wanted the other four to vanish, and asking them to tick four more boxes is the window
	// asking a question it knows the answer to.
	//
	// ⚠ WHICH ONES they are is the ENGINE's answer, not ours: `ibQueryLowering::UngroupedProjections`
	// is the same call CheckNames reads as a refusal. One door, two readings — the check says "this
	// is wrong and here is the field", this says "then here is the field to add". Written twice, the
	// two would drift, and the drift is a window offering a query its own engine then rejects.
	{
		const ibSourceMetaDataScope resolveAgainst(m_metaData);
		for (const ibQueryAstExprPtr& missing
		     : ibQueryLowering::UngroupedProjections(*select, std::map<wxString, ibValue>()))
			if (missing)
				select->m_groupBy.push_back(ibQueryColumnFromPath(ibRenderQueryExpr(*missing)));
	}

	FillAll();
}

void ibDialogQueryConstructor::OnRemoveGrouping(wxCommandEvent&)
{
	if (!CanEdit())
		return;

	ibQuerySelect* select = Current();
	if (select == nullptr)
		return;
	const long index = SelectedRow(m_grouping, m_groupingModel);
	if (index < 0 || static_cast<size_t>(index) >= select->m_groupBy.size())
		return;
	select->m_groupBy.erase(select->m_groupBy.begin() + index);
	FillAll();
}

void ibDialogQueryConstructor::OnAddAggregate(wxCommandEvent&)
{
	if (!CanEdit())
		return;

	ibQuerySelect* select = Current();
	if (select == nullptr)
		return;

	// THE SAME EDITOR, seeded with the call. An aggregate is an expression; the palette already
	// holds SUM/COUNT/MIN/MAX/AVG from the keyword table, and the alias is set where every other
	// projection sets one — the Fields tab, because an aggregate IS a projection.
	ibDialogQueryExpression dialog(this, _("Aggregate field"), AvailableFields(), nullptr,
		m_metaData, m_readOnly);
	dialog.SetText(SeededAggregate(SelectedFieldOf(m_groupingSource)));
	if (dialog.ShowModal() != wxID_OK)
		return;

	ibQueryProjection projection;
	projection.m_expr = dialog.GetExpression();
	if (!projection.m_expr)
		return;
	select->m_selectAll = false;
	ibQueryEnsureUniqueName(*select, projection);
	select->m_projections.push_back(projection);
	FillAll();
}

void ibDialogQueryConstructor::OnRemoveAggregate(wxCommandEvent&)
{
	if (!CanEdit())
		return;

	ibQuerySelect* select = Current();
	if (select == nullptr)
		return;
	const long index = SelectedRow(m_aggregates, m_aggregateModel);
	// The grid shows a SUBSET of the projections, so the row is mapped back through the one place
	// that decides which those are — deleting by grid index would delete somebody else's field.
	const std::vector<size_t> rows = AggregateRows();
	if (index < 0 || static_cast<size_t>(index) >= rows.size())
		return;

	select->m_projections.erase(select->m_projections.begin() + static_cast<long>(rows[index]));
	if (select->m_projections.empty())
		select->m_selectAll = true;
	FillAll();
}

// ===========================================================================
//  Conditions
// ===========================================================================

// THE CHEAP GESTURE STAYS CHEAP. Dragging a field into the conditions, or pressing `>`, or
// double-clicking it, all mean the same thing — "filter on this" — and none of them should cost a
// modal window. The row lands as `Field = &Field`: a comparison against a PARAMETER, which is what
// a condition on a field almost always is, and which PARSES, so it goes through the engine like
// everything else rather than being a half-written string the grid has to tolerate.
//
// The value is then typed over in the cell, or the whole row opened in the expression editor.
void ibDialogQueryConstructor::AddConditionsForSelectedFields()
{
	if (!CanEdit() || m_conditionModel == nullptr)
		return;

	const std::vector<wxString> fields = SelectedFieldsOf(m_conditionSource);
	if (fields.empty())
		return;

	std::vector<ibQueryAstExprPtr> rows = m_conditionModel->Rows();
	for (const wxString& field : fields) {
		// The parameter is named after the field's LAST segment: `Owner.Code` compares against
		// `&Code`, which is the name a caller would think to set.
		const wxString text = field + wxT(" = &") + field.AfterLast(wxT('.'));
		try {
			ibQueryParser parser;
			if (ibQueryAstExprPtr condition = parser.ParseExpression(text))
				rows.push_back(condition);
		}
		catch (const ibBackendException& error) {
			ShowEngineError(error.GetErrorDescription());
			return;
		}
	}
	m_conditionModel->SetRows(rows);
}

// ONE EDITOR FOR A CONDITION, and the field list beside it is the head start the old "simple mode"
// was actually for: the editor opens with `Field ` already written.
void ibDialogQueryConstructor::OnAddCondition(wxCommandEvent&)
{
	if (!CanEdit() || m_conditionModel == nullptr)
		return;

	ibQueryAstExprPtr seeded;
	const wxString field = SelectedFieldOf(m_conditionSource);

	ibDialogQueryExpression dialog(this, _("Condition"), AvailableFields(), seeded, m_metaData, m_readOnly);
	if (!field.IsEmpty())
		dialog.SetText(field + wxT(" = "));
	if (dialog.ShowModal() != wxID_OK)
		return;

	ibQueryAstExprPtr condition = dialog.GetExpression();
	if (!condition)
		return;   // an empty condition is not a condition — nothing to add

	std::vector<ibQueryAstExprPtr> rows = m_conditionModel->Rows();
	rows.push_back(condition);
	m_conditionModel->SetRows(rows);
}

void ibDialogQueryConstructor::OnEditCondition(wxCommandEvent&)
{
	if (!CanEdit() || m_conditionModel == nullptr || m_conditions == nullptr)
		return;

	const unsigned int row = m_conditionModel->GetRow(m_conditions->GetSelection());
	std::vector<ibQueryAstExprPtr> rows = m_conditionModel->Rows();
	if (row >= rows.size())
		return;

	ibDialogQueryExpression dialog(this, _("Condition"), AvailableFields(), rows[row],
		m_metaData, m_readOnly);
	if (dialog.ShowModal() != wxID_OK)
		return;

	ibQueryAstExprPtr edited = dialog.GetExpression();
	if (!edited) {
		// EMPTIED IS DELETED. A condition with no text is not a condition, and leaving the old one
		// standing would mean the editor said OK and nothing happened.
		rows.erase(rows.begin() + row);
	}
	else {
		rows[row] = edited;
	}
	m_conditionModel->SetRows(rows);
}

void ibDialogQueryConstructor::OnRemoveCondition(wxCommandEvent&)
{
	if (!CanEdit() || m_conditionModel == nullptr || m_conditions == nullptr)
		return;

	const unsigned int row = m_conditionModel->GetRow(m_conditions->GetSelection());
	std::vector<ibQueryAstExprPtr> rows = m_conditionModel->Rows();
	if (row >= rows.size())
		return;
	rows.erase(rows.begin() + row);
	m_conditionModel->SetRows(rows);
}
// ===========================================================================
//  Order
// ===========================================================================

void ibDialogQueryConstructor::OnAddOrder(wxCommandEvent&)
{
	if (!CanEdit())
		return;

	ibQuerySelect* select = Current();
	if (select == nullptr)
		return;
	const std::vector<wxString> fields = SelectedFieldsOf(m_orderSource);
	if (fields.empty())
		return;

	for (const wxString& field : fields) {
		ibQueryOrderItem item;
		item.m_expr = ibQueryColumnFromPath(field);
		select->m_orderBy.push_back(item);
	}
	FillAll();
}

void ibDialogQueryConstructor::OnRemoveOrder(wxCommandEvent&)
{
	if (!CanEdit())
		return;

	ibQuerySelect* select = Current();
	if (select == nullptr)
		return;
	const long index = SelectedRow(m_order, m_orderModel);
	if (index < 0 || static_cast<size_t>(index) >= select->m_orderBy.size())
		return;
	select->m_orderBy.erase(select->m_orderBy.begin() + index);
	FillAll();
}

void ibDialogQueryConstructor::OnToggleOrderDirection(wxCommandEvent&)
{
	if (!CanEdit())
		return;

	ibQuerySelect* select = Current();
	if (select == nullptr)
		return;
	const long index = SelectedRow(m_order, m_orderModel);
	if (index < 0 || static_cast<size_t>(index) >= select->m_orderBy.size())
		return;
	select->m_orderBy[index].m_ascending = !select->m_orderBy[index].m_ascending;
	FillAll();
	m_order->Select(m_orderModel->GetItem(static_cast<unsigned int>(index)));
}

void ibDialogQueryConstructor::OnMoveOrder(int delta)
{
	if (!CanEdit())
		return;

	ibQuerySelect* select = Current();
	if (select == nullptr)
		return;
	const long index = SelectedRow(m_order, m_orderModel);
	const long target = index + delta;
	if (index < 0 || target < 0 || static_cast<size_t>(index) >= select->m_orderBy.size()
	    || static_cast<size_t>(target) >= select->m_orderBy.size())
		return;
	std::swap(select->m_orderBy[index], select->m_orderBy[target]);
	FillAll();
	m_order->Select(m_orderModel->GetItem(static_cast<unsigned int>(target)));
}

// ===========================================================================
//  Totals
// ===========================================================================

void ibDialogQueryConstructor::OnAddTotalsAggregate(wxCommandEvent&)
{
	if (!CanEdit())
		return;

	ibQuerySelect* select = Current();
	if (select == nullptr)
		return;

	// ⚠ A TOTAL IS A TOTAL *PER LEVEL*, so there has to be a level. `TOTALS SUM(x)` with no `BY` is
	// not a shorter form — the language has no such clause, and the engine refuses it outright.
	// Letting a measure be added first produced a query that was broken the moment it appeared, and
	// the complaint pointed at a line the author had not written yet. Say what is missing instead,
	// and say it before anything is built.
	//
	// ⚠⚠ AND `OVERALL` IS A LEVEL. It is the level above every dimension, so a query with the box
	// ticked and no dimensions is complete — one row over everything. This guard was written when
	// there was no such thing and kept refusing after there was: the tick said "there is a level"
	// and the window answered "add a level", which is the window arguing with itself. The rule did
	// not change; what counts as a level did.
	if (select->m_totalsBy.empty() && !select->m_totalsOverall) {
		wxMessageBox(_("Add a grouping level first, or tick Grand totals: totals are counted PER "
		               "LEVEL, so there has to be one."),
			GetTitle(), wxOK | wxICON_INFORMATION, this);
		return;
	}

	// SEEDED FROM THE FIELD STANDING ON THIS TAB'S OWN TREE. It read the Grouping tab's tree before,
	// which is a different tab's selection — so on Totals it opened with nothing and the field had
	// to be typed by hand.
	ibDialogQueryExpression dialog(this, _("Totals"), AvailableFields(), nullptr,
		m_metaData, m_readOnly);
	dialog.SetText(SeededAggregate(SelectedFieldOf(m_totalsSource)));
	if (dialog.ShowModal() != wxID_OK)
		return;
	ibQueryAstExprPtr aggregate = dialog.GetExpression();
	if (!aggregate)
		return;

	select->m_hasTotals = true;
	select->m_totalsAggregates.push_back(aggregate);
	FillAll();
}

// EDIT THIS TOTALS LINE — the same arbitrary-expression editor, opened over the aggregate that is
// standing here rather than over a blank.
void ibDialogQueryConstructor::OnEditTotalsAggregate(wxCommandEvent&)
{
	if (!CanEdit())
		return;
	ibQuerySelect* select = Current();
	const long index = SelectedRow(m_totalsAggregates, m_totalsAggregateModel);
	if (select == nullptr || index < 0 || static_cast<size_t>(index) >= select->m_totalsAggregates.size())
		return;

	ibDialogQueryExpression dialog(this, _("Totals"), AvailableFields(),
		select->m_totalsAggregates[index], m_metaData, m_readOnly);
	if (dialog.ShowModal() != wxID_OK)
		return;

	ibQueryAstExprPtr edited = dialog.GetExpression();
	if (!edited) {
		// EMPTIED IS DELETED — a totals line with no expression is not a line.
		select->m_totalsAggregates.erase(select->m_totalsAggregates.begin() + index);
		if (select->m_totalsAggregates.empty() && select->m_totalsBy.empty())
			select->m_hasTotals = false;
	}
	else {
		select->m_totalsAggregates[index] = edited;
	}
	FillAll();
}

void ibDialogQueryConstructor::OnRemoveTotalsAggregate(wxCommandEvent&)
{
	if (!CanEdit())
		return;
	ibQuerySelect* select = Current();
	const long index = SelectedRow(m_totalsAggregates, m_totalsAggregateModel);
	if (select == nullptr || index < 0 || static_cast<size_t>(index) >= select->m_totalsAggregates.size())
		return;

	select->m_totalsAggregates.erase(select->m_totalsAggregates.begin() + index);
	if (select->m_totalsAggregates.empty() && select->m_totalsBy.empty())
		select->m_hasTotals = false;
	FillAll();
}

void ibDialogQueryConstructor::OnAddTotalsDimension(wxCommandEvent&)
{
	if (!CanEdit())
		return;

	ibQuerySelect* select = Current();
	if (select == nullptr)
		return;
	// THE TREE BESIDE IT, like every other tab — and only if nothing is picked there does it fall
	// back to asking. Dragging a field onto the grid lands here too.
	std::vector<wxString> fields = SelectedFieldsOf(m_totalsSource);
	if (fields.empty()) {
		const wxString asked = ChooseField(_("Grouping level"));
		if (asked.IsEmpty())
			return;
		fields.push_back(asked);
	}

	for (const wxString& field : fields) {
		ibQueryTotalDim dimension;
		dimension.m_expr = ibQueryColumnFromPath(field);
		select->m_totalsBy.push_back(dimension);
	}
	select->m_hasTotals = true;
	FillAll();
}

void ibDialogQueryConstructor::OnRemoveTotalsLine(wxCommandEvent&)
{
	if (!CanEdit())
		return;

	ibQuerySelect* select = Current();
	if (select == nullptr)
		return;

	// THE DIMENSION LINE. The aggregates have a delete of their own now that they have a grid of
	// their own — one verb reaching into whichever pane happened to have a selection was how a
	// delete on one side removed a line from the other.
	const long dimension = SelectedRow(m_totalsDimensions, m_totalsDimensionModel);
	if (dimension < 0 || static_cast<size_t>(dimension) >= select->m_totalsBy.size())
		return;
	select->m_totalsBy.erase(select->m_totalsBy.begin() + dimension);

	DropTotalsIfLevelless(*select);
	FillAll();
}

// ⚠ A MEASURE WITHOUT A LEVEL IS NOT A MEASURE. `TOTALS SUM(x)` with no `BY` is a clause the
// language does not have — the engine refuses it outright — so the last level leaving takes the
// whole clause with it, measures and all.
//
// The guard used to fire only when BOTH lists were empty, which left exactly the broken state: the
// last level removed, the measures still there, and a query that would not parse from that moment
// on, complaining about a line the author had not touched.
//
// Losing the measures is a real consequence, so it is SAID rather than done quietly — the author is
// the one who decides whether that was what they meant.
void ibDialogQueryConstructor::DropTotalsIfLevelless(ibQuerySelect& select)
{
	// THE OVERALL LEVEL COUNTS. Removing the last dimension while Grand totals is ticked leaves a
	// query that still has a level to count on, so the measures stay — the mirror of the guard on
	// adding one, and it has to read the same way or removing a dimension would silently throw away
	// totals the query can perfectly well produce.
	if (!select.m_totalsBy.empty() || select.m_totalsOverall)
		return;

	if (!select.m_totalsAggregates.empty()) {
		wxMessageBox(_("The last grouping level is gone, so the totals go with it: they are counted "
		               "PER LEVEL, and there is no level left to count them on."),
			GetTitle(), wxOK | wxICON_INFORMATION, this);
		select.m_totalsAggregates.clear();
	}
	select.m_hasTotals = false;
}

// THE LEVELS APPLY IN ORDER, so their order is a setting — and one moved with the same up/down
// pair the ordering tab uses, because it is the same gesture over the same kind of list.
void ibDialogQueryConstructor::OnMoveTotalsDimension(int delta)
{
	if (!CanEdit())
		return;
	ibQuerySelect* select = Current();
	const long index = SelectedRow(m_totalsDimensions, m_totalsDimensionModel);
	const long target = index + delta;
	if (select == nullptr || index < 0 || target < 0
	    || static_cast<size_t>(index) >= select->m_totalsBy.size()
	    || static_cast<size_t>(target) >= select->m_totalsBy.size())
		return;

	std::swap(select->m_totalsBy[index], select->m_totalsBy[target]);
	FillAll();
	m_totalsDimensions->Select(m_totalsDimensionModel->GetItem(static_cast<unsigned int>(target)));
}


// EDIT THE DIMENSION — the same expression editor. A level is an expression like any other; the
// kind and the name beside it are typed in their own cells.
void ibDialogQueryConstructor::OnEditTotalsDimension(wxCommandEvent&)
{
	if (!CanEdit())
		return;
	ibQuerySelect* select = Current();
	const long index = SelectedRow(m_totalsDimensions, m_totalsDimensionModel);
	if (select == nullptr || index < 0 || static_cast<size_t>(index) >= select->m_totalsBy.size())
		return;

	ibDialogQueryExpression dialog(this, _("Grouping level"), AvailableFields(),
		select->m_totalsBy[index].m_expr, m_metaData, m_readOnly);
	if (dialog.ShowModal() != wxID_OK)
		return;

	ibQueryAstExprPtr edited = dialog.GetExpression();
	if (!edited) {
		select->m_totalsBy.erase(select->m_totalsBy.begin() + index);
		DropTotalsIfLevelless(*select);
	}
	else {
		select->m_totalsBy[index].m_expr = edited;
	}
	FillAll();
}

// (The unfold used to be CYCLED by a button — press it three times to get back where you were.
// It is a registered enumeration, so its cell is a CHOICE over the three words the language has;
// the cycling handler is gone rather than left as a second way to set the same thing.)

// ===========================================================================
//  Unions
// ===========================================================================

void ibDialogQueryConstructor::OnAddUnionBranch(wxCommandEvent&)
{
	if (!CanEdit())
		return;

	ibQuerySelect* select = Current();
	if (select == nullptr)
		return;

	ibQuerySelectPtr branch = std::make_shared<ibQuerySelect>();
	branch->m_selectAll = true;
	if (!ibShowQueryConstructorFor(this, branch, m_metaData, m_readOnly))
		return;
	select->m_unions.push_back(branch);
	FillAll();
}

void ibDialogQueryConstructor::OnEditUnionBranch(wxCommandEvent&)
{
	ibQuerySelect* select = Current();
	if (select == nullptr)
		return;
	const long row = (m_unionModel != nullptr && m_unions != nullptr)
		? static_cast<long>(m_unionModel->GetRow(m_unions->GetSelection())) : -1;
	if (row <= 0)
		return;   // row 0 is this query — it is edited on the tabs, not in a second window

	const size_t index = static_cast<size_t>(row) - 1;
	if (index >= select->m_unions.size())
		return;

	ibQuerySelectPtr branch = select->m_unions[index];
	if (ibShowQueryConstructorFor(this, branch, m_metaData, m_readOnly)) {
		select->m_unions[index] = branch;
		FillAll();
	}
}

void ibDialogQueryConstructor::OnRemoveUnionBranch(wxCommandEvent&)
{
	if (!CanEdit())
		return;

	ibQuerySelect* select = Current();
	if (select == nullptr)
		return;
	const long row = (m_unionModel != nullptr && m_unions != nullptr)
		? static_cast<long>(m_unionModel->GetRow(m_unions->GetSelection())) : -1;
	if (row <= 0)
		return;
	const size_t index = static_cast<size_t>(row) - 1;
	if (index >= select->m_unions.size())
		return;
	select->m_unions.erase(select->m_unions.begin() + static_cast<long>(index));
	FillAll();
}

// A NEW BRANCH IS USUALLY THE LAST ONE, ALTERED — same tables, one condition different. Starting
// from a copy is the gesture that makes a union of five near-identical selects bearable; starting
// from a blank makes it five times the same typing.
void ibDialogQueryConstructor::OnCopyUnionBranch(wxCommandEvent&)
{
	if (!CanEdit())
		return;

	ibQuerySelect* select = StatementSelect();
	if (select == nullptr)
		return;
	const long row = (m_unionModel != nullptr && m_unions != nullptr)
		? static_cast<long>(m_unionModel->GetRow(m_unions->GetSelection())) : -1;
	if (row < 0)
		return;

	// Row 0 is THIS query, and copying it is the ordinary case: "another branch like the one I have
	// already written". A DEEP clone, through the engine's own cloner — a shallow copy would share
	// expression nodes between two branches and edit both at once.
	const ibQuerySelect* source = row == 0
		? select
		: (static_cast<size_t>(row) - 1 < select->m_unions.size() ? select->m_unions[row - 1].get() : nullptr);
	if (source == nullptr)
		return;

	ibQuerySelectPtr copy = ibQueryRewrite::Clone(*source);
	if (!copy)
		return;
	copy->m_unions.clear();     // a branch has no branches of its own
	copy->m_intoTemp.clear();   // and it materialises nothing: that belongs to the statement
	copy->m_indexBy.clear();
	select->m_unions.push_back(copy);
	FillAll();
}

// THE FIRST BRANCH DECIDES THE RESULT'S SHAPE, so moving one into first place is a real edit and
// not a re-ordering of a list. Row 0 is this query's own core; exchanging it with a branch swaps
// the two queries and leaves the branch LIST where it is, because the list belongs to the
// statement rather than to whichever core is currently first.
void ibDialogQueryConstructor::OnMoveUnionBranch(int delta)
{
	if (!CanEdit())
		return;

	ibQuerySelect* select = StatementSelect();
	if (select == nullptr || m_unionModel == nullptr || m_unions == nullptr)
		return;
	const long row = static_cast<long>(m_unionModel->GetRow(m_unions->GetSelection()));
	const long target = row + delta;
	const long count = static_cast<long>(select->m_unions.size()) + 1;
	if (row < 0 || target < 0 || row >= count || target >= count)
		return;

	if (row > 0 && target > 0) {
		std::swap(select->m_unions[row - 1], select->m_unions[target - 1]);
	}
	else {
		// One of the two IS the statement's own core. Exchange everything except the branch list —
		// and except the "keep duplicates" flag, which describes the JOIN AT A POSITION rather than
		// the query, so it stays with the position.
		const size_t index = static_cast<size_t>(row > 0 ? row : target) - 1;
		ibQuerySelect& branch = *select->m_unions[index];

		std::vector<ibQuerySelectPtr> branches;
		branches.swap(select->m_unions);          // the list belongs to the STATEMENT, not to a core
		const bool keepAll = branch.m_unionAll;
		std::swap(*select, branch);
		select->m_unions.swap(branches);
		branch.m_unionAll = keepAll;
		select->m_unionAll = false;               // the first branch is attached by nothing
	}

	m_unionBranch = static_cast<int>(target) - 1;
	FillAll();
}

// THE MAP'S ROWS ARE THE OUTPUT FIELDS — the first branch's projections, which is what a union
// returns. So deleting a row deletes that projection, and the other branches simply stop being
// lined up against a field that no longer exists.
void ibDialogQueryConstructor::OnRemoveUnionField(wxCommandEvent&)
{
	if (!CanEdit() || m_unionFieldModel == nullptr || m_unionFields == nullptr)
		return;

	ibQuerySelect* select = StatementSelect();
	const long row = static_cast<long>(m_unionFieldModel->GetRow(m_unionFields->GetSelection()));
	if (select == nullptr || row < 0 || static_cast<size_t>(row) >= select->m_projections.size())
		return;

	select->m_projections.erase(select->m_projections.begin() + row);
	if (select->m_projections.empty())
		select->m_selectAll = true;   // a query with no named fields reads every one of them
	FillAll();
}

void ibDialogQueryConstructor::OnMoveUnionField(int delta)
{
	if (!CanEdit() || m_unionFieldModel == nullptr || m_unionFields == nullptr)
		return;

	ibQuerySelect* select = StatementSelect();
	const long row = static_cast<long>(m_unionFieldModel->GetRow(m_unionFields->GetSelection()));
	const long target = row + delta;
	if (select == nullptr || row < 0 || target < 0
	    || static_cast<size_t>(row) >= select->m_projections.size()
	    || static_cast<size_t>(target) >= select->m_projections.size())
		return;

	std::swap(select->m_projections[row], select->m_projections[target]);
	FillAll();
	m_unionFields->Select(m_unionFieldModel->GetItem(static_cast<unsigned int>(target)));
}


// ===========================================================================
//  Advanced
// ===========================================================================

void ibDialogQueryConstructor::OnAdvancedChanged(wxCommandEvent&)
{
	if (!CanEdit())
		return;

	if (m_filling)
		return;
	ibQuerySelect* select = Current();
	if (select == nullptr)
		return;

	select->m_distinct  = m_distinct->GetValue();
	select->m_allowed   = m_allowed->GetValue();
	select->m_forUpdate = m_forUpdate->GetValue();
	select->m_top       = m_useTop->GetValue() ? m_topCount->GetValue() : 0;

	m_topCount->Enable(m_useTop->GetValue());

	FillPackage();
	FillPreview();
}

