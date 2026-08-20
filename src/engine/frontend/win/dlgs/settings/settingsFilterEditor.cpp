////////////////////////////////////////////////////////////////////////////
//	The FILTER editor — shared by both settings worlds
////////////////////////////////////////////////////////////////////////////

#include "frontend/win/dlgs/settings/settingsFilterEditor.h"
#include "frontend/win/dlgs/settings/settingsFieldTree.h"
#include "frontend/win/dlgs/settings/settingsStyle.h"
#include "frontend/win/dlgs/settings/filterTreeModel.h"   // the filter is a TREE — model + column ids
#include "frontend/win/dlgs/callbackDropTarget.h"         // the same-process drag: the source knows what moved

#include "backend/appData.h"
#include "backend/metadataConfiguration.h"
#include "backend/objCtor.h"
#include "backend/system/value/valueType.h"                  // ibValueTypeDescription::AdjustValue

#include "frontend/win/ctrls/controlTextEditor.h"
#include "frontend/visualView/ctrl/typeControl.h"
#include "frontend/visualView/ctrl/frame.h"
#include "frontend/visualView/ctrl/tableBoxColumnRenderer.h"  // ibDataViewValueRenderer — the cell is drawn by the TABLE's renderer

#include <wx/menu.h>
#include <wx/sizer.h>
#include <wx/splitter.h>
#include <wx/stattext.h>

// Filter COMMANDS — raised by the toolbar and by the context menu alike, so both
// roads end in one handler and cannot drift apart. (The filter's column ids live
// with its model, in filterTreeModel.h.)
enum {
	kFilterCmdAdd = wxID_HIGHEST + 400,
	kFilterCmdAddGroup,
	kFilterCmdCopy,
	kFilterCmdRemove,
	kFilterCmdMoveUp,
	kFilterCmdMoveDown,
	kFilterCmdGroup,
	kFilterCmdUngroup,
};

// ===========================================================================
//  The value cell each side of a condition is edited through
// ===========================================================================

class ibFilterEditor::ibFilterValueRenderer : public ibDataViewValueRenderer,
	public ibControlFrame, public ibTypeControlFactory {
	ibFilterEditor* m_editor;
	unsigned int m_side;   // kFilterColLeft or kFilterColRight
	// Storage for the two mutable-reference getters the factory line demands.
	mutable ibTypeDescription   m_typeDesc;
	mutable ibSourceDescription m_sourceDesc;
public:

	// --- ibTypeControlFactory — the gate's questions -----------------------------
	// No metadata COLUMN behind this cell: a filter line binds a path the user picked,
	// not a form attribute. The type below is what the gate actually consults.
	virtual const ibBackendSourceColumn* GetSourceAttributeObject() const override { return nullptr; }
	virtual ibSourceObject* GetSourceObject() const override { return nullptr; }
	virtual ibSourceDescription& GetSourceDesc() const override { return m_sourceDesc; }
	virtual const ibMetaData* GetMetaData() const override { return activeMetaData; }

	// WHICH TYPE THIS CELL IS ABOUT TO HOLD — asked when the value choice starts,
	// before anything is picked. The base answers from the metadata (and only puts
	// the question to the user when more than one type is admitted); the left side
	// of a condition needs no asking at all: a condition filters a FIELD.
	virtual ibClassID GetDataType() const override {
		if (m_side == kFilterColLeft)
			return g_compositionFieldCLSID;
		return ibTypeControlFactory::GetDataType();
	}

	// ASK THE ROW. The left side admits exactly one type — a composition field — and
	// the right side admits whatever the chosen field lends it (GetRightTypeDescription).
	virtual ibTypeDescription& GetTypeDesc() const override {
		const ibValueFilterItem* item = GetSelectedItem();
		if (item == nullptr)
			m_typeDesc = ibTypeDescription();
		else if (m_side == kFilterColLeft)
			m_typeDesc = ibTypeDescription(g_compositionFieldCLSID);
		else
			m_typeDesc = item->GetRightTypeDescription();
		return m_typeDesc;
	}


	// WHICH SIDE this renderer edits. The two sides are the same kind of thing —
	// a value that may be a field — so they get the same renderer, told apart by
	// the column it was built for. Without that it could only ever edit the right
	// one, which is how the flat filter used to work.
	ibFilterValueRenderer(ibFilterEditor* editor, unsigned int side)
		// NO TABLE COLUMN behind this cell — the renderer guards on that everywhere
		// (it only uses the column to refresh the owning form), so a filter cell and
		// a table cell are drawn by the very same code.
		: ibDataViewValueRenderer(nullptr), m_editor(editor), m_side(side) {
	}

	// (Render / GetSize / SetValue / GetValue / FinishSelecting / the accessible
	// description all come from ibDataViewValueRenderer — the renderer a TABLE
	// column uses. They were copied here when this cell was written; the copy is
	// what made a number left-aligned in the filter and right-aligned everywhere
	// else, and what made the cell keep painting the old text after a value was
	// written behind the model's back. Only the two questions this cell answers
	// differently are left below.
	virtual bool HasEditorCtrl() const override { return true; }

	// A CELL RENDERS AS A CELL. Opening the editor on the first click turned the
	// whole column into edit boxes as soon as a row was selected: the text got
	// clipped by the buttons and the column stopped reading as a column. The
	// editor opens on activation (double-click / Enter), like everywhere else.

	virtual wxWindow* CreateEditorCtrl(wxWindow* dv,
		wxRect labelRect,
		const wxVariant& value) override {

		m_row = m_item;   // THE row this editor belongs to, held across the modal picker

		ibControlTextEditor* textEditor = new ibControlTextEditor;
		textEditor->SetDVCMode(true);

		// create the window hidden to prevent flicker
		textEditor->Show(false);

		bool result = textEditor->Create(dv, wxID_ANY, value,
			labelRect.GetPosition(),
			labelRect.GetSize());

		if (!result)
			return nullptr;

		textEditor->ShowSelectButton(true);
		textEditor->ShowClearButton(true);
		textEditor->ShowOpenButton(false);

		ibDataViewCtrl* parentWnd = dynamic_cast<ibDataViewCtrl*>(dv->GetParent());
		if (parentWnd != nullptr) {
			textEditor->SetBackgroundColour(parentWnd->GetBackgroundColour());
			textEditor->SetForegroundColour(parentWnd->GetForegroundColour());
			textEditor->SetFont(parentWnd->GetFont());
		}
		else {
			textEditor->SetBackgroundColour(dv->GetBackgroundColour());
			textEditor->SetForegroundColour(dv->GetForegroundColour());
			textEditor->SetFont(dv->GetFont());
		}

		textEditor->SetPasswordMode(false);
		textEditor->SetMultilineMode(false);
		// ALWAYS EDITABLE AS A CONTROL. SetTextEditMode(false) is the read-only
		// policy and it locks the Select / Clear buttons too — which is precisely
		// how the field cell ended up with a picker that could not be opened. A
		// field still cannot be typed into meaningfully: the model refuses text for
		// that column, so what the user types is simply not taken.
		textEditor->SetTextEditMode(true);

		textEditor->Bind(wxEVT_CONTROL_BUTTON_SELECT, &ibFilterValueRenderer::OnSelectButtonPressed, this);
		textEditor->Bind(wxEVT_CONTROL_BUTTON_CLEAR, &ibFilterValueRenderer::OnClearButtonPressed, this);

		textEditor->LayoutControls();
		textEditor->Show(true);

		textEditor->SetInsertionPointEnd();
		return textEditor;
	}

	virtual bool GetValueFromEditorCtrl(wxWindow* ctrl, wxVariant& value) override {
		ibControlTextEditor* textEditor = wxDynamicCast(ctrl, ibControlTextEditor);
		if (textEditor == nullptr)
			return false;
		value = textEditor->GetValue();
		return true;
	}

	// Counter reference — the renderer is not ref-counted (owned by the column).
	virtual void ControlIncrRef() {}
	virtual void ControlDecrRef() {}

public:

	// THE SIDE THIS RENDERER EDITS — both of these feed the quick choice, so a
	// left-hand cell that reported the right-hand value would offer the wrong
	// choices and then write them to the wrong place.
	virtual bool GetControlValue(ibValue& pvarControlVal) const override {
		if (GetSelectedGroup() != nullptr) {
			pvarControlVal = GroupValue();
			return true;
		}
		ibValueFilterItem* item = GetSelectedItem();
		if (item == nullptr)
			return false;
		pvarControlVal = SideValue(item);
		return true;
	}

	virtual bool SetControlValue(const ibValue& varValue = ibValue()) override {
		if (ibValueFilterGroup* group = GetSelectedGroup()) {
			// A GROUP HAS NOTHING TO CLEAR — its operator always has a value. An empty
			// value here comes from the Clear button, and converting it to the
			// enumeration RAISES ("variable type does not support this operation"),
			// which is what crashed the form on "×" over a group row.
			if (varValue.IsEmpty())
				return true;
			group->SetKind(varValue.ConvertToEnumValue<ibFilterGroupKind>());
			NotifyRowChanged();
			return true;
		}
		ibValueFilterItem* item = GetSelectedItem();
		if (item == nullptr)
			return false;
		// ADJUSTED TO THE CELL'S TYPE ON THE WAY IN, exactly as a text control does
		// it (textctrl.cpp): the factory rounds a number to its scale, trims a
		// string, and turns "nothing" into the right kind of empty — the empty
		// value of a single type, or Undefined when the cell is composite. That is
		// why clearing is just this call with nothing in it.
		SetSideValue(item, AdjustValue(varValue));
		return true;
	}

	virtual bool HasQuickChoice() const {
		ibValue selValue; GetControlValue(selValue);
		// A FIELD HAS ONE — its own (the source tree). Saying no here is what kept
		// the platform from ever reaching the type's picker.
		if (selValue.GetClassType() == g_compositionFieldCLSID)
			return true;
		// ASK THE TYPE. This cell is not a form control and has no base to inherit the answer from —
		// but it does not need one: the ctor answers for itself.
		const ibCtorAbstractType* so = activeMetaData->GetAvailableCtor(selValue.GetClassType());
		return ::HasQuickChoice(so);   // the free function (frame.h), not this class's own no-arg one
	}

private:

	// THE ROW THIS CELL IS EDITING. Not the grid's SELECTION: the editor opens on
	// the row that was clicked, and the selection may still be on another one (or
	// may move while a modal picker is up). Reading and writing through the
	// selection is what made the same click work on one row and do nothing on the
	// next — the value was fetched from, and written to, a different line.
	ibDataViewItem EditedRow() const {
		// m_row is captured when the editor is created. m_item alone is not enough:
		// opening a MODAL picker takes the focus away, the editor finishes and the
		// base clears m_item — so by the time the chosen value comes back there is
		// no row left to write it to, and the pick "does nothing".
		if (m_row.IsOk())
			return m_row;
		return m_item.IsOk() ? m_item : m_editor->GetFilterView()->GetSelection();
	}

	// The currently-selected CONDITION, or nullptr — a group row is not one, and
	// neither is an empty selection. The row carries the value it stands for
	// (filterTreeModel.h), so this is a question to the row, not arithmetic on a
	// row number: a tree has no 1-based index to reverse.
	ibValueFilterItem* GetSelectedItem() const {
		const ibDataViewItem item = EditedRow();
		if (!item.IsOk())
			return nullptr;
		const ibFilterTreeNode* node = static_cast<const ibFilterTreeNode*>(item.GetID());
		return node != nullptr ? node->GetItem() : nullptr;
	}

	// Does THIS side hold a field right now?
	bool HoldsField() const {
		const ibValueFilterItem* item = GetSelectedItem();
		if (item == nullptr)
			return false;
		ibValueCompositionField* field = nullptr;
		return SideValue(item).ConvertToValue(field);
	}

	// The selected GROUP — the other kind of row this column serves.
	ibValueFilterGroup* GetSelectedGroup() const {
		const ibDataViewItem item = EditedRow();
		if (!item.IsOk() || m_side != kFilterColLeft)
			return nullptr;
		const ibFilterTreeNode* node = static_cast<const ibFilterTreeNode*>(item.GetID());
		return node != nullptr ? node->GetGroup() : nullptr;
	}

	// EVERY EDITABLE CELL OF A CONDITION IS A VALUE, and its type says how it is
	// picked: a comparison is a ComparisonKind enumeration, a display mode is a
	// FilterDisplayMode one, a side is whatever the user put there. All three are
	// registered types, so the runtime's own quick choice offers the right list —
	// no per-column drop-down with the labels spelled a second time in this file.
	// A GROUP ROW answers with its OPERATOR in the left cell — that is the value
	// this cell edits there, and it is why the quick choice opens on it.
	ibValue GroupValue() const {
		const ibValueFilterGroup* group = GetSelectedGroup();
		return group != nullptr
			? ibValue::CreateEnumObject<ibValueEnumFilterGroupKind>(group->GetKind())
			: ibValue();
	}

	ibValue SideValue(const ibValueFilterItem* item) const {
		switch (m_side) {
		case kFilterColComparison:
			return ibValue::CreateEnumObject<ibValueEnumComparisonKind>(item->GetComparison());
		case kFilterColDisplayMode:
			return ibValue::CreateEnumObject<ibValueEnumFilterDisplayMode>(item->GetDisplayMode());
		case kFilterColRight:
			return item->GetRight();
		default:
			return item->GetLeft();
		}
	}
	void SetSideValue(ibValueFilterItem* item, const ibValue& value) const {
		// A NEW FIELD ON THE LEFT INVALIDATES THE RIGHT. The right-hand side holds a
		// value OF THE LEFT FIELD'S TYPE — filter by a date and the value is a date;
		// change the field to a reference and that date means nothing, so it goes.
		// Only when the TYPE actually changes: re-picking the same field must not
		// wipe what the user already entered.
		if (m_side == kFilterColLeft) {
			const ibTypeDescription before = item->GetRightTypeDescription();
			item->SetLeft(value);
			const ibTypeDescription after = item->GetRightTypeDescription();
			// NOT EMPTY — THE TYPE'S OWN DEFAULT. An empty right-hand side reads as
			// "nothing chosen yet", which for a Boolean is a lie: there is no third
			// state to choose. AdjustValue answers what the type itself considers
			// empty — False for a Boolean, an empty reference of the right kind, 0
			// for a number — so the row is complete the moment the field is picked.
			if (before.GetFirstClsid() != after.GetFirstClsid())
				item->SetRight(ibValueTypeDescription::AdjustValue(after, m_editor->GetMetaData()));
			NotifyRowChanged();
			return;
		}

		switch (m_side) {
		// THE ENUMERATION CARRIES ITS OWN VALUE BACK — the chosen member IS the
		// kind, so nothing here maps a label or an index to a meaning.
		case kFilterColComparison:
			item->SetComparison(value.ConvertToEnumValue<ibComparisonKind>());
			break;
		case kFilterColDisplayMode:
			item->SetDisplayMode(value.ConvertToEnumValue<ibFilterDisplayMode>());
			break;
		// ⭐⭐ NORMALISED TO THE FIELD'S TYPE, like every OTHER path that writes this cell.
		//
		// What an editor hands back is not always the value the condition is about: picking from a
		// list yields the LIST ROW, and a row is not a reference. Stored raw it fails validation on
		// OK — "The value of condition 'Recorder' does not fit the field's type" — with a value the
		// user did choose correctly, from the picker the field itself opened.
		//
		// The two neighbouring writers (the field-change path above, the row-add path below) already
		// go through AdjustValue; this one was the odd one out. AdjustValue answers what the TYPE
		// makes of a value — a row becomes the reference it stands for, an empty becomes the type's
		// own empty — so the cell holds what the comparison will actually be made with.
		case kFilterColRight:
			// The FIELD's type decides what the value becomes — that is the whole point, so the type
			// description leads and the written value follows it (the two-argument overload above
			// asks only "what does this type consider empty", which is a different question).
			item->SetRight(ibValueTypeDescription::AdjustValue(
				item->GetRightTypeDescription(), value, m_editor->GetMetaData()));
			break;
		default:              item->SetLeft(value);  break;
		}

		NotifyRowChanged();
	}

	// AND SAY SO. Writing the object is not enough — the cell paints what the
	// MODEL reports, so a value changed behind its back leaves the old text on
	// screen and the whole cell reads as "nothing happened".
	void NotifyRowChanged() const {
		ibDataViewCtrl* view = m_editor->GetFilterView();
		ibFilterTreeModel* model = m_editor->GetFilterModel();
		if (view == nullptr || model == nullptr)
			return;
		const ibDataViewItem row = EditedRow();
		if (row.IsOk())
			model->ValueChanged(row, m_side);
	}

	//events
	void OnSelectButtonPressed(wxCommandEvent& event) {
		// A GROUP OWNS ONE THING — its operator — and it is a FilterGroupKind value,
		// so it is picked exactly like a comparison is: the runtime's quick choice
		// over the registered enumeration, not a list spelled out here.
		if (GetSelectedGroup() != nullptr) {
			ibValue kind; GetControlValue(kind);
			ibTypeControlFactory::QuickChoice(this, kind.GetClassType(), GetEditorCtrl());
			return;
		}

		ibValueFilterItem* item = GetSelectedItem();
		if (item == nullptr)
			return;

		// A FIELD IS PICKED FROM THE SOURCE TREE, and this cell is the only thing
		// that knows which source — so this one branch stays here instead of
		// becoming a hook on the shared factory that nothing else would ever answer.
		ibValue current; GetControlValue(current);
		// ASKED FIRST, NOT INSIDE THE OR. `m_side == Left || current.ConvertToValue(f)`
		// short-circuits on the left side — the cell that ALWAYS holds a field — so
		// the field was never read and the picker opened knowing nothing about the
		// current value.
		ibValueCompositionField* asField = nullptr;
		const bool holdsField = current.ConvertToValue(asField);
		if (m_side == kFilterColLeft || holdsField) {
			// The editor closes BEFORE the modal picker opens — see the row-value
			// cell: a live editor keeps the mouse, and the picker's tree stops
			// responding to the clicks that expand a reference.
			const ibDataViewItem row = EditedRow();
			wxWindow* parent = m_editor;
			FinishSelecting();
			const wxString held = (asField != nullptr) ? asField->GetPath() : wxString();
			if (ibValueCompositionField* chosen = m_editor->ChooseField(parent, held)) {
				m_row = row;
				SetControlValue(ibValue(chosen));
				m_row = ibDataViewItem();
			}
			return;
		}

		// EVERYTHING ELSE takes the shared route — the one a text control and a
		// table column walk: settle the type, then choose the value of that type.
		// Null form = the metaobject generates its default.
		//
		// AND NOTHING IS CLOSED HERE. The choice is not over when this returns: a
		// quick choice is a popup that lives ON the editor, and a selection form is
		// a window the user is still standing in. Tearing the editor down now takes
		// the popup with it — which is exactly why a boolean's drop-down appeared
		// for an instant and vanished. The editor closes when the value arrives
		// (ChoiceProcessing), like it does for every other control.
		ibTypeControlFactory::ChooseValue(this, nullptr, GetEditorCtrl());
	}

	// CLEARING IS THE SAME ONE CALL every control makes — SetControlValue with
	// nothing in it. What "empty" then means is the TYPE's business, not this
	// cell's: a single-typed cell ends up with the empty value of that type (an
	// empty reference is still a reference), a composite one with Undefined, which
	// is the state that makes the next Select ask for the type again.
	void OnClearButtonPressed(wxCommandEvent& event) {
		SetControlValue();
		FinishSelecting();
	}

	// THE SIDE THAT WAS EDITED gets the value. Writing the right-hand side no
	// matter which cell was open is how the left one came to look inert.
	virtual void ChoiceProcessing(ibValue& vSelected) {
		// The group's operator comes back the same way any chosen value does.
		if (ibValueFilterGroup* group = GetSelectedGroup()) {
			group->SetKind(vSelected.ConvertToEnumValue<ibFilterGroupKind>());
			NotifyRowChanged();
			FinishSelecting();
			return;
		}

		ibValueFilterItem* item = GetSelectedItem();
		if (item == nullptr)
			return;
		SetSideValue(item, vSelected);
		item->SetUse(true);
		FinishSelecting();
	}

private:
	ibDataViewItem m_row;   // the row being edited — see EditedRow
};

// ===========================================================================
//  ibFilterEditor
// ===========================================================================

ibFilterEditor::ibFilterEditor(wxWindow* parent, ibValueListSettings* settings, ibSettingsFieldTree* fields)
	: wxPanel(parent, wxID_ANY), m_settings(settings), m_fields(fields)
{
	// Draggable split: LEFT = available-fields tree (dot-walkable), RIGHT = the composed filter.
	wxSplitterWindow* splitter = new wxSplitterWindow(this, wxID_ANY,
		wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3DSASH);
	splitter->SetMinimumPaneSize(FromDIP(120));

	// ---- LEFT pane: available fields (a reference field expands into its target's fields) ----
	wxPanel* leftPane = new wxPanel(splitter);
	wxBoxSizer* leftSizer = new wxBoxSizer(wxVERTICAL);
	leftSizer->Add(new wxStaticText(leftPane, wxID_ANY, _("Available fields")), 0, wxALL, FromDIP(4));
	m_fieldTree = new wxTreeCtrl(leftPane, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTR_HAS_BUTTONS | wxTR_SINGLE | wxTR_HIDE_ROOT | wxTR_LINES_AT_ROOT | wxTR_NO_LINES | wxTR_TWIST_BUTTONS);
	leftSizer->Add(m_fieldTree, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, FromDIP(4));
	leftPane->SetSizer(leftSizer);

	// ---- RIGHT pane: the filter TREE — Use / Left / Comparison / Right / Display / Presentation ----
	wxPanel* rightPane = new wxPanel(splitter);
	wxBoxSizer* rightSizer = new wxBoxSizer(wxVERTICAL);

	// THE COMMANDS LIVE ON A TOOLBAR, and the context menu offers the same ones.
	// One set of verbs, one implementation — a user who learns either has learned
	// both.
	m_toolbar = new wxToolBar(rightPane, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTB_HORIZONTAL | wxTB_FLAT | wxTB_NODIVIDER);
	// 16×16 — a settings surface's toolbar sits above a grid, and the stock toolbar
	// size (24 or 32, per theme) makes it the loudest thing on the page.
	m_toolbar->SetToolBitmapSize(FromDIP(wxSize(16, 16)));
	m_toolbar->AddTool(kFilterCmdAdd, _("New item"),
		ibSettingsArt(wxASCII_STR(wxART_NEW), this), _("New item (Ins)"));
	m_toolbar->AddTool(kFilterCmdAddGroup, _("New group"),
		ibSettingsArt(wxASCII_STR(wxART_FOLDER), this), _("New group (Ctrl+F9)"));
	m_toolbar->AddTool(kFilterCmdCopy, _("Copy"),
		ibSettingsArt(wxASCII_STR(wxART_COPY), this), _("Copy (F9)"));
	m_toolbar->AddTool(kFilterCmdRemove, _("Delete"),
		ibSettingsArt(wxASCII_STR(wxART_DELETE), this), _("Delete (Del)"));
	m_toolbar->AddSeparator();
	m_toolbar->AddTool(kFilterCmdMoveUp, _("Move up"),
		ibSettingsArt(wxASCII_STR(wxART_GO_UP), this), _("Move up (Ctrl+Shift+Up)"));
	m_toolbar->AddTool(kFilterCmdMoveDown, _("Move down"),
		ibSettingsArt(wxASCII_STR(wxART_GO_DOWN), this), _("Move down (Ctrl+Shift+Down)"));
	m_toolbar->AddSeparator();
	m_toolbar->AddTool(kFilterCmdGroup, _("Group conditions"),
		ibSettingsArt(wxASCII_STR(wxART_LIST_VIEW), this), _("Group conditions"));
	m_toolbar->AddTool(kFilterCmdUngroup, _("Ungroup"),
		ibSettingsArt(wxASCII_STR(wxART_NORMAL_FILE), this), _("Ungroup"));
	m_toolbar->Realize();
	// Same margins as the grid below it.
	rightSizer->Add(m_toolbar, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, FromDIP(4));

	m_view = new ibDataViewCtrl(rightPane, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxDV_ROW_LINES | wxDV_SINGLE);
	ibStyleSettingsGrid(m_view);
	m_view->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, &ibFilterEditor::OnFilterItemActivated, this);

	// ⚠⚠ AND AGAIN WHEN THE WINDOW IS SHOWN. The expansion posted from the load
	// (RefreshFilterTree -> CallAfter) runs while the window is still being BUILT: the control has
	// not fetched a single row yet, so expanding its root is a no-op and a reopened settings window
	// showed the filter folded shut — exactly the state that reads as "nothing set".
	//
	// Show is the first moment the rows genuinely exist, and one more turn of the loop after it is
	// where they have been laid out. Both are posted, so neither is a paint inside a paint.
	Bind(wxEVT_SHOW, [this](wxShowEvent& event) {
		event.Skip();
		if (event.IsShown())
			CallAfter(&ibFilterEditor::ExpandFilterTree);
	});

	// (NO local choice lists any more. A comparison and a display mode are
	// registered ENUMERATIONS — ComparisonKind / FilterDisplayMode in listFilter.h
	// — so their members and captions live there, once. Spelling them again here
	// meant an order that had to match an enum by hand and a second place to
	// translate.)

	m_view->GetRootColumnGroup()->AppendToggleColumn(_("Use"), kFilterColUse,
		wxDATAVIEW_CELL_ACTIVATABLE, wxNOT_FOUND, wxAlignment::wxALIGN_LEFT);
	// LEFT is the expander column: the tree hangs off it, and a group's caption
	// starts here and flows across the columns it has no value in.
	// THE LEFT CELL SERVES BOTH KINDS OF ROW, because both keep something there: a
	// condition its FIELD, a group its OPERATOR. It is the control-backed cell (an
	// ibControlFrame and a type factory), so the operator goes through the runtime's
	// quick choice and the field through the source-tree picker — one column, the
	// row decides.
	ibDataViewColumn* leftColumn = new ibDataViewColumn(_("Left value"),
		new ibFilterValueRenderer(this, kFilterColLeft), kFilterColLeft, wxNOT_FOUND, wxAlignment::wxALIGN_LEFT);
	m_view->GetRootColumnGroup()->AppendColumn(leftColumn);
	ibDataViewColumn* cmpColumn = new ibDataViewColumn(_("Comparison"),
		new ibFilterValueRenderer(this, kFilterColComparison),
		kFilterColComparison, wxNOT_FOUND, wxAlignment::wxALIGN_LEFT);
	m_view->GetRootColumnGroup()->AppendColumn(cmpColumn);
	ibDataViewColumn* valColumn = new ibDataViewColumn(_("Right value"),
		new ibFilterValueRenderer(this, kFilterColRight), kFilterColRight, wxNOT_FOUND, wxAlignment::wxALIGN_LEFT);
	m_view->GetRootColumnGroup()->AppendColumn(valColumn);
	ibDataViewColumn* modeColumn = new ibDataViewColumn(_("Display mode"),
		new ibFilterValueRenderer(this, kFilterColDisplayMode),
		kFilterColDisplayMode, wxNOT_FOUND, wxAlignment::wxALIGN_LEFT);
	m_view->GetRootColumnGroup()->AppendColumn(modeColumn);
	m_view->GetRootColumnGroup()->AppendTextColumn(_("Presentation"), kFilterColPresentation,
		wxDATAVIEW_CELL_EDITABLE, wxNOT_FOUND, wxAlignment::wxALIGN_LEFT);
	// A TREE, not a list. The control defaults to ibDataViewList — it fetches the
	// children and then draws them all at one level, so a nested filter reads as a
	// flat one and the group a line sits in is invisible. That default is why the
	// hierarchy has to be said out loud here.
	m_view->SetViewMode(ibDataViewTree);
	// THE EXPANDER STAYS ON THE FIRST COLUMN — NOT on "Left value". A click in the
	// expander column belongs to the tree (open / close), so the grid refuses to
	// start editing there (datavgen.cpp): hanging the tree off "Left value" is
	// exactly what made the left-hand cell impossible to edit while the right one
	// worked. The Use column carries the indent instead.
	if (m_view->GetColumnCount() > 0)
		m_view->SetExpanderColumn(m_view->GetColumn(0));
	rightSizer->Add(m_view, 1, wxALL | wxEXPAND, FromDIP(4));
	rightPane->SetSizer(rightSizer);

	// WIDE ENOUGH FOR WHAT THE FIELDS ARE CALLED. At 180 a name like "Account dimension Dr1"
	// is cut after "Account dimension", so eight distinct slots read as eight identical rows and
	// the only way to tell them apart is to count positions. The pane is user-resizable; what
	// changes here is what it shows BEFORE anyone resizes it.
	splitter->SplitVertically(leftPane, rightPane, FromDIP(260));
	wxBoxSizer* panelSizer = new wxBoxSizer(wxVERTICAL);
	panelSizer->Add(splitter, 1, wxEXPAND);
	SetSizer(panelSizer);

	// Populate the available-fields tree + wire it: references expand lazily, double-click adds.
	if (m_fields != nullptr) {
		m_fields->Populate(m_fieldTree);
		m_fields->Attach(m_fieldTree);
	}
	m_fieldTree->Bind(wxEVT_TREE_ITEM_ACTIVATED, [this](wxTreeEvent& e) { AddFilterForField(e.GetItem()); });
	rightPane->SetDropTarget(new ibCallbackDropTarget([this] {
		if (m_fields != nullptr) AddFilterForField(m_fields->GetDragItem());
	}));

	// The toolbar and the context menu raise the SAME command ids, so both roads
	// end in one handler.
	m_toolbar->Bind(wxEVT_TOOL, &ibFilterEditor::OnFilterAdd, this, kFilterCmdAdd);
	m_toolbar->Bind(wxEVT_TOOL, &ibFilterEditor::OnFilterAddGroup, this, kFilterCmdAddGroup);
	m_toolbar->Bind(wxEVT_TOOL, &ibFilterEditor::OnFilterCopy, this, kFilterCmdCopy);
	m_toolbar->Bind(wxEVT_TOOL, &ibFilterEditor::OnFilterRemove, this, kFilterCmdRemove);
	m_toolbar->Bind(wxEVT_TOOL, &ibFilterEditor::OnFilterMoveUp, this, kFilterCmdMoveUp);
	m_toolbar->Bind(wxEVT_TOOL, &ibFilterEditor::OnFilterMoveDown, this, kFilterCmdMoveDown);
	m_toolbar->Bind(wxEVT_TOOL, &ibFilterEditor::OnFilterGroupSelected, this, kFilterCmdGroup);
	m_toolbar->Bind(wxEVT_TOOL, &ibFilterEditor::OnFilterUngroup, this, kFilterCmdUngroup);

	m_model = new ibFilterTreeModel();
	m_model->SetRoot(GetFilterRoot());
	m_view->AssociateModel(m_model);
	m_view->Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, &ibFilterEditor::OnContextMenu, this);
	m_view->Bind(wxEVT_DATAVIEW_ITEM_START_EDITING, &ibFilterEditor::OnStartEditing, this);
	// THE OTHER HALF OF "IT CHANGED": a CELL edit writes the buffer without touching the tree's
	// shape, so it never reaches RefreshFilterTree. One bind covers every column.
	m_view->Bind(wxEVT_DATAVIEW_ITEM_VALUE_CHANGED, [this](ibDataViewEvent& e) {
		e.Skip();
		if (!m_reloading && m_onChanged)
			m_onChanged();
	});
}

// ⭐ VIEW ONLY — see the header. The toolbar is disabled (and so stops looking clickable), the
// context menu declines to appear at all, and a cell refuses to open its editor. The field tree on
// the left is left alone on purpose: browsing what a filter COULD name is reading, not editing.
void ibFilterEditor::SetReadOnly(bool readOnly)
{
	m_readOnly = readOnly;
	if (m_toolbar != nullptr)
		m_toolbar->Enable(!readOnly);
	// …and the "Use" tick, which no veto reaches — the model is its only gate.
	if (m_model != nullptr)
		m_model->SetReadOnly(readOnly);
}

void ibFilterEditor::OnStartEditing(ibDataViewEvent& event)
{
	if (m_readOnly)
		event.Veto();
	else
		event.Skip();
}

ibValueFilterGroup* ibFilterEditor::GetFilterRoot() const
{
	return m_settings != nullptr ? m_settings->GetFilterRoot() : nullptr;
}

const ibMetaData* ibFilterEditor::GetMetaData() const
{
	return m_fields != nullptr ? m_fields->GetMetaData() : activeMetaData;
}

ibValueCompositionField* ibFilterEditor::ChooseField(wxWindow* parent, const wxString& held) const
{
	return m_fields != nullptr ? m_fields->ChooseField(parent, held) : nullptr;
}

void ibFilterEditor::SetSettings(ibValueListSettings* settings)
{
	m_settings = settings;
	Reload();
}

void ibFilterEditor::Reload()
{
	// QUIET: filling the editor from the buffer is not somebody editing it. Nested-safe (restores
	// rather than clears) so a reload reached from inside another one cannot un-quiet the outer.
	const bool wasReloading = m_reloading;
	m_reloading = true;
	RefreshFilterTree();
	m_reloading = wasReloading;
}

void ibFilterEditor::ReloadFields()
{
	if (m_fields != nullptr)
		m_fields->Populate(m_fieldTree);
}

// OPEN ON THE ROOT, AND ON EVERY GROUP UNDER IT. A collapsed root hides the whole filter behind one
// line that reads as "nothing set", and a collapsed inner group hides the conditions the author just
// wrote. A filter is short by nature — there is nothing here worth folding away, and a person who
// wants it folded can fold it.
//
// ⚠ DEPTH-FIRST, PARENT BEFORE CHILD: a container's children do not exist for the view until its
// parent is open, so asking a grandchild to expand first does nothing at all.
void ibFilterEditor::ExpandFilterTree()
{
	if (m_model == nullptr || m_view == nullptr)
		return;

	const ibDataViewItem rootItem = m_model->RootItem();
	if (!rootItem.IsOk())
		return;

	std::function<void(const ibDataViewItem&)> openAll = [&](const ibDataViewItem& parent) {
		m_view->Expand(parent);
		ibDataViewItemArray children;
		m_model->GetFirstFetch(parent, ibDataViewItem(), -1, children);
		for (const ibDataViewItem& child : children)
			if (m_model->IsContainer(child))
				openAll(child);
	};
	openAll(rootItem);
}

void ibFilterEditor::RefreshFilterTree(const ibValue& select)
{
	// A STRUCTURAL change (added, removed, grouped) is not a value change: rows
	// appear and disappear, so the view re-reads the shape. The ROWS survive it —
	// they are keyed by the value they stand for — which is what lets the cursor
	// land back on the right line below.
	if (m_model == nullptr || m_view == nullptr)
		return;
	// THE ROOT IS RE-STATED, not assumed: this is also the path taken when the
	// settings are first loaded, and at that point the page has already been built
	// with whatever root existed then (usually none). SetRoot keeps the rows when
	// the tree is the same one, so this costs nothing on an edit.
	m_model->SetRoot(GetFilterRoot());

	// ⚠⚠ EXPANDED ON THE NEXT TURN OF THE EVENT LOOP, not here. Expanding a row the view has not
	// FETCHED yet is a no-op — and during a refresh (and on the very first load, before the first
	// paint) it has not. Called inline, the root stayed shut and read as "no filter".
	CallAfter(&ibFilterEditor::ExpandFilterTree);

	// Rows only exist once fetched, so this asks AFTER the model re-read them.
	const ibDataViewItem sel = m_model->ItemFor(select);
	if (sel.IsOk()) {
		m_view->ExpandAncestors(sel);
		m_view->Select(sel);
		m_view->EnsureVisible(sel);
	}

	// ⭐ AND SAY IT CHANGED. Every structural mutation comes through here — added, removed, grouped,
	// ungrouped, moved — so this is the one place that has to say so, rather than nine handlers each
	// remembering to. The exception is spelled out and is the only one: RELOADING walks the same
	// refresh and is not an edit.
	if (!m_reloading && m_onChanged)
		m_onChanged();
}

// Add a filter row on the chosen field-tree node — its dot-path + leaf id/type come
// straight from the node, so even a DEEP path (Supplier.Region.Country) resolves its
// value editor. New row: default comparison Equal, empty typed value.
// ⚠ VIEW ONLY is guarded HERE because this is where the roads meet: the toolbar, the context menu,
// a double-click on the field tree and a drop on the pane all end in this one function. The field
// tree itself stays browsable — reading what a filter COULD name is reading.
void ibFilterEditor::AddFilterForField(const wxTreeItemId& item)
{
	if (m_readOnly)
		return;
	// INTO THE GROUP THE USER IS STANDING IN — the selected group, or the parent
	// of the selected condition, or the root. A new line landing at the bottom of
	// the whole filter would silently mean something else than where they pointed.
	ibValueFilterGroup* target = m_model != nullptr
		? m_model->GetTargetGroup(m_view->GetSelection()) : nullptr;
	if (target == nullptr)
		return;

	// The tree's own label is the PRESENTATION — that is what the user picked and
	// what the filter line should read back, rather than the technical path.
	//
	// NOT AN ibValuePtr HERE. Wrapping the field and handing THAT to ibValue picks a different
	// constructor than the one meant for a value object — the condition ends up holding something
	// that is not the field, and it crashes the moment the group copies it in. The field is
	// refcounted by the value that takes it, so the raw object is what travels.
	ibValueCompositionField* field = ibSettingsFieldTree::FieldAt(m_fieldTree, item);
	if (field == nullptr)
		return;

	ibValueFilterItem* added = target->Add(ibValue(field), ibComparisonKind_Equal, ibValue(), true);
	// THE FIELD IS KNOWN, SO THE VALUE'S TYPE IS KNOWN — seed the right-hand side
	// with what that type calls empty (False for a Boolean, an empty reference of
	// the right kind) rather than with nothing. Same rule as re-picking the field
	// in the Left cell, so both roads leave the row in the same state.
	if (added != nullptr)
		added->SetRight(ibValueTypeDescription::AdjustValue(
			added->GetRightTypeDescription(), GetMetaData()));
	RefreshFilterTree(added != nullptr ? ibValue(added) : ibValue());
}

// A NEW LINE IS EMPTY. Taking whatever happens to be selected in the field tree
// invents a condition the user did not ask for — and worse, fixes its TYPE, so the
// right-hand side is already narrowed before anything was chosen. The line appears
// blank and the picker fills it in, which is also what makes "add into this group"
// mean what it says.
void ibFilterEditor::OnFilterAdd(wxCommandEvent&)
{
	ibValueFilterGroup* target = m_model != nullptr
		? m_model->GetTargetGroup(m_view->GetSelection()) : nullptr;
	if (target == nullptr)
		return;
	ibValueFilterItem* added = target->Add(ibValue(new ibValueCompositionField()),
		ibComparisonKind_Equal, ibValue(), true);
	RefreshFilterTree(added != nullptr ? ibValue(added) : ibValue());
}

void ibFilterEditor::OnFilterRemove(wxCommandEvent&)
{
	if (m_model == nullptr || m_view == nullptr)
		return;

	const ibDataViewItem sel = m_view->GetSelection();
	if (!sel.IsOk())
		return;

	// The ROOT is not removable — an empty filter is an empty root group, not the
	// absence of one, and taking it away would leave nowhere to add.
	ibValueFilterGroup* parent = m_model->GetOwnerGroup(sel);
	if (parent == nullptr)
		return;

	ibValueFilterItem* line = m_model->GetItem(sel);
	const ibValue child = line != nullptr ? ibValue(line) : ibValue(m_model->GetGroup(sel));
	const size_t idx = parent->IndexOf(child);
	if (idx < parent->Count())
		parent->Remove(idx);

	RefreshFilterTree();
}

void ibFilterEditor::OnFilterAddGroup(wxCommandEvent&)
{
	if (m_model == nullptr || m_view == nullptr)
		return;
	ibValueFilterGroup* target = m_model->GetTargetGroup(m_view->GetSelection());
	if (target == nullptr)
		return;
	// AND by default — the operator most groups keep, and the one the drop-down in
	// the group's own row changes when it is not.
	ibValueFilterGroup* added = target->AddGroup(ibFilterGroupKind_And);
	RefreshFilterTree(added != nullptr ? ibValue(added) : ibValue());
}

void ibFilterEditor::OnFilterCopy(wxCommandEvent&)
{
	if (m_model == nullptr || m_view == nullptr)
		return;
	const ibDataViewItem sel = m_view->GetSelection();
	ibValueFilterItem* source = m_model->GetItem(sel);
	ibValueFilterGroup* target = m_model->GetTargetGroup(sel);
	if (source == nullptr || target == nullptr)
		return;

	// A COPY IS A NEW LINE WITH THE SAME ANSWERS — including the field object on
	// either side, so the copy keeps the type that makes its value editable.
	ibValueFilterItem* copy = target->Add(source->GetLeft(), source->GetComparison(),
		source->GetRight(), source->GetUse());
	if (copy != nullptr) {
		copy->SetDisplayMode(source->GetDisplayMode());
		copy->SetPresentation(source->GetPresentation());
	}
	RefreshFilterTree(copy != nullptr ? ibValue(copy) : ibValue());
}

void ibFilterEditor::OnFilterGroupSelected(wxCommandEvent&)
{
	if (m_model == nullptr || m_view == nullptr)
		return;
	const ibDataViewItem sel = m_view->GetSelection();
	if (!sel.IsOk())
		return;

	// The selected line's PARENT is where the new group goes — grouping wraps
	// lines, it does not move them somewhere else.
	ibValueFilterGroup* parent = m_model->GetOwnerGroup(sel);
	if (parent == nullptr)
		parent = m_model->GetRoot();
	if (parent == nullptr)
		return;

	const ibValue selected = m_model->GetItem(sel) != nullptr
		? ibValue(m_model->GetItem(sel))
		: ibValue(m_model->GetGroup(sel));

	const size_t idx = parent->IndexOf(selected);
	if (idx >= parent->Count())
		return;

	ibValueFilterGroup* wrapper = parent->GroupChildren({ idx }, ibFilterGroupKind_And);
	// The cursor stays on the LINE that was grouped, not on the wrapper: the user
	// asked to put that line in a group, and it is still the line they were on.
	RefreshFilterTree(wrapper != nullptr ? selected : ibValue());
}

void ibFilterEditor::OnFilterUngroup(wxCommandEvent&)
{
	if (m_model == nullptr || m_view == nullptr)
		return;
	const ibDataViewItem sel = m_view->GetSelection();
	ibValueFilterGroup* group = m_model->GetGroup(sel);
	if (group == nullptr)
		return;   // only a group can be ungrouped

	ibValueFilterGroup* parent = m_model->GetOwnerGroup(sel);
	if (parent == nullptr)
		return;   // the ROOT has no parent to lift its children into

	for (size_t i = 0; i < parent->Count(); ++i) {
		if (parent->GetGroup(i) == group) {
			parent->UngroupChild(i);
			break;
		}
	}
	RefreshFilterTree();
}

// Moving belongs to the GROUP — it is the one that knows where its children sit
// (ibValueFilterGroup::MoveChild). The editor only says which line and which way,
// and gets back the line to put the cursor on.
static ibValue ibMoveSelectedFilterChild(ibFilterTreeModel* model, ibDataViewCtrl* view, int delta)
{
	if (model == nullptr || view == nullptr)
		return ibValue();
	const ibDataViewItem sel = view->GetSelection();
	if (!sel.IsOk())
		return ibValue();

	ibValueFilterGroup* parent = model->GetOwnerGroup(sel);
	if (parent == nullptr)
		return ibValue();   // the root line has nowhere to move within

	ibValueFilterItem* line = model->GetItem(sel);
	const ibValue child = line != nullptr ? ibValue(line) : ibValue(model->GetGroup(sel));
	const size_t idx = parent->IndexOf(child);
	if (idx >= parent->Count())
		return ibValue();

	parent->MoveChild(idx, delta);
	// THE LINE TRAVELS WITH THE CURSOR. Moving a row and leaving the selection
	// behind reads as "nothing happened" — the next press then moves a different
	// row, which is worse than doing nothing.
	return child;
}

void ibFilterEditor::OnFilterMoveUp(wxCommandEvent&)
{
	if (m_model == nullptr || m_view == nullptr)
		return;
	RefreshFilterTree(ibMoveSelectedFilterChild(m_model, m_view, -1));
}

void ibFilterEditor::OnFilterMoveDown(wxCommandEvent&)
{
	if (m_model == nullptr || m_view == nullptr)
		return;
	RefreshFilterTree(ibMoveSelectedFilterChild(m_model, m_view, +1));
}

void ibFilterEditor::OnFilterItemActivated(ibDataViewEvent& event)
{
	if (m_view != nullptr)
		m_view->EditItem(event.GetItem(), event.GetDataViewColumn());
	event.Skip();
}

// THE SAME VERBS AS THE TOOLBAR, raising the same ids — one implementation, and a
// user who learns either road has learned both. The accelerators are spelled out
// because that is where people look for them.
void ibFilterEditor::OnContextMenu(ibDataViewEvent&)
{
	// VIEW ONLY — the second road to the verbs the toolbar above already refuses. No menu at all
	// rather than a menu of greyed items: a disabled item still invites the click.
	if (m_readOnly)
		return;

	wxMenu menu;

	// ONE PICTURE PER VERB — the same art ids the toolbar raises, through the same
	// helper. Spelled bare here once, the menu drifted from its own toolbar: the
	// identical command wore a picture on one road and none on the other.
	ibAppendCmd(menu, kFilterCmdAdd, _("New item") + wxT("\tIns"),
		wxASCII_STR(wxART_NEW), this);
	ibAppendCmd(menu, kFilterCmdAddGroup, _("New group") + wxT("\tCtrl+F9"),
		wxASCII_STR(wxART_FOLDER), this);
	menu.AppendSeparator();
	ibAppendCmd(menu, kFilterCmdCopy, _("Copy") + wxT("\tF9"),
		wxASCII_STR(wxART_COPY), this);
	ibAppendCmd(menu, kFilterCmdRemove, _("Delete") + wxT("\tDel"),
		wxASCII_STR(wxART_DELETE), this);
	menu.AppendSeparator();
	ibAppendCmd(menu, kFilterCmdMoveUp, _("Move up") + wxT("\tCtrl+Shift+Up"),
		wxASCII_STR(wxART_GO_UP), this);
	ibAppendCmd(menu, kFilterCmdMoveDown, _("Move down") + wxT("\tCtrl+Shift+Down"),
		wxASCII_STR(wxART_GO_DOWN), this);
	menu.AppendSeparator();
	ibAppendCmd(menu, kFilterCmdGroup, _("Group conditions"),
		wxASCII_STR(wxART_LIST_VIEW), this);
	ibAppendCmd(menu, kFilterCmdUngroup, _("Ungroup"),
		wxASCII_STR(wxART_NORMAL_FILE), this);

	// GREYED, NOT HIDDEN — a command that disappears reads as "this build cannot
	// do that"; a greyed one reads as "not here", which is the truth.
	const ibDataViewItem sel = m_view->GetSelection();
	const bool isGroup = m_model != nullptr && m_model->GetGroup(sel) != nullptr;
	const bool isRoot  = sel.IsOk() && m_model != nullptr && !m_model->GetParent(sel).IsOk();
	menu.Enable(kFilterCmdCopy, m_model != nullptr && m_model->GetItem(sel) != nullptr);
	menu.Enable(kFilterCmdRemove, sel.IsOk() && !isRoot);
	menu.Enable(kFilterCmdMoveUp, sel.IsOk() && !isRoot);
	menu.Enable(kFilterCmdMoveDown, sel.IsOk() && !isRoot);
	menu.Enable(kFilterCmdGroup, sel.IsOk() && !isRoot);
	menu.Enable(kFilterCmdUngroup, isGroup && !isRoot);

	menu.Bind(wxEVT_MENU, [this](wxCommandEvent& e) { OnFilterAdd(e); }, kFilterCmdAdd);
	menu.Bind(wxEVT_MENU, [this](wxCommandEvent& e) { OnFilterAddGroup(e); }, kFilterCmdAddGroup);
	menu.Bind(wxEVT_MENU, [this](wxCommandEvent& e) { OnFilterCopy(e); }, kFilterCmdCopy);
	menu.Bind(wxEVT_MENU, [this](wxCommandEvent& e) { OnFilterRemove(e); }, kFilterCmdRemove);
	menu.Bind(wxEVT_MENU, [this](wxCommandEvent& e) { OnFilterMoveUp(e); }, kFilterCmdMoveUp);
	menu.Bind(wxEVT_MENU, [this](wxCommandEvent& e) { OnFilterMoveDown(e); }, kFilterCmdMoveDown);
	menu.Bind(wxEVT_MENU, [this](wxCommandEvent& e) { OnFilterGroupSelected(e); }, kFilterCmdGroup);
	menu.Bind(wxEVT_MENU, [this](wxCommandEvent& e) { OnFilterUngroup(e); }, kFilterCmdUngroup);

	PopupMenu(&menu);
}
