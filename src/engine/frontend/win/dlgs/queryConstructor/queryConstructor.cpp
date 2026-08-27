////////////////////////////////////////////////////////////////////////////
//	Description : The query constructor - the window and everything on it
//	              (queryConstructor.h)
////////////////////////////////////////////////////////////////////////////

#include "queryConstructorInternal.h"

using namespace queryctor;


// THE QUERY TEXT, STYLED. The KEYWORD SET comes from the language's own table, so a word added to
// the grammar lights up the day it is added and a localized table lights up its own words.
//
// The lexer is the C one, not Scintilla's SQL one, and the reason is COMMENTS. Our query language
// takes its comment form from the shared lexer (ibTranslateCode::SkipSpaces) — `//` to end of line,
// and nothing else. Scintilla's SQL lexer knows the opposite set: it colours `--` and `/* … */`, and
// leaves `//` plain. So the SQL lexer painted as a comment what the engine would refuse, and painted
// as code what the engine ignores — a pane that teaches the wrong language. The C lexer's line
// comment IS ours.
//
// One residue, stated rather than hidden: the C lexer also colours a `/* … */` block, which the
// engine does not accept. Making the two agree exactly means either teaching the engine block
// comments — a change to the SHARED lexer, so it would land in the script language too — or forking
// the vendored SQL lexer. Neither is worth it for a form the language does not have.
//
// The font and the colours are the ENGINE'S defaults (ibFontColorSettings, default-constructed —
// the same object the code editor styles itself from), so this pane looks like every other place
// code is shown in the product rather than like a window with opinions.
void ibStyleQueryText(wxStyledTextCtrl* text)
{
	const ibFontColorSettings settings;
	wxFont font = settings.GetFont();

	text->SetLexer(wxSTC_LEX_CPP);
	text->SetKeyWords(0, ibAllQueryKeywords());

	// STYLE_DEFAULT first, then StyleClearAll — the cascade carries the font and the base colours to
	// every style index, so only the ones that DIFFER are set afterwards.
	text->StyleSetFont(wxSTC_STYLE_DEFAULT, font);
	text->StyleSetForeground(wxSTC_STYLE_DEFAULT,
		settings.GetColors(ibFontColorSettings::DisplayItem_Default).foreColor);
	text->StyleSetBackground(wxSTC_STYLE_DEFAULT,
		settings.GetColors(ibFontColorSettings::DisplayItem_Default).backColor);
	text->StyleClearAll();

	const ibFontColorSettings::Colors keyword = settings.GetColors(ibFontColorSettings::DisplayItem_Keyword);
	text->StyleSetForeground(wxSTC_C_WORD, keyword.foreColor);
	text->StyleSetBold(wxSTC_C_WORD, keyword.bold);
	text->StyleSetForeground(wxSTC_C_STRING,
		settings.GetColors(ibFontColorSettings::DisplayItem_String).foreColor);
	// A DATE literal is written in apostrophes ('20260101'), which the C lexer reads as a character
	// constant — so it takes the string colour, the way the pane's reader means it.
	text->StyleSetForeground(wxSTC_C_CHARACTER,
		settings.GetColors(ibFontColorSettings::DisplayItem_String).foreColor);
	text->StyleSetForeground(wxSTC_C_NUMBER,
		settings.GetColors(ibFontColorSettings::DisplayItem_Number).foreColor);
	// The three comment styles the C lexer can emit — `//` is the one our language has, the other two
	// cost nothing to colour and keep a pasted block from looking like broken code.
	const ibFontColorSettings::Colors comment = settings.GetColors(ibFontColorSettings::DisplayItem_Comment);
	text->StyleSetForeground(wxSTC_C_COMMENTLINE, comment.foreColor);
	text->StyleSetForeground(wxSTC_C_COMMENT, comment.foreColor);
	text->StyleSetForeground(wxSTC_C_COMMENTDOC, comment.foreColor);
	text->StyleSetForeground(wxSTC_C_OPERATOR,
		settings.GetColors(ibFontColorSettings::DisplayItem_Operator).foreColor);

	// LINE NUMBERS. The engine reports where it stopped as `line N (position M)` — that is the whole
	// reason they earn their room here: without them the verdict line names a place the reader has
	// to count to. The margin is sized to the text, so it does not take a fixed slice of a short
	// query, and the folding margin stays off (there is nothing to fold in one statement).
	text->SetMarginType(0, wxSTC_MARGIN_NUMBER);
	text->StyleSetForeground(wxSTC_STYLE_LINENUMBER,
		settings.GetColors(ibFontColorSettings::DisplayItem_Comment).foreColor);
	text->SetMarginWidth(0, text->TextWidth(wxSTC_STYLE_LINENUMBER, wxT("_9999")));
	text->SetMarginWidth(1, 0);
	text->SetTabWidth(4);
	text->SetUseHorizontalScrollBar(true);

	// A PARAMETER IS ITS OWN THING and must read as one. `&Store` is not an identifier of this query
	// — it is a value handed in from outside — and the C lexer, which is what colours the rest,
	// knows nothing about the ampersand: it paints `&` as an operator and `Store` as plain text, so
	// a parameter looked exactly like a column. Marked with an INDICATOR rather than a style,
	// because the lexer owns the styles and would repaint over anything set there; an indicator is
	// drawn on top and survives re-lexing. The colour is the preprocessor slot — the same one the
	// code editor uses for "this is substituted, not evaluated here".
	text->IndicatorSetStyle(kQueryParameterIndicator, wxSTC_INDIC_TEXTFORE);
	text->IndicatorSetForeground(kQueryParameterIndicator,
		settings.GetColors(ibFontColorSettings::DisplayItem_Preprocessor).foreColor);

	// AND RE-MARKED AS IT IS TYPED. Marking only where the text is set programmatically leaves a
	// parameter plain until something else happens to refill the pane — the colour arrives late,
	// which reads as a glitch rather than as a rule. The lexer repaints on every change; this
	// follows it, because an indicator is not restored by the lexer.
	text->Bind(wxEVT_STC_CHANGE, [text](wxStyledTextEvent& event) {
		event.Skip();
		ibMarkQueryParameters(text);
	});
}

// MARK EVERY `&name` IN THE PANE. Called after the text is set — the indicator has to be re-applied
// then, because the text it marked is gone. Scanning here rather than asking the lexer keeps this
// independent of which lexer the pane uses; the rule is the language's own and fits in one line of
// prose: an ampersand followed by a name.
void ibMarkQueryParameters(wxStyledTextCtrl* text)
{
	if (text == nullptr)
		return;

	// WALKED IN SCINTILLA'S OWN POSITIONS, which are BYTES. Scanning a wxString instead would put
	// the mark left of the word the moment a parameter is named in Cyrillic, because two bytes go
	// to one character — and the bug would only show up for people whose names are not ASCII.
	// A byte >= 0x80 is part of a name here for the same reason: it is the tail of such a character.
	const int length = text->GetTextLength();
	text->SetIndicatorCurrent(kQueryParameterIndicator);
	text->IndicatorClearRange(0, length);

	for (int pos = 0; pos + 1 < length; ++pos) {
		if (text->GetCharAt(pos) != '&')
			continue;

		int end = pos + 1;
		for (; end < length; ++end) {
			const int ch = text->GetCharAt(end);
			const bool nameChar = (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z')
				|| (ch >= 'a' && ch <= 'z') || ch == '_' || ch >= 0x80;
			if (!nameChar)
				break;
		}
		if (end == pos + 1)
			continue;   // a bare ampersand is not a parameter

		text->IndicatorFillRange(pos, end - pos);
		pos = end - 1;
	}
}

wxToolBar* ibDialogQueryConstructor::MakeToolBar(wxWindow* parent)
{
	wxToolBar* bar = new wxToolBar(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTB_HORIZONTAL | wxTB_FLAT | wxTB_NODIVIDER);
	// 16×16 — a settings toolbar sits above a list, and the stock size (24 or 32, per theme) makes
	// it the loudest thing on the page. Same call listSettings makes, for the same reason.
	bar->SetToolBitmapSize(FromDIP(wxSize(16, 16)));
	m_commandBars.push_back(bar);
	return bar;
}

void ibDialogQueryConstructor::AddTool(wxToolBar* bar, const wxString& label, const wxString& artId,
                                       std::function<void(wxCommandEvent&)> handler)
{
	// ⚠ THE PRODUCT'S OWN IDS, NOT THE STOCK ONES. The frontend art provider serves exactly
	// wxART_ADD / EDIT / DELETE / UP / DOWN / SORT (artProvider.cpp) — ask it for `wxART_NEW` or
	// `wxART_LIST_VIEW` and it answers with nothing, MSW has no 16x16 stock bitmap for them either,
	// and the tool draws as a blank square. That is why the "nested table" button was invisible:
	// it was there, with no picture on it, and a toolbar of blanks is a toolbar you cannot read.
	const wxSize size = FromDIP(wxSize(16, 16));
	wxBitmapBundle bitmap = wxArtProvider::GetBitmapBundle(artId, wxART_FRONTEND, size);
	if (!bitmap.IsOk())
		bitmap = wxArtProvider::GetBitmapBundle(artId, wxASCII_STR(wxART_MENU), size);

	const int id = wxWindow::NewControlId();
	// AND IF THERE IS STILL NO PICTURE, SHOW THE WORD. A verb that cannot draw itself must not
	// become an invisible button — the label is the fallback, not the empty rectangle.
	if (bitmap.IsOk()) {
		bar->AddTool(id, label, bitmap, label);
	}
	else {
		bar->SetWindowStyle(bar->GetWindowStyle() | wxTB_TEXT);
		bar->AddTool(id, label, wxBitmapBundle(), label);
	}
	bar->Bind(wxEVT_TOOL, handler, id);

	// …AND THE BAR REMEMBERS ITS VERBS. That is what lets the right-click offer the same ones without
	// a second list written by hand — see AttachContextMenu.
	m_barVerbs[bar].push_back({ label, handler });
}

// THE RIGHT-CLICK OFFERS WHAT THE TOOLBAR OFFERS — the same verbs, in the same order, from the SAME
// registration. A person who learns either has learned both.
//
// ⚠ Not a menu per pane. A menu written out beside a toolbar is a second list of the same commands,
// and the day a verb is added to one of them the two disagree — which is how a window ends up with a
// button that has no menu item and a menu item that does nothing. The toolbar records each verb as it
// is added (AddTool); this replays that record. A tool added tomorrow is in the menu the same day,
// with nothing edited here.
void ibDialogQueryConstructor::AttachContextMenu(wxWindow* target, wxToolBar* bar)
{
	if (target == nullptr || bar == nullptr)
		return;

	auto popup = [this, bar](wxWindow* over) {
		const auto verbs = m_barVerbs.find(bar);
		if (verbs == m_barVerbs.end() || verbs->second.empty() || !CanEdit())
			return;

		wxMenu menu;
		std::vector<int> ids;
		for (const Verb& verb : verbs->second) {
			const int id = wxWindow::NewControlId();
			ids.push_back(id);
			menu.Append(id, verb.m_label);
		}
		menu.Bind(wxEVT_MENU, [&verbs, &ids](wxCommandEvent& event) {
			for (size_t i = 0; i < ids.size(); ++i)
				if (ids[i] == event.GetId()) {
					wxCommandEvent unused;
					if (verbs->second[i].m_handler)
						verbs->second[i].m_handler(unused);
					return;
				}
		});
		over->PopupMenu(&menu);
	};

	// A GRID AND A TREE ASK DIFFERENTLY, so both are bound — the pane does not have to say which it
	// is. The row under the cursor is selected first, because a verb acts on the selection and a
	// right-click that does not move it would act on whatever was chosen before.
	if (ibDataViewCtrl* grid = dynamic_cast<ibDataViewCtrl*>(target)) {
		grid->Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, [grid, popup](ibDataViewEvent& event) {
			if (event.GetItem().IsOk())
				grid->Select(event.GetItem());
			popup(grid);
		});
		return;
	}
	if (wxTreeCtrl* tree = dynamic_cast<wxTreeCtrl*>(target)) {
		tree->Bind(wxEVT_TREE_ITEM_MENU, [tree, popup](wxTreeEvent& event) {
			if (event.GetItem().IsOk())
				tree->SelectItem(event.GetItem());
			popup(tree);
		});
		return;
	}
	target->Bind(wxEVT_CONTEXT_MENU, [target, popup](wxContextMenuEvent&) { popup(target); });
}

// A MOVE button — the narrow column that sits BETWEEN two panes. Its whole job is direction, so it
// is a glyph and it is square; a word here would make the column as wide as the pane beside it.
wxButton* ibDialogQueryConstructor::AddMoveButton(wxWindow* parent, wxSizer* sizer, const wxString& glyph,
                                                  const wxString& tip,
                                                  std::function<void(wxCommandEvent&)> handler)
{
	wxButton* button = new wxButton(parent, wxID_ANY, glyph, wxDefaultPosition,
		parent->FromDIP(wxSize(30, 26)));
	button->SetToolTip(tip);
	button->Bind(wxEVT_BUTTON, handler);
	sizer->Add(button, 0, wxBOTTOM, parent->FromDIP(3));
	m_commandButtons.push_back(button);
	return button;
}

bool ibDialogQueryConstructor::IsMetaDataReadOnly(const ibMetaData* metaData)
{
	if (metaData == nullptr)
		return false;   // no config in play (a runtime list's own query) — nothing forbids editing it

	// The DESIGNER question, asked where it is already answered: a configuration loaded read-only
	// says so through its tree. No tree = a runtime host with no designer surface, which does not
	// make a query unreadable.
	const ibBackendMetadataTree* const tree = metaData->GetMetaTree();
	return tree != nullptr && !tree->IsEditable();
}

// ===========================================================================
//  Construction
// ===========================================================================


// A HOST THAT FOLDS ELSEWHERE GETS NO TOTALS — and not only as a hidden tab.
//
// ⭐ Max, 2026-08-19: "the arbitrary query of a list, and the composer — they exclude TOTALS from
// what it shows. And if any slip through, it throws them out." The tab going is half the promise:
// text pasted in, or a query written before the rule existed, would otherwise keep a TOTALS clause
// that nothing in the window can show, edit or delete. So it is dropped on the way in, once, here.
//
// Why those two hosts: a composition's totals ARE its resources and its levels ARE its groupings,
// and a dynamic list folds through its own settings. In both, a TOTALS in the text is the same
// setting written twice — in the copy nobody can see.
// A LEVEL'S FIELDS ARE ONE CELL, WRITTEN AS A LIST — `Partner, Contract` groups by both together.
// Split on the commas that are OUTSIDE brackets and outside a literal, so a call in the list
// (`SUBSTRING(Name, 1, 3)`) stays one field. Both quote characters are honoured, since which one a
// literal uses is the lexer's business and this only has to avoid cutting inside one.
static std::vector<wxString> ibSplitLevelFields(const wxString& text)
{
	std::vector<wxString> parts;
	wxString current;
	int      depth = 0;
	wxChar   quote = 0;
	for (size_t i = 0; i < text.length(); ++i) {
		const wxChar c = text[i];
		if (quote != 0) {
			current += c;
			if (c == quote) quote = 0;
			continue;
		}
		if (c == wxT('"') || c == wxT('\'')) { quote = c; current += c; continue; }
		if (c == wxT('(')) ++depth;
		else if (c == wxT(')')) --depth;
		if (c == wxT(',') && depth <= 0) { parts.push_back(current); current.clear(); continue; }
		current += c;
	}
	parts.push_back(current);
	for (wxString& part : parts)
		part.Trim(true).Trim(false);
	return parts;
}

static void ibDropTotalsFromPackage(ibQueryPackage& package)
{
	for (ibQueryAstStatement& statement : package.m_statements) {
		if (!statement.m_select)
			continue;
		statement.m_select->m_totalsBy.clear();
		statement.m_select->m_totalsAggregates.clear();
		statement.m_select->m_totalsOverall = false;
		statement.m_select->m_hasTotals = false;
	}
}
ibDialogQueryConstructor::ibDialogQueryConstructor(wxWindow* parent, const ibQueryPackage& package,
                                                   const ibMetaData* metaData, bool readOnly, int exclude)
	: wxDialog(parent, wxID_ANY, _("Query constructor"), wxDefaultPosition, wxSize(1040, 720),
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
	, m_package(package)
	, m_metaData(metaData)
	, m_model(metaData)
	// The metadata is asked FIRST, and the caller can only add to its answer. A configuration
	// opened without the right to change it stays unchangeable through this window too.
	, m_readOnly(readOnly || IsMetaDataReadOnly(metaData))
	, m_exclude(exclude)
{
	// ⭐ WHAT THIS HOST DOES NOT TAKE, TAKEN OUT AT ONCE. Hiding the tab is only half of it: the
	// package may already carry a TOTALS clause (pasted text, a query written before the rule), and
	// what no tab can show, nobody can remove either.
	if ((m_exclude & ibQueryExclude_Totals) != 0)
		ibDropTotalsFromPackage(m_package);

	// ONE FONT, SET BEFORE ANYTHING IS BUILT. Controls made at different times otherwise pick up
	// different defaults and the window reads as several dialogs stitched together — and setting it
	// afterwards is worse: the notebook keeps the tab strip it measured for the old font, which is
	// what drew the first tab as "Fielbles and fields". Children inherit it at construction.
	SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));

	// THE WINDOW WEARS THE SAME PICTURE THE MENU ITEM DOES. The context menu that opens this, the
	// button on the list's Query tab and this title bar are three ways to the one thing, and one
	// picture on all three is what says so. Asked of the art provider by id — the same id, not a
	// copy of the bitmap.
	{
		const wxBitmap picture = wxArtProvider::GetBitmap(wxART_QUERY_CONSTRUCTOR, wxART_FRONTEND,
			FromDIP(wxSize(16, 16)));
		if (picture.IsOk()) {
			wxIcon icon;
			icon.CopyFromBitmap(picture);
			SetIcon(icon);
		}
	}

	// A constructor opened on nothing still opens on SOMETHING: one empty statement, so every tab
	// has a query to edit. Building from nothing is the same road as building from something.
	if (m_package.m_statements.empty()) {
		ibQueryAstStatement statement;
		statement.m_select = std::make_shared<ibQuerySelect>();
		statement.m_select->m_selectAll = true;
		m_package.m_statements.push_back(statement);
	}

	wxBoxSizer* outer = new wxBoxSizer(wxVERTICAL);

	// SAY IT AT THE TOP, ONCE. A window whose buttons are simply grey leaves the user guessing
	// whether something is broken; the sentence tells them the query is there to be read and that
	// nothing they do here will be kept.
	if (m_readOnly) {
		wxStaticText* banner = new wxStaticText(this, wxID_ANY,
			// ASCII ONLY in this literal: the file is UTF-8 with no BOM, MSVC reads it in the
			// system codepage, and an em dash typed here reaches the screen as two garbage glyphs.
			_("View only. This query belongs to a configuration that cannot be changed. "
			  "Nothing edited here will be saved."));
		banner->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
		outer->Add(banner, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(8));
	}

	wxBoxSizer* upper = new wxBoxSizer(wxHORIZONTAL);

	// THE PACKAGE IS THE SHELL'S TOP LEVEL — a list of statements AROUND the tabs, because that is
	// what a package is: the tabs edit one statement, the strip says which.
	m_notebook = new wxNotebook(this, wxID_ANY);
	// The branch strip follows the tab: switching branches is meaningless where the branches
	// themselves are being configured.
	m_notebook->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, [this](wxBookCtrlEvent& e) {
		// A DELIBERATE move is remembered; one made by a refill is not. Without the guard the window
		// would "choose" whatever a rebuild happened to land on, and the tab a person picked would be
		// forgotten by the very event that lost it.
		if (!m_filling && e.GetSelection() != wxNOT_FOUND)
			m_wantedTab = m_notebook->GetPageText(static_cast<size_t>(e.GetSelection()));
		ShowBranchStrip();
		e.Skip();
	});
	// The order: what the query READS first, then how it is narrowed, then how it is presented —
	// with the package LAST, because it is the thing the other tabs belong to rather than another
	// aspect of one query.
	//
	// `queryPage` marks the tabs that describe a QUERY. A statement that drops a temp table has no
	// query, so those tabs are REMOVED for it rather than greyed: a disabled tab invites clicking,
	// and there is nothing behind it to see.
	//
	// The LAST field is the exclusion bit — which flag of the host's mask turns this tab off. Every
	// tab carries one, so a host that owns a setting itself (a composition's totals, a list's
	// grouping) says so with a flag rather than with a new parameter here.
	m_pages.push_back({ BuildTablesPage(m_notebook),     _("Tables and fields"), true,  false, false, false, ibQueryExclude_Tables });
	m_pages.push_back({ BuildLinksPage(m_notebook),      _("Links"),             true,  true,  false, false, ibQueryExclude_Links });
	m_pages.push_back({ BuildGroupingPage(m_notebook),   _("Grouping"),          true,  false, false, false, ibQueryExclude_Grouping });
	m_pages.push_back({ BuildConditionsPage(m_notebook), _("Conditions"),        true,  false, false, false, ibQueryExclude_Conditions });
	m_pages.push_back({ BuildAdvancedPage(m_notebook),   _("Advanced"),          false, false, false, false, ibQueryExclude_Advanced });
	// UNIONS / ALIASES — one tab, because it is one question. An output field's NAME is what a union
	// lines its branches up BY, so the place that shows the line-up is the place the name is typed;
	// the tab is called both things because it genuinely is both.
	m_pages.push_back({ BuildUnionsPage(m_notebook),     _("Unions / Aliases"),  true,  false, false, false, ibQueryExclude_Unions });
	// ⭐ ORDER IS REFUSED FOR A TEMP TABLE TOO, exactly as Totals is below it (Max): what is
	// materialised under a name is ROWS, and a table keeps no order — sorting on the way into storage
	// is work thrown away in the same breath. The parser refuses it, so the tab goes rather than
	// being left to write text the engine will reject.
	m_pages.push_back({ BuildOrderPage(m_notebook),      _("Order"),             true,  false, false, true,  ibQueryExclude_Order });
	// Totals is refused for a statement that makes a temp table — the parser says so, so the tab
	// goes rather than being left to write text the engine rejects. A HOST may exclude it as well.
	m_pages.push_back({ BuildTotalsPage(m_notebook),     _("Totals"),            true,  false, false, true,  ibQueryExclude_Totals });
	m_pages.push_back({ BuildIndexPage(m_notebook),      _("Index"),             true,  false, true,  false, ibQueryExclude_Index });
	m_pages.push_back({ BuildPackagePage(m_notebook),    _("Query batch"),       false, false, false, false, ibQueryExclude_Batch });
	// ⭐ …AND ONE TIER UP: links between the results the package has NAMED. Not a query tab — it is
	// about the package, like the batch beside it — and it appears only once there are two names to
	// relate. It shares the exclusion bit of the batch: a host that has no package has no results
	// to link either.
	m_pages.push_back({ BuildSelectionLinksPage(m_notebook), _("Selection links"), false, false, false, false, ibQueryExclude_Batch, true });
	SyncNotebookPages();
	upper->Add(m_notebook, 1, wxEXPAND | wxALL, FromDIP(6));

	// THE UNION BRANCHES AS TABS, down the right edge. A branch is a query — its own tables, its own
	// conditions, its own ordering — so it is edited with the SAME tabs, and these say which one
	// those tabs are showing. Tabs and not a list: a branch is not something you pick out of a
	// collection, it is the query you are currently in, and that is what a tab means.
	//
	// The pages are empty on purpose. The content beside them IS the main notebook; this control is
	// here for its tab strip, so it is kept as narrow as the strip needs.
	m_branchStrip = new wxNotebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxNB_RIGHT);
	m_branchStrip->SetMinSize(FromDIP(wxSize(34, -1)));
	m_branchStrip->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, &ibDialogQueryConstructor::OnBranchSelected, this);
	upper->Add(m_branchStrip, 0, wxEXPAND | wxTOP | wxBOTTOM, FromDIP(6));

	// AND THE BATCH, one level further out. A statement of a batch is a QUERY — its own tables, its
	// own conditions, its own everything — so it is switched the same way a union branch is, and by
	// the same kind of control. The list on the "Query batch" page is where statements are ADDED and
	// ORDERED; this is where you simply move between them, which is what you do most.
	m_batchStrip = new wxNotebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxNB_RIGHT);
	m_batchStrip->SetMinSize(FromDIP(wxSize(34, -1)));
	m_batchStrip->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, &ibDialogQueryConstructor::OnBatchSelected, this);
	upper->Add(m_batchStrip, 0, wxEXPAND | wxTOP | wxBOTTOM | wxRIGHT, FromDIP(6));

	outer->Add(upper, 1, wxEXPAND);

	// ⭐ THE TEXT HAS ITS OWN WINDOW, and the tabs get the room back.
	//
	// It used to sit under the tabs the whole time, on the argument that showing the round trip
	// teaches the language. It does — but a fifth of the window spent on eight visible lines of a
	// forty-line query teaches very little, and those eight lines are the SELECT list, which is the
	// least interesting part of any query. The round trip is not lost: it is one button away, at full
	// height, where the text can actually be read and edited.
	//
	// The control itself stays a child of this dialog, hidden. It is where the text LIVES — every
	// fill writes to it, the selection verbs read it — and the window borrows it while it is open.
	// One control, one text, no second copy to keep in step.
	m_preview = new wxStyledTextCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize);
	ibStyleQueryText(m_preview);
	m_preview->Bind(wxEVT_KILL_FOCUS, &ibDialogQueryConstructor::OnPreviewFocusLost, this);
	m_preview->Show(false);

	// THE ENGINE'S ANSWER STAYS IN SIGHT. Not "there is a problem on the Conditions tab" — the
	// parser's own line and position, printed. A layer that rewords the core's diagnostics is a
	// layer that will eventually word them wrongly. It is one line, and it is the one thing under the
	// tabs that has to be readable without opening anything.
	// ⚠ `wxST_NO_AUTORESIZE`, OR THE LINE KEEPS THE TAIL OF THE LAST MESSAGE. Without it the control
	// SHRINKS to each new label, and the strip it vacates is never repainted by anybody — so a short
	// verdict ("The query engine reads this query.") left the end of the previous, longer one
	// standing beside it, and the two read as one piece of nonsense.
	//
	// Fixed width means the control owns the whole line and erases all of it on every paint.
	m_status = new wxStaticText(this, wxID_ANY, wxEmptyString,
		wxDefaultPosition, wxDefaultSize, wxST_NO_AUTORESIZE);
	outer->Add(m_status, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(10));

	wxBoxSizer* buttons = new wxBoxSizer(wxHORIZONTAL);
	// THE DOOR TO THE TEXT. Open whether or not the query is sound — reading what the constructor
	// wrote is exactly what a person does when it is NOT sound.
	wxButton* queryText = new wxButton(this, wxID_ANY, _("Query text..."));
	queryText->Bind(wxEVT_BUTTON, &ibDialogQueryConstructor::OnShowQueryText, this);
	buttons->Add(queryText, 0, wxRIGHT, FromDIP(6));

	// Checking stays available when the query may only be read — asking the engine whether a query
	// is sound is a way of READING it, and the answer is worth having whether or not it can be fixed.
	wxButton* check = new wxButton(this, wxID_ANY, _("Check query"));
	check->Bind(wxEVT_BUTTON, &ibDialogQueryConstructor::OnCheck, this);
	buttons->Add(check, 0, wxRIGHT, FromDIP(6));
	buttons->AddStretchSpacer();
	// No OK where there is nothing to accept — one button that closes, and no promise that
	// something was kept.
	buttons->Add(CreateStdDialogButtonSizer(m_readOnly ? wxCLOSE : (wxOK | wxCANCEL)), 0);
	outer->Add(buttons, 0, wxEXPAND | wxALL, FromDIP(6));

	SetSizer(outer);
	Bind(wxEVT_BUTTON, &ibDialogQueryConstructor::OnOk, this, wxID_OK);
	// A reading window closes on CANCEL whatever the button says — the caller then writes nothing
	// back, and there is no path through which it could.
	Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CANCEL); }, wxID_CLOSE);

	if (m_readOnly) {
		// The buttons go grey together — the SIGN that this is a reading window. The guarantee is
		// CanEdit() inside the handlers, because a double-click reaches one with no button in the way.
		for (wxButton* button : m_commandButtons)
			if (button != nullptr) button->Enable(false);
		if (m_preview != nullptr)
			m_preview->SetReadOnly(true);
	}

	// ⭐⭐ `SELECT *` IS OPENED AS THE FIELDS IT STANDS FOR.
	//
	// The star is a promise about a shape nobody has written down — and this window is where a shape
	// is written down. Left as a star, the Fields pane is EMPTY over a query that plainly selects
	// something, and every tab that offers the result's fields (Totals, the union line-up) has
	// nothing to offer. Expanded, the author sees exactly what the query returns and can take one
	// away, which is the whole reason to open the constructor on such a query.
	//
	// Done ONCE, on opening: after this the projections are the author's own, and nothing here
	// re-expands anything.
	ExpandStars();

	FillSourceTree();
	FillAll();

	// (A query the engine cannot read never gets this far: ibShowQueryConstructor refuses to open on
	//  one, with the engine's own words. A warning shown here, over an opened window, was the half
	//  measure that replaced.)
}

// THE PACKAGE IS A TAB, and the tabs edit whichever statement it has selected. It was a side strip
// first, which cost every other tab a fifth of the window for a list that is usually one line long
// — the package is a thing you visit, not a thing you watch.
wxWindow* ibDialogQueryConstructor::BuildPackagePage(wxWindow* parent)
{
	wxPanel* panel = new wxPanel(parent);
	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

	wxToolBar* bar = MakeToolBar(panel);
	AddTool(bar, _("Add query"), wxART_ADD,    [this](wxCommandEvent& e) { OnAddStatement(e); });
	AddTool(bar, _("Delete"),    wxART_DELETE, [this](wxCommandEvent& e) { OnRemoveStatement(e); });
	bar->AddSeparator();
	AddTool(bar, _("Move up"),   wxART_UP,   [this](wxCommandEvent&) { OnMoveStatement(-1); });
	AddTool(bar, _("Move down"), wxART_DOWN, [this](wxCommandEvent&) { OnMoveStatement(+1); });
	bar->Realize();
	sizer->Add(bar, 0, wxEXPAND);

	// A STATEMENT IS NAMED PLAINLY — "Query 1", "Query 2". The second column is what it does, read
	// off the AST: a plain select, one that makes a temp table, one that drops it.
	m_statementModel = new ibQueryGridModel();
	m_statementModel->SetReader([this](unsigned int row, unsigned int col) -> wxString {
		if (row >= m_package.m_statements.size())
			return wxEmptyString;
		const ibQueryAstStatement& statement = m_package.m_statements[row];
		if (col == kGridCol1)
			return wxString::Format(_("Query %u"), row + 1);
		if (!statement.m_dropTemp.IsEmpty())
			return wxString::Format(_("drops the temp table %s"), statement.m_dropTemp);
		if (statement.m_select && !statement.m_select->m_intoTemp.IsEmpty())
			return wxString::Format(_("makes the temp table %s"), statement.m_select->m_intoTemp);
		if (statement.m_select && !statement.m_select->m_ontoName.IsEmpty())
			return wxString::Format(_("selects as %s"), statement.m_select->m_ontoName);
		return _("selects");
	});
	m_statements = MakeGrid(panel, m_statementModel, [this] { FillAll(); });
	AttachContextMenu(m_statements, bar);
	m_statements->GetRootColumnGroup()->AppendColumn(TextColumn(_("Query"),  kGridCol1, FromDIP(160)));
	m_statements->GetRootColumnGroup()->AppendColumn(TextColumn(_("Does"),   kGridCol2, FromDIP(520)));
	m_statements->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, &ibDialogQueryConstructor::OnStatementSelected, this);
	sizer->Add(m_statements, 1, wxEXPAND | wxALL, FromDIP(3));

	panel->SetSizer(sizer);
	return panel;
}

// Three panes, and the MOVE BUTTONS SIT BETWEEN THEM rather than under each — the arrangement
// every tool of this kind uses, and for a reason: a `>` under the left pane says "do something to
// this list", the same `>` between the panes says "send it there", which is what it does.
//
// The dividers are SPLITTERS: how much room the table list needs against the field list is a
// judgement about the query being written, not about the window, so it belongs to the person
// writing it.
wxWindow* ibDialogQueryConstructor::BuildTablesPage(wxWindow* parent)
{
	wxPanel* page = new wxPanel(parent);

	// splitterOuter: [available] | [ splitterInner: [tables] | [fields] ]
	wxSplitterWindow* splitterOuter = new wxSplitterWindow(page, wxID_ANY,
		wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3DSASH);
	splitterOuter->SetMinimumPaneSize(FromDIP(140));

	// ---- LEFT: the catalogue. A WALK over the queryable factory (plus the temp tables this
	// package has declared so far), never a written-out list of metaclasses.
	wxPanel* leftPane = new wxPanel(splitterOuter);
	wxBoxSizer* left = new wxBoxSizer(wxVERTICAL);
	left->Add(new wxStaticText(leftPane, wxID_ANY, _("Available tables")), 0, wxALL, FromDIP(3));
	// NO TOOLBAR HERE. This pane is the CATALOGUE — what the configuration has. Nothing is created
	// in it, so a verb standing over it had nothing to act on: "add a nested table" makes a table
	// for THIS QUERY, which is the middle pane, and that is where it now lives beside "edit nested".
	m_sourceTree = new wxTreeCtrl(leftPane, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTR_HAS_BUTTONS | wxTR_SINGLE | wxTR_HIDE_ROOT | wxTR_LINES_AT_ROOT | wxTR_NO_LINES | wxTR_TWIST_BUTTONS);
	m_sourceTree->Bind(wxEVT_TREE_ITEM_ACTIVATED, [this](wxTreeEvent&) { wxCommandEvent e; OnAddTable(e); });
	m_sourceTree->Bind(wxEVT_TREE_BEGIN_DRAG, &ibDialogQueryConstructor::OnSourceBeginDrag, this);
	m_sourceTree->Bind(wxEVT_TREE_ITEM_EXPANDING, &ibDialogQueryConstructor::OnFieldTreeExpanding, this);
	left->Add(m_sourceTree, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(3));
	leftPane->SetSizer(left);

	wxSplitterWindow* splitterInner = new wxSplitterWindow(splitterOuter, wxID_ANY,
		wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3DSASH);
	splitterInner->SetMinimumPaneSize(FromDIP(160));

	// ---- MIDDLE: the chosen tables, each expanding into its own fields. This is where "ask the
	// SOURCE which it is" lands: a real table answers through its descriptor, a nested one through
	// its projections, and this pane never learns the difference.
	wxPanel* middlePane = new wxPanel(splitterInner);
	wxBoxSizer* middleRow = new wxBoxSizer(wxHORIZONTAL);

	wxBoxSizer* toTables = new wxBoxSizer(wxVERTICAL);
	toTables->AddStretchSpacer();
	AddMoveButton(middlePane, toTables, ArrowRight(), _("Add the selected table"),
		[this](wxCommandEvent& e) { OnAddTable(e); });
	AddMoveButton(middlePane, toTables, ArrowLeft(), _("Remove the selected table"),
		[this](wxCommandEvent& e) { OnRemoveTable(e); });
	toTables->AddStretchSpacer();
	middleRow->Add(toTables, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(3));

	wxBoxSizer* middle = new wxBoxSizer(wxVERTICAL);
	middle->Add(new wxStaticText(middlePane, wxID_ANY, _("Tables")), 0, wxALL, FromDIP(3));
	wxToolBar* middleBar = MakeToolBar(middlePane);
	// A NESTED TABLE IS A TABLE OF THIS QUERY, so it is added where the query's tables are — and it
	// opens THIS SAME window one level down over the inner select.
	AddTool(middleBar, _("Nested table"), wxART_ADD,
		[this](wxCommandEvent& e) { OnAddNestedTable(e); });
	AddTool(middleBar, _("Edit nested"), wxART_EDIT,
		[this](wxCommandEvent& e) { OnEditNestedTable(e); });
	AddTool(middleBar, _("Delete"), wxART_DELETE,
		[this](wxCommandEvent& e) { OnRemoveTable(e); });
	middleBar->Realize();
	middle->Add(middleBar, 0, wxEXPAND);
	// wxTR_EDIT_LABELS — A TABLE'S ALIAS IS ITS LABEL. `FROM Catalog.Products AS p` is the name the
	// rest of the query calls that table by, and the place a person expects to type a thing's name
	// is on the thing. F2 or a slow second click renames it; a FIELD row refuses (see the BEGIN
	// handler), because a field's name is not the author's to change here.
	m_tables = new wxTreeCtrl(middlePane, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTR_HAS_BUTTONS | wxTR_SINGLE | wxTR_HIDE_ROOT | wxTR_LINES_AT_ROOT | wxTR_NO_LINES
		| wxTR_TWIST_BUTTONS | wxTR_EDIT_LABELS);
	m_tables->Bind(wxEVT_TREE_BEGIN_LABEL_EDIT, &ibDialogQueryConstructor::OnTableAliasEditBegin, this);
	m_tables->Bind(wxEVT_TREE_END_LABEL_EDIT,   &ibDialogQueryConstructor::OnTableAliasEditEnd, this);
	// AND THE SAME VERBS ON A RIGHT-CLICK. A rename is something you reach for ON the thing, so the
	// menu is where the hand already is; it raises the tree's own label editor, so there is one
	// renaming mechanism and not a dialog beside it.
	m_tables->Bind(wxEVT_TREE_ITEM_MENU, &ibDialogQueryConstructor::OnTableContextMenu, this);
	m_tables->Bind(wxEVT_TREE_ITEM_ACTIVATED, [this](wxTreeEvent&) { wxCommandEvent e; OnAddField(e); });
	m_tables->Bind(wxEVT_TREE_BEGIN_DRAG, &ibDialogQueryConstructor::OnTableBeginDrag, this);
	m_tables->Bind(wxEVT_TREE_ITEM_EXPANDING, &ibDialogQueryConstructor::OnFieldTreeExpanding, this);
	// DROPPING A TABLE HERE ADDS IT — the gesture the panes are arranged for, and the one a person
	// reaches for before finding the button.
	m_tables->SetDropTarget(new ibCallbackDropTarget([this] { wxCommandEvent e; OnAddTable(e); }));
	middle->Add(m_tables, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(3));
	middleRow->Add(middle, 1, wxEXPAND);
	middlePane->SetSizer(middleRow);

	// ---- RIGHT: the projections, in order.
	wxPanel* rightPane = new wxPanel(splitterInner);
	wxBoxSizer* rightRow = new wxBoxSizer(wxHORIZONTAL);

	wxBoxSizer* toFields = new wxBoxSizer(wxVERTICAL);
	toFields->AddStretchSpacer();
	AddMoveButton(rightPane, toFields, ArrowRight(), _("Add the selected field"),
		[this](wxCommandEvent& e) { OnAddField(e); });
	AddMoveButton(rightPane, toFields, ArrowLeft(), _("Remove the selected field"),
		[this](wxCommandEvent& e) { OnRemoveField(e); });
	toFields->AddStretchSpacer();
	rightRow->Add(toFields, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(3));

	wxBoxSizer* right = new wxBoxSizer(wxVERTICAL);
	right->Add(new wxStaticText(rightPane, wxID_ANY, _("Fields")), 0, wxALL, FromDIP(3));
	wxToolBar* rightBar = MakeToolBar(rightPane);
	AddTool(rightBar, _("Expression"), wxART_ADD,       [this](wxCommandEvent& e) { OnAddFieldExpression(e); });
	// EDIT THE CURRENT FIELD — the arbitrary-expression editor, over the field standing here. The
	// same window the conditions, the joins and the totals open; there is one of it in this dialog.
	AddTool(rightBar, _("Edit"),       wxART_EDIT, [this](wxCommandEvent& e) { OnEditFieldExpression(e); });
	// ⚠ AND DELETE, ON THE PANE ITSELF. The verb existed and was reachable only by the `‹` button in
	// the narrow column BETWEEN the panes — which reads as "move it back", not as "remove it", and is
	// not where anyone looks for a delete. Every other pane in this window carries its own verbs on
	// its own toolbar; this one was missing the one that takes something away.
	AddTool(rightBar, _("Delete"),     wxART_DELETE, [this](wxCommandEvent& e) { OnRemoveField(e); });
	rightBar->AddSeparator();
	AddTool(rightBar, _("Move up"),   wxART_UP,   [this](wxCommandEvent&) { OnMoveField(-1); });
	AddTool(rightBar, _("Move down"), wxART_DOWN, [this](wxCommandEvent&) { OnMoveField(+1); });
	rightBar->Realize();
	right->Add(rightBar, 0, wxEXPAND);
	// THE PROJECTIONS, AS A GRID — the field, edited where it stands. Typing over it replaces the
	// expression through the engine's parser, so a nonsense expression is refused by the engine and
	// not by us; double-clicking opens the full expression editor over the same cell, because a
	// SELECT list takes expressions and a one-line cell is not where a CASE is written.
	//
	// ⚠ NO ALIAS COLUMN HERE, deliberately. An output field's NAME is set in one place — the Unions
	// tab, where the field map lines the branches up by it. Adding a field GENERATES the name (see
	// ibQueryEnsureUniqueName at every add), so it is already there to be changed; offering a second
	// place to type it would be two controls over one word.
	m_fieldModel = new ibQueryGridModel();
	m_fieldModel->SetReader([this](unsigned int row, unsigned int) -> wxString {
		const ibQuerySelect* select = Current();
		if (select == nullptr || row >= select->m_projections.size())
			return wxEmptyString;
		const ibQueryProjection& projection = select->m_projections[row];
		return projection.m_expr ? ibRenderQueryExpr(*projection.m_expr) : wxString();
	});
	m_fieldModel->SetWriter([this](unsigned int row, unsigned int, const wxString& text) -> bool {
		ibQuerySelect* select = Current();
		if (!CanEdit() || select == nullptr || row >= select->m_projections.size())
			return false;
		// THE ENGINE READS IT, not us. An expression typed into the cell goes through the same parser
		// the runtime uses, and its complaint is what the user is shown.
		try {
			ibQueryParser parser;
			select->m_projections[row].m_expr = parser.ParseExpression(text);
		}
		catch (const ibBackendException& error) {
			ShowEngineError(error.GetErrorDescription());
			return false;
		}
		// A TYPED EXPRESSION IS NAMED TOO. Typing `1` over a column turns a field that had a name
		// into one that has none, and the Unions tab is where a name is CHANGED, not where it is
		// invented — so it is given one here, the same way an added field is.
		ibQueryEnsureUniqueName(*select, select->m_projections[row]);
		return true;
	});
	m_fields = MakeGrid(rightPane, m_fieldModel, [this] { FillAll(); });
	// ⚠ NOT TYPED INTO. A field is an EXPRESSION, and an expression is written in the expression
	// editor — with the fields to hand, the language palette beside it and the engine reading it.
	// A one-line cell invites half of one, and half an expression is a lexical error shown back to
	// the author about text the window itself put there.
	//
	// So: the cell SHOWS it, the editor CHANGES it. (The names are the other way round — an alias
	// is typed where it stands, because a name is not an expression.)
	m_fieldModel->SetIconColumn(kGridCol1, ibValue::GetIconGroup());
	// PER ROW: the column already said what it looks like; this only carries the answer.
	m_fieldModel->SetIconReader([this](unsigned int row) -> wxIcon {
		const ibQuerySelect* select = Current();
		return select != nullptr && row < select->m_projections.size()
			? IconOfExpr(select->m_projections[row].m_expr) : wxNullIcon;
	});
	m_fields->GetRootColumnGroup()->AppendColumn(IconColumn(_("Field"), kGridCol1, FromDIP(240)));   // fits its pane — see the totals grid
	// EditItem is inert on an inert column, so the double-click lands here instead.
	m_fields->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED,
		[this](ibDataViewEvent&) { wxCommandEvent e; OnEditFieldExpression(e); });
	AttachContextMenu(m_fields, rightBar);   // the same verbs the toolbar above it carries
	// THE DROP IS ANSWERED BY WHERE IT CAME FROM. From the chosen tables it is "select this field";
	// from the CATALOGUE it is "read this table and select this field" — one gesture, because that
	// is what the author meant.
	m_fields->SetDropTarget(new ibCallbackDropTarget([this] {
		if (m_dragTree == m_sourceTree && AddCatalogueFieldToSelect())
			return;
		wxCommandEvent e; OnAddField(e);
	}));
	right->Add(m_fields, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(3));
	rightRow->Add(right, 1, wxEXPAND);
	rightPane->SetSizer(rightRow);

	splitterInner->SplitVertically(middlePane, rightPane, FromDIP(300));
	splitterOuter->SplitVertically(leftPane, splitterInner, FromDIP(240));

	wxBoxSizer* pageSizer = new wxBoxSizer(wxVERTICAL);
	pageSizer->Add(splitterOuter, 1, wxEXPAND | wxALL, FromDIP(4));
	page->SetSizer(pageSizer);
	return page;
}

// TWO VIEWS OF ONE THING. The diagram above, the list below, a splitter between them — and both
// refilled from `m_joins` after every change, so neither can drift from the other or from the text.
// The picture is where a join is MADE (drag a field onto a field); the list is where it is read at
// a glance and where its exact predicate is edited.
wxWindow* ibDialogQueryConstructor::BuildLinksPage(wxWindow* parent)
{
	wxPanel* page = new wxPanel(parent);

	// ⚠ THE DIAGRAM IS GONE. It drew the two tables as boxes to drag a field between — a second way
	// to say what the row below says, and the row says it better now that both tables are chosen
	// from drop-downs and the condition has the expression editor behind its "...". Two pictures of
	// one thing cost a splitter, a refill and a drag protocol, and taught nothing the grid does not.
	wxPanel* bottomPane = page;
	wxBoxSizer* bottom = new wxBoxSizer(wxVERTICAL);
	wxToolBar* bar = MakeToolBar(bottomPane);
	// A LINK IS MADE HERE TOO, not only by adding a table. Joining a table that is already in the
	// query — or writing a second condition between the same pair — had no verb at all: the only
	// way to get a link was to add a source, which is a different act with a different meaning.
	AddTool(bar, _("Add link"),  wxART_NEW,    [this](wxCommandEvent& e) { OnAddLink(e); });
	// A SECOND LINK IS USUALLY THE FIRST ONE WITH A NAME CHANGED. Copying it onto the next unlinked
	// table and editing the cell is one gesture where retyping the whole condition was several.
	AddTool(bar, _("Copy link"), wxASCII_STR(wxART_COPY),
		[this](wxCommandEvent& e) { OnCopyLink(e); });
	AddTool(bar, _("Condition"), wxART_EDIT,   [this](wxCommandEvent& e) { OnEditLink(e); });
	AddTool(bar, _("Delete"),    wxART_DELETE, [this](wxCommandEvent& e) { OnRemoveLink(e); });
	bar->Realize();
	bottom->Add(bar, 0, wxEXPAND);

	// THE GRID, on a dataview model — the two "all" boxes are edited IN PLACE, which is the whole
	// reason it is a model and not a filled list. The kind lives in the AST as an enum; the boxes
	// are how a person reads and writes it (queryLinkModel.h).
	m_links = new ibDataViewCtrl(bottomPane, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxDV_ROW_LINES | wxDV_SINGLE);
	m_linkModel = new ibQueryLinkModel();
	m_linkModel->SetOnChanged([this] { FillAll(); });
	m_linkModel->SetOnError([this](const wxString& message) { ShowEngineError(message); });
	m_links->AssociateModel(m_linkModel);

	// THE TABLES ARE CHOSEN, NOT TYPED. Which tables the query reads is the Tables tab's verb — a
	// name typed here would have to invent adding or removing a source — but WHICH of the tables
	// already in the query this link joins is exactly this row's business, and a drop-down is what
	// says so. The list is the live sources, minus the one picked on the other side: a table joined
	// to itself in one row is not a link, it is a typo the grid should not have offered.
	m_links->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(_("Table 1"),
		new ibRowChoiceRenderer([this]() -> wxArrayString { return LinkTableChoices(/*leftSide*/true); }),
		kLinkColLeftTable, FromDIP(210), wxAlignment::wxALIGN_LEFT));
	m_links->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(_("All"),
		new ibDataViewToggleRenderer(ibDataViewToggleRenderer::GetDefaultType(), wxDATAVIEW_CELL_ACTIVATABLE),
		kLinkColAllLeft, FromDIP(44), wxAlignment::wxALIGN_CENTER));
	m_links->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(_("Table 2"),
		new ibRowChoiceRenderer([this]() -> wxArrayString { return LinkTableChoices(/*leftSide*/false); }),
		kLinkColRightTable, FromDIP(210), wxAlignment::wxALIGN_LEFT));
	m_links->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(_("All"),
		new ibDataViewToggleRenderer(ibDataViewToggleRenderer::GetDefaultType(), wxDATAVIEW_CELL_ACTIVATABLE),
		kLinkColAllRight, FromDIP(44), wxAlignment::wxALIGN_CENTER));
	// ⭐ THE SWITCH, BEFORE THE CONDITION IT SWITCHES. Cleared, the cell beside it is a closed list of
	// the field pairs these two tables offer; ticked, it is free text with the "..." into the
	// expression editor. Neither changes the query — only which of the two ways this row is written.
	m_links->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(_("Arbitrary"),
		new ibDataViewToggleRenderer(ibDataViewToggleRenderer::GetDefaultType(),
			m_readOnly ? wxDATAVIEW_CELL_INERT : wxDATAVIEW_CELL_ACTIVATABLE),
		kLinkColArbitrary, FromDIP(80), wxAlignment::wxALIGN_CENTER));
	// THE LINK CONDITION IS AN EXPRESSION, and it gets the cell that says so: a list of the ready
	// links beside a "..." into the full editor — the same cell the totals use, not a second one.
	//
	// A plain text column made the arbitrary link unreachable: `a.x = b.y` had to be typed blind,
	// `a.x = b.y AND a.z = b.w` even more so, and the fields of the two tables were three tabs away
	// while typing it. The engine now accepts an arbitrary ON (queryLowering splits it: the first
	// comparison is the join key, the rest are conditions), so the cell has to be able to write one.
	m_links->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(_("Link condition"),
		new ibExpressionCellRenderer(
			[this]() -> wxArrayString { return LinkConditionChoices(); },
			[this](wxString& text) -> bool { return EditLinkCondition(text); },
			wxDATAVIEW_CELL_EDITABLE,
			[this]() -> bool {
				if (m_linkModel == nullptr || m_links == nullptr)
					return true;
				return m_linkModel->IsArbitrary(m_linkModel->GetRow(m_links->GetSelection()));
			}),
		kLinkColCondition, FromDIP(340), wxAlignment::wxALIGN_LEFT));

	// The condition cell opens IN PLACE like every other editable cell; the "..." inside it is the
	// full expression editor, and the toolbar's "Condition" verb is the same door for the keyboard.
	EditOnActivate(m_links);
	AttachContextMenu(m_links, bar);
	bottom->Add(m_links, 1, wxEXPAND | wxALL, FromDIP(3));
	page->SetSizer(bottom);
	return page;
}

// ⭐⭐ LINKING THE PACKAGE'S NAMED SELECTIONS — and NOTHING ELSE HAPPENS.
////////////////////////////////////////////////////////////////////////////
//
// Max, 2026-08-21, after three shapes of this tab: mark two statements as named selections and set
// the links between them — no temporary tables, no substitutions, no source appearing in anybody's
// Tables, no statement added to the package. That is the whole feature, and it is why this tab does
// not edit a statement at all: it edits `ibQueryPackage::m_links`, which belongs to the PACKAGE.
//
// A link INSIDE a query is still an ordinary JOIN and still lives on the Links tab. This is the
// other thing — a relation the package declares between two of its results — and the text spells it
// where a statement would stand: `JOIN T1 AND T2 ON …` (no new keyword; the position decides).
//
// The grid is the same five cells a join is read through, so nothing had to be learnt to use it.
//
// See docs/query-constructor.md §8a.
wxWindow* ibDialogQueryConstructor::BuildSelectionLinksPage(wxWindow* parent)
{
	wxPanel* page = new wxPanel(parent);
	wxBoxSizer* box = new wxBoxSizer(wxVERTICAL);

	wxToolBar* bar = MakeToolBar(page);
	AddTool(bar, _("Add link"), wxART_NEW,    [this](wxCommandEvent& e) { OnSelectionLinkAdd(e); });
	AddTool(bar, _("Delete"),   wxART_DELETE, [this](wxCommandEvent& e) { OnSelectionLinkRemove(e); });
	bar->Realize();
	box->Add(bar, 0, wxEXPAND);

	m_selectionLinks = new ibDataViewCtrl(page, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxDV_ROW_LINES | wxDV_SINGLE);
	m_selectionLinkModel = new ibQuerySelectionLinkModel();
	m_selectionLinkModel->SetOnChanged([this] { FillAll(); });
	m_selectionLinkModel->SetOnError([this](const wxString& message) { ShowEngineError(message); });
	m_selectionLinks->AssociateModel(m_selectionLinkModel);

	m_selectionLinks->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(_("Selection 1"),
		new ibRowChoiceRenderer([this]() -> wxArrayString { return NamedResultChoices(/*leftSide*/true); }),
		kLinkColLeftTable, FromDIP(210), wxAlignment::wxALIGN_LEFT));
	m_selectionLinks->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(_("All"),
		new ibDataViewToggleRenderer(ibDataViewToggleRenderer::GetDefaultType(), wxDATAVIEW_CELL_ACTIVATABLE),
		kLinkColAllLeft, FromDIP(44), wxAlignment::wxALIGN_CENTER));
	m_selectionLinks->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(_("Selection 2"),
		new ibRowChoiceRenderer([this]() -> wxArrayString { return NamedResultChoices(/*leftSide*/false); }),
		kLinkColRightTable, FromDIP(210), wxAlignment::wxALIGN_LEFT));
	m_selectionLinks->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(_("All"),
		new ibDataViewToggleRenderer(ibDataViewToggleRenderer::GetDefaultType(), wxDATAVIEW_CELL_ACTIVATABLE),
		kLinkColAllRight, FromDIP(44), wxAlignment::wxALIGN_CENTER));
	// ⚠ NO "ARBITRARY" COLUMN HERE, unlike the Links tab. There it distinguishes a link the author
	// wrote by hand from one picked out of the ready field pairs; between two SELECTIONS there is no
	// such distinction — every one of them is written by hand — so the cell said "yes" on every row
	// and refused every click, which is a column that carries no information and promises an edit
	// that cannot happen.
	// THE CONDITION — the ready field pairs of the two SELECTIONS beside a "…" into the full editor,
	// exactly as a link between two tables is written. Their fields are the projections of the
	// statements that named them, so there is a list to offer and no reason to make a person type it.
	m_selectionLinks->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(_("Link condition"),
		new ibExpressionCellRenderer(
			[this]() -> wxArrayString { return SelectionLinkConditionChoices(); },
			[this](wxString& text) -> bool { return EditSelectionLinkCondition(text); },
			wxDATAVIEW_CELL_EDITABLE),
		kLinkColCondition, FromDIP(340), wxAlignment::wxALIGN_LEFT));

	EditOnActivate(m_selectionLinks);
	AttachContextMenu(m_selectionLinks, bar);
	box->Add(m_selectionLinks, 1, wxEXPAND | wxALL, FromDIP(3));
	page->SetSizer(box);
	return page;
}

// A pane that is a titled list with its own verb band above it — the shape every two-list tab in
// this window uses, written once so the tabs cannot drift apart in spacing or in behaviour.
// THE FIELDS YOU CAN GROUP BY ARE SHOWN, not remembered. A tab that only lists what has already
// been chosen makes the author go somewhere else to find out what there was — so the left pane is
// the available fields, and the two right panes are what has been done with them: the grouping
// keys above, the aggregates below.
wxWindow* ibDialogQueryConstructor::BuildGroupingPage(wxWindow* parent)
{
	wxPanel* page = new wxPanel(parent);

	wxSplitterWindow* splitter = new wxSplitterWindow(page, wxID_ANY,
		wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3DSASH);
	splitter->SetMinimumPaneSize(FromDIP(150));

	// ---- LEFT: what there is to group by.
	wxPanel* leftPane = new wxPanel(splitter);
	wxBoxSizer* left = new wxBoxSizer(wxVERTICAL);
	left->Add(new wxStaticText(leftPane, wxID_ANY, _("Fields")), 0, wxALL, FromDIP(3));
	m_groupingSource = new wxTreeCtrl(leftPane, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTR_HAS_BUTTONS | wxTR_SINGLE | wxTR_HIDE_ROOT | wxTR_LINES_AT_ROOT | wxTR_NO_LINES | wxTR_TWIST_BUTTONS);
	m_groupingSource->Bind(wxEVT_TREE_ITEM_ACTIVATED, [this](wxTreeEvent&) { wxCommandEvent e; OnAddGrouping(e); });
	// DRAGGABLE, like every other field list — you can drag anything you can see.
	m_groupingSource->Bind(wxEVT_TREE_BEGIN_DRAG, [this](wxTreeEvent& e) { OnFieldTreeBeginDrag(e); });
	m_groupingSource->Bind(wxEVT_TREE_ITEM_EXPANDING, &ibDialogQueryConstructor::OnFieldTreeExpanding, this);
	left->Add(m_groupingSource, 1, wxEXPAND | wxALL, FromDIP(3));
	leftPane->SetSizer(left);

	// ---- RIGHT: the two things a grouping is made of, one above the other.
	wxSplitterWindow* rightSplit = new wxSplitterWindow(splitter, wxID_ANY,
		wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3DSASH);
	rightSplit->SetMinimumPaneSize(FromDIP(70));

	wxPanel* keysPane = new wxPanel(rightSplit);
	wxBoxSizer* keysRow = new wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer* keysMove = new wxBoxSizer(wxVERTICAL);
	keysMove->AddStretchSpacer();
	AddMoveButton(keysPane, keysMove, ArrowRight(), _("Group by the selected field"),
		[this](wxCommandEvent& e) { OnAddGrouping(e); });
	AddMoveButton(keysPane, keysMove, ArrowLeft(), _("Remove the grouping field"),
		[this](wxCommandEvent& e) { OnRemoveGrouping(e); });
	keysMove->AddStretchSpacer();
	keysRow->Add(keysMove, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(3));
	wxBoxSizer* keys = new wxBoxSizer(wxVERTICAL);
	keys->Add(new wxStaticText(keysPane, wxID_ANY, _("Grouping fields")), 0, wxALL, FromDIP(3));
	// EDITED IN PLACE, like every other grid here. A grouping key is an expression; typing over it
	// goes through the engine's parser, and what the parser says is what the user is shown.
	m_groupingModel = new ibQueryGridModel();
	m_groupingModel->SetReader([this](unsigned int row, unsigned int) -> wxString {
		const ibQuerySelect* select = Current();
		if (select == nullptr || row >= select->m_groupBy.size() || !select->m_groupBy[row])
			return wxEmptyString;
		return ibRenderQueryExpr(*select->m_groupBy[row]);
	});
	m_groupingModel->SetWriter([this](unsigned int row, unsigned int, const wxString& text) -> bool {
		ibQuerySelect* select = Current();
		if (!CanEdit() || select == nullptr || row >= select->m_groupBy.size())
			return false;
		try {
			ibQueryParser parser;
			select->m_groupBy[row] = parser.ParseExpression(text);
		}
		catch (const ibBackendException& error) {
			ShowEngineError(error.GetErrorDescription());
			return false;
		}
		return true;
	});
	m_grouping = MakeGrid(keysPane, m_groupingModel, [this] { FillAll(); });
	m_groupingModel->SetIconColumn(kGridCol1, ibValue::GetIconGroup());
	m_groupingModel->SetIconReader([this](unsigned int row) -> wxIcon {
		const ibQuerySelect* select = Current();
		return select != nullptr && row < select->m_groupBy.size()
			? IconOfExpr(select->m_groupBy[row]) : wxNullIcon;
	});
	m_grouping->GetRootColumnGroup()->AppendColumn(IconColumn(_("Grouping field"), kGridCol1, FromDIP(420)));
	m_grouping->SetDropTarget(new ibCallbackDropTarget([this] { wxCommandEvent e; OnAddGrouping(e); }));
	keys->Add(m_grouping, 1, wxEXPAND | wxALL, FromDIP(3));
	keysRow->Add(keys, 1, wxEXPAND);
	keysPane->SetSizer(keysRow);

	// The aggregates are PROJECTIONS whose expression is an aggregate call — the same list the
	// Fields tab shows, seen through the question this tab asks. One AST, two views. Shown with
	// the FUNCTION in its own column, because that is the thing being chosen about them.
	wxPanel* aggPane = new wxPanel(rightSplit);
	wxBoxSizer* aggRow = new wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer* aggMove = new wxBoxSizer(wxVERTICAL);
	aggMove->AddStretchSpacer();
	AddMoveButton(aggPane, aggMove, ArrowRight(), _("Sum the selected field"),
		[this](wxCommandEvent& e) { OnAddAggregate(e); });
	AddMoveButton(aggPane, aggMove, ArrowLeft(), _("Remove the aggregate field"),
		[this](wxCommandEvent& e) { OnRemoveAggregate(e); });
	aggMove->AddStretchSpacer();
	aggRow->Add(aggMove, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(3));
	wxBoxSizer* agg = new wxBoxSizer(wxVERTICAL);
	agg->Add(new wxStaticText(aggPane, wxID_ANY, _("Aggregate fields")), 0, wxALL, FromDIP(3));
	// The aggregate rows are PROJECTIONS whose expression is an aggregate call, so the row indexes
	// into the projection list — see AggregateRows(), which is the one place that decides which
	// projections those are.
	m_aggregateModel = new ibQueryGridModel();
	m_aggregateModel->SetReader([this](unsigned int row, unsigned int col) -> wxString {
		const ibQuerySelect* select = Current();
		const std::vector<size_t> rows = AggregateRows();
		if (select == nullptr || row >= rows.size())
			return wxEmptyString;
		const ibQueryProjection& projection = select->m_projections[rows[row]];
		if (!projection.m_expr || projection.m_expr->m_kind != ibQueryAstExprKind::Func)
			return wxEmptyString;
		// THE ARGUMENT in one column and the FUNCTION in the other — `SUM(x)` in a single cell buries
		// the thing actually being chosen about an aggregate.
		if (col == kGridCol1)
			return projection.m_expr->m_star ? wxString(wxT("*"))
				: (projection.m_expr->m_arg ? ibRenderQueryExpr(*projection.m_expr->m_arg) : wxString());
		// ⭐⭐ …AND OVER WHAT IT IS COMPUTED. Here the grouping is the QUERY's own (GROUP BY), so this
		// is a window in the plainest sense: the DBMS folds the rows, then computes this over the
		// folded ones. `RANK() OVER (ORDER BY SUM(x) DESC)` — the place of a group — is an ordinary
		// query on the server, no tree involved.
		if (col == kGridCol3) {
			if (!projection.m_expr->m_over)
				return wxEmptyString;
			wxString over;
			for (const ibQueryAstExprPtr& key : projection.m_expr->m_over->m_partitionBy) {
				if (!key) continue;
				if (!over.IsEmpty()) over += wxT(", ");
				over += ibRenderQueryExpr(*key);
			}
			return over;
		}
		// The MODIFIER IS PART OF WHAT THIS CELL SAYS. Showing the bare word over
		// `COUNT(DISTINCT Board)` would let the row read as a plain count — and picking any value in
		// the dropdown would then quietly agree with the lie.
		return ibQueryKeywordText(projection.m_expr->m_func)
			+ (projection.m_expr->m_distinctArg
				? wxT(" (") + ibQueryKeywordText(ibQueryKeyword::Distinct) + wxT(")") : wxString());
	});
	m_aggregateModel->SetWriter([this](unsigned int row, unsigned int col, const wxString& text) -> bool {
		ibQuerySelect* select = Current();
		const std::vector<size_t> rows = AggregateRows();
		if (!CanEdit() || select == nullptr || row >= rows.size())
			return false;
		ibQueryProjection& projection = select->m_projections[rows[row]];
		if (!projection.m_expr || projection.m_expr->m_kind != ibQueryAstExprKind::Func)
			return false;
		// ⚠⚠ THE FUNCTION IS SET ON THE NODE, NEVER REBUILT AS TEXT.
		//
		// This used to render the argument back to text, glue the chosen word around it and re-parse
		// the result. Two things were wrong with it and both bit: the round trip through text can
		// only ever LOSE (a rendered argument is re-read, and anything the renderer spells slightly
		// differently comes back as something else), and a word that is not an aggregate produced a
		// nonsense expression the parser then blamed the whole query for — seen live as
		// `SUM(Products.Name)  + 1 AS Field1` with "unexpected text after the query".
		//
		// The function IS a field of the node. Setting it is one assignment that cannot fail and
		// cannot touch the argument.
		if (col == kGridCol2) {
			// `COUNT (DISTINCT)` is ONE choice in the list and TWO fields on the node — the function
			// and its modifier. Read the modifier off first so what is left is a plain keyword.
			wxString word = text.Upper();
			const wxString distinctMark = wxT(" (") + ibQueryKeywordText(ibQueryKeyword::Distinct) + wxT(")");
			const bool distinct = word.EndsWith(distinctMark.Upper());
			if (distinct)
				word = word.Left(word.length() - distinctMark.length());

			const ibQueryKeyword chosen = ibFindQueryKeyword(word.Trim());
			if (!ibIsAggregateKeyword(chosen))
				return false;   // not an aggregate: the cell keeps what it had

			projection.m_expr->m_func = chosen;
			// ⚠ ALWAYS WRITTEN, both ways. Assigning it only when true left `COUNT (DISTINCT)`
			// stuck on a row after the author picked plain `COUNT` — the word changed and the query
			// did not.
			projection.m_expr->m_distinctArg = distinct;
			return true;
		}

		// The ARGUMENT is an expression, so it IS read by the engine — the same parser the runtime
		// uses, and its complaint is what the author is shown.
		try {
			ibQueryParser parser;
			projection.m_expr->m_arg  = parser.ParseExpression(text);
			projection.m_expr->m_star = false;
		}
		catch (const ibBackendException& error) {
			ShowEngineError(error.GetErrorDescription());
			return false;
		}
		return true;
	});
	m_aggregates = MakeGrid(aggPane, m_aggregateModel, [this] { FillAll(); });
	m_aggregateModel->SetIconColumn(kGridCol1, ibValue::GetIconGroup());
	// AN AGGREGATE WEARS ITS ARGUMENT PICTURE: what SUM(x) is about is x.
	m_aggregateModel->SetIconReader([this](unsigned int row) -> wxIcon {
		const ibQuerySelect* select = Current();
		const std::vector<size_t> rows = AggregateRows();
		if (select == nullptr || row >= rows.size()) return wxNullIcon;
		const ibQueryAstExprPtr& expr = select->m_projections[rows[row]].m_expr;
		return expr ? IconOfExpr(expr->m_arg) : wxNullIcon;
	});
	m_aggregates->GetRootColumnGroup()->AppendColumn(IconColumn(_("Summed field"), kGridCol1, FromDIP(320)));
	// ⭐ THE AGGREGATES THAT FIT THIS ROW'S FIELD — asked of the engine, per row. A string field
	// offers Count / Min / Max and no Sum, because there is no sum of strings; a number offers all
	// five. The same list CheckNames refuses by, so the cell cannot offer what the query rejects.
	m_aggregates->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(_("Function"),
		new ibRowChoiceRenderer([this]() -> wxArrayString {
			wxArrayString words;
			const ibQuerySelect* select = Current();
			const long row = SelectedRow(m_aggregates, m_aggregateModel);
			const std::vector<size_t> rows = AggregateRows();

			ibTypeDescription type;   // unknown by default — and unknown offers everything
			if (select != nullptr && row >= 0 && static_cast<size_t>(row) < rows.size()) {
				const ibQueryProjection& projection = select->m_projections[rows[row]];
				if (projection.m_expr && projection.m_expr->m_arg
				    && projection.m_expr->m_arg->m_kind == ibQueryAstExprKind::Column) {
					const ibSourceMetaDataScope resolveAgainst(m_metaData);
					type = m_model.TypeOfPath(*select, projection.m_expr->m_arg->m_path,
					                          m_package, m_statement);
				}
			}
			// …AND ITS DISTINCT FORM where that asks a different question — `COUNT (DISTINCT)` counts
			// how many DIFFERENT values there are, `COUNT` how many rows have one. Which functions
			// get the twin is the keyword table's answer (ibDistinctMattersFor), so MIN and MAX do
			// not: their result is the same value however often it occurs, and a padded list is a
			// list people stop reading.
			for (ibQueryKeyword keyword : ibQueryLowering::AggregatesFor(type)) {
				words.Add(ibQueryKeywordText(keyword));
				if (ibDistinctMattersFor(keyword))
					words.Add(ibQueryKeywordText(keyword) + wxT(" (")
						+ ibQueryKeywordText(ibQueryKeyword::Distinct) + wxT(")"));
			}
			return words;
		}, m_readOnly ? wxDATAVIEW_CELL_INERT : wxDATAVIEW_CELL_EDITABLE),
		kGridCol2, FromDIP(140), wxAlignment::wxALIGN_LEFT));
	m_aggregates->SetDropTarget(new ibCallbackDropTarget([this] { wxCommandEvent e; OnAddAggregate(e); }));
	agg->Add(m_aggregates, 1, wxEXPAND | wxALL, FromDIP(3));
	aggRow->Add(agg, 1, wxEXPAND);
	aggPane->SetSizer(aggRow);

	rightSplit->SplitHorizontally(keysPane, aggPane, FromDIP(230));
	splitter->SplitVertically(leftPane, rightSplit, FromDIP(280));

	wxBoxSizer* pageSizer = new wxBoxSizer(wxVERTICAL);
	pageSizer->Add(splitter, 1, wxEXPAND | wxALL, FromDIP(4));
	page->SetSizer(pageSizer);
	return page;
}

// THE FIELDS ARE ON THE LEFT HERE TOO. A conditions tab that only shows the conditions already
// written sends the author back to another tab to remember what a field was called — so the left
// pane lists them, and double-clicking one starts a condition on it.
wxWindow* ibDialogQueryConstructor::BuildConditionsPage(wxWindow* parent)
{
	wxPanel* page = new wxPanel(parent);

	wxSplitterWindow* splitter = new wxSplitterWindow(page, wxID_ANY,
		wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3DSASH);
	splitter->SetMinimumPaneSize(FromDIP(150));

	wxPanel* leftPane = new wxPanel(splitter);
	wxBoxSizer* left = new wxBoxSizer(wxVERTICAL);
	left->Add(new wxStaticText(leftPane, wxID_ANY, _("Fields")), 0, wxALL, FromDIP(3));
	m_conditionSource = new wxTreeCtrl(leftPane, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTR_HAS_BUTTONS | wxTR_SINGLE | wxTR_HIDE_ROOT | wxTR_LINES_AT_ROOT | wxTR_NO_LINES | wxTR_TWIST_BUTTONS);
	// DOUBLE-CLICK ADDS THE CONDITION, it does not open a window to ask about it. Every other tab's
	// tree adds what you clicked; this one alone raised a modal, which is why the gesture felt
	// broken rather than different.
	m_conditionSource->Bind(wxEVT_TREE_ITEM_ACTIVATED,
		[this](wxTreeEvent&) { AddConditionsForSelectedFields(); });
	// DRAGGABLE, like every other field list — you can drag anything you can see.
	m_conditionSource->Bind(wxEVT_TREE_BEGIN_DRAG, [this](wxTreeEvent& e) { OnFieldTreeBeginDrag(e); });
	m_conditionSource->Bind(wxEVT_TREE_ITEM_EXPANDING, &ibDialogQueryConstructor::OnFieldTreeExpanding, this);
	left->Add(m_conditionSource, 1, wxEXPAND | wxALL, FromDIP(3));
	leftPane->SetSizer(left);

	wxPanel* rightPane = new wxPanel(splitter);
	wxBoxSizer* rightRow = new wxBoxSizer(wxHORIZONTAL);

	wxBoxSizer* move = new wxBoxSizer(wxVERTICAL);
	move->AddStretchSpacer();
	AddMoveButton(rightPane, move, ArrowRight(), _("Add a condition on the selected field"),
		[this](wxCommandEvent&) { AddConditionsForSelectedFields(); });
	AddMoveButton(rightPane, move, ArrowLeft(), _("Delete the condition"),
		[this](wxCommandEvent& e) { OnRemoveCondition(e); });
	move->AddStretchSpacer();
	rightRow->Add(move, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(3));

	wxBoxSizer* right = new wxBoxSizer(wxVERTICAL);
	wxToolBar* bar = MakeToolBar(rightPane);
	AddTool(bar, _("Add"),    wxART_ADD,       [this](wxCommandEvent& e) { OnAddCondition(e); });
	AddTool(bar, _("Edit"),   wxART_EDIT, [this](wxCommandEvent& e) { OnEditCondition(e); });
	AddTool(bar, _("Delete"), wxART_DELETE,    [this](wxCommandEvent& e) { OnRemoveCondition(e); });
	bar->Realize();
	right->Add(bar, 0, wxEXPAND);

	m_conditions = new ibDataViewCtrl(rightPane, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxDV_ROW_LINES | wxDV_SINGLE);
	m_conditionModel = new ibQueryConditionModel();
	m_conditionModel->SetOnChanged([this] { FillAll(); });
	// THE ENGINE'S OWN WORDS, on the verdict line under the text — not a dialog, because a cell
	// being typed into is a place where a half-written condition is normal.
	m_conditionModel->SetOnError([this](const wxString& message) { ShowEngineError(message); });
	m_conditions->AssociateModel(m_conditionModel);

	m_conditions->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(wxT("#"),
		new ibDataViewTextRenderer(ibDataViewTextRenderer::GetDefaultType(), wxDATAVIEW_CELL_INERT),
		kConditionColNumber, FromDIP(40), wxAlignment::wxALIGN_RIGHT));   // the row's position, not a field
	// ⭐ "ARBITRARY" IS A SWITCH NOW, and it has something to switch. It stood here once as an
	// OBSERVATION — a tick-box that could not be ticked, which reads as broken whatever it meant —
	// and was taken out for that reason. What was missing was the other half: a shape to switch TO.
	//
	// Cleared, the condition is CHOSEN out of what the engine can build over this query's fields.
	// Ticked, it is WRITTEN — free text with the "..." into the expression editor. The query text is
	// identical either way, which is what makes this a property of the row's editor and not of the
	// query. A condition that cannot be decomposed keeps the box ticked whoever clears it: that one
	// is still the observation, and the model says so rather than mangling the condition.
	m_conditions->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(_("Arbitrary"),
		new ibDataViewToggleRenderer(ibDataViewToggleRenderer::GetDefaultType(),
			m_readOnly ? wxDATAVIEW_CELL_INERT : wxDATAVIEW_CELL_ACTIVATABLE),
		kConditionColArbitrary, FromDIP(80), wxAlignment::wxALIGN_CENTER));
	// TYPED INTO WHERE IT STANDS. The full editor is still behind the "..." for anything that does
	// not fit a line, but changing `Price > 100` to `Price > 200` should not need a window.
	// THE SAME CELL AS THE LINKS' CONDITION — a list of the ready shapes, editable text, and the
	// "..." into the expression editor. A plain text column meant an arbitrary condition had to be
	// typed blind with the fields on the other side of the window, which is the state this tab was
	// in: the row said `Catalog1.DataVersion = &DataVersion` and there was no way to make it say
	// anything else without retyping it by hand.
	m_conditions->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(_("Condition"),
		new ibExpressionCellRenderer(
			[this]() -> wxArrayString { return ConditionChoices(); },
			[this](wxString& text) -> bool { return EditConditionText(text); },
			wxDATAVIEW_CELL_EDITABLE,
			// The row is asked the way every other verb over this grid asks it — through the model,
			// which is what turns a selected ITEM into an index.
			[this]() -> bool {
				if (m_conditionModel == nullptr || m_conditions == nullptr)
					return true;
				return m_conditionModel->IsArbitrary(
					m_conditionModel->GetRow(m_conditions->GetSelection()));
			}),
		kConditionColText, FromDIP(520), wxAlignment::wxALIGN_LEFT));

	// AND THE CELL HAS TO OPEN. This grid is built by hand rather than through MakeGrid, so it never
	// got the one line that answers a double-click by editing the cell — the expression cell was there
	// with its "..." inside and nothing could reach it. A cell you cannot open is a cell that is not
	// there; the tab read as "the condition cannot be changed".
	EditOnActivate(m_conditions);

	// ⚠ THE ONE PANE THAT HAD NO DROP TARGET. Every other list in the window accepted a dragged
	// field; this one silently did not, so the gesture that works everywhere else did nothing here.
	m_conditions->SetDropTarget(new ibCallbackDropTarget([this] { AddConditionsForSelectedFields(); }));
	AttachContextMenu(m_conditions, bar);
	right->Add(m_conditions, 1, wxEXPAND | wxALL, FromDIP(3));
	rightRow->Add(right, 1, wxEXPAND);
	rightPane->SetSizer(rightRow);

	splitter->SplitVertically(leftPane, rightPane, FromDIP(280));

	wxBoxSizer* pageSizer = new wxBoxSizer(wxVERTICAL);
	pageSizer->Add(splitter, 1, wxEXPAND | wxALL, FromDIP(4));
	page->SetSizer(pageSizer);
	return page;
}

// INDEX — only ever shown for a statement that MAKES a temp table, because only such a table can
// be indexed. The columns here are what the store builds its lookup over, so a later statement
// filtering one of them finds its rows instead of walking the table (queryTempStore.cpp).
wxWindow* ibDialogQueryConstructor::BuildIndexPage(wxWindow* parent)
{
	wxPanel* page = new wxPanel(parent);

	wxSplitterWindow* splitter = new wxSplitterWindow(page, wxID_ANY,
		wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3DSASH);
	splitter->SetMinimumPaneSize(FromDIP(150));

	wxPanel* leftPane = new wxPanel(splitter);
	wxBoxSizer* left = new wxBoxSizer(wxVERTICAL);
	left->Add(new wxStaticText(leftPane, wxID_ANY, _("Fields")), 0, wxALL, FromDIP(3));
	m_indexSource = new wxTreeCtrl(leftPane, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTR_HAS_BUTTONS | wxTR_SINGLE | wxTR_HIDE_ROOT | wxTR_LINES_AT_ROOT | wxTR_NO_LINES | wxTR_TWIST_BUTTONS);
	m_indexSource->Bind(wxEVT_TREE_ITEM_ACTIVATED, [this](wxTreeEvent&) { wxCommandEvent e; OnAddIndexField(e); });
	// DRAGGABLE, like every other field list — you can drag anything you can see.
	m_indexSource->Bind(wxEVT_TREE_BEGIN_DRAG, [this](wxTreeEvent& e) { OnFieldTreeBeginDrag(e); });
	m_indexSource->Bind(wxEVT_TREE_ITEM_EXPANDING, &ibDialogQueryConstructor::OnFieldTreeExpanding, this);
	left->Add(m_indexSource, 1, wxEXPAND | wxALL, FromDIP(3));
	leftPane->SetSizer(left);

	wxPanel* rightPane = new wxPanel(splitter);
	wxBoxSizer* rightRow = new wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer* move = new wxBoxSizer(wxVERTICAL);
	move->AddStretchSpacer();
	AddMoveButton(rightPane, move, ArrowRight(), _("Index by the selected field"),
		[this](wxCommandEvent& e) { OnAddIndexField(e); });
	AddMoveButton(rightPane, move, ArrowLeft(), _("Remove the indexed field"),
		[this](wxCommandEvent& e) { OnRemoveIndexField(e); });
	move->AddStretchSpacer();
	rightRow->Add(move, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(3));

	wxBoxSizer* right = new wxBoxSizer(wxVERTICAL);
	right->Add(new wxStaticText(rightPane, wxID_ANY, _("Index by")), 0, wxALL, FromDIP(3));
	m_indexModel = new ibQueryGridModel();
	m_indexModel->SetReader([this](unsigned int row, unsigned int) -> wxString {
		const ibQuerySelect* select = Current();
		if (select == nullptr || row >= select->m_indexBy.size() || !select->m_indexBy[row])
			return wxEmptyString;
		return ibRenderQueryExpr(*select->m_indexBy[row]);
	});
	m_indexModel->SetWriter([this](unsigned int row, unsigned int, const wxString& text) -> bool {
		ibQuerySelect* select = Current();
		if (!CanEdit() || select == nullptr || row >= select->m_indexBy.size())
			return false;
		try {
			ibQueryParser parser;
			select->m_indexBy[row] = parser.ParseExpression(text);
		}
		catch (const ibBackendException& error) {
			ShowEngineError(error.GetErrorDescription());
			return false;
		}
		return true;
	});
	m_indexFields = MakeGrid(rightPane, m_indexModel, [this] { FillAll(); });
	m_indexModel->SetIconColumn(kGridCol1, ibValue::GetIconGroup());
	m_indexModel->SetIconReader([this](unsigned int row) -> wxIcon {
		const ibQuerySelect* select = Current();
		return select != nullptr && row < select->m_indexBy.size()
			? IconOfExpr(select->m_indexBy[row]) : wxNullIcon;
	});
	m_indexFields->GetRootColumnGroup()->AppendColumn(IconColumn(_("Indexed field"), kGridCol1, FromDIP(420)));
	m_indexFields->SetDropTarget(new ibCallbackDropTarget([this] { wxCommandEvent e; OnAddIndexField(e); }));
	right->Add(m_indexFields, 1, wxEXPAND | wxALL, FromDIP(3));
	rightRow->Add(right, 1, wxEXPAND);
	rightPane->SetSizer(rightRow);

	splitter->SplitVertically(leftPane, rightPane, FromDIP(280));

	wxBoxSizer* pageSizer = new wxBoxSizer(wxVERTICAL);
	pageSizer->Add(splitter, 1, wxEXPAND | wxALL, FromDIP(4));
	page->SetSizer(pageSizer);
	return page;
}

wxWindow* ibDialogQueryConstructor::BuildOrderPage(wxWindow* parent)
{
	wxPanel* page = new wxPanel(parent);

	wxSplitterWindow* splitter = new wxSplitterWindow(page, wxID_ANY,
		wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3DSASH);
	splitter->SetMinimumPaneSize(FromDIP(150));

	wxPanel* leftPane = new wxPanel(splitter);
	wxBoxSizer* left = new wxBoxSizer(wxVERTICAL);
	left->Add(new wxStaticText(leftPane, wxID_ANY, _("Fields")), 0, wxALL, FromDIP(3));
	m_orderSource = new wxTreeCtrl(leftPane, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTR_HAS_BUTTONS | wxTR_SINGLE | wxTR_HIDE_ROOT | wxTR_LINES_AT_ROOT | wxTR_NO_LINES | wxTR_TWIST_BUTTONS);
	m_orderSource->Bind(wxEVT_TREE_ITEM_ACTIVATED, [this](wxTreeEvent&) { wxCommandEvent e; OnAddOrder(e); });
	// DRAGGABLE, like every other field list — you can drag anything you can see.
	m_orderSource->Bind(wxEVT_TREE_BEGIN_DRAG, [this](wxTreeEvent& e) { OnFieldTreeBeginDrag(e); });
	m_orderSource->Bind(wxEVT_TREE_ITEM_EXPANDING, &ibDialogQueryConstructor::OnFieldTreeExpanding, this);
	left->Add(m_orderSource, 1, wxEXPAND | wxALL, FromDIP(3));
	leftPane->SetSizer(left);

	wxPanel* rightPane = new wxPanel(splitter);
	wxBoxSizer* rightRow = new wxBoxSizer(wxHORIZONTAL);

	wxBoxSizer* move = new wxBoxSizer(wxVERTICAL);
	move->AddStretchSpacer();
	AddMoveButton(rightPane, move, ArrowRight(), _("Order by the selected field"),
		[this](wxCommandEvent& e) { OnAddOrder(e); });
	AddMoveButton(rightPane, move, ArrowLeft(), _("Remove the ordering field"),
		[this](wxCommandEvent& e) { OnRemoveOrder(e); });
	move->AddStretchSpacer();
	rightRow->Add(move, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(3));

	wxBoxSizer* right = new wxBoxSizer(wxVERTICAL);
	wxToolBar* bar = MakeToolBar(rightPane);
	AddTool(bar, _("Direction"), wxART_EDIT, [this](wxCommandEvent& e) { OnToggleOrderDirection(e); });
	bar->AddSeparator();
	AddTool(bar, _("Move up"),   wxART_UP,   [this](wxCommandEvent&) { OnMoveOrder(-1); });
	AddTool(bar, _("Move down"), wxART_DOWN, [this](wxCommandEvent&) { OnMoveOrder(+1); });
	bar->Realize();
	right->Add(bar, 0, wxEXPAND);

	// ORDER IS THE SETTING HERE, so the rows carry their position and the direction is a column of
	// its own rather than a suffix on the field's name — and both are typed into where they stand.
	m_orderModel = new ibQueryGridModel();
	m_orderModel->SetReader([this](unsigned int row, unsigned int col) -> wxString {
		const ibQuerySelect* select = Current();
		if (select == nullptr || row >= select->m_orderBy.size())
			return wxEmptyString;
		const ibQueryOrderItem& item = select->m_orderBy[row];
		if (col == kGridCol1)
			return item.m_expr ? ibRenderQueryExpr(*item.m_expr) : wxString();
		return ibQueryKeywordText(item.m_ascending ? ibQueryKeyword::Asc : ibQueryKeyword::Desc);
	});
	m_orderModel->SetWriter([this](unsigned int row, unsigned int col, const wxString& text) -> bool {
		ibQuerySelect* select = Current();
		if (!CanEdit() || select == nullptr || row >= select->m_orderBy.size())
			return false;
		ibQueryOrderItem& item = select->m_orderBy[row];
		if (col == kGridCol2) {
			// THE TWO WORDS THE LANGUAGE HAS, matched against the language's own spelling — not a
			// second list of "ASC"/"DESC" kept here to drift out of step with the keyword table.
			item.m_ascending = !text.IsSameAs(ibQueryKeywordText(ibQueryKeyword::Desc), false);
			return true;
		}
		try {
			ibQueryParser parser;
			item.m_expr = parser.ParseExpression(text);
		}
		catch (const ibBackendException& error) {
			ShowEngineError(error.GetErrorDescription());
			return false;
		}
		return true;
	});
	m_order = MakeGrid(rightPane, m_orderModel, [this] { FillAll(); });
	AttachContextMenu(m_order, bar);
	m_orderModel->SetIconColumn(kGridCol1, ibValue::GetIconGroup());
	m_orderModel->SetIconReader([this](unsigned int row) -> wxIcon {
		const ibQuerySelect* select = Current();
		return select != nullptr && row < select->m_orderBy.size()
			? IconOfExpr(select->m_orderBy[row].m_expr) : wxNullIcon;
	});
	m_order->GetRootColumnGroup()->AppendColumn(IconColumn(_("Field"), kGridCol1, FromDIP(380)));
	// TWO WORDS, and the language owns both — a choice, not a typed word.
	m_order->GetRootColumnGroup()->AppendColumn(ChoiceColumn(_("Direction"), kGridCol2, FromDIP(140),
		{ ibQueryKeyword::Asc, ibQueryKeyword::Desc }));
	m_order->SetDropTarget(new ibCallbackDropTarget([this] { wxCommandEvent e; OnAddOrder(e); }));
	right->Add(m_order, 1, wxEXPAND | wxALL, FromDIP(3));
	rightRow->Add(right, 1, wxEXPAND);
	rightPane->SetSizer(rightRow);

	splitter->SplitVertically(leftPane, rightPane, FromDIP(280));

	wxBoxSizer* pageSizer = new wxBoxSizer(wxVERTICAL);
	pageSizer->Add(splitter, 1, wxEXPAND | wxALL, FromDIP(4));
	page->SetSizer(pageSizer);
	return page;
}

// TOTALS — the fields on the LEFT, the two halves of the clause on the RIGHT, one above the other.
// The clause has exactly two halves and they are different questions: WHAT is totalled (the
// aggregates, which roll in place at every level) and AT WHICH LEVELS (the dimensions, in order,
// each one a subtotal node). Putting them side by side made them look like two lists of the same
// kind of thing, which they are not.
//
// The dimension carries a NAME of its own — ibQueryTotalDim::m_alias, added to the language for
// this: two levels over the same column (Date by month, Date by day) are two output columns, and
// without a name of its own the second answers to the first's.
wxWindow* ibDialogQueryConstructor::BuildTotalsPage(wxWindow* parent)
{
	wxPanel* page = new wxPanel(parent);

	wxSplitterWindow* splitter = new wxSplitterWindow(page, wxID_ANY,
		wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3DSASH);
	splitter->SetMinimumPaneSize(FromDIP(150));

	// ---- LEFT: what there is to total, and to total BY.
	wxPanel* leftPane = new wxPanel(splitter);
	wxBoxSizer* left = new wxBoxSizer(wxVERTICAL);
	left->Add(new wxStaticText(leftPane, wxID_ANY, _("Fields")), 0, wxALL, FromDIP(3));
	m_totalsSource = new wxTreeCtrl(leftPane, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTR_HAS_BUTTONS | wxTR_SINGLE | wxTR_HIDE_ROOT | wxTR_LINES_AT_ROOT | wxTR_NO_LINES | wxTR_TWIST_BUTTONS);
	m_totalsSource->Bind(wxEVT_TREE_ITEM_ACTIVATED, [this](wxTreeEvent&) { wxCommandEvent e; OnAddTotalsDimension(e); });
	m_totalsSource->Bind(wxEVT_TREE_BEGIN_DRAG, [this](wxTreeEvent& e) { OnFieldTreeBeginDrag(e); });
	m_totalsSource->Bind(wxEVT_TREE_ITEM_EXPANDING, &ibDialogQueryConstructor::OnFieldTreeExpanding, this);
	left->Add(m_totalsSource, 1, wxEXPAND | wxALL, FromDIP(3));
	leftPane->SetSizer(left);

	// ---- RIGHT: the levels above, the grand-totals line, the aggregates below.
	wxSplitterWindow* rightSplit = new wxSplitterWindow(splitter, wxID_ANY,
		wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3DSASH);
	rightSplit->SetMinimumPaneSize(FromDIP(80));

	wxPanel* dimPane = new wxPanel(rightSplit);
	wxBoxSizer* dimRow = new wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer* dimMove = new wxBoxSizer(wxVERTICAL);
	dimMove->AddStretchSpacer();
	AddMoveButton(dimPane, dimMove, ArrowRight(), _("Total by the selected field"),
		[this](wxCommandEvent& e) { OnAddTotalsDimension(e); });
	AddMoveButton(dimPane, dimMove, ArrowLeft(), _("Remove the grouping level"),
		[this](wxCommandEvent& e) { OnRemoveTotalsLine(e); });
	dimMove->AddStretchSpacer();
	dimRow->Add(dimMove, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(3));

	wxBoxSizer* dims = new wxBoxSizer(wxVERTICAL);
	wxToolBar* dimBar = MakeToolBar(dimPane);
	// ⭐⭐ ADD A SEPARATOR — a NODE the groupings are hung on. Without one the tab behaves exactly as
	// it always has (one hidden node, one ladder); with one, the levels added after it land on it,
	// and a report reads each node as a selection of its own.
	AddTool(dimBar, _("Add separator"), wxART_NEW, [this](wxCommandEvent& e) { OnAddTotalsSplit(e); });
	dimBar->AddSeparator();
	AddTool(dimBar, _("Edit"),   wxART_EDIT, [this](wxCommandEvent& e) { OnEditTotalsDimension(e); });
	AddTool(dimBar, _("Delete"), wxART_DELETE,    [this](wxCommandEvent& e) { OnRemoveTotalsLine(e); });
	dimBar->AddSeparator();
	AddTool(dimBar, _("Move up"),   wxART_UP,   [this](wxCommandEvent&) { OnMoveTotalsDimension(-1); });
	AddTool(dimBar, _("Move down"), wxART_DOWN, [this](wxCommandEvent&) { OnMoveTotalsDimension(+1); });
	dimBar->Realize();
	dims->Add(dimBar, 0, wxEXPAND);

	// THREE COLUMNS, all typed into: the field, how it unfolds, and the name the level answers to.
	m_totalsDimensionModel = new ibQueryTotalsTreeModel();
	// HOW MANY ROWS HANG WHERE — the tree asks these two and builds itself; the dialog answers out of
	// the AST, so there is no second copy of the ladder anywhere.
	m_totalsDimensionModel->SetNodeCount([this]() -> int {
		const ibQuerySelect* select = Current();
		return select != nullptr ? static_cast<int>(select->m_totalsSplits.size()) : 0;
	});
	m_totalsDimensionModel->SetLevelsOf([this](int node) -> int {
		const std::vector<ibQueryTotalDim>* levels =
			LevelsOfNode(const_cast<ibQuerySelect*>(Current()), node);
		return levels != nullptr ? static_cast<int>(levels->size()) : 0;
	});
	m_totalsDimensionModel->SetReader([this](const ibTotalsRow& at, unsigned int col) -> wxString {
		const ibQuerySelect* select = Current();
		if (select == nullptr)
			return wxEmptyString;

		// ⭐ A SEPARATOR'S OWN LINE. It says what it IS in the field cell, unfolds nothing (a node is
		// not keyed by a value, so the totals-kind cell has nothing to show), and carries its NAME in
		// the alias cell — which is the cell a person renames it in.
		if (at.IsNodeHeader()) {
			if (col == kGridCol1)
				return ibQueryKeywordText(ibQueryKeyword::Split);
			if (col == kGridCol2)
				return wxEmptyString;
			return static_cast<size_t>(at.m_node) < select->m_totalsSplits.size()
				? select->m_totalsSplits[at.m_node].m_name : wxString();
		}

		const std::vector<ibQueryTotalDim>* levels =
			const_cast<ibDialogQueryConstructor*>(this)->LevelsOfNode(const_cast<ibQuerySelect*>(select), at.m_node);
		if (levels == nullptr || static_cast<size_t>(at.m_level) >= levels->size())
			return wxEmptyString;
		const ibQueryTotalDim& dim = (*levels)[at.m_level];

		if (col == kGridCol1) {
			// THE LEVEL'S FIELDS, AS A LIST. A level groups by all of them together, so the cell shows
			// them the way the query text spells them inside its brackets — one field is the same
			// reading with a list of one.
			//
			// ⭐ AND IT SHOWS THE WHOLE FIELD, not the column part of it. `Period PERIODS(Month, &A,
			// &B)` used to appear here as `Period`: the cell rendered the expression alone, so what
			// the query said was invisible in the form — and the writer below, rebuilding the level
			// from what it saw, dropped it. A cell that shows less than it edits loses the rest.
			wxString fields;
			for (const ibQueryTotalField& field : dim.m_fields) {
				if (!field.m_expr) continue;
				if (!fields.IsEmpty()) fields += wxT(", ");
				fields += ibRenderTotalField(field);
			}
			return fields;
		}
		if (col == kGridCol2)
			return ibQueryKeywordText(dim.HeadUnfold() == ibQueryDimUnfold::Hierarchy ? ibQueryKeyword::Hierarchy
				: dim.HeadUnfold() == ibQueryDimUnfold::HierarchyOnly ? ibQueryKeyword::HierarchyOnly
				: ibQueryKeyword::Elements);
		// EMPTY MEANS "the column's own name" — shown as what it WILL be, so the cell is not blank
		// where the result will in fact have a name.
		return ibQueryDimensionName(dim);
	});
	m_totalsDimensionModel->SetWriter([this](const ibTotalsRow& at, unsigned int col, const wxString& text) -> bool {
		ibQuerySelect* select = Current();
		if (!CanEdit() || select == nullptr)
			return false;

		// RENAMING A SEPARATOR — the alias cell is its name, and the other two are not its to write:
		// what a node IS does not change, and it unfolds nothing.
		if (at.IsNodeHeader()) {
			if (col != kGridCol3 || static_cast<size_t>(at.m_node) >= select->m_totalsSplits.size())
				return false;
			wxString name = text;
			name.Trim(true).Trim(false);
			if (!name.IsEmpty() && !AcceptName(name, _("separator name")))
				return false;
			// UNIQUE AMONG THE SEPARATORS, because that is what a reader addresses them by: two nodes
			// with one name leaves one of them unreachable and the query says nothing about it.
			for (size_t other = 0; other < select->m_totalsSplits.size(); ++other) {
				if (other == static_cast<size_t>(at.m_node) || name.IsEmpty())
					continue;
				if (select->m_totalsSplits[other].m_name.IsSameAs(name, false)) {
					wxMessageBox(wxString::Format(_("A separator called '%s' is already there."), name),
						_("Query constructor"), wxOK | wxICON_WARNING, this);
					return false;
				}
			}
			select->m_totalsSplits[at.m_node].m_name = name;
			return true;
		}

		std::vector<ibQueryTotalDim>* levels = LevelsOfNode(select, at.m_node);
		if (levels == nullptr || static_cast<size_t>(at.m_level) >= levels->size())
			return false;
		ibQueryTotalDim& dim = (*levels)[at.m_level];
		if (col == kGridCol2) {
			// THE LANGUAGE'S OWN THREE WORDS, matched against the keyword table rather than against a
			// second list of them kept here.
			const ibQueryDimUnfold unfold =
				text.IsSameAs(ibQueryKeywordText(ibQueryKeyword::Hierarchy), false) ? ibQueryDimUnfold::Hierarchy
				: text.IsSameAs(ibQueryKeywordText(ibQueryKeyword::HierarchyOnly), false) ? ibQueryDimUnfold::HierarchyOnly
				: ibQueryDimUnfold::Elements;
			// A HIERARCHY UNFOLDS ONE PARENT CHAIN, so a level grouped by several fields has none to
			// walk. Said here, at the cell, rather than left for the engine to refuse after the whole
			// query is written.
			if (unfold != ibQueryDimUnfold::Elements && !dim.IsSingleField()) {
				wxMessageBox(_("This level groups by several fields, and a hierarchy unfolds one field's "
				               "parent chain.\n\nGive the hierarchy field a level of its own."),
					_("Query constructor"), wxOK | wxICON_WARNING, this);
				return false;
			}
			if (ibQueryTotalField* head = dim.Head())
				head->m_unfold = unfold;
			return true;
		}
		if (col == kGridCol3) {
			wxString alias = text;
			alias.Trim(true).Trim(false);
			// ⚠ THE CELL SHOWS THE NATURAL NAME WHEN THERE IS NO ALIAS — so committing it unchanged
			// must NOT store it. Otherwise the first click into the cell turned a hint into a real
			// alias and the text grew `AS DataVersion` over a column already called DataVersion:
			// redundancy the author never wrote, on a line they only looked at.
			//
			// The name matters where it is NOT the column's own — two levels over the same column
			// (Date by month, Date by day) are two output columns, and that is what this is for.
			const ibQueryTotalField* head = dim.Head();
			const wxString natural = head != nullptr && head->m_expr
			                         && head->m_expr->m_kind == ibQueryAstExprKind::Column
			                         && !head->m_expr->m_path.empty() ? head->m_expr->m_path.back() : wxString();
			if (alias.IsSameAs(natural, false)) { dim.m_alias.clear(); return true; }
			if (!alias.IsEmpty() && !AcceptName(alias, _("totals level name")))
				return false;

			// ⭐⭐ UNIQUE WHERE IT IS READ FROM — which is the hidden node's levels PLUS this node's,
			// and never a neighbouring node's.
			//
			// A walk reaches a level by descending: the levels above the node, then the node's own.
			// Two separators may therefore both group by Item and each roll its own totals — that is
			// the whole reason a person adds the second one (Max, 2026-08-27). Checked across ALL
			// levels, as it used to be, that legitimate query was refused by the window while the
			// engine ran it perfectly well.
			std::vector<const ibQueryTotalDim*> reachable;
			for (const ibQueryTotalDim& above : select->m_totalsBy)
				reachable.push_back(&above);
			if (at.m_node != wxNOT_FOUND && static_cast<size_t>(at.m_node) < select->m_totalsSplits.size())
				for (const ibQueryTotalDim& mine : select->m_totalsSplits[at.m_node].m_levels)
					reachable.push_back(&mine);

			for (const ibQueryTotalDim* otherPtr : reachable) {
				if (otherPtr == &dim || alias.IsEmpty())
					continue;
				const ibQueryTotalDim& other = *otherPtr;
				const wxString otherName = ibQueryDimensionName(other);
				if (otherName.IsSameAs(alias, false)) {
					wxMessageBox(wxString::Format(
							_("Another totals level is already called '%s'.\n\nLevels are read back "
							  "by name, so two the same would make one of them unreachable."), alias),
						_("Query constructor"), wxOK | wxICON_WARNING, this);
					return false;
				}
			}

			dim.m_alias = alias;
			return true;
		}
		try {
			// THE WHOLE LEVEL IS REWRITTEN FROM THE CELL — one field or several. Read through the
			// LANGUAGE (ParseTotalsField), so a field says here everything it may say in a query:
			// the column, the unfold, and PERIODS with its unit and bounds. Picking out only the
			// expression — which is what this did — meant every edit of this cell quietly threw away
			// whatever else the author had written.
			const std::vector<wxString> pieces = ibSplitLevelFields(text);
			std::vector<ibQueryTotalField> parsed;
			ibQueryParser parser;
			for (const wxString& piece : pieces) {
				if (piece.IsEmpty()) continue;
				parsed.push_back(parser.ParseTotalsField(piece));
			}
			if (parsed.empty())
				return false;                       // an empty cell removes nothing — the toolbar does that
			if (parsed.size() > 1) {
				// Refused where it is written, for the same reason the engine refuses it: a hierarchy
				// unfolds one parent chain and a key of several fields has none.
				if (dim.HeadUnfold() != ibQueryDimUnfold::Elements) {
					wxMessageBox(_("This level unfolds through a hierarchy, which follows one field's "
					               "parent chain.\n\nRemove the hierarchy first, or give the extra "
					               "fields a level of their own."),
						_("Query constructor"), wxOK | wxICON_WARNING, this);
					return false;
				}
				// …and the same for PERIODS, which the engine refuses for a reason of the same shape:
				// a period is a SCALE, and a key made of several fields has none to pad along.
				for (const ibQueryTotalField& field : parsed)
					if (field.m_periods) {
						wxMessageBox(_("A level of several fields cannot be read by periods: a period "
						               "is a scale, and a key made of several fields has none.\n\n"
						               "Give the period a level of its own."),
							_("Query constructor"), wxOK | wxICON_WARNING, this);
						return false;
					}
			}
			// THE UNFOLD IS INHERITED ONLY WHERE THE TEXT DID NOT STATE ONE. The cell beside this one
			// edits it, so a field typed plainly keeps what that cell says; a field typed WITH a word
			// (`Store HIERARCHY`) says it here, and what the author wrote wins over what was there.
			else if (const ibQueryTotalField* head = dim.Head()) {
				if (parsed.front().m_unfold == ibQueryDimUnfold::Elements && !parsed.front().m_periods)
					parsed.front().m_unfold = head->m_unfold;
			}
			dim.m_fields = std::move(parsed);
		}
		catch (const ibBackendException& error) {
			ShowEngineError(error.GetErrorDescription());
			return false;
		}
		return true;
	});
	m_totalsDimensions = MakeTreeGrid(dimPane, m_totalsDimensionModel, [this] { FillAll(); });
	AttachContextMenu(m_totalsDimensions, dimBar);
	// ⚠⚠ THE WIDTHS ADD UP TO LESS THAN THE PANE, and that is the whole bug behind "the ALIAS cell
	// will not open". The three columns came to more than the grid is wide, so the LAST one hung off
	// the right edge behind a horizontal scrollbar — and the editor for a cell is created at the
	// cell's RECTANGLE, which for a column outside the view is not a place a control can appear.
	// The cell was never refusing to open; it was opening where nobody could see it.
	//
	// So the sizes are chosen to FIT (and it is the last column that must not overflow — the one a
	// person reaches for last is the one that breaks first).
	//
	// The picture and the choice renderer are back: neither was ever the cause, and taking them out
	// cost a working cell for a guess. (Sorry, Max — that one was mine.)
	m_totalsDimensionModel->SetIconColumn(kGridCol1, ibValue::GetIconGroup());
	m_totalsDimensionModel->SetIconReader([this](const ibTotalsRow& at) -> wxIcon {
		// A SEPARATOR IS NOT A FIELD, so it carries no field picture — what it is is said in the cell
		// itself (`SPLIT`), and borrowing a field's icon would draw it as one more grouping.
		if (!at.IsLevel())
			return wxNullIcon;
		const ibQuerySelect* select = Current();
		const std::vector<ibQueryTotalDim>* levels =
			const_cast<ibDialogQueryConstructor*>(this)->LevelsOfNode(const_cast<ibQuerySelect*>(select), at.m_node);
		// The icon is the HEAD field's — a level of several fields shows what it leads with.
		const ibQueryTotalField* head = levels != nullptr && static_cast<size_t>(at.m_level) < levels->size()
			? (*levels)[at.m_level].Head() : nullptr;
		return head != nullptr ? IconOfExpr(head->m_expr) : wxNullIcon;
	});
	m_totalsDimensions->GetRootColumnGroup()->AppendColumn(IconColumn(_("Grouping field"), kGridCol1, FromDIP(250)));
	// THE UNFOLD IS A TYPE (a registered runtime enumeration — see ibQueryDimUnfold), so the cell
	// offers its three words and nothing else.
	m_totalsDimensions->GetRootColumnGroup()->AppendColumn(ChoiceColumn(_("Totals kind"), kGridCol2, FromDIP(130),
		{ ibQueryKeyword::Elements, ibQueryKeyword::Hierarchy, ibQueryKeyword::HierarchyOnly }));
	m_totalsDimensions->GetRootColumnGroup()->AppendColumn(TextColumn(_("Alias"), kGridCol3, FromDIP(160), true));

	// ⭐⭐ AND IT IS DRAWN AS A TREE — a separator is a node and its groupings are its children.
	//
	// 🛑 THE MODEL IS NOT ENOUGH. The view draws a LIST until it is told otherwise, whatever the
	// model answers about parents and containers: a separator came out as one more flat row with no
	// expander and nothing under it, which reads as "the tree does not work" and is really "nobody
	// asked for a tree" (Max, 2026-08-27). Two calls say it: the view MODE, and which column carries
	// the expander.
	m_totalsDimensions->SetViewMode(ibDataViewTree);
	if (m_totalsDimensions->GetColumnCount() > 0)
		m_totalsDimensions->SetExpanderColumn(m_totalsDimensions->GetColumn(0));

	// ⚠⚠ AND A DOOR THAT DOES NOT DEPEND ON THE GRID'S EDITING MACHINERY AT ALL.
	//
	// Five attempts went into making this one cell open in place (the column from the event, the
	// column from the mouse, owning the activation, deferring to the next event loop turn) and it
	// still did not. The name is what the author needs to change; WHICH widget takes the keystrokes
	// is my problem, not theirs, and five rounds of it is four too many.
	//
	// So the alias opens in a prompt — small, certain, and validated by the same rules as the cell
	// (the lexer decides what a name is; a name another level already answers to is refused). If the
	// in-place editor starts working later, this stays the fallback rather than a second mechanism:
	// it is the SAME write, through the same model.
	// (No handler on the activation here. The control opens an editable cell BY ITSELF — that is
	// what it was doing before, and what a handler that consumed the event took away. See MakeGrid.)
	// ⭐⭐ DROPPED ON A SEPARATOR, IT LANDS ON THAT SEPARATOR. The drop carries WHERE it happened, so
	// the row under the cursor decides which node the field is hung on — pick the separator with the
	// mouse and the field goes into it, exactly as dragging onto a folder puts a file in the folder
	// (Max, 2026-08-27).
	//
	// Done by moving the CARET to the row that was dropped on, and then adding as the toolbar does:
	// "add to the node the caret is on" is one rule, and the drop is one more way of saying where the
	// caret should be. A second road into "which node" would be a second thing to keep in step.
	m_totalsDimensions->SetDropTarget(new ibCallbackDropTarget(
		[this](wxCoord x, wxCoord y, const wxString& text) -> bool {
			ibDataViewItem      target;
			ibDataViewColumn*   column = nullptr;
			m_totalsDimensions->HitTest(wxPoint(x, y), target, column);

			// ⭐⭐ A ROW OF THIS GRID, DRAGGED WITHIN IT — the payload says so and carries WHERE IT
			// CAME FROM. Everything else dropped here is a FIELD from the tree on the left, which is
			// the case this target has always served; the two are told apart by the mark, not by
			// guessing at the text.
			if (text.StartsWith(kTotalsDragMark)) {
				const ibTotalsRow from = ParseTotalsDrag(text);
				const ibTotalsRow onto = target.IsOk() ? ibQueryTotalsTreeModel::AtOf(target) : ibTotalsRow();
				MoveTotalsLevelTo(from, onto);
				return true;
			}

			// A FIELD FROM THE TREE — into the node the pointer was over: a separator's own row means
			// that separator, an ordinary row means the node THAT row hangs on, and empty space means
			// the common ladder. Named outright rather than through the caret, which does not follow
			// a drop in time to be asked.
			const ibTotalsRow onto = target.IsOk() ? ibQueryTotalsTreeModel::AtOf(target) : ibTotalsRow();
			AddTotalsFieldsTo(onto.m_node);
			return true;
		}));

	// ⭐ AND THE GRID IS A DRAG SOURCE, so a grouping can be carried onto a separator with the mouse
	// — the same two moves the arrows make, aimed with the pointer instead. The payload is the row's
	// COORDINATE (which node, which level), because that is what the drop needs and nothing else.
	//
	// ⚠ A SEPARATOR IS NOT DRAGGED THIS WAY. Its place among the nodes is what the arrows set; a
	// node dropped INTO another node would have to mean something (nesting) that this shape does not
	// have.
#if wxUSE_DRAG_AND_DROP && wxUSE_UNICODE
	// ⚠ THE GRID'S OWN DRAG, NOT A TEXT DROP TARGET. Once a dataview is a drag SOURCE it handles the
	// drop itself and reports it through its own events — a `wxDropTarget` set on the window is not
	// what gets asked, so a drag begun here simply went nowhere (seen live, 2026-08-27: the row
	// followed the pointer and nothing happened when it was let go). The text target stays for
	// fields dragged in from the tree, which is a drop from OUTSIDE this control.
	m_totalsDimensions->EnableDragSource(wxDF_UNICODETEXT);
	m_totalsDimensions->EnableDropTarget(wxDF_UNICODETEXT);

	m_totalsDimensions->Bind(wxEVT_DATAVIEW_ITEM_BEGIN_DRAG, [this](ibDataViewEvent& event) {
		const ibTotalsRow at = ibQueryTotalsTreeModel::AtOf(event.GetItem());
		if (!CanEdit() || !at.IsLevel()) {
			event.Veto();   // a separator keeps its place — the arrows set that
			return;
		}
		event.SetDataObject(new wxTextDataObject(
			wxString::Format(wxT("%s%d:%d"), kTotalsDragMark, at.m_node, at.m_level)));
		event.SetDragFlags(wxDrag_AllowMove);
	});

	m_totalsDimensions->Bind(wxEVT_DATAVIEW_ITEM_DROP_POSSIBLE, [](ibDataViewEvent& event) {
		if (event.GetDataFormat() != wxDF_UNICODETEXT) {
			event.Veto();
			return;
		}
		// ⚠ COPY, NOT MOVE — and this is what the crossed-out cursor was about. The field tree starts
		// its drag as `wxDrag_CopyOnly` (OnFieldTreeBeginDrag), so asking for a MOVE effect is asking
		// the source for something it refused to allow, and the system forbids the drop outright: the
		// pointer says "not here" over a grid that was perfectly willing to take it (Max, 2026-08-27).
		//
		// The effect changes nothing about what happens on our side: a level dragged within the grid
		// is removed from its old node by the handler itself, whatever the drag was labelled.
		event.SetDropEffect(wxDragCopy);
	});

	m_totalsDimensions->Bind(wxEVT_DATAVIEW_ITEM_DROP, [this](ibDataViewEvent& event) {
		if (event.GetDataFormat() != wxDF_UNICODETEXT) {
			event.Veto();
			return;
		}
		wxTextDataObject carried;
		carried.SetData(wxDF_UNICODETEXT, event.GetDataSize(), event.GetDataBuffer());
		const wxString text = carried.GetText();

		// WHERE IT LANDED — the row under the pointer. Dropped on nothing at all, it is the common
		// ladder, which is what dragging a grouping out of a separator means.
		const ibDataViewItem target = event.GetItem();
		const ibTotalsRow    onto   = target.IsOk() ? ibQueryTotalsTreeModel::AtOf(target) : ibTotalsRow();

		// ⭐⭐ BOTH DROPS ARRIVE HERE, and they are told apart by the MARK the payload carries.
		//
		// 🛑 THEY DID NOT USED TO. Fields dragged from the tree came through a `wxDropTarget` set on
		// the window — until this grid became a drag SOURCE, at which point the control took the drop
		// over and that target stopped being asked at all. Dragging a field in silently stopped
		// working while double-clicking still added it, which is exactly the shape of "the mouse does
		// not work" (Max, 2026-08-27). One handler, two payloads: our own rows carry the mark, and
		// anything else is a field from the tree.
		if (text.StartsWith(kTotalsDragMark))
			MoveTotalsLevelTo(ParseTotalsDrag(text), onto);
		else
			AddTotalsFieldsTo(onto.m_node);
	});
#endif
	dims->Add(m_totalsDimensions, 1, wxEXPAND | wxALL, FromDIP(3));

	// GRAND TOTALS — stated, and stated truly. Our totals tree HAS a root and the root IS the grand
	// total: TOTALS with no dimensions is the grand total alone, and with dimensions it is what the
	// levels roll up into. So the line is not a switch, it is a fact about the result, and a
	// checkbox that could be cleared would promise a shape the engine does not produce.
	// ⭐ THE OVERALL LEVEL — a real setting now, and it stands between the two grids because that is
	// where it stands in the query: above every dimension, below the aggregates it folds.
	//
	// It used to be a TICKED, DISABLED box explaining that the grand total "is always produced" —
	// true of the FOLD and false of the RESULT. The tree's root always carries the whole-result
	// aggregates; whether that root is walked as a ROW is a choice, and the box was refusing to make
	// it while looking like it already had. The mechanism was finished; only this was not connected.
	m_grandTotals = new wxCheckBox(dimPane, wxID_ANY, _("Grand totals"));
	m_grandTotals->SetToolTip(_("One row above every dimension, folding the whole result "
	                            "(written as OVERALL, first in the BY list)."));
	m_grandTotals->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent& event) {
		ibQuerySelect* select = Current();
		if (!CanEdit() || select == nullptr) {
			event.Skip();
			return;
		}
		select->m_totalsOverall = m_grandTotals->GetValue();
		// TOTALS EXISTS THE MOMENT A LEVEL IS ASKED FOR. `BY OVERALL` on its own is a whole totals
		// query — one row over everything — so ticking this is enough to make one.
		if (select->m_totalsOverall)
			select->m_hasTotals = true;
		FillAll();
	});
	dims->Add(m_grandTotals, 0, wxLEFT | wxBOTTOM, FromDIP(6));

	// ⭐⭐ BY PERIODS — the level's periodicity, said with a mouse.
	//
	// It belongs to ONE level, so it stands under the grid and follows the selection: the switch,
	// the unit, and the two bounds. That is where a person expects it, and it is also the only shape
	// that is honest — a column in the grid would have to hold three answers in one cell.
	//
	// ⚠ THE BOUNDS DO NOT FILTER. They say which periods to REPORT — a quiet month inside them is
	// padded in, a month outside them that has rows is still shown. Left empty, the data's own first
	// and last periods are the range, which is why they are ordinary empty fields and not dates
	// pre-filled with something plausible.
	//
	// ⚠ AND ONLY A DATE HAS PERIODS. The panel greys out for a level keyed by anything else, asked
	// of the field's TYPE rather than of its name (see LevelIsDated) — and for a level of several
	// fields, which has no single scale to walk.
	// ONE PANE, so the strip can be TAKEN AWAY rather than dimmed. A level keyed by something that is
	// not a date has no periodicity at all, and dead controls under the grid would announce a setting
	// that does not exist for it (Max).
	m_periodPane = new wxPanel(dimPane);
	wxBoxSizer* periodRow = new wxBoxSizer(wxHORIZONTAL);
	m_byPeriods = new wxCheckBox(m_periodPane, wxID_ANY, _("By periods"));
	m_byPeriods->SetToolTip(_("Group this level by calendar periods, and report every period in the "
	                          "range — including the ones nothing happened in."));
	m_byPeriods->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { ApplyTotalsPeriods(); });
	periodRow->Add(m_byPeriods, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(6));

	periodRow->Add(new wxStaticText(m_periodPane, wxID_ANY, _("Period:")),
		0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(12));
	m_periodUnit = new wxChoice(m_periodPane, wxID_ANY);
	// THE TEN WORDS THE LANGUAGE HAS, read out of the one table that holds them. A list typed in
	// here would be a second vocabulary, and the day one of them is renamed the two disagree.
	for (const std::pair<ibTotalsPeriod, wxString>& unit : ibPeriodUnits())
		m_periodUnit->Append(unit.second);
	m_periodUnit->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { ApplyTotalsPeriods(); });
	periodRow->Add(m_periodUnit, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(4));

	// The bounds are EXPRESSIONS — `&PeriodStart` is what an author writes far more often than a
	// literal date, and both are read by the same parser the field itself goes through.
	auto addBound = [&](const wxString& label, wxTextCtrl*& field, const wxString& tip) {
		periodRow->Add(new wxStaticText(m_periodPane, wxID_ANY, label),
			0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(12));
		field = new wxTextCtrl(m_periodPane, wxID_ANY, wxEmptyString, wxDefaultPosition,
			wxSize(FromDIP(150), -1), wxTE_PROCESS_ENTER);
		field->SetToolTip(tip);
		// Committed on leaving the field or on Enter, never per keystroke: each commit re-parses the
		// whole field, and a half-typed `&Peri` is not an error yet.
		//
		// ⭐ AND ONLY IF SOMETHING WAS TYPED. The edit event is what says so — the panel fills itself
		// with ChangeValue, which raises none, so this flag is true exactly when a person put it
		// there. A focus change on its own is not an edit and must write nothing.
		field->Bind(wxEVT_TEXT, [this](wxCommandEvent& event) { m_periodBoundsEdited = true; event.Skip(); });
		field->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& event) {
			if (m_periodBoundsEdited) { m_periodBoundsEdited = false; ApplyTotalsPeriods(); }
			event.Skip();
		});
		field->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) {
			if (m_periodBoundsEdited) { m_periodBoundsEdited = false; ApplyTotalsPeriods(); }
		});
		periodRow->Add(field, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(4));
	};
	addBound(_("From:"), m_periodFrom,
		_("Report periods from this moment. Empty = from the earliest period in the data.\n"
		  "A bound does not filter rows — it says which periods to show."));
	addBound(_("To:"), m_periodTo,
		_("Report periods up to this moment. Empty = to the latest period in the data."));
	m_periodPane->SetSizer(periodRow);
	m_periodPane->Hide();   // shown by FillTotalsPeriods, once there is a dated level selected
	dims->Add(m_periodPane, 0, wxEXPAND | wxBOTTOM, FromDIP(4));


	// The panels belong to whichever level is selected, so they are refilled when that changes.
	m_totalsDimensions->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED,
		[this](ibDataViewEvent& event) { FillTotalsPeriods(); event.Skip(); });

	dimRow->Add(dims, 1, wxEXPAND);
	dimPane->SetSizer(dimRow);

	wxPanel* aggPane = new wxPanel(rightSplit);
	wxBoxSizer* aggRow = new wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer* aggMove = new wxBoxSizer(wxVERTICAL);
	aggMove->AddStretchSpacer();
	AddMoveButton(aggPane, aggMove, ArrowRight(), _("Total the selected field"),
		[this](wxCommandEvent& e) { OnAddTotalsAggregate(e); });
	AddMoveButton(aggPane, aggMove, ArrowLeft(), _("Remove the totals field"),
		[this](wxCommandEvent& e) { OnRemoveTotalsAggregate(e); });
	aggMove->AddStretchSpacer();
	aggRow->Add(aggMove, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(3));

	wxBoxSizer* aggs = new wxBoxSizer(wxVERTICAL);
	wxToolBar* aggBar = MakeToolBar(aggPane);
	AddTool(aggBar, _("Edit"),   wxART_EDIT, [this](wxCommandEvent& e) { OnEditTotalsAggregate(e); });
	AddTool(aggBar, _("Delete"), wxART_DELETE,    [this](wxCommandEvent& e) { OnRemoveTotalsAggregate(e); });
	aggBar->Realize();
	aggs->Add(aggBar, 0, wxEXPAND);

	// The totals FIELD and the EXPRESSION that computes it — the expression is the whole aggregate
	// call, because that is what a totals line is, and it opens in the arbitrary-expression editor.
	m_totalsAggregateModel = new ibQueryGridModel();
	m_totalsAggregateModel->SetReader([this](unsigned int row, unsigned int col) -> wxString {
		const ibQuerySelect* select = Current();
		if (select == nullptr || row >= select->m_totalsAggregates.size() || !select->m_totalsAggregates[row].m_expr)
			return wxEmptyString;
		const ibQueryTotalAggregate& resource = select->m_totalsAggregates[row];
		const ibQueryAstExpr& expr = *resource.m_expr;
		// ⭐ OVER WHAT THE FIGURE IS COMPUTED. Empty reads as empty on purpose: the absence IS the
		// ordinary answer ("the area comes from the groupings"), not an unfilled setting.
		if (col == kGridCol3)
			return resource.m_scope;
		if (col == kGridCol2)
			return ibRenderQueryExpr(expr);
		// The field the total is OVER — the argument, which is what a person looks down this column for.
		if (expr.m_kind == ibQueryAstExprKind::Func)
			return expr.m_star ? wxString(wxT("*")) : (expr.m_arg ? ibRenderQueryExpr(*expr.m_arg) : wxString());
		return ibRenderQueryExpr(expr);
	});
	m_totalsAggregateModel->SetWriter([this](unsigned int row, unsigned int col, const wxString& text) -> bool {
		ibQuerySelect* select = Current();
		if (!CanEdit() || select == nullptr || row >= select->m_totalsAggregates.size())
			return false;
		// ⭐ THE AREA IS A NAME, NOT AN EXPRESSION — the level this figure is computed over. Stored as
		// typed; whether such a grouping exists is the ENGINE's judgement (the lowering refuses an
		// unknown one by name), so this cell does not grow a second opinion about it.
		if (col == kGridCol3) {
			wxString scope = text;
			scope.Trim(true).Trim(false);
			select->m_totalsAggregates[row].m_scope = scope;
			return true;
		}
		// Typing over the FIELD keeps the function that was there; typing over the EXPRESSION replaces
		// the whole call. Either way the engine's parser is what reads it.
		wxString source = text;
		if (col == kGridCol1) {
			const ibQueryAstExprPtr& current = select->m_totalsAggregates[row].m_expr;
			const ibQueryKeyword func = current && current->m_kind == ibQueryAstExprKind::Func
				? current->m_func : ibQueryKeyword::Sum;
			source = ibQueryKeywordText(func) + wxT("(") + text + wxT(")");
		}
		try {
			ibQueryParser parser;
			select->m_totalsAggregates[row].m_expr = parser.ParseExpression(source);
		}
		catch (const ibBackendException& error) {
			ShowEngineError(error.GetErrorDescription());
			return false;
		}
		return true;
	});
	m_totalsAggregates = MakeGrid(aggPane, m_totalsAggregateModel, [this] { FillAll(); });
	AttachContextMenu(m_totalsAggregates, aggBar);
	m_totalsAggregateModel->SetIconColumn(kGridCol1, ibValue::GetIconGroup());
	m_totalsAggregateModel->SetIconReader([this](unsigned int row) -> wxIcon {
		const ibQuerySelect* select = Current();
		if (select == nullptr || row >= select->m_totalsAggregates.size()) return wxNullIcon;
		const ibQueryAstExprPtr& expr = select->m_totalsAggregates[row].m_expr;
		return expr ? IconOfExpr(expr->m_arg) : wxNullIcon;
	});
	m_totalsAggregates->GetRootColumnGroup()->AppendColumn(IconColumn(_("Totals field"), kGridCol1, FromDIP(150)));
	// ⭐ READY EXPRESSIONS TO PICK FROM, AND THE EDITOR FOR EVERYTHING ELSE.
	//
	// A totals expression is nearly always one of four calls over the field on this row, so the cell
	// offers those WHOLE — `SUM(Products.Qty)`, `COUNT(Products.Qty)` — filtered by what that field's
	// type can actually be folded by (the engine's own list). Picking is one gesture instead of
	// typing a call by hand.
	//
	// And it stays a free cell: a DOUBLE-CLICK opens the arbitrary-expression editor over the same
	// row, which is where a condition inside a total, or anything the four calls do not cover, is
	// written. Quick choice and full freedom side by side — neither takes the other away.
	m_totalsAggregates->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(_("Expression"),
		new ibExpressionCellRenderer([this]() -> wxArrayString {
			wxArrayString words;
			const ibQuerySelect* select = Current();
			const long row = SelectedRow(m_totalsAggregates, m_totalsAggregateModel);
			if (select == nullptr || row < 0 || static_cast<size_t>(row) >= select->m_totalsAggregates.size())
				return words;

			const ibQueryAstExprPtr& current = select->m_totalsAggregates[row].m_expr;
			if (!current)
				return words;
			// THE FIELD THIS TOTAL IS OVER — the argument of the call, or the whole expression when it
			// is not one. A ready call can only be offered around something.
			const ibQueryAstExprPtr argument = current->m_kind == ibQueryAstExprKind::Func
				? current->m_arg : current;
			if (!argument)
				return words;
			const wxString field = ibRenderQueryExpr(*argument);
			if (field.IsEmpty())
				return words;

			ibTypeDescription type;
			if (argument->m_kind == ibQueryAstExprKind::Column) {
				const ibSourceMetaDataScope resolveAgainst(m_metaData);
				type = m_model.TypeOfPath(*select, argument->m_path, m_package, m_statement);
			}
			// THE ENGINE COMPOSES THE OFFERS (AggregateCallsFor) — the ready calls over this field,
			// including the DISTINCT twin where it asks a different question.
			for (const wxString& call : ibQueryLowering::AggregateCallsFor(type, field))
				words.Add(call);
			return words;
		},
		// "..." - the arbitrary-expression editor over what the cell currently holds.
		[this](wxString& text) {
			// ⚠ NO WINDOW CALLS HERE — this cell writes a TOTALS figure, which folds nodes and takes
			// aggregates only. The area such a figure is computed over is the neighbouring column.
			ibDialogQueryExpression editor(this, _("Totals"), AvailableFields(), nullptr,
				m_metaData, m_readOnly, /*allowWindows*/false);
			editor.SetText(text);
			if (editor.ShowModal() != wxID_OK)
				return false;
			text = editor.GetText();
			return true;
		},
		m_readOnly ? wxDATAVIEW_CELL_INERT : wxDATAVIEW_CELL_EDITABLE),
		kGridCol2, FromDIP(200), wxAlignment::wxALIGN_LEFT));
	// ⭐ AND THE NAME THE FIGURE ANSWERS TO — the same column a LEVEL has, in the same place, because
	// it is the same question: what is this column of the result called. Left empty the engine names
	// it after the argument, which is what every query written before this meant.
	// ⛔ NO "ALIAS" COLUMN HERE ANY MORE. A column of the result is NAMED WHERE COLUMNS ARE NAMED —
	// in the selection, and on the Unions / Aliases tab, which is the one place that name is settled
	// for every united selection at once. A second name written beside the figure was a duplicate of
	// it: two places saying one thing, and the drift starts the day somebody renames the field
	// (Max, 2026-08-27: "it takes the alias from where the union is — and it is unclear why one was
	// started here as well").
	//
	// ⚠ THE LANGUAGE STILL READS `TOTALS SUM(x) AS Name` — a query written before this keeps its name
	// and keeps working. What is gone is the SECOND DOOR to it, not the word.
	// ⭐⭐ …AND OVER WHAT THE FIGURE IS COMPUTED — the level it belongs to, picked from the groupings
	// THIS query already declares. That is the whole of what other systems reach through an
	// expression evaluated "in the context of a grouping", and here it is one cell.
	//
	// The list is asked of the model beside it, so there is no second place where groupings are
	// enumerated and nothing to keep in step: add a level below, and it is offered here at once.
	// Empty is a legitimate choice and the first one — it means "the area comes from the ladder",
	// which is what every totals written before this said.
	// Two ways in, one meaning — the same pair every other cell of this window offers: the LIST for
	// the level you already know the name of, and the "..." for the TREE, where the separators are
	// nodes and their levels hang inside them. A flat list can spell `Splitter1.Level`; it cannot
	// show that the level lives in that separator, and picking an area IS picking a place.
	m_totalsAggregates->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(_("Computed over"),
		new ibExpressionCellRenderer([this]() -> wxArrayString { return TotalsScopeChoices(); },
			[this](wxString& text) { return PickTotalsScope(text); },
			m_readOnly ? wxDATAVIEW_CELL_INERT : wxDATAVIEW_CELL_EDITABLE),
		kGridCol3, FromDIP(150), wxAlignment::wxALIGN_LEFT));
	// …AND THE WAY OUT OF THE LIST IS THE "...", not the double-click.
	//
	// ⚠ THIS BINDING USED TO OPEN THE DIALOG, and that is why the "..." could not be reached: MakeGrid
	// already answers an activation by OPENING THE CELL, a handler bound later runs FIRST, and neither
	// one skips — so the double-click went to the window and the cell (the only thing that carries the
	// button) never opened. Two doors onto the same expression, and the one nearer the mouse won.
	//
	// One gesture, one meaning, and the same everywhere in this window: a double-click opens the CELL;
	// the "..." inside it opens the EDITOR. The toolbar's Edit verb is the same door for the keyboard.
	m_totalsAggregates->SetDropTarget(new ibCallbackDropTarget([this] { wxCommandEvent e; OnAddTotalsAggregate(e); }));
	aggs->Add(m_totalsAggregates, 1, wxEXPAND | wxALL, FromDIP(3));
	aggRow->Add(aggs, 1, wxEXPAND);
	aggPane->SetSizer(aggRow);

	rightSplit->SplitHorizontally(dimPane, aggPane, FromDIP(300));
	splitter->SplitVertically(leftPane, rightSplit, FromDIP(280));

	wxBoxSizer* pageSizer = new wxBoxSizer(wxVERTICAL);
	pageSizer->Add(splitter, 1, wxEXPAND | wxALL, FromDIP(4));
	page->SetSizer(pageSizer);
	return page;
}

// TWO GRIDS, and the split is the question a union raises: WHICH BRANCHES there are, and WHICH
// COLUMN OF EACH lines up with which output field. The second is not a setting — our lowering
// lines branches up BY NAME, so the map is a table of what already matches (queryUnionModel.h).
wxWindow* ibDialogQueryConstructor::BuildUnionsPage(wxWindow* parent)
{
	wxPanel* page = new wxPanel(parent);

	wxSplitterWindow* splitter = new wxSplitterWindow(page, wxID_ANY,
		wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3DSASH);
	splitter->SetMinimumPaneSize(FromDIP(150));

	// ---- LEFT: the branches.
	wxPanel* leftPane = new wxPanel(splitter);
	wxBoxSizer* left = new wxBoxSizer(wxVERTICAL);
	wxToolBar* bar = MakeToolBar(leftPane);
	AddTool(bar, _("Add branch"),       wxART_ADD,    [this](wxCommandEvent& e) { OnAddUnionBranch(e); });
	// No copy picture in the product's set (artProvider.cpp serves add/edit/delete/up/down/sort), so
	// this one falls through to its word — which AddTool now handles rather than drawing a blank.
	AddTool(bar, _("Duplicate branch"), wxASCII_STR(wxART_COPY),
		[this](wxCommandEvent& e) { OnCopyUnionBranch(e); });
	AddTool(bar, _("Edit branch"),      wxART_EDIT,   [this](wxCommandEvent& e) { OnEditUnionBranch(e); });
	AddTool(bar, _("Delete"),           wxART_DELETE, [this](wxCommandEvent& e) { OnRemoveUnionBranch(e); });
	bar->AddSeparator();
	// BRANCH ORDER IS SEMANTICS, not presentation: the FIRST branch decides the shape of the whole
	// union (its columns are the result's, and every other branch is lined up against them).
	AddTool(bar, _("Move up"),   wxART_UP,   [this](wxCommandEvent&) { OnMoveUnionBranch(-1); });
	AddTool(bar, _("Move down"), wxART_DOWN, [this](wxCommandEvent&) { OnMoveUnionBranch(+1); });
	bar->Realize();
	left->Add(bar, 0, wxEXPAND);

	m_unions = new ibDataViewCtrl(leftPane, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxDV_ROW_LINES | wxDV_SINGLE);
	m_unionModel = new ibQueryUnionModel();
	m_unionModel->SetOnChanged([this] { FillAll(); });
	m_unions->AssociateModel(m_unionModel);
	m_unions->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(_("Name"),
		new ibDataViewTextRenderer(), kUnionColName, FromDIP(180), wxAlignment::wxALIGN_LEFT));
	m_unions->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(_("No duplicates"),
		new ibDataViewToggleRenderer(ibDataViewToggleRenderer::GetDefaultType(), wxDATAVIEW_CELL_ACTIVATABLE),
		kUnionColKeepDuplicates, FromDIP(110), wxAlignment::wxALIGN_CENTER));
	m_unions->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, [this](ibDataViewEvent&) { wxCommandEvent e; OnEditUnionBranch(e); });
	AttachContextMenu(m_unions, bar);
	left->Add(m_unions, 1, wxEXPAND | wxALL, FromDIP(3));
	leftPane->SetSizer(left);

	// ---- RIGHT: the field map. Its branch columns are rebuilt in FillUnions, because how many
	// there are is a fact about the query, not about the window.
	wxPanel* rightPane = new wxPanel(splitter);
	wxBoxSizer* right = new wxBoxSizer(wxVERTICAL);
	right->Add(new wxStaticText(rightPane, wxID_ANY,
		// ⚠ ASCII ONLY IN THIS FILE'S LITERALS. It is UTF-8 without a BOM and MSVC reads it in the
		// system codepage, so an em dash typed here reaches the screen as mojibake. The move glyphs
		// taught this once; a dash in a sentence is the same trap wearing different clothes.
		_("The first column is the field's ALIAS: what the result calls it, and what every branch "
		  "is lined up by. Type over it to rename the output field.")),
		0, wxALL, FromDIP(3));

	// THE FIELD MAP HAS ITS OWN VERBS, and they act on the PROJECTION a row stands for: the order of
	// the output fields and whether one is there at all are decided here as readily as on the Fields
	// tab, because this is where a person is looking when they think about the result's shape.
	wxToolBar* fieldBar = MakeToolBar(rightPane);
	AddTool(fieldBar, _("Delete"),    wxART_DELETE,  [this](wxCommandEvent& e) { OnRemoveUnionField(e); });
	fieldBar->AddSeparator();
	AddTool(fieldBar, _("Move up"),   wxART_UP,   [this](wxCommandEvent&) { OnMoveUnionField(-1); });
	AddTool(fieldBar, _("Move down"), wxART_DOWN, [this](wxCommandEvent&) { OnMoveUnionField(+1); });
	fieldBar->Realize();
	right->Add(fieldBar, 0, wxEXPAND);

	m_unionFields = new ibDataViewCtrl(rightPane, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxDV_ROW_LINES | wxDV_SINGLE);
	m_unionFieldModel = new ibQueryUnionFieldModel();
	m_unionFieldModel->SetOnChanged([this] { FillAll(); });
	// WHAT A BRANCH CAN SUPPLY — its own tables' fields, asked of the same model every other field
	// list in this window asks. The model holds the AST; only the host reaches the metadata.
	m_unionFieldModel->SetBranchFields([this](unsigned int branch) -> wxArrayString {
		wxArrayString words;
		const ibQuerySelect* select = m_unionFieldModel->BranchSelectAt(branch);
		if (select == nullptr)
			return words;
		const ibSourceMetaDataScope resolveAgainst(m_metaData);
		const auto add = [&](const ibQuerySource& source) {
			const wxString table = ibQuerySourceName(source);
			for (const ibQueryConstructorField& field : m_model.GetFields(source, m_package, m_statement))
				words.Add(table.IsEmpty() ? field.m_name : table + wxT(".") + field.m_name);
		};
		add(select->m_from);
		for (const ibQueryAstJoin& join : select->m_joins)
			add(join.m_source);
		return words;
	});
	// THE OUTPUT FIELDS WEAR THEIR PICTURES, like the field list on every other tab: these rows are
	// the union's result columns, and the first branch's projections are what they are made of.
	m_unionFieldModel->SetIconReader([this](unsigned int row) -> wxIcon {
		const ibQuerySelect* select = Current();
		return select != nullptr && row < select->m_projections.size()
			? IconOfExpr(select->m_projections[row].m_expr) : wxNullIcon;
	});
	// A REFUSED RENAME IS TOLD, not left on the verdict line at the far edge of the window: the eyes
	// are on the cell, and the cell is about to put its old text back.
	m_unionFieldModel->SetOnError([this](const wxString& message) {
		wxMessageBox(message, _("Query constructor"), wxOK | wxICON_WARNING, this);
	});
	m_unionFields->AssociateModel(m_unionFieldModel);
	EditOnActivate(m_unionFields);   // the Alias cell — the one place an output field is named
	right->Add(m_unionFields, 1, wxEXPAND | wxALL, FromDIP(3));
	rightPane->SetSizer(right);

	splitter->SplitVertically(leftPane, rightPane, FromDIP(320));

	wxBoxSizer* pageSizer = new wxBoxSizer(wxVERTICAL);
	pageSizer->Add(splitter, 1, wxEXPAND | wxALL, FromDIP(4));
	page->SetSizer(pageSizer);
	return page;
}
wxWindow* ibDialogQueryConstructor::BuildAdvancedPage(wxWindow* parent)
{
	wxPanel* page = new wxPanel(parent);
	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
	wxBoxSizer* groups = new wxBoxSizer(wxHORIZONTAL);

	auto changed = [this](wxCommandEvent& e) { OnAdvancedChanged(e); };

	// ---- WHICH RECORDS: the three words that narrow the selection itself.
	wxStaticBoxSizer* selection = new wxStaticBoxSizer(wxVERTICAL, page, _("Record selection"));
	wxWindow* selectionBox = selection->GetStaticBox();

	wxBoxSizer* topRow = new wxBoxSizer(wxHORIZONTAL);
	m_useTop = new wxCheckBox(selectionBox, wxID_ANY, _("First"));
	m_useTop->Bind(wxEVT_CHECKBOX, changed);
	topRow->Add(m_useTop, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
	m_topCount = new wxSpinCtrl(selectionBox, wxID_ANY, wxEmptyString, wxDefaultPosition,
		FromDIP(wxSize(90, -1)), wxSP_ARROW_KEYS, 1, 1000000, 10);
	m_topCount->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) { wxCommandEvent e; OnAdvancedChanged(e); });
	topRow->Add(m_topCount, 0, wxALIGN_CENTER_VERTICAL);
	selection->Add(topRow, 0, wxALL, FromDIP(4));

	m_distinct = new wxCheckBox(selectionBox, wxID_ANY, _("No duplicates"));
	m_distinct->Bind(wxEVT_CHECKBOX, changed);
	selection->Add(m_distinct, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(4));

	// ALLOWED — a word, not a switch buried in a list, because the sentence it changes matters:
	// a refusal becomes silence, and silence is only honest when it was asked for.
	m_allowed = new wxCheckBox(selectionBox, wxID_ANY, _("Allowed only"));
	m_allowed->SetToolTip(_("Skip what this user may not read instead of refusing the whole query"));
	m_allowed->Bind(wxEVT_CHECKBOX, changed);
	selection->Add(m_allowed, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(4));
	groups->Add(selection, 0, wxEXPAND | wxALL, FromDIP(6));

	// ---- WHAT THIS STATEMENT IS. A KIND, not a checkbox: three different statements, and the name
	// is asked by the two kinds that need one.
	wxStaticBoxSizer* kind = new wxStaticBoxSizer(wxVERTICAL, page, _("Query kind"));
	wxWindow* kindBox = kind->GetStaticBox();

	// ALL THREE SHOWN AT ONCE. A kind is a choice among named alternatives, and a drop-down hides
	// two of them behind the one currently picked — which is how somebody spends a while looking
	// for "where do I make a temporary table".
	wxArrayString kinds;
	kinds.Add(_("Select"));
	kinds.Add(_("Create a temporary table"));
	kinds.Add(_("Drop a temporary table"));
	// A FOURTH: the select still hands its result back, and NAMES it (ONTO). The pair to the second
	// kind and not the same thing — a temporary table is read by later statements, a named result is
	// asked for by whoever runs the package, by name instead of by position.
	kinds.Add(_("Select under a name"));
	m_queryKind = new wxRadioBox(kindBox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
		kinds, 1, wxRA_SPECIFY_COLS);
	m_queryKind->SetSelection(0);
	m_queryKind->Bind(wxEVT_RADIOBOX, &ibDialogQueryConstructor::OnQueryKindChanged, this);
	kind->Add(m_queryKind, 0, wxEXPAND | wxALL, FromDIP(4));

	// ONE FIELD, because all three naming kinds ask the same thing: what this statement's name is.
	// Which name it becomes — a temporary table's or a result's — is what the kind above says.
	kind->Add(new wxStaticText(kindBox, wxID_ANY, _("Name")), 0,
		wxLEFT | wxRIGHT, FromDIP(4));
	m_tempName = new wxTextCtrl(kindBox, wxID_ANY, wxEmptyString, wxDefaultPosition,
		FromDIP(wxSize(240, -1)));
	// Applied when the caret leaves it — and WITHOUT pulling the caret back, or leaving the box
	// would be impossible.
	m_tempName->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& e) { ApplyQueryKind(false); e.Skip(); });
	kind->Add(m_tempName, 0, wxEXPAND | wxALL, FromDIP(4));
	groups->Add(kind, 0, wxEXPAND | wxTOP | wxBOTTOM | wxRIGHT, FromDIP(6));

	sizer->Add(groups, 0, wxEXPAND);

	m_forUpdate = new wxCheckBox(page, wxID_ANY, _("Lock the rows read, so they can be changed after"));
	m_forUpdate->Bind(wxEVT_CHECKBOX, changed);
	sizer->Add(m_forUpdate, 0, wxALL, FromDIP(10));

	// The reference also lets the author pick WHICH tables are locked. Ours locks the read, whole —
	// `ibReadPageRequest::m_lockForUpdate` is one flag on one statement — so there is nothing here
	// to choose between, and a list that pretended otherwise would be a control over nothing.

	page->SetSizer(sizer);
	return page;
}

