////////////////////////////////////////////////////////////////////////////
//	Description : "Arbitrary expression" — one editor for every expression
//	              (queryExpressionDialog.h)
////////////////////////////////////////////////////////////////////////////

#include "queryExpressionDialog.h"
#include "queryConstructor.h"            // ibShowQueryConstructorFor — the nested query
#include "queryCaseDialog.h"             // ibShowQueryCaseBuilder — CASE WHEN as the list it is

#include "../callbackDropTarget.h"      // one drop target for the product: a drop is a callback
#include "queryFieldTree.h"              // the field row: its node, its walk, its drag

#include "artProvider/artProvider.h"     // wxART_QUERY_CONSTRUCTOR - the window wears the menu item picture
#include <wx/artprov.h>

#include "backend/query/queryParser.h"
#include "backend/query/queryRender.h"
#include "backend/query/queryKeywords.h"
#include "backend/backend_exception.h"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/splitter.h>
#include <wx/msgdlg.h>
#include <wx/dnd.h>
#include <wx/imaglist.h>
#include <wx/stc/stc.h>

#include <algorithm>
#include <functional>   // the name check walks the tree through a recursive lambda

namespace {

wxString Kw(ibQueryKeyword kw) { return ibQueryKeywordText(kw); }

// A leaf of the language tree carries the TEXT it inserts — which is never a literal here, always
// the active keyword table's spelling.
class ibLanguageLeaf : public wxTreeItemData
{
public:
	explicit ibLanguageLeaf(wxString text) : m_text(std::move(text)) {}
	wxString m_text;
};

} // namespace

ibDialogQueryExpression::ibDialogQueryExpression(wxWindow* parent, const wxString& title,
                                                 const std::vector<ibQueryConstructorField>& fields,
                                                 const ibQueryAstExprPtr& existing,
                                                 const ibMetaData* metaData, bool readOnly, bool allowWindows)
	: wxDialog(parent, wxID_ANY, title.IsEmpty() ? _("Arbitrary expression") : title,
		wxDefaultPosition, wxSize(820, 560), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
	, m_fields(fields)
	, m_metaData(metaData)
	, m_readOnly(readOnly)
	, m_allowWindows(allowWindows)
	, m_model(metaData)   // the same config the fields came from — so a reference can be walked here too
{
	// The same picture the constructor wears: this window writes the same language, one level down.
	{
		const wxBitmap picture = wxArtProvider::GetBitmap(wxART_QUERY_CONSTRUCTOR, wxART_FRONTEND,
			FromDIP(wxSize(16, 16)));
		if (picture.IsOk()) {
			wxIcon icon;
			icon.CopyFromBitmap(picture);
			SetIcon(icon);
		}
	}

	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

	// The two palettes side by side, the text below — what the expression is ABOUT above what it
	// says, because the text is written by picking from them.
	wxSplitterWindow* palettes = new wxSplitterWindow(this, wxID_ANY,
		wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3DSASH);
	palettes->SetMinimumPaneSize(FromDIP(150));

	wxPanel* leftPane = new wxPanel(palettes);
	wxBoxSizer* left = new wxBoxSizer(wxVERTICAL);
	left->Add(new wxStaticText(leftPane, wxID_ANY, _("Fields")), 0, wxALL, FromDIP(3));
	// THE SAME TREE THE CONSTRUCTOR SHOWS, and for the same reason: a flat list cannot say what a
	// reference LEADS to, so every field behind one was unreachable from this window — you had to
	// close it, unfold the reference in the constructor, and come back. Twist buttons, the field
	// picture, drag onto the text: nothing here is special to this dialog.
	m_fieldTree = new wxTreeCtrl(leftPane, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTR_HAS_BUTTONS | wxTR_SINGLE | wxTR_HIDE_ROOT | wxTR_LINES_AT_ROOT | wxTR_NO_LINES
		| wxTR_TWIST_BUTTONS);
	{
		wxImageList* images = new wxImageList(16, 16);
		m_fieldIcon = images->Add(ibValue::GetIconGroup());
		m_fieldTree->AssignImageList(images);
	}
	// GROUPED BY SOURCE, in the order the query names them. Two tables merged into one list is
	// unreadable the moment both have a `Code`: either the reader cannot tell which is which, or
	// every line carries the table name and the field's own name is pushed off the right. A heading
	// per table says it once. (The fields carry which source they came from — m_source.)
	const wxTreeItemId fieldRoot = m_fieldTree->AddRoot(wxEmptyString);
	std::vector<wxString> groups;
	for (const ibQueryConstructorField& field : m_fields)
		if (std::find(groups.begin(), groups.end(), field.m_source) == groups.end())
			groups.push_back(field.m_source);

	for (const wxString& group : groups) {
		// A source with no name of its own (a single-table query) needs no heading — its fields ARE
		// the list, and a group of one over all of them says nothing.
		const wxTreeItemId parent = (group.IsEmpty() || groups.size() == 1)
			? fieldRoot
			: m_fieldTree->AppendItem(fieldRoot, group, m_fieldIcon, m_fieldIcon);

		for (const ibQueryConstructorField& field : m_fields)
			if (field.m_source == group)
				ibQueryAddFieldNode(m_fieldTree, parent, field, m_fieldIcon);

		if (parent != fieldRoot)
			m_fieldTree->Expand(parent);
	}

	m_fieldTree->Bind(wxEVT_TREE_ITEM_ACTIVATED,  &ibDialogQueryExpression::OnInsertField, this);
	m_fieldTree->Bind(wxEVT_TREE_ITEM_EXPANDING,  &ibDialogQueryExpression::OnFieldExpanding, this);
	// NOT Allow()ed — we run the drag ourselves (the same trap that produced a wxDragImage assert
	// in the constructor: letting the tree drag too makes MSW refuse the second BeginDrag).
	m_fieldTree->Bind(wxEVT_TREE_BEGIN_DRAG,      &ibDialogQueryExpression::OnFieldDrag, this);
	left->Add(m_fieldTree, 1, wxEXPAND | wxALL, FromDIP(3));
	leftPane->SetSizer(left);

	wxPanel* rightPane = new wxPanel(palettes);
	wxBoxSizer* right = new wxBoxSizer(wxVERTICAL);
	right->Add(new wxStaticText(rightPane, wxID_ANY, _("Query language")), 0, wxALL, FromDIP(3));
	// wxTR_TWIST_BUTTONS + no lines — the modern arrow, the same as every other tree in this window.
	// The boxed [+] is the one thing left here that looked like a different decade.
	m_language = new wxTreeCtrl(rightPane, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTR_HAS_BUTTONS | wxTR_SINGLE | wxTR_HIDE_ROOT | wxTR_LINES_AT_ROOT | wxTR_NO_LINES
		| wxTR_TWIST_BUTTONS);
	m_language->Bind(wxEVT_TREE_ITEM_ACTIVATED, &ibDialogQueryExpression::OnInsertLanguage, this);
	m_language->Bind(wxEVT_TREE_BEGIN_DRAG, &ibDialogQueryExpression::OnLanguageDrag, this);
	right->Add(m_language, 1, wxEXPAND | wxALL, FromDIP(3));
	rightPane->SetSizer(right);

	palettes->SplitVertically(leftPane, rightPane, FromDIP(380));
	sizer->Add(palettes, 1, wxEXPAND | wxALL, FromDIP(6));

	// THE SAME STYLED EDITOR the constructor's text pane uses — one language, one look, one helper.
	// An expression is query text like any other, and it was the last place still shown as plain
	// grey characters.
	m_text = new wxStyledTextCtrl(this, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(-1, 150)));
	ibStyleQueryText(m_text);
	if (existing) {
		m_text->SetText(ibRenderQueryExpr(*existing));
		ibMarkQueryParameters(m_text);
	}
	// A DROP LANDS WHERE IT WAS DROPPED. The field travels as its own text, so the drop needs only
	// to put the caret under the mouse and write it — no payload format of our own.
	// WHERE IT WAS DROPPED, not where the caret happened to be. A field dragged into the middle of a
	// half-written CASE WHEN belongs at the mouse; leaving it at the old caret is the kind of
	// almost-right that makes a gesture not worth using. PositionFromPoint is the styled editor own
	// answer to "which character is here", so the drop lands where it was aimed.
	m_text->SetDropTarget(new ibCallbackDropTarget([this](wxCoord x, wxCoord y, const wxString& text) {
		if (text.IsEmpty())
			return false;
		const int position = m_text->PositionFromPoint(wxPoint(x, y));
		if (position >= 0)
			m_text->GotoPos(position);
		m_text->ReplaceSelection(text);
		m_text->SetFocus();
		return true;
	}));
	sizer->Add(m_text, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(6));

	wxBoxSizer* buttons = new wxBoxSizer(wxHORIZONTAL);
	// THE THING THAT MAKES THIS A CONSTRUCTOR. `IN (SELECT …)` is an expression whose right side is
	// a query, so it is authored in the query constructor — one level down, the same window.
	wxButton* nested = new wxButton(this, wxID_ANY, _("Nested query"));
	nested->Bind(wxEVT_BUTTON, &ibDialogQueryExpression::OnNestedQuery, this);
	nested->Enable(!m_readOnly);
	buttons->Add(nested, 0, wxRIGHT, FromDIP(6));

	// AND OPEN THE ONE THAT IS ALREADY THERE. Writing `IN (SELECT …)` is the easy half; the half
	// that was missing is going BACK into that subquery with the tabs — selecting it here and
	// pressing this opens the constructor over exactly that span and puts the result back in place.
	// Without it, a nested query could be created and never edited again except as text.
	wxButton* editSelection = new wxButton(this, wxID_ANY, _("Query constructor"));
	editSelection->Bind(wxEVT_BUTTON, &ibDialogQueryExpression::OnEditSelection, this);
	editSelection->Enable(!m_readOnly);
	buttons->Add(editSelection, 0, wxRIGHT, FromDIP(6));

	// AND THE ONE CONSTRUCTION THAT IS A LIST. `CASE WHEN … THEN … END` is n branches tried in
	// ORDER, and order is its meaning — which is a thing you edit as rows with move buttons, not as
	// a sentence. The palette's skeleton drops five keywords into the text and leaves the author to
	// keep the WHENs and THENs paired by hand; this opens the branches as what they are.
	//
	// Same shape as the two buttons beside it: it works ON THE SELECTION, so a CASE already written
	// can be opened, rearranged and written back — the round trip, not a one-way generator.
	wxButton* choice = new wxButton(this, wxID_ANY, ibQueryKeywordText(ibQueryKeyword::Case));
	choice->Bind(wxEVT_BUTTON, &ibDialogQueryExpression::OnEditChoice, this);
	choice->Enable(!m_readOnly);
	buttons->Add(choice, 0, wxRIGHT, FromDIP(6));
	buttons->AddStretchSpacer();
	buttons->Add(CreateStdDialogButtonSizer(m_readOnly ? wxCLOSE : (wxOK | wxCANCEL)), 0);
	sizer->Add(buttons, 0, wxEXPAND | wxALL, FromDIP(6));

	SetSizer(sizer);
	FillLanguageTree();

	if (m_readOnly) {
		m_text->SetReadOnly(true);
		Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CANCEL); }, wxID_CLOSE);
	}
	Bind(wxEVT_BUTTON, &ibDialogQueryExpression::OnOk, this, wxID_OK);

	// ⭐⭐ THE CARET IS ALREADY IN THE TEXT WHEN THE WINDOW OPENS. This dialog exists to write ONE
	// thing; opening it and landing the focus on the OK button means the first keystroke goes
	// nowhere and the author has to click into the very box the window was opened for.
	//
	// AT THE END of what is already written, not selecting it — a caret that arrives with the whole
	// expression highlighted destroys it on the first character typed. The same place the editor
	// puts it after every other insertion, so the continuation point is one habit.
	//
	// ⚠ ON INIT_DIALOG, not in the constructor: the modal show sets the initial focus itself, after
	// the constructor has run, and would take it back.
	Bind(wxEVT_INIT_DIALOG, [this](wxInitDialogEvent& event) {
		event.Skip();   // the dialog still transfers its data
		if (m_readOnly)
			return;
		m_text->SetFocus();
		m_text->GotoPos(m_text->GetLastPosition());
	});
}

// ---------------------------------------------------------------------------
//  The language, as a tree — every word from the keyword table
// ---------------------------------------------------------------------------

void ibDialogQueryExpression::FillLanguageTree()
{
	const wxTreeItemId root = m_language->AddRoot(wxEmptyString);

	auto group = [this, &root](const wxString& name) { return m_language->AppendItem(root, name); };
	auto leaf  = [this](const wxTreeItemId& parent, const wxString& label, const wxString& text) {
		m_language->AppendItem(parent, label, -1, -1, new ibLanguageLeaf(text));
	};

	// AGGREGATES — a call, so the caret lands where the argument goes.
	//
	// ⭐ AND ITS DISTINCT FORM, where that is a different question. `COUNT(DISTINCT Board)` asks how
	// many different boards; `COUNT(Board)` how many rows have one. The palette is where a person
	// finds out the language HAS the modifier — it was in the parser and nowhere a reader could see
	// it, which is the same as not having it.
	//
	// Which functions get the twin is the KEYWORD TABLE's answer (ibDistinctMattersFor), not a list
	// kept here: MIN/MAX give the same value either way, and offering `MIN(DISTINCT …)` would pad
	// the palette with a choice that changes nothing.
	const wxTreeItemId functions = group(_("Functions"));
	for (const ibQueryKeyword kw : { ibQueryKeyword::Sum, ibQueryKeyword::Count, ibQueryKeyword::Min,
	                                 ibQueryKeyword::Max, ibQueryKeyword::Avg }) {
		leaf(functions, Kw(kw) + wxT("()"), Kw(kw) + wxT("()"));
		if (ibDistinctMattersFor(kw)) {
			const wxString call = Kw(kw) + wxT("(") + Kw(ibQueryKeyword::Distinct) + wxT(" )");
			leaf(functions, call, call);
		}
	}

	// ⭐ THE SUBSTITUTION, beside the folds — and NOT beside `IS NULL`, which is a different thing
	// wearing nearly the same word. `IS NULL` ASKS (a predicate, in a condition); `ISNULL(a, b)`
	// ANSWERS WITH SOMETHING ELSE (a value, in an expression). Filed where each is used, so the two
	// are chosen apart rather than confused by proximity.
	leaf(functions, Kw(ibQueryKeyword::IsNull) + wxT("(, )"), Kw(ibQueryKeyword::IsNull) + wxT("(, )"));

	// ⭐ THE SCALAR CALLS, FROM THE TABLE THAT DECLARES THEM — the word AND how many arguments it
	// takes both come from ibQueryScalarFnArity, so the skeleton this drops is the one the parser
	// will accept. A palette with its own idea of the signatures is a palette that writes queries
	// the engine refuses, and the person reading the refusal has no way to know which of the two is
	// wrong. Filed in the three folders a person looks in — dates, text, and the rest.
	auto call = [](ibQueryScalarFn fn) {
		size_t least = 0, most = 0;
		ibQueryScalarFnArity(fn, least, most);
		wxString args;
		for (size_t i = 0; i < least; ++i)
			args += (i == 0 ? wxString() : wxT(", "));
		return ibQueryScalarFnText(fn) + wxT("(") + args + wxT(")");
	};
	auto callGroup = [&](const wxTreeItemId& parent, std::initializer_list<ibQueryScalarFn> fns) {
		for (const ibQueryScalarFn fn : fns) {
			const wxString text = call(fn);
			leaf(parent, text, text);
		}
	};

	const wxTreeItemId dates = m_language->AppendItem(functions, _("Date functions"));
	callGroup(dates, { ibQueryScalarFn::Year, ibQueryScalarFn::Quarter, ibQueryScalarFn::Month,
	                   ibQueryScalarFn::DayOfYear, ibQueryScalarFn::Day, ibQueryScalarFn::Week,
	                   ibQueryScalarFn::WeekDay, ibQueryScalarFn::Hour, ibQueryScalarFn::Minute,
	                   ibQueryScalarFn::Second, ibQueryScalarFn::BeginOfPeriod, ibQueryScalarFn::EndOfPeriod,
	                   ibQueryScalarFn::DateAdd, ibQueryScalarFn::DateDiff });

	const wxTreeItemId strings = m_language->AppendItem(functions, _("String functions"));
	callGroup(strings, { ibQueryScalarFn::Substring });

	const wxTreeItemId others = m_language->AppendItem(functions, _("Other functions"));
	callGroup(others, { ibQueryScalarFn::ValueType, ibQueryScalarFn::Presentation,
	                    ibQueryScalarFn::RefPresentation });

	const wxTreeItemId operators = group(_("Operators"));
	// Comparison glyphs are punctuation — the lexer reads them from characters, not a table, so
	// these are the only literals in this file.
	for (const wxChar* glyph : { wxT("="), wxT("<>"), wxT("<"), wxT("<="), wxT(">"), wxT(">="),
	                             wxT("+"), wxT("-"), wxT("*"), wxT("/"), wxT("%") })
		leaf(operators, glyph, glyph);
	for (const ibQueryKeyword kw : { ibQueryKeyword::And, ibQueryKeyword::Or, ibQueryKeyword::Not,
	                                 ibQueryKeyword::Like, ibQueryKeyword::In, ibQueryKeyword::Between,
	                                 ibQueryKeyword::Refs })
		leaf(operators, Kw(kw), Kw(kw) + wxT(" "));
	// `IN HIERARCHY` is the same operator told how far down to look, so it is offered as the phrase
	// rather than as a second word somebody has to know goes after IN.
	leaf(operators, Kw(ibQueryKeyword::In) + wxT(" ") + Kw(ibQueryKeyword::Hierarchy) + wxT("()"),
		Kw(ibQueryKeyword::In) + wxT(" ") + Kw(ibQueryKeyword::Hierarchy) + wxT("()"));
	leaf(operators, Kw(ibQueryKeyword::Is) + wxT(" ") + Kw(ibQueryKeyword::Null),
		Kw(ibQueryKeyword::Is) + wxT(" ") + Kw(ibQueryKeyword::Null));
	leaf(operators, Kw(ibQueryKeyword::Is) + wxT(" ") + Kw(ibQueryKeyword::Not) + wxT(" ") + Kw(ibQueryKeyword::Null),
		Kw(ibQueryKeyword::Is) + wxT(" ") + Kw(ibQueryKeyword::Not) + wxT(" ") + Kw(ibQueryKeyword::Null));

	const wxTreeItemId other = group(_("Other"));
	// CASE written whole: the shape is what a person forgets, not the word.
	leaf(other, Kw(ibQueryKeyword::Case),
		Kw(ibQueryKeyword::Case) + wxT(" ") + Kw(ibQueryKeyword::When) + wxT("  ")
		+ Kw(ibQueryKeyword::Then) + wxT("  ") + Kw(ibQueryKeyword::Else) + wxT("  ")
		+ Kw(ibQueryKeyword::End));
	leaf(other, Kw(ibQueryKeyword::Value) + wxT("()"), Kw(ibQueryKeyword::Value) + wxT("()"));
	// CAST written whole, for the same reason CASE is: what a person forgets is the `AS`, not the word.
	leaf(other, Kw(ibQueryKeyword::Cast) + wxT("( ") + Kw(ibQueryKeyword::As) + wxT(" )"),
		Kw(ibQueryKeyword::Cast) + wxT("( ") + Kw(ibQueryKeyword::As) + wxT(" )"));
	leaf(other, ibQueryScalarFnText(ibQueryScalarFn::Type) + wxT("()"),
		ibQueryScalarFnText(ibQueryScalarFn::Type) + wxT("()"));
	leaf(other, wxT("DATETIME(y, m, d)"), ibQueryScalarFnText(ibQueryScalarFn::DateTime) + wxT("(, , )"));
	leaf(other, Kw(ibQueryKeyword::True),  Kw(ibQueryKeyword::True));
	leaf(other, Kw(ibQueryKeyword::False), Kw(ibQueryKeyword::False));
	leaf(other, Kw(ibQueryKeyword::Null),  Kw(ibQueryKeyword::Null));
	// A PARAMETER is how a value reaches a query from outside — the one piece of syntax a person
	// writing a condition needs and cannot find among the fields.
	leaf(other, wxT("&") + wxString(_("Parameter")), wxT("&"));

	m_language->Expand(functions);
	m_language->Expand(operators);
}

void ibDialogQueryExpression::InsertAtCaret(const wxString& text)
{
	if (m_readOnly || text.IsEmpty())
		return;
	m_text->ReplaceSelection(text);   // replaces the selection, leaves the caret after it
	m_text->SetFocus();
}

void ibDialogQueryExpression::OnFieldExpanding(wxTreeEvent& event)
{
	event.Skip();
	ibQueryExpandFieldNode(m_fieldTree, event.GetItem(), m_model, m_fieldIcon);
}

void ibDialogQueryExpression::OnFieldDrag(wxTreeEvent& event)
{
	const ibQueryTreeNode* node = ibQueryNodeOf(m_fieldTree, event.GetItem());
	if (node != nullptr)
		ibQueryBeginTextDrag(m_fieldTree, event.GetItem(), node->m_field);
}

// AND THE LANGUAGE PALETTE DRAGS TOO — dropping `SUM()` or the whole `CASE WHEN … END` skeleton
// writes exactly what a double-click would, at the point it was dropped. A tree DOES raise a
// begin-drag, and it must NOT be Allow()ed: we run the drag ourselves (see queryConstructor.cpp,
// where the same trap produced an assert out of wxDragImage).
void ibDialogQueryExpression::OnLanguageDrag(wxTreeEvent& event)
{
	const ibLanguageLeaf* leaf = dynamic_cast<ibLanguageLeaf*>(m_language->GetItemData(event.GetItem()));
	if (leaf != nullptr)   // a group row carries nothing to insert
		ibQueryBeginTextDrag(m_language, event.GetItem(), leaf->m_text);
}

void ibDialogQueryExpression::OnInsertField(wxTreeEvent& event)
{
	const ibQueryTreeNode* node = ibQueryNodeOf(m_fieldTree, event.GetItem());
	if (node == nullptr)
		return;
	// The TECHNICAL path - the synonym is what a person reads, never what a query writes.
	InsertAtCaret(node->m_field);
}

void ibDialogQueryExpression::OnInsertLanguage(wxTreeEvent& event)
{
	const ibLanguageLeaf* leaf = dynamic_cast<ibLanguageLeaf*>(m_language->GetItemData(event.GetItem()));
	if (leaf == nullptr)
		return;   // a group row
	InsertAtCaret(leaf->m_text);
}

void ibDialogQueryExpression::OnNestedQuery(wxCommandEvent&)
{
	if (m_readOnly)
		return;

	ibQuerySelectPtr inner = std::make_shared<ibQuerySelect>();
	inner->m_selectAll = true;
	if (!ibShowQueryConstructorFor(this, inner, m_metaData, /*readOnly*/ false))
		return;

	// Wrapped in parentheses because that is where a query goes inside an expression — after IN,
	// as a scalar. Written out rather than assembled: the parser reads it back either way.
	InsertAtCaret(wxT("(") + ibRenderQuery(*inner) + wxT(")"));
}

// EDIT THE SUBQUERY THAT IS ALREADY THERE. Creating `IN (SELECT …)` was the easy half; going back
// INTO it was impossible except as text, which is what "I want to open the constructor on exactly
// this piece" means. The selection is a span in this control, so it knows where it goes back —
// which is the fact that makes this editable rather than a window for looking.
void ibDialogQueryExpression::OnEditSelection(wxCommandEvent&)
{
	if (m_readOnly)
		return;

	int from = m_text->GetSelectionStart();
	int to   = m_text->GetSelectionEnd();
	// NO SELECTION = THE WHOLE TEXT. An expression that IS a query (the common case after "Nested
	// query") should not have to be selected to be opened.
	if (from == to) { from = 0; to = m_text->GetLastPosition(); }

	wxString fragment = m_text->GetRange(from, to);
	fragment.Trim(true).Trim(false);
	if (fragment.IsEmpty()) {
		wxMessageBox(_("Select the subquery to open, or write one with \"Nested query\"."),
			GetTitle(), wxOK | wxICON_INFORMATION, this);
		return;
	}

	// `(SELECT …)` is how a subquery is written inside an expression — unwrapped for the parser and
	// re-wrapped on the way back, so the text around it stays valid.
	const bool parenthesised = fragment.StartsWith(wxT("(")) && fragment.EndsWith(wxT(")"));
	if (parenthesised)
		fragment = fragment.Mid(1, fragment.length() - 2).Trim(true).Trim(false);

	if (!ibShowQueryConstructor(this, fragment, m_metaData, /*readOnly*/ false))
		return;

	m_text->SetSelection(from, to);
	m_text->ReplaceSelection(parenthesised ? wxT("(") + fragment + wxT(")") : fragment);
	m_text->SetFocus();
}

wxString ibDialogQueryExpression::GetText() const
{
	return m_text->GetText();
}

void ibDialogQueryExpression::SetText(const wxString& text)
{
	m_text->SetText(text);
	ibMarkQueryParameters(m_text);
	m_text->GotoPos(m_text->GetLastPosition());   // the caret lands where the writing continues
	m_text->SetFocus();
}

void ibDialogQueryExpression::OnOk(wxCommandEvent& event)
{
	wxString text = m_text->GetText();
	text.Trim(true).Trim(false);
	if (text.IsEmpty()) {
		// EMPTY IS AN ANSWER for some callers (a join with no condition joins by reference), so it
		// is accepted and handed back as a null expression rather than refused here.
		m_expression = nullptr;
		event.Skip();
		EndModal(wxID_OK);
		return;
	}

	try {
		ibQueryParser parser;
		m_expression = parser.ParseExpression(text);
	}
	catch (const ibBackendException& e) {
		wxMessageBox(e.GetErrorDescription(), GetTitle(), wxOK | wxICON_ERROR, this);
		return;
	}

	// …AND THEN WHETHER THE WORDS IN IT EXIST. Parsing only says the sentence is well formed; the
	// mistake people make here is a NAME — a field typed from memory, a type whose kind is spelled
	// wrong, a period that is not one of the nine. Said now, beside the text, it is a sentence about
	// what to fix; found later by the engine it is a refusal in a query this window never showed.
	const wxString complaint = CheckExpressionNames(m_expression);
	if (!complaint.IsEmpty()) {
		wxMessageBox(complaint, GetTitle(), wxOK | wxICON_ERROR, this);
		return;
	}

	EndModal(wxID_OK);
}

// The walk itself. Only what this window can answer for is checked — the fields it was handed and
// the closed vocabularies of the language. A type name (`Catalog.Goods` after REFS / CAST / TYPE) is
// checked for SHAPE only: whether that catalog exists is the configuration's answer, and the engine
// gives it with the source span when the query is stored.
wxString ibDialogQueryExpression::CheckExpressionNames(const ibQueryAstExprPtr& expression) const
{
	if (!expression)
		return wxEmptyString;

	wxString complaint;

	// A field is known when the whole dotted path starts at something this query offers: a field's
	// own name, or a source's name followed by one of its fields. Anything DEEPER is a dot-walk into
	// a reference, which this window does not resolve — the engine does, and it knows the targets.
	auto knownFirstSegment = [&](const wxString& name) {
		for (const ibQueryConstructorField& f : m_fields)
			if (f.m_name.CmpNoCase(name) == 0 || f.m_source.CmpNoCase(name) == 0)
				return true;
		return false;
	};

	std::function<void(const ibQueryAstExpr&)> walk = [&](const ibQueryAstExpr& e) {
		if (!complaint.IsEmpty())
			return;   // one complaint at a time: the first unknown word is the one to fix

		switch (e.m_kind) {
		case ibQueryAstExprKind::Column:
			if (!e.m_path.empty() && !knownFirstSegment(e.m_path.front())) {
				wxString offered;
				int shown = 0;
				for (const ibQueryConstructorField& f : m_fields) {
					if (shown++ >= 6) { offered += wxT(", …"); break; }
					offered += (offered.IsEmpty() ? wxString() : wxT(", ")) + f.m_name;
				}
				complaint = wxString::Format(
					_("'%s' is not a field of this query. It offers: %s"), e.m_path.front(), offered);
			}
			break;

		case ibQueryAstExprKind::ScalarCall: {
			// The period word of a calendar call — the one closed vocabulary a person gets wrong,
			// and the one this window can settle without asking anybody.
			size_t unitAt = 0;
			if (ibQueryScalarFnUnitArg(e.m_scalar, unitAt) && unitAt < e.m_args.size() && e.m_args[unitAt]) {
				const ibQueryAstExpr& unit = *e.m_args[unitAt];
				ibTotalsPeriod parsed = ibTotalsPeriod::Month;
				const bool named = unit.m_kind == ibQueryAstExprKind::Column && unit.m_path.size() == 1;
				if (!named || !ibReadPeriodUnit(unit.m_path.front(), parsed)) {
					wxString words;
					for (const std::pair<ibTotalsPeriod, wxString>& u : ibPeriodUnits())
						words += (words.IsEmpty() ? wxString() : wxT(", ")) + u.second;
					complaint = wxString::Format(
						_("%s takes a period as its argument %u. Write one of: %s"),
						ibQueryScalarFnText(e.m_scalar), static_cast<unsigned>(unitAt + 1), words);
					return;
				}
			}
			for (size_t i = 0; i < e.m_args.size(); ++i)
				if (e.m_args[i] && !(ibQueryScalarFnUnitArg(e.m_scalar, unitAt) && i == unitAt))
					walk(*e.m_args[i]);
			break;
		}

		// A TYPE is a kind and a name — `Catalog.Goods`. One bare word is the mistake worth catching
		// here, because it reads like a field and is not one.
		case ibQueryAstExprKind::Refs:
			if (e.m_path.size() < 2)
				complaint = _("REFS takes a type: <Kind>.<Name>, for instance Document.Expense");
			else if (e.m_lhs)
				walk(*e.m_lhs);
			break;

		case ibQueryAstExprKind::Cast:
			if (e.m_path.size() < 2)
				complaint = _("CAST narrows to a type: CAST(<field> AS <Kind>.<Name>)");
			else if (e.m_arg)
				walk(*e.m_arg);
			break;

		default:
			// Everything else is structure: walk whatever it holds.
			if (e.m_lhs)  walk(*e.m_lhs);
			if (e.m_rhs)  walk(*e.m_rhs);
			if (e.m_arg)  walk(*e.m_arg);
			if (e.m_low)  walk(*e.m_low);
			if (e.m_high) walk(*e.m_high);
			if (e.m_else) walk(*e.m_else);
			for (const auto& wt : e.m_cases) {
				if (wt.first)  walk(*wt.first);
				if (wt.second) walk(*wt.second);
			}
			for (const ibQueryAstExprPtr& item : e.m_list)
				if (item) walk(*item);
			for (const ibQueryAstExprPtr& item : e.m_args)
				if (item) walk(*item);
			break;
		}
	};

	walk(*expression);
	return complaint;
}

// THE CHOICE, AS THE LIST IT IS. Same gesture as "Query constructor" beside it: the SELECTION (or
// the whole text when nothing is selected) is opened, rearranged, and written back where it was.
//
// Opening something that is not a CASE is not refused — the builder simply starts empty, because
// "turn this into a choice" is a reasonable thing to mean and the text is only replaced on OK.
void ibDialogQueryExpression::OnEditChoice(wxCommandEvent&)
{
	if (m_readOnly)
		return;

	int from = m_text->GetSelectionStart();
	int to   = m_text->GetSelectionEnd();
	if (from == to) { from = 0; to = m_text->GetLastPosition(); }

	wxString fragment = m_text->GetRange(from, to);
	fragment.Trim(true).Trim(false);

	// READ IT IF IT READS. A fragment that does not parse is not an error here: it means there is
	// nothing to open ON, and the builder starts from nothing.
	ibQueryAstExprPtr existing;
	if (!fragment.IsEmpty()) {
		try {
			ibQueryParser parser;
			existing = parser.ParseExpression(fragment);
		}
		catch (const ibBackendException&) {
			existing = nullptr;
		}
	}

	if (!ibShowQueryCaseBuilder(this, m_fields, existing, m_metaData, m_readOnly))
		return;
	if (!existing)
		return;

	m_text->SetSelection(from, to);
	m_text->ReplaceSelection(ibRenderQueryExpr(*existing));
	m_text->SetFocus();
}
