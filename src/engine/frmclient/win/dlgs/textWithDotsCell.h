#ifndef __TEXT_WITH_DOTS_CELL_H__
#define __TEXT_WITH_DOTS_CELL_H__

////////////////////////////////////////////////////////////////////////////
// A PLAIN TEXT CELL WITH A "..." — no drop-down.
////////////////////////////////////////////////////////////////////////////
//
// ⭐ The expression cell used the query constructor's, which is a COMBO: it exists there because a
// totals expression is nearly always one of the ready calls, and the list is the point. A parameter
// expression has no such list — the offered items were just the text already in the cell, so the
// arrow opened a menu of one (Max: "get rid of the combobox there"). What is wanted is the ordinary
// value cell: type in it, or press "..." for room to write.
//
// Here and not inside the composer's settings since the appearance window edits its values through it
// too (2026-09-29). `typed` false is the cell a person does not type into — what it shows is a summary
// of what the "..." edits (a field's appearance).
////////////////////////////////////////////////////////////////////////////

#include <functional>
#include <utility>

#include "frmclient/win/ctrls/controlTextEditor.h"
#include "frmclient/visualView/ctrl/frame.h"                 // ibControlFrame
#include "frmclient/visualView/ctrl/tableBoxColumnRenderer.h" // ibDataViewValueRenderer — the TABLE's own renderer

class ibTextWithDotsRenderer : public ibDataViewValueRenderer, public ibControlFrame {
public:
	using Expand = std::function<bool(wxString& text)>;

	ibTextWithDotsRenderer(wxWindow* host, Expand expand, bool typed = true)
		: ibDataViewValueRenderer(nullptr), m_host(host), m_expand(std::move(expand)), m_typed(typed) {
	}

	virtual bool HasEditorCtrl() const override { return true; }
	bool EditOnSingleClick() const override { return true; }

	virtual wxWindow* CreateEditorCtrl(wxWindow* dv, wxRect labelRect, const wxVariant& value) override {
		m_text = value.GetString();

		ibControlTextEditor* editor = new ibControlTextEditor;
		editor->SetDVCMode(true);
		editor->Show(false);
		if (!editor->Create(dv, wxID_ANY, value, labelRect.GetPosition(), labelRect.GetSize()))
			return nullptr;

		editor->ShowSelectButton(true);    // the "..." — room to write what does not fit
		editor->ShowClearButton(true);
		editor->ShowOpenButton(false);
		editor->SetTextEditMode(m_typed);  // typing straight into the cell is the ordinary case
		// …and a cell that is NOT typed into is still edited through its "..." and cleared with its "×".
		// SetTextEditMode(false) is the control's READ-ONLY switch and greys both (2026-09-29: the appearance
		// cell's "..." did nothing), so they are given back here.
		if (!m_typed) {
			editor->EnableSelectButton(true);
			editor->EnableClearButton(true);
		}
		editor->Bind(wxEVT_CONTROL_BUTTON_SELECT, &ibTextWithDotsRenderer::OnExpand, this);
		editor->Bind(wxEVT_CONTROL_BUTTON_CLEAR, &ibTextWithDotsRenderer::OnClear, this);
		editor->LayoutControls();
		editor->Show(true);
		return editor;
	}

	// WHAT THE CELL COMMITS is whatever the box holds — typed or written in the dialog. With the box
	// already gone (the dialog's own closing takes the editor with it), what it last held is what
	// this renderer kept, so the written text is not lost with the window that wrote it.
	virtual bool GetValueFromEditorCtrl(wxWindow* editor, wxVariant& value) override {
		if (ibControlTextEditor* box = dynamic_cast<ibControlTextEditor*>(editor)) {
			m_text = box->GetValue();
			value  = m_text;
			return true;
		}
		value = m_text;
		return true;
	}


	// NO QUICK CHOICE HERE — the cell holds TEXT (an expression, a type description), and the "..."
	// is what opens the real editor for it. Saying so is what keeps the runtime from offering a
	// value picker over a piece of code.
	virtual bool HasQuickChoice() const override { return false; }
	virtual void ChoiceProcessing(ibValue&) override {}
	virtual void ControlIncrRef() override {}
	virtual void ControlDecrRef() override {}

private:
	// ⚠⚠ THE BOX MAY NOT SURVIVE THE DIALOG. `m_expand` opens a MODAL window on top of a live cell
	// editor; the editor loses focus, the grid closes it, and the pointer read before the call is
	// then a dead object — writing the result back through it is a use-after-free (two crash dumps,
	// 2026-08-21, both landing on this line).
	//
	// So the editor is asked for AGAIN afterwards, and the text is kept here as well: the cell
	// commits `m_text` when the editor is already gone, which is what makes the dialog's result
	// survive its own window closing. (The value cell beside this one learnt the same lesson from
	// the other side — it closes the editor itself before opening a picker.)
	void OnExpand(wxCommandEvent&) {
		ibControlTextEditor* box = dynamic_cast<ibControlTextEditor*>(GetEditorCtrl());
		wxString text = box != nullptr ? box->GetValue() : m_text;
		if (!m_expand || !m_expand(text))
			return;

		m_text = text;   // what the cell commits, whether or not the box is still there
		if (ibControlTextEditor* alive = dynamic_cast<ibControlTextEditor*>(GetEditorCtrl()))
			alive->SetValue(text);
	}
	void OnClear(wxCommandEvent&) {
		if (ibControlTextEditor* box = dynamic_cast<ibControlTextEditor*>(GetEditorCtrl()))
			box->SetValue(wxEmptyString);
	}

	wxWindow* m_host;
	Expand    m_expand;
	bool      m_typed;
	wxString  m_text;
};

#endif
