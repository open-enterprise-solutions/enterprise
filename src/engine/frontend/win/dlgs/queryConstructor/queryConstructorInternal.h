#ifndef __QUERY_CONSTRUCTOR_INTERNAL_H__
#define __QUERY_CONSTRUCTOR_INTERNAL_H__

////////////////////////////////////////////////////////////////////////////
// The constructor's OWN INTERIOR - shared by its four translation units.
////////////////////////////////////////////////////////////////////////////
//
// queryConstructor.cpp grew past four thousand lines, which is not a size problem so much as a
// READING problem: building a page, filling it from the AST, and acting on what the user does to
// it are three different jobs, and reading any one of them meant scrolling past the other two.
// So the window is now four files that answer four questions:
//
//   queryConstructor.cpp      WHAT IS THERE  - the window, its tabs, its grids and trees
//   queryConstructorFill.cpp  WHAT IT SHOWS  - AST -> tabs, one Fill per pane, plus which
//                                              statement/branch is currently being shown
//   queryConstructorEdit.cpp  WHAT IT DOES   - every verb: add, remove, move, rename, edit
//   queryConstructorText.cpp  WHAT IT IS     - the text pane, the parse gate, OK, and the two
//                                              entry points the rest of the product opens it by
//
// The class is unchanged; this is where the file-local part of it lives so all four can see it.
// The include list is deliberately the WHOLE one rather than a per-file minimum: these four files
// are one window cut in four, and a header per cut would be four lists to keep in step.
//
////////////////////////////////////////////////////////////////////////////

#include "queryConstructor.h"
#include "queryJoinDiagram.h"
#include "queryLinkModel.h"
#include "queryConditionModel.h"
#include "queryUnionModel.h"
#include "queryGridModel.h"
#include "queryExpressionDialog.h"

#include "backend/query/queryParser.h"
#include "backend/query/queryRender.h"
#include "backend/query/queryRewrite.h"    // Clone — the engine's own deep copy of a select
#include "backend/query/queryLowering.h"   // CheckNames — the engine resolves the names, we only ask
#include "backend/query/queryable.h"       // ibSourceMetaDataScope — WHICH config the names resolve against
#include "backend/query/queryKeywords.h"   // the ACTIVE keyword table — the one spelling of a keyword, and the highlighter's word set
#include "backend/backend_exception.h"
#include "backend/metaData.h"
#include "backend/backend_metatree.h"   // ibBackendMetadataTree::IsEditable — where "read-only" is already answered
#include "artProvider/artProvider.h"    // wxART_FRONTEND — the product's own pictures, asked for first

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/statbox.h>
#include <wx/button.h>
#include <wx/msgdlg.h>
#include <wx/choicdlg.h>
#include <wx/textdlg.h>   // the totals level's name — a prompt, because the cell would not open   // wxGetSingleChoiceIndex — the fallback when no field is picked in a tree
#include <wx/panel.h>
#include <wx/settings.h>
#include <wx/splitter.h>
#include <wx/toolbar.h>
#include <wx/artprov.h>
#include <wx/dnd.h>
#include <wx/wupdlock.h>
#include <wx/stc/stc.h>

#include "../callbackDropTarget.h"   // the same-process drag: a drop is a notification, the source knows what moved
#include "queryFieldTree.h"   // the field row: its node, its walk, its drag — shared with the expression editor

#include "mainFrame/settings/fontcolorsettings.h"   // the engine's own font + colours, so this pane matches the code editor

#include <algorithm>
#include <functional>
#include <map>

// The window's own small vocabulary - names it uses everywhere and nobody else needs.
namespace queryctor {


// (The unfold is shown in the LANGUAGE'S own words now — ibQueryKeywordText(Elements / Hierarchy /
// HierarchyOnly) — so the totals grid and the query text cannot call the same thing two names.)

// A source as a person reads it: the nested case says so instead of showing nothing, because a
// blank line in the table list is the one thing that makes a query unreadable.
// THE NAME A CHOSEN TABLE IS KNOWN BY — its alias when it has one, else the LAST segment of its
// path. Short on purpose: `Catalog.Products` is where it came FROM, and that belongs in the
// catalogue on the left; in the query it is `Products`, which is what every field of it is
// qualified with and what a rename replaces. Writing the whole dotted path here said the same
// thing twice and left no room for the part that varies.
// (It is ibQuerySourceLabel — this header held a byte-identical copy of what the constructor model
//  held on the other side of the DLL boundary. Both ask now.)


// An aggregate projection is one whose expression IS an aggregate call — the Grouping tab's
// second list is that subset of the projections, not a list of its own.
inline bool IsAggregateProjection(const ibQueryProjection& projection)
{
	return projection.m_expr && projection.m_expr->m_kind == ibQueryAstExprKind::Func;
}

// ARROWS BUILT FROM CODE POINTS, never written as literals in this source. The file is UTF-8
// WITHOUT a BOM and MSVC then reads it in the system codepage — a literal ↑ came out as mojibake
// on the button, seen live 2026-08-06. A wxUniChar is codepage-proof.
inline wxString Glyph(int codePoint) { return wxString(wxUniChar(codePoint)); }

inline wxString ArrowRight() { return Glyph(0x203A); }   // the "forward" chevron
inline wxString ArrowLeft()  { return Glyph(0x2039); }   // the "back" chevron
// (No "field → alias" text any more: the alias has a COLUMN of its own where it is set — the
// Unions tab's field map — instead of being glued onto the end of a field's name.)

// A TEXT COLUMN, and whether it can be TYPED INTO. The fork's text renderer is INERT unless it is
// told otherwise — which is the whole reason the grids in this window opened as things to look at
// rather than things to edit. Made a helper so a new column cannot forget to ask.
inline ibDataViewColumn* TextColumn(const wxString& title, unsigned int col, int width, bool editable = false)
{
	return new ibDataViewColumn(title,
		new ibDataViewTextRenderer(ibDataViewTextRenderer::GetDefaultType(),
			editable ? wxDATAVIEW_CELL_EDITABLE : wxDATAVIEW_CELL_INERT),
		col, width, wxAlignment::wxALIGN_LEFT);
}

// A COLUMN THAT CARRIES THE FIELD PICTURE. The same picture the trees use, so a field reads as one
// thing whether it is being chosen on the left or listed on the right — which is what "everything
// should look the same" comes down to. INERT: these columns hold a PATH, and a path is changed in
// the expression editor, not typed over.
inline ibDataViewColumn* IconColumn(const wxString& title, unsigned int col, int width)
{
	return new ibDataViewColumn(title,
		new ibDataViewIconTextRenderer(ibDataViewIconTextRenderer::GetDefaultType(), wxDATAVIEW_CELL_INERT),
		col, width, wxAlignment::wxALIGN_LEFT);
}

// A CELL OVER A CLOSED SET IS A CHOICE, NOT A TYPED WORD. The unfold, the sort direction and the
// aggregate function are each a fixed set the LANGUAGE already names — so the cell offers exactly
// those words and cannot hold anything else. Typed text there would be a second, weaker spelling of
// an enumeration that exists, and the first misspelling would reach the parser as a syntax error
// about something the user was never free to choose.
//
// The words come from `ibQueryKeywordText`, so the list is the keyword table's, not a copy of it.
// ⭐ A CHOICE WHOSE LIST DEPENDS ON THE ROW. `SUM` over a string is not a mistake to be caught after
// the fact, it is a choice that should never have been offered — and which choices fit depends on
// the field THIS row aggregates, so the list cannot be fixed when the column is made.
//
// The words come from the ENGINE (ibQueryLowering::AggregatesFor), which is the same answer
// CheckNames reads as a refusal. Offering what the engine would reject is how a window teaches
// people to distrust it.
class ibRowChoiceRenderer : public ibDataViewChoiceRenderer
{
public:
	using Choices = std::function<wxArrayString()>;

	explicit ibRowChoiceRenderer(Choices choices, ibDataViewCellMode mode = wxDATAVIEW_CELL_EDITABLE)
		: ibDataViewChoiceRenderer(wxArrayString(), mode), m_choices(std::move(choices)) {}

	wxWindow* CreateEditorCtrl(wxWindow* parent, wxRect rect, const wxVariant& value) override
	{
		wxArrayString words = m_choices ? m_choices() : wxArrayString();

		// ⚠⚠ WHAT THE ROW ALREADY HOLDS IS ALWAYS IN THE LIST, even when the engine would not offer
		// it. The list is what FITS this row's field; the value is what the query SAYS. They differ
		// exactly when the query is wrong — `SUM` over a reference, where the offer is COUNT alone —
		// and that is the moment the author most needs to see what is written.
		//
		// Without this the cell opened EMPTY on a perfectly readable `SUM(Parent)`: the selection
		// silently failed to match, so the window hid the very thing the engine was complaining
		// about, and the complaint on the verdict line named a function nothing on screen showed.
		// A cell never loses what it holds; the refusal is the ENGINE's line to deliver, not a blank.
		const wxString held = value.GetString();
		if (!held.IsEmpty() && words.Index(held, false) == wxNOT_FOUND)
			words.Insert(held, 0);

		wxChoice* editor = new wxChoice(parent, wxID_ANY, rect.GetTopLeft(), rect.GetSize(), words);
		editor->SetStringSelection(held);
		return editor;
	}

	bool GetValueFromEditorCtrl(wxWindow* editor, wxVariant& value) override
	{
		wxChoice* choice = dynamic_cast<wxChoice*>(editor);
		if (choice == nullptr)
			return false;
		value = choice->GetStringSelection();
		return true;
	}

	// ⚠⚠ MEASURED BY THE VALUE, not by the list. The base measures the widest of its FIXED choices —
	// and this renderer has none, because its list is built per row. Constructed with an empty array
	// it reported a near-zero width, and the cell drew nothing at all: the Function and Expression
	// columns came up blank over perfectly good aggregates.
	wxSize GetSize() const override
	{
		wxVariant value;
		GetValue(value);
		const wxString text = value.GetString();
		wxSize size = text.IsEmpty() ? GetTextExtent(wxT("Wg")) : GetTextExtent(text);
		// Room for the drop-down button on the right, as the base allows for it.
		size.x += wxSystemSettings::GetMetric(wxSYS_VSCROLL_X);
		size.x += GetTextExtent(wxT("M")).x;
		return size;
	}

private:
	Choices m_choices;
};

// ⭐ AN EXPRESSION CELL: A LIST, A KEYBOARD AND A DOOR — all three, because an expression is not a
// closed set.
//
// The aggregate FUNCTION is a closed set and a plain dropdown is the whole of it (ibRowChoiceRenderer
// above). A totals EXPRESSION is not: `SUM(Qty)` is the common case and belongs in a list, but
// `SUM(CASE WHEN … END)` is a perfectly ordinary thing to want and no list can hold it. Offering only
// the list took the language away; offering only the text made the common case a typing exercise.
//
//   * the DROPDOWN — the ready calls over this row's field, filtered by what its type can be folded
//     by (the engine's own answer);
//   * the TEXT — editable, because it is an expression and the engine reads it either way;
//   * the "..." — the arbitrary-expression editor, with the fields, the palette and the parser.
//
// Nothing here decides for the author; it only makes the usual thing short.
class ibExpressionCellRenderer : public ibDataViewCustomRenderer
{
public:
	using Choices = std::function<wxArrayString()>;
	using Expand  = std::function<bool(wxString& text)>;   // "..." — true when it changed the text

	ibExpressionCellRenderer(Choices choices, Expand expand, ibDataViewCellMode mode)
		: ibDataViewCustomRenderer(wxT("string"), mode, wxALIGN_LEFT)
		, m_choices(std::move(choices)), m_expand(std::move(expand)) {}

	bool HasEditorCtrl() const override { return true; }

	wxWindow* CreateEditorCtrl(wxWindow* parent, wxRect rect, const wxVariant& value) override
	{
		wxPanel* host = new wxPanel(parent, wxID_ANY, rect.GetTopLeft(), rect.GetSize());
		wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);

		const wxArrayString words = m_choices ? m_choices() : wxArrayString();
		wxComboBox* combo = new wxComboBox(host, wxID_ANY, value.GetString(),
			wxDefaultPosition, wxDefaultSize, words);
		row->Add(combo, 1, wxEXPAND);

		// THE DOOR OUT OF THE LIST, right next to it. Square and narrow: it is a way through, not a
		// verb of its own.
		wxButton* more = new wxButton(host, wxID_ANY, wxT("..."), wxDefaultPosition,
			host->FromDIP(wxSize(24, 20)));
		more->Bind(wxEVT_BUTTON, [this, combo](wxCommandEvent&) {
			wxString text = combo->GetValue();
			if (m_expand && m_expand(text))
				combo->SetValue(text);
		});
		row->Add(more, 0, wxEXPAND);

		host->SetSizer(row);
		host->Layout();
		return host;
	}

	bool GetValueFromEditorCtrl(wxWindow* editor, wxVariant& value) override
	{
		if (editor == nullptr)
			return false;
		for (wxWindow* child : editor->GetChildren())
			if (wxComboBox* combo = dynamic_cast<wxComboBox*>(child)) {
				value = combo->GetValue();
				return true;
			}
		return false;
	}

	bool Render(wxRect rect, wxDC* dc, int state) override { RenderText(m_text, 0, rect, dc, state); return true; }
	bool SetValue(const wxVariant& value) override { m_text = value.GetString(); return true; }
	bool GetValue(wxVariant& value) const override { value = m_text; return true; }

	wxSize GetSize() const override
	{
		wxSize size = m_text.IsEmpty() ? GetTextExtent(wxT("Wg")) : GetTextExtent(m_text);
		size.x += wxSystemSettings::GetMetric(wxSYS_VSCROLL_X);
		return size;
	}

private:
	Choices  m_choices;
	Expand   m_expand;
	wxString m_text;
};

inline ibDataViewColumn* ChoiceColumn(const wxString& title, unsigned int col, int width,
                               const std::vector<ibQueryKeyword>& choices)
{
	wxArrayString words;
	for (ibQueryKeyword keyword : choices)
		words.Add(ibQueryKeywordText(keyword));
	return new ibDataViewColumn(title,
		new ibDataViewChoiceRenderer(words, wxDATAVIEW_CELL_EDITABLE),
		col, width, wxAlignment::wxALIGN_LEFT);
}

// EVERY LIST IN THIS WINDOW IS THE SAME CONTROL, made the same way. Six listboxes and two listctrls
// standing beside three dataview grids is what "the designer catches the eye" meant — different row
// heights, different grid lines, and no cell you could edit where it stood.
// ⚠ A DOUBLE-CLICK MUST OPEN THE CELL. The grid answers a double-click with ACTIVATE and nothing
// else, so an editable cell could only be opened with F2 or by clicking a selected row a second
// time — which is why every grid in this window felt like it "needed some number of clicks".
// `EditItem` on the activation is what listSettings already does, for the same reason.
inline void EditOnActivate(ibDataViewCtrl* grid)
{
	// ⚠⚠ AND THE HANDLER MUST NOT Skip(). This took four attempts, so the reason is written down.
	//
	// The control raises ITEM_ACTIVATED on a double-click and passes the CLICKED COLUMN with it
	// (datavgen.cpp, `le(wxEVT_DATAVIEW_ITEM_ACTIVATED, this, col, item)`) — so the column was never
	// the problem, and binding a mouse handler here was worse than useless: mouse events go to the
	// control's inner window and never reach this one at all.
	//
	// What killed it is the line right after:  `if (ProcessWindowEvent(le)) return;`
	// Skipping means "not handled", so the control carried on and treated the double-click as a
	// plain click — selecting the row and, in doing so, closing the editor we had just opened. From
	// outside that is exactly "the cell will not open".
	//
	// So: open the cell and OWN the event.
	grid->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, [grid](ibDataViewEvent& event) {
		ibDataViewColumn* column = event.GetDataViewColumn();
		if (column == nullptr)
			column = grid->GetCurrentColumn();
		if (column == nullptr || column->GetRenderer() == nullptr)
			return;
		if (column->GetRenderer()->GetMode() != wxDATAVIEW_CELL_EDITABLE) {
			event.Skip();   // an inert cell (a path, a derived name): let whoever else wants it have it
			return;
		}

		// ⚠⚠ AND IT IS OPENED ON THE NEXT TURN OF THE EVENT LOOP, not here. This is the part four
		// earlier attempts missed: the control is still INSIDE its own click handling when it
		// raises this event, and what follows that handling — the click it simulates, the rename
		// timer it starts with `m_currentCol` — tears down an editor opened underneath it. The
		// editor appears and vanishes in the same breath, which from outside is "the cell will not
		// open at all".
		//
		// CallAfter puts the edit after all of that, when the control is idle and nothing is left
		// to undo it.
		const ibDataViewItem item = event.GetItem();
		grid->Select(item);
		grid->CallAfter([grid, item, column] { grid->EditItem(item, column); });
	});
}

inline ibDataViewCtrl* MakeGrid(wxWindow* parent, ibQueryGridModel* model, std::function<void()> onChanged)
{
	ibDataViewCtrl* grid = new ibDataViewCtrl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxDV_ROW_LINES | wxDV_SINGLE);
	model->SetOnChanged(std::move(onChanged));
	grid->AssociateModel(model);
	EditOnActivate(grid);
	return grid;
}

} // namespace queryctor

#endif
