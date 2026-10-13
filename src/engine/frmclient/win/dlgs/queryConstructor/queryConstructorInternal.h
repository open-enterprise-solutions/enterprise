#ifndef _FRMCLIENT_QUERY_CONSTRUCTOR_INTERNAL_H__
#define _FRMCLIENT_QUERY_CONSTRUCTOR_INTERNAL_H__

// THE CONSTRUCTOR'S OWN INTERIOR — the desktop's (frontend/win/dlgs/queryConstructor/queryConstructorInternal.h): its
// grids, its models, its parser and renderer — the designer's. What a client's window takes of it is the expression
// cell, a cell of wx and nothing else, and it is copied here whole.

#include "frmclient/win/ctrls/dataview/dataview.h"

#include <wx/combobox.h>
#include <wx/button.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/settings.h>

#include <functional>

namespace queryctor {

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
	// ⭐ IS THIS ROW WRITTEN BY HAND? Asked per row, because "arbitrary" is a switch on the ROW and
	// not on the column. Null = every row is free, which is what the links and the totals want.
	using Freeform = std::function<bool()>;

	ibExpressionCellRenderer(Choices choices, Expand expand, ibDataViewCellMode mode,
	                         Freeform freeform = nullptr)
		: ibDataViewCustomRenderer(wxT("string"), mode, wxALIGN_LEFT)
		, m_choices(std::move(choices)), m_expand(std::move(expand))
		, m_freeform(std::move(freeform)) {}

	bool HasEditorCtrl() const override { return true; }

	// A CLICK HERE IS FOR THE EDITOR — the button is the whole point of the cell.
	bool EditOnSingleClick() const override { return true; }

	wxWindow* CreateEditorCtrl(wxWindow* parent, wxRect rect, const wxVariant& value) override
	{
		wxPanel* host = new wxPanel(parent, wxID_ANY, rect.GetTopLeft(), rect.GetSize());
		wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);

		// ⭐ TWO SHAPES, ONE CELL — and the ROW decides which. Free, it is an editable box with the
		// "..." beside it: the author writes the condition. Not free, it is a CLOSED list of what the
		// engine can build over this query's fields, and neither typing nor the editor is offered —
		// because a row that says it is not arbitrary and then lets anything be typed into it is a
		// switch that does not switch anything.
		const bool freeform = !m_freeform || m_freeform();

		wxArrayString words = m_choices ? m_choices() : wxArrayString();

		// ⚠⚠ WHAT THE ROW ALREADY HOLDS IS ALWAYS IN THE LIST — the same rule ibRowChoiceRenderer
		// above states, and leaving it out of this cell is what made conditions disappear.
		//
		// A CLOSED list (the switch cleared) cannot show a string that is not one of its entries: the
		// selection silently fails, the box comes up EMPTY, and closing it writes that empty back. An
		// empty condition means "delete this row" to the model — so a condition the author never
		// touched vanished simply because the engine's offer did not happen to contain it word for
		// word. A cell never loses what it holds.
		const wxString held = value.GetString();
		if (!held.IsEmpty() && words.Index(held, false) == wxNOT_FOUND)
			words.Insert(held, 0);

		wxComboBox* combo = new wxComboBox(host, wxID_ANY, held,
			wxDefaultPosition, wxDefaultSize, words, freeform ? 0 : wxCB_READONLY);
		if (!freeform)
			combo->SetStringSelection(held);   // a read-only box shows its SELECTION, not its text
		row->Add(combo, 1, wxEXPAND);

		// THE DOOR OUT OF THE LIST, right next to it. Square and narrow: it is a way through, not a
		// verb of its own.
		if (freeform) {
			wxButton* more = new wxButton(host, wxID_ANY, wxT("..."), wxDefaultPosition,
				host->FromDIP(wxSize(24, 20)));
			more->Bind(wxEVT_BUTTON, [this, combo](wxCommandEvent&) {
				wxString text = combo->GetValue();
				if (m_expand && m_expand(text))
					combo->SetValue(text);
			});
			row->Add(more, 0, wxEXPAND);
		}

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
				// ⚠ A CLOSED BOX WITH NOTHING SELECTED WRITES NOTHING. Refusing here is the second
				// half of the guard above: even if a list somehow arrives without the held value in
				// it, the cell declines to answer rather than answering "empty" — which downstream
				// reads as "delete this row". Losing work must not be reachable by accident.
				if (combo->HasFlag(wxCB_READONLY) && combo->GetSelection() == wxNOT_FOUND)
					return false;
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
	Freeform m_freeform;
	wxString m_text;
};

} // namespace queryctor

#endif
