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
#include "backend/metaData.h"                               // ibMetaData — the configuration a type is read against
#include "backend/system/value/composition/valueComposerSettings.h"           // ibValueCompositionField — the one value with a picker
#include "backend/system/value/valueType.h"                 // ibValueTypeDescription::AdjustValue — text shaped by the declared type
#include "frontend/win/ctrls/controlTextEditor.h"
#include "frontend/visualView/ctrl/typeControl.h"           // ibTypeControlFactory::QuickChoice
#include "frontend/visualView/ctrl/frame.h"                 // ibControlFrame
#include "frontend/visualView/ctrl/tableBoxColumnRenderer.h" // ibDataViewValueRenderer — the TABLE's own renderer

class ibRowValueCellRenderer : public ibDataViewValueRenderer, public ibControlFrame,
	public ibTypeControlFactory {
public:
	using Getter = std::function<ibValue(const ibDataViewItem&)>;
	using Setter = std::function<void(const ibDataViewItem&, const ibValue&)>;
	// ⭐⭐ WHAT THIS ROW MAY HOLD, when the row knows it in advance. A sort line's value IS its type —
	// a direction, a grouping kind — so the cell can ask the value itself. A REPORT PARAMETER cannot:
	// it is usually EMPTY and its type is declared beside it, so a cell that asks the value gets
	// nothing and the "…" opens nothing (Max, 2026-08-28: "and how am I supposed to change it?").
	//
	// Given this, the cell answers the type question itself and the "…" walks the ONE route
	// (ibTypeControlFactory::ChooseValue): settle the type from what is declared, then choose a value
	// of it — which is also what puts the designer's predefined-value form under the same button.
	using TypeOf = std::function<ibTypeDescription(const ibDataViewItem&)>;
	// …AND THE CONFIGURATION THAT TYPE IS READ AGAINST. It travels WITH the type because it is the
	// same question: whoever declares "this row may hold Catalog.Goods" is the one who knows which
	// open configuration that name belongs to. There are always several — the base, the one being
	// compared, one loaded from a file — so a cell that reached for the ACTIVE one would quietly
	// resolve a name in somebody else's configuration.
	using MetaOf = std::function<const ibMetaData*()>;
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
	// …and where the declared type comes from, when the row has one — together with the configuration
	// it is read against. Unset = the value speaks for itself, which is how every caller before this
	// one works.
	void SetTypeSource(TypeOf typeOf, MetaOf metaOf) {
		m_typeOf = std::move(typeOf);
		m_metaOf = std::move(metaOf);
	}

	// --- ibTypeControlFactory — answered only where a type was declared -------------------------
	// No metadata COLUMN behind this cell: it edits a line of a settings description, not a form
	// attribute. The type below is what the gate actually consults.
	virtual const ibBackendSourceColumn* GetSourceAttributeObject() const override { return nullptr; }
	virtual ibSourceObject* GetSourceObject() const override { return nullptr; }
	virtual ibSourceDescription& GetSourceDesc() const override { return m_sourceDesc; }
	// ⚠ THE DOOR IS HANDED IN, NEVER REACHED FOR — see MetaOf above. A cell whose row declares
	// nothing has no type to resolve either, so nullptr is the honest answer there.
	virtual const ibMetaData* GetMetaData() const override { return m_metaOf ? m_metaOf() : nullptr; }
	virtual ibTypeDescription& GetTypeDesc() const override {
		m_typeDesc = (m_typeOf && m_row.IsOk()) ? m_typeOf(m_row) : ibTypeDescription();
		return m_typeDesc;
	}

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
	// The quick choice's own road out — a date picked in the calendar, an enumeration member picked in
	// the popup. It ends the editing itself, so the only thing missing was the repaint: without it the
	// cell keeps the text it was opened with until something else redraws the row.
	virtual void ChoiceProcessing(ibValue& chosen) override {
		SetControlValue(chosen);
		FinishSelecting();
		RedrawOwnerView();
	}
	virtual void ControlIncrRef() override {}
	virtual void ControlDecrRef() override {}

	// THE INHERITED ONE IS FOR A TABLE COLUMN. It unbinds the column's handlers and
	// asserts when there is no column (this cell has none — it belongs to a dialog,
	// not to a tablebox), which crashed the moment the editor lost focus.
	//
	// Nothing to read back on the way out: a chosen value was written when it was CHOSEN, and typed
	// text is written on the keystroke (see OnTyped) rather than at whichever door the editor left by.
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

		// ⭐⭐ WHAT IS TYPED LANDS AS IT IS TYPED. A row that declares a PRIMITIVE — a string, a number,
		// a date — is filled in by writing in it, and this cell used to drop that text on the way out:
		// it only ever accepted values it was GIVEN, which is right for a field or a reference and
		// leaves a String parameter impossible to fill (Max, 2026-08-28: "I cannot set the value").
		//
		// Written on the KEYSTROKE rather than on the way out, because the way out of this editor has
		// several doors — Enter, focus, a modal opening over it — and a value that depends on which
		// one was used is a value that goes missing on the others.
		if (m_typeOf) {
			editor->Bind(wxEVT_CONTROL_TEXT_INPUT, &ibRowValueCellRenderer::OnTyped, this);
			editor->Bind(wxEVT_CONTROL_TEXT_CLEAR, &ibRowValueCellRenderer::OnTyped, this);
		}
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

		// ⭐⭐ A ROW THAT DECLARES ITS TYPE HOLDS A VALUE, NOT A FIELD — so the "…" must never offer the
		// source tree here. An EMPTY cell used to fall into the field picker below (empty reads as "no
		// field chosen yet"), which is what opened "Select field" over a report parameter (Max,
		// 2026-08-28: "there should be no picker here — an empty reference plus the predefined
		// elements"). Asked FIRST, because emptiness is exactly the state both branches claim.
		if (m_typeOf) {
			// ⭐⭐ THE EDITOR CLOSES ONLY IF THE VALUE IS ALREADY THERE. A MODAL — the designer's
			// declared-value window — has answered by the time the call returns, and the cell must be
			// redrawn or it keeps showing the text it was opened with ("the value is not set straight
			// away"). A TRANSIENT POPUP — the calendar behind a date — has not: it is still open, it is
			// parented to this very editor, and closing the editor takes the calendar down with it,
			// which is what made a date impossible to change at all (Max, 2026-08-28). That road
			// finishes itself through ChoiceProcessing when the person picks.
			//
			// So the difference is not guessed from which window opened: it is READ off the value.
			ibValue before; GetControlValue(before);
			if (ibTypeControlFactory::ChooseValue(this, nullptr, GetEditorCtrl())) {
				ibValue after; GetControlValue(after);
				if (before != after) {
					FinishSelecting();
					RedrawOwnerView();
				}
			}
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

	// ⭐⭐ CLEARING ANSWERS THE DECLARATION. A cell that admits ONE type still holds a value of that
	// type when it is emptied — an empty REFERENCE of it, an empty string, a zero — because "which
	// type is this" was never in question there. A COMPOSITE cell has no such answer: emptying it
	// takes the type away too, and it goes back to holding nothing (Max, 2026-08-28: "if it is a
	// composite type it clears; if it is not, the reference stays the empty one").
	// THE TYPED TEXT, SHAPED BY THE DECLARATION — the same adjustment a report parameter's expression
	// result goes through, so a value typed here and one computed there end up under one rule. Only
	// where a PRIMITIVE is declared: a reference's text is its rendering, not an edit of it.
	void OnTyped(wxCommandEvent& event) {
		if (!m_typeOf || !m_row.IsOk() || !m_set)
			return;
		const ibTypeDescription declared = m_typeOf(m_row);
		if (declared.GetClsidCount() != 1 || !IsPrimitive(declared.GetByIdx(0)))
			return;
		m_set(m_row, ibValueTypeDescription::AdjustValue(declared, ibValue(event.GetString()), GetMetaData()));
	}

	// A CELL IN A DIALOG HAS NO FORM TO REFRESH — the tablebox road (m_tableBoxColumn) is null here,
	// so nothing was telling the view to repaint and a chosen value only appeared on the next click.
	// The renderer's own column knows the control it stands in; that is the whole route.
	void RedrawOwnerView() {
		if (ibDataViewColumn* column = GetOwner())
			if (wxWindow* view = column->GetOwner())
				view->Refresh();
	}

	// ⭐ CLEARING ANSWERS THE DECLARATION, and the product already answers it: `AdjustValue` over a type
	// description with nothing to adjust gives the empty value of a SINGLE declared type — the empty
	// reference of that catalog, an empty string — and undefined for a composite one, which is
	// precisely the difference (Max, 2026-08-28: "if it is a composite type it clears; if it is not,
	// the reference stays the empty one").
	void OnClear(wxCommandEvent&) {
		SetControlValue((m_typeOf && m_row.IsOk())
			? ibValueTypeDescription::AdjustValue(m_typeOf(m_row), GetMetaData()) : ibValue());
		FinishSelecting();
		RedrawOwnerView();
	}

	wxWindow*      m_host;      // the window a modal picker is parented to
	FieldChooser   m_chooser;
	Getter         m_get;
	Setter         m_set;
	Expand         m_expand;    // set = the "…" opens the ROW's own window instead of the picker
	TypeOf         m_typeOf;    // set = the row declares what it may hold; the cell then routes through ChooseValue
	MetaOf         m_metaOf;    // …and in which open configuration that declaration is read
	ibDataViewItem m_row;

	// Storage for the two mutable-reference getters the type-factory line demands.
	mutable ibTypeDescription   m_typeDesc;
	mutable ibSourceDescription m_sourceDesc;
};

#endif // __ROW_VALUE_CELL_H__
