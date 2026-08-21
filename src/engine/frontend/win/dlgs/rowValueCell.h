#ifndef __ROW_VALUE_CELL_H__
#define __ROW_VALUE_CELL_H__

////////////////////////////////////////////////////////////////////////////
// THE ROW-VALUE CELL — the one cell every settings grid edits its lines through.
////////////////////////////////////////////////////////////////////////////
//
// A sort line has a FIELD and a DIRECTION; a grouping line has a FIELD and a KIND; a
// composition's output level has both as well. All of those are values of REGISTERED
// types (a composition field, a SortDirection, a grouping kind), so the cell does not
// know what it is editing: it asks the row for the value, and the VALUE decides how it
// is chosen — a field through the source-tree picker, an enumeration through the
// runtime's own quick choice. Adding a column therefore costs a getter and a setter,
// not a cell.
//
// ⭐ WHY IT LIVES HERE and not inside the list settings any more (2026-08-19). The
// composition's output tree edits exactly these two things, and a second, simpler cell
// beside this one would be a second set of rules about the same values — the shape the
// filter editor was already split apart to avoid. What the two windows share is the
// MACHINERY, not the layout: the picker itself stays with whoever knows the source, and
// arrives here as a callback.
//
// Drawn by the table's own renderer, so a value reads here exactly as in a list.
////////////////////////////////////////////////////////////////////////////

#include <functional>
#include <utility>

#include "backend/tabularModelView.h"                       // ibDataViewItem
#include "backend/composition/compositionField.h"           // ibValueCompositionField — the one value with a picker
#include "frontend/win/ctrls/controlTextEditor.h"
#include "frontend/visualView/ctrl/typeControl.h"           // ibTypeControlFactory::QuickChoice
#include "frontend/visualView/ctrl/frame.h"                 // ibControlFrame
#include "frontend/visualView/ctrl/tableBoxColumnRenderer.h" // ibDataViewValueRenderer — the TABLE's own renderer

class ibRowValueCellRenderer : public ibDataViewValueRenderer, public ibControlFrame {
public:
	using Getter = std::function<ibValue(const ibDataViewItem&)>;
	using Setter = std::function<void(const ibDataViewItem&, const ibValue&)>;
	// WHO KNOWS THE SOURCE opens the field picker. The cell does not: which fields exist is
	// a question about the thing being configured, and the window that owns it answers.
	// `held` — the path the line already carries, so the picker opens standing on it.
	using FieldChooser = std::function<ibValueCompositionField*(wxWindow* parent, const wxString& held)>;

	// ⭐ …OR THE ROW OPENS SOMETHING OF ITS OWN. A cell that stands for ONE value is edited by
	// choosing that value; a cell that stands for a WHOLE NODE — a grouping is a LIST of fields —
	// has to open the window that edits the node. Set this and the "…" calls it instead of the
	// picker: same button, and it means the same thing (open what edits this cell), which is why it
	// is one button and not two.
	using Expand = std::function<void(const ibDataViewItem& row)>;

	ibRowValueCellRenderer(wxWindow* host, FieldChooser chooser, Getter get, Setter set)
		: ibDataViewValueRenderer(nullptr), m_host(host), m_chooser(std::move(chooser)),
		  m_get(std::move(get)), m_set(std::move(set)) {
	}

	void SetExpand(Expand expand) { m_expand = std::move(expand); }

	virtual bool HasEditorCtrl() const override { return true; }

	// A CLICK HERE IS FOR THE EDITOR — the button is the whole point of the cell.
	bool EditOnSingleClick() const override { return true; }

	// The control side of the cell — the quick choice reads and writes through these.
	virtual bool GetControlValue(ibValue& out) const override {
		if (!m_row.IsOk() || !m_get)
			return false;
		out = m_get(m_row);
		return true;
	}
	virtual bool SetControlValue(const ibValue& value = ibValue()) override {
		if (!m_row.IsOk() || !m_set)
			return false;
		m_set(m_row, value);
		return true;
	}
	virtual bool HasQuickChoice() const override { return true; }
	virtual void ChoiceProcessing(ibValue& chosen) override {
		SetControlValue(chosen);
		FinishSelecting();
	}
	virtual void ControlIncrRef() override {}
	virtual void ControlDecrRef() override {}

	// THE INHERITED ONE IS FOR A TABLE COLUMN. It unbinds the column's handlers and
	// asserts when there is no column (this cell has none — it belongs to a dialog,
	// not to a tablebox), which crashed the moment the editor lost focus. Nothing to
	// read back here anyway: the value was written when it was CHOSEN.
	virtual bool GetValueFromEditorCtrl(wxWindow*, wxVariant&) override { return false; }

	virtual wxWindow* CreateEditorCtrl(wxWindow* dv, wxRect labelRect, const wxVariant& value) override {
		m_row = m_item;   // the row this editor belongs to — never the grid's selection

		ibControlTextEditor* editor = new ibControlTextEditor;
		editor->SetDVCMode(true);
		editor->Show(false);
		if (!editor->Create(dv, wxID_ANY, value, labelRect.GetPosition(), labelRect.GetSize()))
			return nullptr;

		editor->ShowSelectButton(true);
		editor->ShowClearButton(true);
		editor->ShowOpenButton(false);
		// NOT SetTextEditMode(false): that flag is the read-only POLICY — it locks the
		// Select and Clear buttons along with the text (controlTextEditor.h says so).
		// Using it to stop typing into a field is what disabled "…" and "×" on every
		// tab at once. Typing is harmless here because the cell only accepts values
		// it was GIVEN — the text is never read back as a field.
		editor->SetTextEditMode(true);
		editor->Bind(wxEVT_CONTROL_BUTTON_SELECT, &ibRowValueCellRenderer::OnSelect, this);
		editor->Bind(wxEVT_CONTROL_BUTTON_CLEAR, &ibRowValueCellRenderer::OnClear, this);
		editor->LayoutControls();
		editor->Show(true);
		return editor;
	}

private:
	// THE VALUE DECIDES. A field opens the source tree (the host is the only thing that
	// knows the source); anything else is a registered type and goes through the runtime's
	// own quick choice, which is what makes a direction and a grouping kind editable
	// without a line of list-building here.
	void OnSelect(wxCommandEvent&) {
		// THE ROW'S OWN WINDOW, when it has one — closing the editor first for the same reason the
		// picker does: a modal opened over a live cell editor never sees the clicks meant for it.
		if (m_expand) {
			const ibDataViewItem row = m_row;
			FinishSelecting();
			m_expand(row);
			return;
		}

		ibValue current; GetControlValue(current);
		ibValueCompositionField* asField = nullptr;
		if (current.ConvertToValue(asField) || current.IsEmpty()) {
			// CLOSE THE CELL EDITOR FIRST. The picker is a modal window opened ON TOP
			// of a live editor that holds the mouse and the focus — with it up, the
			// picker's tree never sees the clicks that would expand a reference, so
			// the tree looked like it "could not unfold". The row was captured when
			// the editor was created, so nothing is lost by closing it now.
			const ibDataViewItem row = m_row;
			wxWindow* parent = m_host;
			FinishSelecting();
			if (!m_chooser)
				return;
			const wxString held = (asField != nullptr) ? asField->GetPath() : wxString();
			if (ibValueCompositionField* chosen = m_chooser(parent, held)) {
				m_row = row;
				SetControlValue(ibValue(chosen));
			}
			return;
		}
		ibTypeControlFactory::QuickChoice(this, current.GetClassType(), GetEditorCtrl());
	}

	void OnClear(wxCommandEvent&) {
		SetControlValue();
		FinishSelecting();
	}

	wxWindow*      m_host;      // the window a modal picker is parented to
	FieldChooser   m_chooser;
	Getter         m_get;
	Setter         m_set;
	Expand         m_expand;    // set = the "…" opens the ROW's own window instead of the picker
	ibDataViewItem m_row;
};

#endif // __ROW_VALUE_CELL_H__
