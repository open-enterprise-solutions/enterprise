#include "frontend/win/dlgs/listSettings/listSettings.h"
#include "frontend/win/dlgs/listSettings/filterTreeModel.h"   // the filter is a TREE — model + column ids
#include "backend/metaCollection/metaObjectComposite.h"
#include "backend/metaCollection/partial/commonObject.h"
#include "backend/metaCollection/attribute/metaAttributeObject.h"

#include "backend/metadataConfiguration.h"
#include "backend/objCtor.h"

#include "frontend/win/ctrls/controlTextEditor.h"
#include "frontend/visualView/ctrl/typeControl.h"
#include "frontend/visualView/ctrl/frame.h"



#include "frontend/visualView/ctrl/tableBoxColumnRenderer.h"   // ibDataViewValueRenderer — the cell is drawn by the TABLE's renderer

#include "backend/system/value/valueType.h"
#include "backend/model.h"                                  // ibValueModel + ibValueModelColumnCollection (PATH A field source)
#include "backend/system/value/valueDynamicList.h"   // ibValueDynamicList — the dialog is built on it
#include "backend/query/queryable.h"                            // ibBackendQueryable::GetColumns
#include "backend/query/queryColumn.h"                          // ibBackendQueryColumn
#include "backend/srcDataObject.h"                              // ibSourceDataObject::ibSourceExplorer + ConvertToMetaIds (dot-walk)
#include "backend/metaCollection/partial/reference/reference.h" // ibValueReferenceDataObject::Create (reference-as-source)

#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/button.h>
#include <wx/treectrl.h>
#include <wx/splitter.h>
#include <wx/dnd.h>
#include <wx/menu.h>
#include <wx/imaglist.h>
#include <wx/stattext.h>
#include <wx/app.h>

// Filter-tab COMMANDS — raised by the toolbar and by the context menu alike, so
// both roads end in one handler and cannot drift apart. (The filter's column ids
// live with its model, in filterTreeModel.h — the tab is a tree now.)
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

// Sort / Group tab dataview columns.
// COLUMN 0 IS RESERVED by the ibDataViewCtrl fork (a model column 0 paints blank
// and does not edit) — the filter tab starts at 1 for exactly this reason, and the
// sort / grouping tabs kept starting at 0, which is why their Field cell could not
// be opened at all: F2 and the Select button had nothing to open.
enum { eOrderField = 1, eOrderDir };
enum { eGroupField = 1, eGroupKind };

// ---------------------------------------------------------------------------
//  Available-fields tree — the LEFT panel of a settings tab lists the
//  source's fields; a reference field lazily expands into its target's fields
//  (Supplier.Region.Country...). Double-clicking (or Add) puts the field into the
//  tab's list on the right. Modelled on advpropSource's source picker, embedded
//  rather than modal (the header TODO to unify the two into one picker stands).
// ---------------------------------------------------------------------------

// Tree-item payload for one source field: its dot-path (technical names), the leaf
// id + type (so a filter row edits its value through the runtime), and the referenced
// target ids for lazy expansion.
struct ibSourceFieldNode : public wxTreeItemData {
	wxString              m_path;          // technical: Reference.Number — what the query is built from
	// READABLE, and accumulated the SAME way: "Ссылка.Номер". A field two hops deep
	// presented by its leaf alone ("Номер") loses the only thing that says WHICH
	// number it is — which is why a dot-walked field read as a plain one.
	wxString              m_presentation;
	ibMetaID              m_leafId = wxNOT_FOUND;
	ibTypeDescription     m_type;
	std::vector<ibMetaID> m_refTypes;   // non-empty => reference field, lazy-expand
	bool                  m_loaded = false;
};

// ===========================================================================
//  Shared helpers — art, menu commands, grid styling, source-field walk
// ===========================================================================


// Append each field of `explorer` under `parent`, carrying the accumulated dot-path.
// A reference field gets a dummy [+] and expands lazily (ExpandSourceFieldNode); a
// tabular section is skipped (a setting binds a scalar / reference field, not a section).
static void AppendSourceFields(wxTreeCtrl* tree, const wxTreeItemId& parent,
	const ibSourceDataObject::ibSourceExplorer& explorer, const wxString& prefix,
	const ibMetaData* metaData, const wxString& prefixText = wxEmptyString)
{
	for (unsigned int i = 0; i < explorer.GetHelperCount(); ++i) {
		const auto* col = explorer.GetHelper(i);
		if (col == nullptr || col->IsTableSection())
			continue;
		ibSourceFieldNode* data = new ibSourceFieldNode();
		data->m_path     = prefix.IsEmpty() ? col->GetSourceName() : prefix + wxT(".") + col->GetSourceName();
		const wxString label = col->GetSourceSynonym().IsEmpty() ? col->GetSourceName() : col->GetSourceSynonym();
		data->m_presentation = prefixText.IsEmpty() ? label : prefixText + wxT(".") + label;
		data->m_leafId   = static_cast<ibMetaID>(col->GetSourceId());
		data->m_type     = col->GetTypeDesc();
		data->m_refTypes = ibValueReferenceDataObject::ConvertToMetaIds(col->GetClsidList(), metaData);
		// THE SYNONYM IS WHAT A USER READS. The name is the technical identifier the
		// PATH is built from (above) — showing it in the picker makes the form speak
		// in identifiers instead of in the words the configuration author chose.
		// GetSynonym falls back to the name when there is none, so nothing is blank.
		const wxTreeItemId item = tree->AppendItem(parent, label, 0, 0, data);   // icon 0 = attribute
		if (!data->m_refTypes.empty())
			tree->AppendItem(item, wxEmptyString);   // dummy -> [+] (a reference expands into its target's fields)
	}
}

// Lazily build a reference field node's children (its target's fields). An EMPTY typed
// reference-as-source vends the target's explorer; its fields are copied into tree nodes
// synchronously, so the temporary reference can die after.
static void ExpandSourceFieldNode(wxTreeCtrl* tree, const wxTreeItemId& item, const ibMetaData* metaData)
{
	ibSourceFieldNode* data = dynamic_cast<ibSourceFieldNode*>(tree->GetItemData(item));
	if (data == nullptr || data->m_refTypes.empty() || data->m_loaded || metaData == nullptr)
		return;
	// FILL FIRST, THEN DROP THE DUMMY. Deleting it up front and marking the node
	// loaded meant that if the target vended nothing this time, the node lost its
	// [+] for good and could never be expanded again — the tree simply stopped
	// unfolding. Now the placeholder only goes once there is something to replace
	// it with, and a fruitless attempt can be retried.
	const size_t before = tree->GetChildrenCount(item, false);
	for (const ibMetaID& target : data->m_refTypes) {
		ibValue refValue = ibValueReferenceDataObject::Create(metaData, target);
		ibSourceDataObject* refObj = nullptr;
		if (!refValue.ConvertToValue(refObj) || refObj == nullptr)
			continue;
		if (const auto* refExplorer = refObj->GetSourceExplorer())
			// NO DEPTH LIMIT. The tree unfolds LAZILY — a level exists only where the
			// user opened it — so a self-referencing type cannot run away on its own.
			AppendSourceFields(tree, item, *refExplorer, data->m_path, metaData, data->m_presentation);
	}

	if (tree->GetChildrenCount(item, false) <= before)
		return;   // nothing came back — keep the [+] and let the user try again

	// Drop the placeholder (it is the FIRST child, the real fields were appended after it).
	wxTreeItemIdValue cookie;
	const wxTreeItemId dummy = tree->GetFirstChild(item, cookie);
	if (dummy.IsOk() && tree->GetItemData(dummy) == nullptr)
		tree->Delete(dummy);
	data->m_loaded = true;
}

// ONE PICTURE PER VERB. The toolbars and the context menus show the same command;
// letting each spell its own art means they drift the moment one of them changes.
// (Art ids, not files: they follow the platform's theme, as the rest of the shell does.)
static wxBitmapBundle ibSettingsArt(const wxString& artId, const wxWindow* owner)
{
	return wxArtProvider::GetBitmapBundle(artId, wxASCII_STR(wxART_MENU),
		owner != nullptr ? owner->FromDIP(wxSize(16, 16)) : wxSize(16, 16));
}

// Append a command that LOOKS the same wherever it appears.
static wxMenuItem* ibAppendCmd(wxMenu& menu, int id, const wxString& label,
	const wxString& artId, const wxWindow* owner)
{
	wxMenuItem* item = menu.Append(id, label);
	if (item != nullptr)
		item->SetBitmap(ibSettingsArt(artId, owner));
	return item;
}

// THE TABLES READ AS TABLES. They used to take the dialog's own grey background,
// which made the grid and the panel one flat surface — the rows had no field of
// their own to sit on. A list background (the system's own, so it follows the
// theme) plus a faint alternating row makes the data area obvious without drawing
// a single border.
static void ibStyleSettingsGrid(ibDataViewCtrl* view)
{
	if (view == nullptr)
		return;
	view->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_LISTBOX));
	view->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_LISTBOXTEXT));

	// A TINT OF THE BACKGROUND, not a fixed grey: on a dark theme the same rule
	// lightens instead of darkening, so the banding stays subtle either way.
	const wxColour base = wxSystemSettings::GetColour(wxSYS_COLOUR_LISTBOX);
	const bool dark = (base.Red() + base.Green() + base.Blue()) < 3 * 128;
	view->SetAlternateRowColour(dark ? base.ChangeLightness(115) : base.ChangeLightness(96));
}

// ===========================================================================
//  Cell renderers — the value cell each tab edits through
// ===========================================================================


// Drop target for a tab's right-hand composition panel: a field dragged from the LEFT
// tree (remembered as m_dragItem) is added to that tab's list on drop. The text payload
// only exists to enable DnD; the field itself comes from m_dragItem (same-process drag).
class ibFieldDropTarget : public wxTextDropTarget {
public:
	explicit ibFieldDropTarget(std::function<void()> onDrop) : m_onDrop(std::move(onDrop)) {}
	bool OnDropText(wxCoord, wxCoord, const wxString&) override { if (m_onDrop) m_onDrop(); return true; }
private:
	std::function<void()> m_onDrop;
};

// THE ROW-VALUE CELL — one cell for every editable thing on the Sort and Group
// tabs. A sort line has a FIELD and a DIRECTION; a grouping line has a FIELD and a
// KIND. Three of those four are values of registered types (a composition field, a
// SortDirection, a grouping kind), so the cell does not know what it is editing:
// it asks the row for the value, and the VALUE decides how it is chosen — a field
// through the source-tree picker, an enumeration through the runtime's quick
// choice. That is why adding "kind" later costs a getter and a setter, not a cell.
//
// Drawn by the table's own renderer, so a value reads here exactly as in a list.
class ibRowValueCellRenderer : public ibDataViewValueRenderer, public ibControlFrame {
public:
	using Getter = std::function<ibValue(const ibDataViewItem&)>;
	using Setter = std::function<void(const ibDataViewItem&, const ibValue&)>;

	ibRowValueCellRenderer(ibDialogListSettings* dialog, Getter get, Setter set)
		: ibDataViewValueRenderer(nullptr), m_dialog(dialog),
		  m_get(std::move(get)), m_set(std::move(set)) {
	}

	virtual bool HasEditorCtrl() const override { return true; }

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
	// THE VALUE DECIDES. A field opens the source tree (this dialog is the only
	// thing that knows the source); anything else is a registered type and goes
	// through the runtime's own quick choice, which is what makes a direction and a
	// grouping kind editable without a line of list-building here.
	void OnSelect(wxCommandEvent&) {
		ibValue current; GetControlValue(current);
		ibValueCompositionField* asField = nullptr;
		if (current.ConvertToValue(asField) || current.IsEmpty()) {
			// CLOSE THE CELL EDITOR FIRST. The picker is a modal window opened ON TOP
			// of a live editor that holds the mouse and the focus — with it up, the
			// picker's tree never sees the clicks that would expand a reference, so
			// the tree looked like it "could not unfold". The row was captured when
			// the editor was created, so nothing is lost by closing it now.
			const ibDataViewItem row = m_row;
			wxWindow* parent = m_dialog;
			FinishSelecting();
			const wxString held = (asField != nullptr) ? asField->GetPath() : wxString();
			if (ibValueCompositionField* chosen = m_dialog->ChooseField(parent, held)) {
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

	ibDialogListSettings* m_dialog;
	Getter m_get;
	Setter m_set;
	ibDataViewItem m_row;
};

// THE CELL STANDS IN THE SAME LINE AS EVERY OTHER VALUE EDITOR — ibTypeControlFactory,
// the one gate that already knows the whole sequence: nothing there yet → make the
// declared type (asking only when there is more than one) → quick choice → the type's
// own selection form. A text control on a form gets that for free; this cell used to,
// and lost it when it stopped deriving the factory, which is why the sequence was
// written out again by hand here.
//
// The gate asks ONE question — GetTypeDesc — and the answer belongs to the ROW, not to
// the renderer: a condition knows that its left side is a composition FIELD and that its
// right side takes whatever type that field lends. So this only forwards.
class ibDialogListSettings::ibFilterValueRenderer : public ibDataViewValueRenderer,
	public ibControlFrame, public ibTypeControlFactory {
	ibDialogListSettings* m_dialog;
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
	ibFilterValueRenderer(ibDialogListSettings* dialog, unsigned int side)
		// NO TABLE COLUMN behind this cell — the renderer guards on that everywhere
		// (it only uses the column to refresh the owning form), so a filter cell and
		// a table cell are drawn by the very same code.
		: ibDataViewValueRenderer(nullptr), m_dialog(dialog), m_side(side) {
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
		const ibCtorAbstractType* so = activeMetaData->GetAvailableCtor(selValue.GetClassType());
		if (so != nullptr && so->GetObjectTypeCtor() == ibCtorObjectType_object_primitive) {
			return true;
		}
		else if (so != nullptr && so->GetObjectTypeCtor() == ibCtorObjectType_object_enum) {
			return true;
		}
		else if (so != nullptr && so->GetObjectTypeCtor() == ibCtorObjectType_object_meta_value) {
			const ibCtorMetaValueType* meta_so = dynamic_cast<const ibCtorMetaValueType*>(so);
			if (meta_so != nullptr) {
				const ibValueMetaObjectRecordDataRef* metaObject = dynamic_cast<const ibValueMetaObjectRecordDataRef*>(meta_so->GetMetaObject());
				if (metaObject != nullptr)
					return metaObject->HasQuickChoice();
			}
		}
		return false;
	}

private:

	// The currently-selected CONDITION, or nullptr — a group row is not one, and
	// neither is an empty selection. The row carries the value it stands for
	// (filterTreeModel.h), so this is a question to the row, not arithmetic on a
	// row number: a tree has no 1-based index to reverse.
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
		return m_item.IsOk() ? m_item : m_dialog->GetFilterView()->GetSelection();
	}

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
				item->SetRight(ibValueTypeDescription::AdjustValue(after, m_dialog->SourceMetaData()));
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
		case kFilterColRight: item->SetRight(value); break;
		default:              item->SetLeft(value);  break;
		}

		NotifyRowChanged();
	}

	// AND SAY SO. Writing the object is not enough — the cell paints what the
	// MODEL reports, so a value changed behind its back leaves the old text on
	// screen and the whole cell reads as "nothing happened".
	void NotifyRowChanged() const {
		ibDataViewCtrl* view = m_dialog->GetFilterView();
		ibFilterTreeModel* model = m_dialog->GetFilterModel();
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
			wxWindow* parent = m_dialog;
			FinishSelecting();
			const wxString held = (asField != nullptr) ? asField->GetPath() : wxString();
			if (ibValueCompositionField* chosen = m_dialog->ChooseField(parent, held)) {
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
//  Tab models — Sort and Group are flat lists (the Filter tab has its own tree model)
// ===========================================================================


// ---- Sort model — virtual list over the dialog's BUFFER sort list (Field + editable Direction). ----
class ibDialogListSettings::ibOrderModel : public ibDataViewVirtualListModel {
	ibDialogListSettings* m_dialog;
public:
	explicit ibOrderModel(ibDialogListSettings* dialog) : ibDataViewVirtualListModel(), m_dialog(dialog) {}
	ibValueSortList* GetOrder() const { return m_dialog->GetOrderList(); }
	void ResetFromList() { ibValueSortList* o = GetOrder(); Reset(o != nullptr ? (unsigned int)o->Count() : 0u); }
	virtual void GetValueByRow(wxVariant& variant, unsigned row, unsigned col) const override {
		ibValueSortList* o = GetOrder();
		if (o == nullptr) return;
		if (row >= o->Count())
			return;   // same reason as the grouping model above
		ibValueSortItem* it = o->GetItem(row);
		if (it == nullptr) return;
		// EVERY COLUMN IS A VALUE PRESENTING ITSELF — the field by its readable path,
		// the direction by its enumeration caption. No indices, no parallel lists.
		if (col == eOrderField)
			variant = it->GetFieldObject() != nullptr ? it->GetFieldObject()->GetString() : it->GetField();
		else if (col == eOrderDir)
			variant = ibValue::CreateEnumObject<ibValueEnumSortDirection>(it->GetDirection()).GetString();
	}
	virtual bool SetValueByRow(const wxVariant& variant, unsigned row, unsigned col) override {
		ibValueSortList* o = GetOrder();
		if (o == nullptr) return false;
		ibValueSortItem* it = o->GetItem(row);
		if (it == nullptr) return false;
		if (col == eOrderDir) {   // the Direction choice carries the ibSortDirection as its index
			it->SetDirection(static_cast<ibSortDirection>(variant.GetLong()));
			return true;
		}
		return false;
	}
};

// ---- Group model — virtual list over the dialog's BUFFER group list (Field). ----
class ibDialogListSettings::ibGroupModel : public ibDataViewVirtualListModel {
	ibDialogListSettings* m_dialog;
public:
	explicit ibGroupModel(ibDialogListSettings* dialog) : ibDataViewVirtualListModel(), m_dialog(dialog) {}
	ibValueGroupList* GetGroup() const { return m_dialog->GetGroupList(); }
	void ResetFromList() { ibValueGroupList* g = GetGroup(); Reset(g != nullptr ? (unsigned int)g->Count() : 0u); }
	virtual void GetValueByRow(wxVariant& variant, unsigned row, unsigned col) const override {
		ibValueGroupList* g = GetGroup();
		if (g == nullptr) return;
		// BOUNDS FIRST. The view paints rows it has, the list may already have fewer
		// (a Reset lands after the paint is queued), and GetKind / GetField index
		// straight into the vector — reading past the end crashed on repaint.
		if (row >= g->Count())
			return;
		if (col == eGroupField)
			variant = g->GetFieldObject(row) != nullptr ? g->GetFieldObject(row)->GetString() : g->GetField(row);
		else if (col == eGroupKind)
			variant = ibValue::CreateEnumObject<ibValueEnumGroupKind>(g->GetKind(row)).GetString();
	}
	virtual bool SetValueByRow(const wxVariant&, unsigned, unsigned) override { return false; }
};

// ===========================================================================
//  Construction, buffers, and the small shared accessors
// ===========================================================================


// ---------------------------------------------------------------------------
//  Construction
// ---------------------------------------------------------------------------

// The two public ctors differ ONLY in how they bind the field source (m_list set or
// not). Everything else — the three tabs, load, OK binding — is identical; the field
// source is read once, in BuildFilterFields(), which dispatches on m_list.

ibDialogListSettings::ibDialogListSettings(wxWindow* parent, ibValueDynamicList* list)
	: wxDialog(parent, wxID_ANY, _("List settings"), wxDefaultPosition, wxSize(660, 450),
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	  m_model(list), m_list(list), m_settings(new ibValueListSettings())
{
	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

	wxNotebook* notebook = new wxNotebook(this, wxID_ANY);
	// Tabs are GATED by the model's Features (Max: "turn the flag off → the tab is hidden; the default
	// parameters still change"). Default = all on. The first available tab is the selected one.
	const ibValueModel::Features feats = (m_model != nullptr) ? m_model->GetFeatures() : ibValueModel::Features{};
	// THE QUERY TAB IS A DEVELOPER TOOL. Writing the list's source query is
	// configuring the form, not using it — an end user opening "Filter" has no
	// business being offered the query text, and nothing they could do there would
	// survive as their setting.
	if (appData->DesignerMode())
		notebook->AddPage(BuildQueryPage(notebook), _("Query"), true);   // FIRST tab — arbitrary-query source
	if (feats.Has(ibValueModel::Features::Filters))  notebook->AddPage(BuildFilterPage(notebook), _("Filter"), notebook->GetPageCount() == 0);
	if (feats.Has(ibValueModel::Features::Sorting))  notebook->AddPage(BuildOrderPage(notebook),  _("Sort"),   notebook->GetPageCount() == 0);
	if (feats.Has(ibValueModel::Features::Grouping)) notebook->AddPage(BuildGroupPage(notebook),  _("Group"),  notebook->GetPageCount() == 0);
	mainSizer->Add(notebook, 1, wxALL | wxEXPAND, FromDIP(6));

	wxStdDialogButtonSizer* btns = CreateStdDialogButtonSizer(wxOK | wxCANCEL);
	mainSizer->Add(btns, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxALIGN_RIGHT, FromDIP(6));

	SetSizer(mainSizer);

	// Transactional open: load the dialog's BUFFER from the composer (the store) so it shows the current state.
	if (m_model != nullptr && m_settings != nullptr)
		// THE LIVE SETTINGS TOO: a tree filter reaches the composer as one expression
		// and cannot be read back out of it — the model still holds the tree itself.
		ibLoadSettingsFromComposer(m_settings, m_model->GetModelComposer(), m_model->GetListSettings());

	LoadFromSettings();

	Bind(wxEVT_BUTTON, &ibDialogListSettings::OnOk, this, wxID_OK);
}

// General-model ctor — same body as the dynamic-list ctor, but m_list stays null
// (no source explorer / no composer). The Filter "Add" picker's fields come from the
// model's columns (PATH A — BuildFilterFieldsFromColumns). m_list-specific code below
// (FillFieldChoice, the source-explorer field builder, RefreshComposerSettings) is
// guarded on m_list != nullptr, so the dialog runs correctly with m_list == null.
ibDialogListSettings::ibDialogListSettings(wxWindow* parent, ibValueModel* model)
	: wxDialog(parent, wxID_ANY, _("List settings"), wxDefaultPosition, wxSize(660, 450),
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	  m_model(model), m_list(nullptr), m_settings(new ibValueListSettings())
{
	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

	wxNotebook* notebook = new wxNotebook(this, wxID_ANY);
	// Tabs are GATED by the model's Features (Max: "turn the flag off → the tab is hidden; the default
	// parameters still change"). Default = all on. The first available tab is the selected one.
	const ibValueModel::Features feats = (m_model != nullptr) ? m_model->GetFeatures() : ibValueModel::Features{};
	if (feats.Has(ibValueModel::Features::Filters))  notebook->AddPage(BuildFilterPage(notebook), _("Filter"), notebook->GetPageCount() == 0);
	if (feats.Has(ibValueModel::Features::Sorting))  notebook->AddPage(BuildOrderPage(notebook),  _("Sort"),   notebook->GetPageCount() == 0);
	if (feats.Has(ibValueModel::Features::Grouping)) notebook->AddPage(BuildGroupPage(notebook),  _("Group"),  notebook->GetPageCount() == 0);
	mainSizer->Add(notebook, 1, wxALL | wxEXPAND, FromDIP(6));

	wxStdDialogButtonSizer* btns = CreateStdDialogButtonSizer(wxOK | wxCANCEL);
	mainSizer->Add(btns, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxALIGN_RIGHT, FromDIP(6));

	SetSizer(mainSizer);

	// Transactional open: load the dialog's edit BUFFER (m_settings) FROM the composer (the committed store)
	// so it shows the current Filter / Sort / Group; the user edits the buffer; OK commits it back
	// (ShowListSettingsDialog), Cancel discards. The fetch reads the composer, never this buffer.
	if (m_model != nullptr && m_settings != nullptr)
		// THE LIVE SETTINGS TOO: a tree filter reaches the composer as one expression
		// and cannot be read back out of it — the model still holds the tree itself.
		ibLoadSettingsFromComposer(m_settings, m_model->GetModelComposer(), m_model->GetListSettings());

	LoadFromSettings();

	Bind(wxEVT_BUTTON, &ibDialogListSettings::OnOk, this, wxID_OK);
}

ibValueFilterList* ibDialogListSettings::GetFilterList() const
{
	return m_settings != nullptr ? m_settings->GetFilter() : nullptr;
}

ibValueSortList* ibDialogListSettings::GetOrderList() const
{
	return m_settings != nullptr ? m_settings->GetOrder() : nullptr;
}

ibValueGroupList* ibDialogListSettings::GetGroupList() const
{
	return m_settings != nullptr ? m_settings->GetGroup() : nullptr;
}

ibValueFilterGroup* ibDialogListSettings::GetFilterRoot() const
{
	return m_settings ? m_settings->GetFilterRoot() : nullptr;
}

// A ROW OF A VIRTUAL LIST IS ITS 1-BASED ID. Written once here rather than in each
// lambda: the off-by-one lived in five copies and every one of them had to agree.
void ibDialogListSettings::SelectLastRow(ibDataViewCtrl* view, size_t count)
{
	if (view == nullptr || count == 0)
		return;
	const ibDataViewItem row(reinterpret_cast<void*>(count));   // 1-based id of the last line
	view->Select(row);
	view->EnsureVisible(row);
}

// ===========================================================================
//  Page construction — one builder per tab
// ===========================================================================


// ---------------------------------------------------------------------------
//  Pages
// ---------------------------------------------------------------------------

// The FIRST tab (dynamic-list only) — arbitrary-query source: an enable flag + the query text. When off, the list
// takes its picked metaobject source; when on, this TEXT is the source (composer.FromText). Edits the list's OWN
// UseCustomQuery / CustomQuery properties (serialised), NOT the settings buffer; applied to the list on OK.
wxWindow* ibDialogListSettings::BuildQueryPage(wxWindow* parent)
{
	wxPanel* page = new wxPanel(parent, wxID_ANY);
	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

	m_queryUseCheck = new wxCheckBox(page, wxID_ANY, _("Arbitrary query"));
	sizer->Add(m_queryUseCheck, 0, wxALL, FromDIP(6));

	m_queryText = new wxTextCtrl(page, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxHSCROLL);
	m_queryText->Enable(false);   // meaningful only when the flag is on
	sizer->Add(m_queryText, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, FromDIP(6));

	m_queryUseCheck->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent& e) {
		if (m_queryText != nullptr) m_queryText->Enable(e.IsChecked());
	});

	page->SetSizer(sizer);
	return page;
}

wxWindow* ibDialogListSettings::BuildFilterPage(wxWindow* parent)
{
	wxPanel* panel = new wxPanel(parent);
	// Draggable split: LEFT = available-fields tree (dot-walkable), RIGHT = the composed filter.
	wxSplitterWindow* splitter = new wxSplitterWindow(panel, wxID_ANY,
		wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3DSASH);
	splitter->SetMinimumPaneSize(FromDIP(120));

	// ---- LEFT pane: available fields (a reference field expands into its target's fields) ----
	wxPanel* leftPane = new wxPanel(splitter);
	wxBoxSizer* leftSizer = new wxBoxSizer(wxVERTICAL);
	leftSizer->Add(new wxStaticText(leftPane, wxID_ANY, _("Available fields")), 0, wxALL, FromDIP(4));
	m_filterFieldTree = new wxTreeCtrl(leftPane, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTR_HAS_BUTTONS | wxTR_SINGLE | wxTR_HIDE_ROOT | wxTR_LINES_AT_ROOT | wxTR_NO_LINES | wxTR_TWIST_BUTTONS);
	leftSizer->Add(m_filterFieldTree, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, FromDIP(4));
	leftPane->SetSizer(leftSizer);

	// ---- RIGHT pane: the filter TREE — Use / Left / Comparison / Right / Display / Presentation ----
	wxPanel* rightPane = new wxPanel(splitter);
	wxBoxSizer* rightSizer = new wxBoxSizer(wxVERTICAL);

	// THE COMMANDS LIVE ON A TOOLBAR, and the context menu offers the same ones.
	// One set of verbs, one implementation — a user who learns either has learned
	// both.
	m_filterToolbar = new wxToolBar(rightPane, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTB_HORIZONTAL | wxTB_FLAT | wxTB_NODIVIDER);
	// 16×16 — a settings dialog's toolbar sits above a grid, and the stock toolbar
	// size (24 or 32, per theme) makes it the loudest thing on the page.
	m_filterToolbar->SetToolBitmapSize(FromDIP(wxSize(16, 16)));
	m_filterToolbar->AddTool(kFilterCmdAdd, _("New item"),
		ibSettingsArt(wxASCII_STR(wxART_NEW), this), _("New item (Ins)"));
	m_filterToolbar->AddTool(kFilterCmdAddGroup, _("New group"),
		ibSettingsArt(wxASCII_STR(wxART_FOLDER), this), _("New group (Ctrl+F9)"));
	m_filterToolbar->AddTool(kFilterCmdCopy, _("Copy"),
		ibSettingsArt(wxASCII_STR(wxART_COPY), this), _("Copy (F9)"));
	m_filterToolbar->AddTool(kFilterCmdRemove, _("Delete"),
		ibSettingsArt(wxASCII_STR(wxART_DELETE), this), _("Delete (Del)"));
	m_filterToolbar->AddSeparator();
	m_filterToolbar->AddTool(kFilterCmdMoveUp, _("Move up"),
		ibSettingsArt(wxASCII_STR(wxART_GO_UP), this), _("Move up (Ctrl+Shift+Up)"));
	m_filterToolbar->AddTool(kFilterCmdMoveDown, _("Move down"),
		ibSettingsArt(wxASCII_STR(wxART_GO_DOWN), this), _("Move down (Ctrl+Shift+Down)"));
	m_filterToolbar->AddSeparator();
	m_filterToolbar->AddTool(kFilterCmdGroup, _("Group conditions"),
		ibSettingsArt(wxASCII_STR(wxART_LIST_VIEW), this), _("Group conditions"));
	m_filterToolbar->AddTool(kFilterCmdUngroup, _("Ungroup"),
		ibSettingsArt(wxASCII_STR(wxART_NORMAL_FILE), this), _("Ungroup"));
	m_filterToolbar->Realize();
	rightSizer->Add(m_filterToolbar, 0, wxEXPAND);

	m_filterView = new ibDataViewCtrl(rightPane, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxDV_ROW_LINES | wxDV_SINGLE);
	ibStyleSettingsGrid(m_filterView);
	m_filterView->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, &ibDialogListSettings::OnFilterItemActivated, this);

	// (NO local choice lists any more. A comparison and a display mode are
	// registered ENUMERATIONS — ComparisonKind / FilterDisplayMode in listFilter.h
	// — so their members and captions live there, once. Spelling them again here
	// meant an order that had to match an enum by hand and a second place to
	// translate.)

	m_filterView->AppendToggleColumn(_("Use"), kFilterColUse,
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
	m_filterView->AppendColumn(leftColumn);
	ibDataViewColumn* cmpColumn = new ibDataViewColumn(_("Comparison"),
		new ibFilterValueRenderer(this, kFilterColComparison),
		kFilterColComparison, wxNOT_FOUND, wxAlignment::wxALIGN_LEFT);
	m_filterView->AppendColumn(cmpColumn);
	ibDataViewColumn* valColumn = new ibDataViewColumn(_("Right value"),
		new ibFilterValueRenderer(this, kFilterColRight), kFilterColRight, wxNOT_FOUND, wxAlignment::wxALIGN_LEFT);
	m_filterView->AppendColumn(valColumn);
	ibDataViewColumn* modeColumn = new ibDataViewColumn(_("Display mode"),
		new ibFilterValueRenderer(this, kFilterColDisplayMode),
		kFilterColDisplayMode, wxNOT_FOUND, wxAlignment::wxALIGN_LEFT);
	m_filterView->AppendColumn(modeColumn);
	m_filterView->AppendTextColumn(_("Presentation"), kFilterColPresentation,
		wxDATAVIEW_CELL_EDITABLE, wxNOT_FOUND, wxAlignment::wxALIGN_LEFT);
	// A TREE, not a list. The control defaults to ibDataViewList — it fetches the
	// children and then draws them all at one level, so a nested filter reads as a
	// flat one and the group a line sits in is invisible. That default is why the
	// hierarchy has to be said out loud here.
	m_filterView->SetViewMode(ibDataViewTree);
	// THE EXPANDER STAYS ON THE FIRST COLUMN — NOT on "Left value". A click in the
	// expander column belongs to the tree (open / close), so the grid refuses to
	// start editing there (datavgen.cpp): hanging the tree off "Left value" is
	// exactly what made the left-hand cell impossible to edit while the right one
	// worked. The Use column carries the indent instead.
	if (m_filterView->GetColumnCount() > 0)
		m_filterView->SetExpanderColumn(m_filterView->GetColumn(0));
	rightSizer->Add(m_filterView, 1, wxALL | wxEXPAND, FromDIP(4));
	rightPane->SetSizer(rightSizer);

	splitter->SplitVertically(leftPane, rightPane, FromDIP(180));
	wxBoxSizer* panelSizer = new wxBoxSizer(wxVERTICAL);
	panelSizer->Add(splitter, 1, wxEXPAND);
	panel->SetSizer(panelSizer);

	// Populate the available-fields tree + wire it: references expand lazily, double-click adds.
	PopulateFieldTree(m_filterFieldTree);
	m_filterFieldTree->Bind(wxEVT_TREE_ITEM_EXPANDING, &ibDialogListSettings::OnFieldTreeExpanding, this);
	m_filterFieldTree->Bind(wxEVT_TREE_ITEM_ACTIVATED, &ibDialogListSettings::OnFilterFieldActivated, this);
	m_filterFieldTree->Bind(wxEVT_TREE_BEGIN_DRAG, &ibDialogListSettings::OnFieldTreeBeginDrag, this);
	rightPane->SetDropTarget(new ibFieldDropTarget([this]{ AddFilterForField(m_dragItem); }));

	// The toolbar and the context menu raise the SAME command ids, so both roads
	// end in one handler.
	m_filterToolbar->Bind(wxEVT_TOOL, &ibDialogListSettings::OnFilterAdd, this, kFilterCmdAdd);
	m_filterToolbar->Bind(wxEVT_TOOL, &ibDialogListSettings::OnFilterAddGroup, this, kFilterCmdAddGroup);
	m_filterToolbar->Bind(wxEVT_TOOL, &ibDialogListSettings::OnFilterCopy, this, kFilterCmdCopy);
	m_filterToolbar->Bind(wxEVT_TOOL, &ibDialogListSettings::OnFilterRemove, this, kFilterCmdRemove);
	m_filterToolbar->Bind(wxEVT_TOOL, &ibDialogListSettings::OnFilterMoveUp, this, kFilterCmdMoveUp);
	m_filterToolbar->Bind(wxEVT_TOOL, &ibDialogListSettings::OnFilterMoveDown, this, kFilterCmdMoveDown);
	m_filterToolbar->Bind(wxEVT_TOOL, &ibDialogListSettings::OnFilterGroupSelected, this, kFilterCmdGroup);
	m_filterToolbar->Bind(wxEVT_TOOL, &ibDialogListSettings::OnFilterUngroup, this, kFilterCmdUngroup);

	m_filterModel = new ibFilterTreeModel();
	m_filterModel->SetRoot(GetFilterRoot());
	m_filterView->AssociateModel(m_filterModel);
	m_filterView->Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, &ibDialogListSettings::OnListContextMenu, this);

	return panel;
}

wxWindow* ibDialogListSettings::BuildOrderPage(wxWindow* parent)
{
	wxPanel* panel = new wxPanel(parent);
	wxSplitterWindow* splitter = new wxSplitterWindow(panel, wxID_ANY,
		wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3DSASH);
	splitter->SetMinimumPaneSize(FromDIP(120));

	// ---- LEFT pane: available fields (dot-walkable) ----
	wxPanel* leftPane = new wxPanel(splitter);
	wxBoxSizer* leftSizer = new wxBoxSizer(wxVERTICAL);
	leftSizer->Add(new wxStaticText(leftPane, wxID_ANY, _("Available fields")), 0, wxALL, FromDIP(4));
	m_orderFieldTree = new wxTreeCtrl(leftPane, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTR_HAS_BUTTONS | wxTR_SINGLE | wxTR_HIDE_ROOT | wxTR_LINES_AT_ROOT | wxTR_NO_LINES | wxTR_TWIST_BUTTONS);
	leftSizer->Add(m_orderFieldTree, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, FromDIP(4));
	leftPane->SetSizer(leftSizer);

	// ---- RIGHT pane: the sort list — Field + editable Direction (choice), model-driven ----
	wxPanel* rightPane = new wxPanel(splitter);
	wxBoxSizer* rightSizer = new wxBoxSizer(wxVERTICAL);
	m_orderView = new ibDataViewCtrl(rightPane, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxDV_ROW_LINES | wxDV_SINGLE);
	ibStyleSettingsGrid(m_orderView);
	// FIELD and DIRECTION are both VALUES of the row — a composition field and a
	// SortDirection member — so one cell serves both: the field opens the source
	// tree, the direction opens its enumeration. Nothing here spells a list.
	m_orderView->AppendColumn(new ibDataViewColumn(_("Field"),
		new ibRowValueCellRenderer(this,
			[this](const ibDataViewItem& row) -> ibValue {
				ibValueSortItem* line = OrderLineAt(row);
				return line != nullptr ? ibValue(line->GetFieldObject()) : ibValue();
			},
			[this](const ibDataViewItem& row, const ibValue& value) {
				ibValueSortItem* line = OrderLineAt(row);
				if (line == nullptr)
					return;
				// CLEARING SENDS AN EMPTY VALUE, and an empty value fails the cast —
				// so testing only "did it convert" made "×" do nothing at all.
				ibValueCompositionField* field = nullptr;
				line->SetField(value.ConvertToValue(field) ? field : nullptr);
				if (m_orderModel != nullptr) m_orderModel->ResetFromList();
			}),
		eOrderField, wxNOT_FOUND, wxAlignment::wxALIGN_LEFT));

	m_orderView->AppendColumn(new ibDataViewColumn(_("Direction"),
		new ibRowValueCellRenderer(this,
			[this](const ibDataViewItem& row) -> ibValue {
				ibValueSortItem* line = OrderLineAt(row);
				return line != nullptr
					? ibValue::CreateEnumObject<ibValueEnumSortDirection>(line->GetDirection())
					: ibValue();
			},
			[this](const ibDataViewItem& row, const ibValue& value) {
				if (ibValueSortItem* line = OrderLineAt(row))
					line->SetDirection(value.ConvertToEnumValue<ibSortDirection>());
				if (m_orderModel != nullptr) m_orderModel->ResetFromList();
			}),
		eOrderDir, wxNOT_FOUND, wxAlignment::wxALIGN_LEFT));

	rightSizer->Add(m_orderView, 1, wxALL | wxEXPAND, FromDIP(4));

	// THE SAME COMMANDS AS THE FILTER TAB, on the same kind of toolbar: add,
	// delete, and the two that make an ordered list an ordered list. Two buttons at
	// the bottom and no way to reorder was a form that could only express "these
	// fields", never "in this order" — which is the whole content of a sort.
	wxToolBar* toolbar = new wxToolBar(rightPane, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTB_HORIZONTAL | wxTB_FLAT | wxTB_NODIVIDER);
	toolbar->SetToolBitmapSize(FromDIP(wxSize(16, 16)));
	toolbar->AddTool(wxID_ADD, _("Add"),
		ibSettingsArt(wxASCII_STR(wxART_NEW), this), _("Add (Ins)"));
	toolbar->AddTool(wxID_REMOVE, _("Delete"),
		ibSettingsArt(wxASCII_STR(wxART_DELETE), this), _("Delete (Del)"));
	toolbar->AddSeparator();
	toolbar->AddTool(wxID_UP, _("Move up"),
		ibSettingsArt(wxASCII_STR(wxART_GO_UP), this), _("Move up"));
	toolbar->AddTool(wxID_DOWN, _("Move down"),
		ibSettingsArt(wxASCII_STR(wxART_GO_DOWN), this), _("Move down"));
	toolbar->Realize();
	rightSizer->Insert(0, toolbar, 0, wxEXPAND);
	rightPane->SetSizer(rightSizer);

	toolbar->Bind(wxEVT_TOOL, &ibDialogListSettings::OnOrderAdd, this, wxID_ADD);
	toolbar->Bind(wxEVT_TOOL, &ibDialogListSettings::OnOrderRemove, this, wxID_REMOVE);
	toolbar->Bind(wxEVT_TOOL, [this](wxCommandEvent&) { MoveOrderLine(-1); }, wxID_UP);
	toolbar->Bind(wxEVT_TOOL, [this](wxCommandEvent&) { MoveOrderLine(+1); }, wxID_DOWN);

	splitter->SplitVertically(leftPane, rightPane, FromDIP(180));
	wxBoxSizer* panelSizer = new wxBoxSizer(wxVERTICAL);
	panelSizer->Add(splitter, 1, wxEXPAND);
	panel->SetSizer(panelSizer);

	PopulateFieldTree(m_orderFieldTree);
	m_orderFieldTree->Bind(wxEVT_TREE_ITEM_EXPANDING, &ibDialogListSettings::OnFieldTreeExpanding, this);
	m_orderFieldTree->Bind(wxEVT_TREE_ITEM_ACTIVATED, &ibDialogListSettings::OnOrderFieldActivated, this);
	m_orderFieldTree->Bind(wxEVT_TREE_BEGIN_DRAG, &ibDialogListSettings::OnFieldTreeBeginDrag, this);
	rightPane->SetDropTarget(new ibFieldDropTarget([this]{ AddOrderForField(m_dragItem); }));

	// DOUBLE CLICK OPENS THE EDITOR — the grid sends an ACTIVATE for it, and without
	// this binding the cell could only be opened with F2 (the filter tab had it, the
	// other two did not, which is why they felt dead).
	m_orderView->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, [this](ibDataViewEvent& e) {
		if (m_orderView != nullptr)
			m_orderView->EditItem(e.GetItem(), e.GetDataViewColumn());
		e.Skip();
	});

	m_orderModel = new ibOrderModel(this);
	m_orderView->AssociateModel(m_orderModel);
	m_orderView->Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, &ibDialogListSettings::OnListContextMenu, this);
	m_orderModel->ResetFromList();

	return panel;
}

wxWindow* ibDialogListSettings::BuildGroupPage(wxWindow* parent)
{
	wxPanel* panel = new wxPanel(parent);
	wxSplitterWindow* splitter = new wxSplitterWindow(panel, wxID_ANY,
		wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3DSASH);
	splitter->SetMinimumPaneSize(FromDIP(120));

	// ---- LEFT pane: available fields (dot-walkable) ----
	wxPanel* leftPane = new wxPanel(splitter);
	wxBoxSizer* leftSizer = new wxBoxSizer(wxVERTICAL);
	leftSizer->Add(new wxStaticText(leftPane, wxID_ANY, _("Available fields")), 0, wxALL, FromDIP(4));
	m_groupFieldTree = new wxTreeCtrl(leftPane, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTR_HAS_BUTTONS | wxTR_SINGLE | wxTR_HIDE_ROOT | wxTR_LINES_AT_ROOT | wxTR_NO_LINES | wxTR_TWIST_BUTTONS);
	leftSizer->Add(m_groupFieldTree, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, FromDIP(4));
	leftPane->SetSizer(leftSizer);

	// ---- RIGHT pane: the grouping list ----
	wxPanel* rightPane = new wxPanel(splitter);
	wxBoxSizer* rightSizer = new wxBoxSizer(wxVERTICAL);
	m_groupView = new ibDataViewCtrl(rightPane, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxDV_ROW_LINES | wxDV_SINGLE);
	ibStyleSettingsGrid(m_groupView);
	// FIELD and KIND — the same two questions a sort line answers, so the same cell.
	// The kind is a registered enumeration now (GroupKind), which is what makes
	// "elements / hierarchy"selectable at all.
	m_groupView->AppendColumn(new ibDataViewColumn(_("Field"),
		new ibRowValueCellRenderer(this,
			[this](const ibDataViewItem& row) -> ibValue {
				const size_t idx = GroupIndexAt(row);
				ibValueGroupList* g = GetGroupList();
				return (g != nullptr && idx < g->Count()) ? ibValue(g->GetFieldObject(idx)) : ibValue();
			},
			[this](const ibDataViewItem& row, const ibValue& value) {
				ibValueGroupList* g = GetGroupList();
				const size_t idx = GroupIndexAt(row);
				if (g == nullptr || idx >= g->Count())
					return;
				// An empty value CLEARS the line's field (same reason as the sort tab).
				ibValueCompositionField* field = nullptr;
				const bool chosen = value.ConvertToValue(field);
				const ibQueryDimUnfold kind = g->GetKind(idx);
				g->Remove(idx);
				if (chosen) g->Add(field, kind);
				else        g->Add(wxEmptyString, kind);
				if (m_groupModel != nullptr) m_groupModel->ResetFromList();
			}),
		eGroupField, wxNOT_FOUND, wxAlignment::wxALIGN_LEFT));

	m_groupView->AppendColumn(new ibDataViewColumn(_("Kind"),
		new ibRowValueCellRenderer(this,
			[this](const ibDataViewItem& row) -> ibValue {
				ibValueGroupList* g = GetGroupList();
				const size_t idx = GroupIndexAt(row);
				return (g != nullptr && idx < g->Count())
					? ibValue::CreateEnumObject<ibValueEnumGroupKind>(g->GetKind(idx)) : ibValue();
			},
			[this](const ibDataViewItem& row, const ibValue& value) {
				ibValueGroupList* g = GetGroupList();
				const size_t idx = GroupIndexAt(row);
				if (g != nullptr && idx < g->Count()) {
					ibValueCompositionField* field = g->GetFieldObject(idx);
					const ibQueryDimUnfold kind = value.ConvertToEnumValue<ibQueryDimUnfold>();
					g->Remove(idx);
					g->Add(field, kind);
				}
				if (m_groupModel != nullptr) m_groupModel->ResetFromList();
			}),
		eGroupKind, wxNOT_FOUND, wxAlignment::wxALIGN_LEFT));

	rightSizer->Add(m_groupView, 1, wxALL | wxEXPAND, FromDIP(4));

	// Same toolbar, same verbs — a grouping list is ordered too: "by Warehouse,
	// then by Item" is a different report from the other way round.
	wxToolBar* toolbar = new wxToolBar(rightPane, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTB_HORIZONTAL | wxTB_FLAT | wxTB_NODIVIDER);
	toolbar->SetToolBitmapSize(FromDIP(wxSize(16, 16)));
	toolbar->AddTool(wxID_ADD, _("Add"),
		ibSettingsArt(wxASCII_STR(wxART_NEW), this), _("Add (Ins)"));
	toolbar->AddTool(wxID_REMOVE, _("Delete"),
		ibSettingsArt(wxASCII_STR(wxART_DELETE), this), _("Delete (Del)"));
	toolbar->AddSeparator();
	toolbar->AddTool(wxID_UP, _("Move up"),
		ibSettingsArt(wxASCII_STR(wxART_GO_UP), this), _("Move up"));
	toolbar->AddTool(wxID_DOWN, _("Move down"),
		ibSettingsArt(wxASCII_STR(wxART_GO_DOWN), this), _("Move down"));
	toolbar->Realize();
	rightSizer->Insert(0, toolbar, 0, wxEXPAND);
	rightPane->SetSizer(rightSizer);

	toolbar->Bind(wxEVT_TOOL, &ibDialogListSettings::OnGroupAdd, this, wxID_ADD);
	toolbar->Bind(wxEVT_TOOL, &ibDialogListSettings::OnGroupRemove, this, wxID_REMOVE);
	toolbar->Bind(wxEVT_TOOL, [this](wxCommandEvent&) { MoveGroupLine(-1); }, wxID_UP);
	toolbar->Bind(wxEVT_TOOL, [this](wxCommandEvent&) { MoveGroupLine(+1); }, wxID_DOWN);

	splitter->SplitVertically(leftPane, rightPane, FromDIP(180));
	wxBoxSizer* panelSizer = new wxBoxSizer(wxVERTICAL);
	panelSizer->Add(splitter, 1, wxEXPAND);
	panel->SetSizer(panelSizer);

	PopulateFieldTree(m_groupFieldTree);
	m_groupFieldTree->Bind(wxEVT_TREE_ITEM_EXPANDING, &ibDialogListSettings::OnFieldTreeExpanding, this);
	m_groupFieldTree->Bind(wxEVT_TREE_ITEM_ACTIVATED, &ibDialogListSettings::OnGroupFieldActivated, this);
	m_groupFieldTree->Bind(wxEVT_TREE_BEGIN_DRAG, &ibDialogListSettings::OnFieldTreeBeginDrag, this);
	rightPane->SetDropTarget(new ibFieldDropTarget([this]{ AddGroupForField(m_dragItem); }));

	m_groupView->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, [this](ibDataViewEvent& e) {
		if (m_groupView != nullptr)
			m_groupView->EditItem(e.GetItem(), e.GetDataViewColumn());
		e.Skip();
	});

	m_groupModel = new ibGroupModel(this);
	m_groupView->AssociateModel(m_groupModel);
	m_groupView->Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, &ibDialogListSettings::OnListContextMenu, this);
	m_groupModel->ResetFromList();

	return panel;
}

// ===========================================================================
//  Load / apply / the metadata door
// ===========================================================================


// ---------------------------------------------------------------------------
//  Load / Apply (between the dialog UI and the dialog's BUFFER ibValueListSettings)
//
//  Every tab (Filter / Sort / Group) is model-driven and edits its buffer list in
//  place, so there is no per-tab copy step — Load just syncs the model row counts,
//  Apply is a no-op. The buffer is committed to the composer on OK.
// ---------------------------------------------------------------------------

void ibDialogListSettings::LoadFromSettings()
{
	// Query tab (dynamic-list only) — load the list's arbitrary-query flag + text into the controls.
	if (m_list != nullptr && m_queryUseCheck != nullptr) {
		m_queryUseCheck->SetValue(m_list->IsArbitraryQuery());
		if (m_queryText != nullptr) {
			m_queryText->SetValue(m_list->GetArbitraryQueryText());
			m_queryText->Enable(m_list->IsArbitraryQuery());
		}
	}
	if (m_settings == nullptr)
		return;
	// Every tab's model binds straight to its buffer list (Filter / Order / Group) — just
	// sync the row counts to what ibLoadSettingsFromComposer put in the buffer.
	RefreshFilterTree();
	if (m_orderModel != nullptr)
		m_orderModel->ResetFromList();
	if (m_groupModel != nullptr)
		m_groupModel->ResetFromList();
}

void ibDialogListSettings::ApplyToSettings()
{
	// Query tab (dynamic-list only) — apply the arbitrary-query flag + text onto the list, then re-apply the source.
	if (m_list != nullptr && m_queryUseCheck != nullptr) {
		m_list->SetArbitraryQuery(m_queryUseCheck->GetValue());
		if (m_queryText != nullptr)
			m_list->SetArbitraryQueryText(m_queryText->GetValue());
		m_list->ApplySource();
	}
	// Every tab edits its buffer list IN PLACE through its model — nothing to copy back here.
	// The buffer is committed to the composer on OK (ibCommitSettingsToComposer).
}

// ---------------------------------------------------------------------------
//  Add / Remove handlers
// ---------------------------------------------------------------------------

// The config metaData that resolves reference targets — the dynamic list's own, else the ACTIVE
// config. Without a valid metaData, ConvertToMetaIds returns nothing and every field looks like a
// leaf (no [+]) — which is exactly the "flat list" bug.
const ibMetaData* ibDialogListSettings::SourceMetaData() const
{
	if (m_list != nullptr)
		if (const ibMetaData* md = m_list->GetSourceMetaData())
			return md;
	return activeMetaData;
}

// ===========================================================================
//  Available-fields tree — shared by all three tabs
// ===========================================================================


void ibDialogListSettings::PopulateFieldTree(wxTreeCtrl* tree)
{
	if (tree == nullptr)
		return;
	// Attribute icon (index 0) on every field — same icon the source-explorer picker uses.
	wxImageList* imgs = new wxImageList(16, 16);
	imgs->Add(ibValue::GetIconGroup());
	tree->AssignImageList(imgs);

	const ibMetaData* metaData = SourceMetaData();
	const wxTreeItemId root = tree->AddRoot(wxEmptyString);

	if (m_list != nullptr) {
		if (const auto* explorer = m_list->GetSourceExplorer())
			AppendSourceFields(tree, root, *explorer, wxEmptyString, metaData);
		return;
	}
	if (m_model != nullptr) {
		// A model that IS a source (value-table, …) vends a self-describing explorer — use it so
		// its reference columns expand, just like the dynamic-list path above.
		if (ibSourceDataObject* src = dynamic_cast<ibSourceDataObject*>(m_model)) {
			if (const auto* explorer = src->GetSourceExplorer()) {
				AppendSourceFields(tree, root, *explorer, wxEmptyString, metaData);
				return;
			}
		}
		// Non-source model — flat columns, but a reference-typed column still gets its [+].
		ibValueModel::ibValueModelColumnCollection* columns = m_model->GetColumnCollection();
		if (columns == nullptr)
			return;
		for (unsigned int i = 0; i < columns->GetColumnCount(); ++i) {
			const auto* col = columns->GetColumnInfo(i);
			if (col == nullptr)
				continue;
			ibSourceFieldNode* data = new ibSourceFieldNode();
			data->m_path     = col->GetColumnName();
			data->m_leafId   = static_cast<ibMetaID>(col->GetColumnID());
			data->m_type     = col->GetColumnType();
			data->m_refTypes = ibValueReferenceDataObject::ConvertToMetaIds(col->GetColumnType().GetClsidList(), metaData);
			const wxTreeItemId item = tree->AppendItem(root, col->GetColumnName(), 0, 0, data);
			if (!data->m_refTypes.empty())
				tree->AppendItem(item, wxEmptyString);   // dummy -> [+]
		}
	}
}

// Root an available-fields tree (shared by all three tabs). ALWAYS via a source EXPLORER when the
// thing IS a source (dynamic list OR a source-model like a value-table), so a REFERENCE column gets
// a [+] and expands into its target's fields — exactly like advpropSource's picker. Only a
// non-source model falls back to flat columns (and even those get a [+] when their type is a reference).
// ===========================================================================
//  The FIELD PICKER — the available-fields tree as a form
// ===========================================================================
//
// A field is a VALUE. Choosing one is therefore choosing a value, and it happens
// where every other value choice happens: the Select button of the cell. What
// opens is this form — the same tree the tab shows on the left, because there is
// only one answer to "which fields does this source have".
// Walk a dotted path down the tree, loading each reference on the way, and land the
// cursor on the leaf. The path is the technical one (that is what a field stores).
void ibDialogListSettings::SelectFieldByPath(wxTreeCtrl* tree, const wxString& path)
{
	if (tree == nullptr || path.IsEmpty())
		return;
	wxTreeItemId parent = tree->GetRootItem();
	wxStringTokenizer parts(path, wxT("."));
	while (parts.HasMoreTokens() && parent.IsOk()) {
		const wxString segment = parts.GetNextToken();
		wxTreeItemIdValue cookie;
		wxTreeItemId child = tree->GetFirstChild(parent, cookie);
		wxTreeItemId found;
		while (child.IsOk()) {
			const ibSourceFieldNode* node = dynamic_cast<ibSourceFieldNode*>(tree->GetItemData(child));
			if (node != nullptr) {
				const wxString last = node->m_path.AfterLast(wxT('.'));
				if ((last.IsEmpty() ? node->m_path : last).IsSameAs(segment, false)) {
					found = child;
					break;
				}
			}
			child = tree->GetNextChild(parent, cookie);
		}
		if (!found.IsOk())
			return;
		if (parts.HasMoreTokens()) {
			ExpandSourceFieldNode(tree, found, SourceMetaData());   // the road continues — load it
			tree->Expand(found);
		}
		parent = found;
	}
	if (parent.IsOk() && parent != tree->GetRootItem()) {
		tree->SelectItem(parent);
		tree->EnsureVisible(parent);
	}
}

ibValueCompositionField* ibDialogListSettings::ChooseField(wxWindow* parent, const wxString& currentPath)
{
	wxDialog dlg(parent != nullptr ? parent : this, wxID_ANY, _("Select field"),
		wxDefaultPosition, FromDIP(wxSize(320, 420)),
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);

	wxTreeCtrl* tree = new wxTreeCtrl(&dlg, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTR_HAS_BUTTONS | wxTR_SINGLE | wxTR_HIDE_ROOT | wxTR_LINES_AT_ROOT | wxTR_NO_LINES | wxTR_TWIST_BUTTONS);
	PopulateFieldTree(tree);
	// References expand by the SAME handler the tabs use — a deep path
	// (Supplier.Region.Country) has to be reachable from here too, or the picker
	// would be a worse door than the tree beside it.
	// EXPANDING IS HANDLED HERE, in the picker's own lambda, and the node is filled
	// BEFORE wx decides what to draw. Routing it through the dialog's method left
	// the picker's tree refusing to unfold while the identical tree on the tab
	// behind it worked — the same handler, a different window.
	tree->Bind(wxEVT_TREE_ITEM_EXPANDING, [tree, this](wxTreeEvent& e) {
		ExpandSourceFieldNode(tree, e.GetItem(), SourceMetaData());
		e.Skip();
	});
	// A CLICK ON THE ARROW is not the only way in: selecting a reference loads its
	// fields too, so the node is ready by the time the user reaches for the arrow.
	tree->Bind(wxEVT_TREE_SEL_CHANGED, [tree, this](wxTreeEvent& e) {
		ExpandSourceFieldNode(tree, e.GetItem(), SourceMetaData());
		e.Skip();
	});

	// DOUBLE-CLICK ON A REFERENCE OPENS IT; double-click on a FIELD chooses it.
	//
	// This is what "the picker does not work" was: a reference is ALSO a field (it
	// has a leaf id), so double-clicking it — the natural way to go deeper — closed
	// the window and picked the reference itself. Expanding by the little arrow is
	// the only thing that ever worked, and it is not what anyone does.
	tree->Bind(wxEVT_TREE_ITEM_ACTIVATED, [&dlg, tree, this](wxTreeEvent& e) {
		const wxTreeItemId item = e.GetItem();
		if (!item.IsOk())
			return;
		const ibSourceFieldNode* node = dynamic_cast<ibSourceFieldNode*>(tree->GetItemData(item));
		if (node != nullptr && !node->m_refTypes.empty()) {
			ExpandSourceFieldNode(tree, item, SourceMetaData());
			tree->Expand(item);
			return;   // a reference is a ROAD — going down it is not choosing it
		}
		dlg.EndModal(wxID_OK);
	});

	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(tree, 1, wxALL | wxEXPAND, FromDIP(6));
	sizer->Add(dlg.CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxALL | wxEXPAND, FromDIP(6));
	dlg.SetSizer(sizer);

	// THE TREE GETS THE FOCUS, and the cursor stands on the field the cell already
	// holds. Without this the focus lands on the OK button: the tree looks alive but
	// answers no key and no arrow, which reads exactly as "it does not expand".
	// Standing on the current value also means re-picking starts where the user is,
	// instead of at the top of a list they have to re-read.
	dlg.Bind(wxEVT_INIT_DIALOG, [tree, currentPath, this](wxInitDialogEvent& e) {
		tree->SetFocus();
		if (!currentPath.IsEmpty())
			SelectFieldByPath(tree, currentPath);
		else if (tree->GetRootItem().IsOk()) {
			wxTreeItemIdValue cookie;
			const wxTreeItemId first = tree->GetFirstChild(tree->GetRootItem(), cookie);
			if (first.IsOk())
				tree->SelectItem(first);
		}
		e.Skip();
	});

	if (dlg.ShowModal() != wxID_OK)
		return nullptr;

	const wxTreeItemId chosen = tree->GetSelection();
	ibSourceFieldNode* node = chosen.IsOk()
		? dynamic_cast<ibSourceFieldNode*>(tree->GetItemData(chosen)) : nullptr;
	if (node == nullptr || node->m_leafId == wxNOT_FOUND)
		return nullptr;   // a reference NODE is a road, not a field

	// The label the user picked is the presentation; the leaf id and type are what
	// let the OTHER side of the condition edit its value through the runtime.
	ibValueCompositionField* field =
		new ibValueCompositionField(node->m_path, node->m_presentation);
	field->SetTypeInfo(node->m_leafId, node->m_type);
	return field;
}

// Lazily expand a reference field — shared by all three tabs' field trees (the tree that fired the
// event is the one to expand); metaData = the config that resolves reference targets.
void ibDialogListSettings::OnFieldTreeExpanding(wxTreeEvent& evt)
{
	wxTreeCtrl* tree = wxDynamicCast(evt.GetEventObject(), wxTreeCtrl);
	if (tree != nullptr)
		ExpandSourceFieldNode(tree, evt.GetItem(), SourceMetaData());
}

// Start dragging a field out of the LEFT tree; dropping on the tab's right panel adds it
// (the dropped field is the remembered m_dragItem — same-process drag, text payload is a stub).
void ibDialogListSettings::OnFieldTreeBeginDrag(wxTreeEvent& evt)
{
	wxTreeCtrl* tree = wxDynamicCast(evt.GetEventObject(), wxTreeCtrl);
	if (tree == nullptr)
		return;
	ibSourceFieldNode* node = dynamic_cast<ibSourceFieldNode*>(tree->GetItemData(evt.GetItem()));
	if (node == nullptr || node->m_leafId == wxNOT_FOUND)
		return;   // only a real field is draggable
	m_dragItem = evt.GetItem();
	wxTextDataObject data(node->m_path);
	wxDropSource source(tree);
	source.SetData(data);
	source.DoDragDrop(wxDrag_CopyOnly);
}

// ===========================================================================
//  Context menu — one handler, routed by the view that fired
// ===========================================================================


// Right-click on a composition list (Filter / Sort / Group) — a command menu to add the
// currently-selected available field or remove the selected row, routed by which view fired.
void ibDialogListSettings::OnListContextMenu(ibDataViewEvent& evt)
{
	const wxObject* src = evt.GetEventObject();
	wxMenu menu;

	if (src == m_filterView) {
		// THE SAME VERBS AS THE TOOLBAR, raising the same ids — one implementation,
		// and a user who learns either road has learned both. The accelerators are
		// spelled out because that is where people look for them.
		// ONE PICTURE PER VERB — the same art ids the toolbar above raises, through
		// the same helper. Spelled bare here once, the menu drifted from its own
		// toolbar: the identical command wore a picture on one road and none on the
		// other.
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
		const ibDataViewItem sel = m_filterView->GetSelection();
		const bool isGroup = m_filterModel != nullptr && m_filterModel->GetGroup(sel) != nullptr;
		const bool isRoot  = sel.IsOk() && !m_filterModel->GetParent(sel).IsOk();
		menu.Enable(kFilterCmdCopy, m_filterModel != nullptr && m_filterModel->GetItem(sel) != nullptr);
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
	}
	else {
		// THE SAME FOUR VERBS THEIR TOOLBARS CARRY — add, delete, and the two that
		// make an ordered list ordered. The menu used to offer two of them, so the
		// same tab answered differently depending on where the user clicked.
		ibAppendCmd(menu, wxID_ADD, _("Add") + wxT("	Ins"), wxASCII_STR(wxART_NEW), this);
		ibAppendCmd(menu, wxID_REMOVE, _("Delete") + wxT("	Del"), wxASCII_STR(wxART_DELETE), this);
		menu.AppendSeparator();
		ibAppendCmd(menu, wxID_UP, _("Move up"), wxASCII_STR(wxART_GO_UP), this);
		ibAppendCmd(menu, wxID_DOWN, _("Move down"), wxASCII_STR(wxART_GO_DOWN), this);

		if (src == m_orderView) {
			menu.Bind(wxEVT_MENU, [this](wxCommandEvent& e) { OnOrderAdd(e); }, wxID_ADD);
			menu.Bind(wxEVT_MENU, [this](wxCommandEvent& e) { OnOrderRemove(e); }, wxID_REMOVE);
			menu.Bind(wxEVT_MENU, [this](wxCommandEvent&)   { MoveOrderLine(-1); }, wxID_UP);
			menu.Bind(wxEVT_MENU, [this](wxCommandEvent&)   { MoveOrderLine(+1); }, wxID_DOWN);
		}
		else if (src == m_groupView) {
			menu.Bind(wxEVT_MENU, [this](wxCommandEvent& e) { OnGroupAdd(e); }, wxID_ADD);
			menu.Bind(wxEVT_MENU, [this](wxCommandEvent& e) { OnGroupRemove(e); }, wxID_REMOVE);
			menu.Bind(wxEVT_MENU, [this](wxCommandEvent&)   { MoveGroupLine(-1); }, wxID_UP);
			menu.Bind(wxEVT_MENU, [this](wxCommandEvent&)   { MoveGroupLine(+1); }, wxID_DOWN);
		}
	}

	PopupMenu(&menu);
}

// ===========================================================================
//  FILTER tab — a tree of groups over conditions
// ===========================================================================


void ibDialogListSettings::RefreshFilterTree(const ibValue& select)
{
	// A STRUCTURAL change (added, removed, grouped) is not a value change: rows
	// appear and disappear, so the view re-reads the shape. The ROWS survive it —
	// they are keyed by the value they stand for — which is what lets the cursor
	// land back on the right line below.
	if (m_filterModel == nullptr || m_filterView == nullptr)
		return;
	// THE ROOT IS RE-STATED, not assumed: this is also the path the dialog takes
	// when it first loads the settings, and at that point the page has already
	// been built with whatever root existed then (usually none). SetRoot keeps the
	// rows when the tree is the same one, so this costs nothing on an edit.
	m_filterModel->SetRoot(GetFilterRoot());

	// OPEN ON THE ROOT. It is a row now, and a collapsed one hides the whole
	// filter behind a single line that reads as "nothing set".
	const ibDataViewItem rootItem = m_filterModel->RootItem();
	if (rootItem.IsOk())
		m_filterView->Expand(rootItem);

	// Rows only exist once fetched, so this asks AFTER the model re-read them.
	const ibDataViewItem sel = m_filterModel->ItemFor(select);
	if (sel.IsOk()) {
		m_filterView->ExpandAncestors(sel);
		m_filterView->Select(sel);
		m_filterView->EnsureVisible(sel);
	}
}

// Add a filter row on the chosen field-tree node — its dot-path + leaf id/type come
// straight from the node, so even a DEEP path (Supplier.Region.Country) resolves its
// value editor. New row: default comparison Equal, empty typed value (the Value-column
// Select button then routes through ShowSelectType before QuickChoice / ProcessChoice).
void ibDialogListSettings::AddFilterForField(const wxTreeItemId& item)
{
	ibSourceFieldNode* node = item.IsOk()
		? dynamic_cast<ibSourceFieldNode*>(m_filterFieldTree->GetItemData(item)) : nullptr;
	if (node == nullptr || node->m_leafId == wxNOT_FOUND)
		return;
	// The tree's own label is the PRESENTATION — that is what the user picked and
	// what the filter line should read back, rather than the technical path.
	// NOT AN ibValuePtr HERE. Wrapping the field in a smart pointer and then handing
	// THAT to ibValue picks a different constructor than the one meant for a value
	// object — the condition ends up holding something that is not the field, and it
	// crashes the moment the group copies it in. The field is refcounted by the value
	// that takes it, so the raw object is what travels.
	ibValueCompositionField* field =
		new ibValueCompositionField(node->m_path, node->m_presentation);
	field->SetTypeInfo(node->m_leafId, node->m_type);

	// INTO THE GROUP THE USER IS STANDING IN — the selected group, or the parent
	// of the selected condition, or the root. A new line landing at the bottom of
	// the whole filter would silently mean something else than where they pointed.
	ibValueFilterGroup* target = m_filterModel != nullptr
		? m_filterModel->GetTargetGroup(m_filterView->GetSelection()) : nullptr;
	if (target == nullptr)
		return;

	ibValueFilterItem* added = target->Add(ibValue(field), ibComparisonKind_Equal, ibValue(), true);
	// THE FIELD IS KNOWN, SO THE VALUE'S TYPE IS KNOWN — seed the right-hand side
	// with what that type calls empty (False for a Boolean, an empty reference of
	// the right kind) rather than with nothing. Same rule as re-picking the field
	// in the Left cell, so both roads leave the row in the same state.
	if (added != nullptr)
		added->SetRight(ibValueTypeDescription::AdjustValue(
			added->GetRightTypeDescription(), SourceMetaData()));
	RefreshFilterTree(added != nullptr ? ibValue(added) : ibValue());
}

// A NEW LINE IS EMPTY. Taking whatever happens to be selected in the field tree
// invents a condition the user did not ask for — and worse, fixes its TYPE, so the
// right-hand side is already narrowed before anything was chosen. The line appears
// blank and the picker fills it in, which is also what makes "add into this group"
// mean what it says.
void ibDialogListSettings::OnFilterAdd(wxCommandEvent&)
{
	ibValueFilterGroup* target = m_filterModel != nullptr
		? m_filterModel->GetTargetGroup(m_filterView->GetSelection()) : nullptr;
	if (target == nullptr)
		return;
	ibValueFilterItem* added = target->Add(ibValue(new ibValueCompositionField()),
		ibComparisonKind_Equal, ibValue(), true);
	RefreshFilterTree(added != nullptr ? ibValue(added) : ibValue());
}
void ibDialogListSettings::OnFilterFieldActivated(wxTreeEvent& e) { AddFilterForField(e.GetItem()); }

void ibDialogListSettings::OnFilterRemove(wxCommandEvent&)
{
	if (m_filterModel == nullptr || m_filterView == nullptr)
		return;

	const ibDataViewItem sel = m_filterView->GetSelection();
	if (!sel.IsOk())
		return;

	// The ROOT is not removable — an empty filter is an empty root group, not the
	// absence of one, and taking it away would leave nowhere to add.
	ibValueFilterGroup* parent = m_filterModel->GetOwnerGroup(sel);
	if (parent == nullptr)
		return;

	ibValueFilterItem* line = m_filterModel->GetItem(sel);
	const ibValue child = line != nullptr ? ibValue(line) : ibValue(m_filterModel->GetGroup(sel));
	const size_t idx = parent->IndexOf(child);
	if (idx < parent->Count())
		parent->Remove(idx);

	RefreshFilterTree();
}

void ibDialogListSettings::OnFilterAddGroup(wxCommandEvent&)
{
	if (m_filterModel == nullptr || m_filterView == nullptr)
		return;
	ibValueFilterGroup* target = m_filterModel->GetTargetGroup(m_filterView->GetSelection());
	if (target == nullptr)
		return;
	// AND by default — the operator most groups keep, and the one the drop-down in
	// the group's own row changes when it is not.
	ibValueFilterGroup* added = target->AddGroup(ibFilterGroupKind_And);
	RefreshFilterTree(added != nullptr ? ibValue(added) : ibValue());
}

void ibDialogListSettings::OnFilterCopy(wxCommandEvent&)
{
	if (m_filterModel == nullptr || m_filterView == nullptr)
		return;
	const ibDataViewItem sel = m_filterView->GetSelection();
	ibValueFilterItem* source = m_filterModel->GetItem(sel);
	ibValueFilterGroup* target = m_filterModel->GetTargetGroup(sel);
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

void ibDialogListSettings::OnFilterGroupSelected(wxCommandEvent&)
{
	if (m_filterModel == nullptr || m_filterView == nullptr)
		return;
	const ibDataViewItem sel = m_filterView->GetSelection();
	if (!sel.IsOk())
		return;

	// The selected line's PARENT is where the new group goes — grouping wraps
	// lines, it does not move them somewhere else.
	ibValueFilterGroup* parent = m_filterModel->GetOwnerGroup(sel);
	if (parent == nullptr)
		parent = m_filterModel->GetRoot();
	if (parent == nullptr)
		return;

	const ibValue selected = m_filterModel->GetItem(sel) != nullptr
		? ibValue(m_filterModel->GetItem(sel))
		: ibValue(m_filterModel->GetGroup(sel));

	const size_t idx = parent->IndexOf(selected);
	if (idx >= parent->Count())
		return;

	ibValueFilterGroup* wrapper = parent->GroupChildren({ idx }, ibFilterGroupKind_And);
	// The cursor stays on the LINE that was grouped, not on the wrapper: the user
	// asked to put that line in a group, and it is still the line they were on.
	RefreshFilterTree(wrapper != nullptr ? selected : ibValue());
}

void ibDialogListSettings::OnFilterUngroup(wxCommandEvent&)
{
	if (m_filterModel == nullptr || m_filterView == nullptr)
		return;
	const ibDataViewItem sel = m_filterView->GetSelection();
	ibValueFilterGroup* group = m_filterModel->GetGroup(sel);
	if (group == nullptr)
		return;   // only a group can be ungrouped

	ibValueFilterGroup* parent = m_filterModel->GetOwnerGroup(sel);
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
// (ibValueFilterGroup::MoveChild). The dialog only says which line and which way,
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

void ibDialogListSettings::OnFilterMoveUp(wxCommandEvent&)
{
	if (m_filterModel == nullptr || m_filterView == nullptr)
		return;
	RefreshFilterTree(ibMoveSelectedFilterChild(m_filterModel, m_filterView, -1));
}

void ibDialogListSettings::OnFilterMoveDown(wxCommandEvent&)
{
	if (m_filterModel == nullptr || m_filterView == nullptr)
		return;
	RefreshFilterTree(ibMoveSelectedFilterChild(m_filterModel, m_filterView, +1));
}

void ibDialogListSettings::OnFilterItemActivated(ibDataViewEvent& event)
{
	if (m_filterView != nullptr)
		m_filterView->EditItem(event.GetItem(), event.GetDataViewColumn());
	event.Skip();
}

// ===========================================================================
//  SORT tab
// ===========================================================================


ibValueSortItem* ibDialogListSettings::OrderLineAt(const ibDataViewItem& row) const
{
	ibValueSortList* o = GetOrderList();
	const size_t id = reinterpret_cast<size_t>(row.GetID());
	return (o != nullptr && id > 0 && id <= o->Count()) ? o->GetItem(id - 1) : nullptr;
}

// Add the chosen available field to the sort list (default Ascending; direction is edited
// inline in the Direction column). Mutates the dialog's BUFFER list, then refreshes the model.
void ibDialogListSettings::AddOrderForField(const wxTreeItemId& item)
{
	ibSourceFieldNode* node = item.IsOk()
		? dynamic_cast<ibSourceFieldNode*>(m_orderFieldTree->GetItemData(item)) : nullptr;
	if (node == nullptr || node->m_leafId == wxNOT_FOUND)
		return;
	ibValueSortList* o = GetOrderList();
	if (o == nullptr)
		return;
	// THE FIELD THE PICKER RESOLVED, whole: adding by path alone would rebuild a
	// bare field and lose the readable path and the type behind it.
	ibValueCompositionField* field = new ibValueCompositionField(node->m_path, node->m_presentation);
	field->SetTypeInfo(node->m_leafId, node->m_type);
	if (ibValueSortItem* line = o->Add(node->m_path))
		line->SetField(field);
	if (m_orderModel != nullptr)
		m_orderModel->ResetFromList();
	SelectLastRow(m_orderView, o->Count());
}

// A NEW LINE IS EMPTY here too — the field is chosen in the row, by hand. Taking
// whatever is selected in the field tree conjures a line the user did not ask for.
void ibDialogListSettings::OnOrderAdd(wxCommandEvent&)
{
	ibValueSortList* o = GetOrderList();
	if (o == nullptr)
		return;
	o->Add(wxEmptyString, ibSortDirection_Ascending);
	if (m_orderModel != nullptr)
		m_orderModel->ResetFromList();
	SelectLastRow(m_orderView, o->Count());
}
void ibDialogListSettings::OnOrderFieldActivated(wxTreeEvent& e) { AddOrderForField(e.GetItem()); }

void ibDialogListSettings::OnOrderRemove(wxCommandEvent&)
{
	// THE LIST REMOVES ITS OWN LINE. Rebuilding the whole sort order to drop one
	// row was how this used to work — and it lost everything the line carried that
	// a rebuild could not spell back (the field object, its resolved type).
	ibValueSortList* o = GetOrderList();
	if (o == nullptr || m_orderView == nullptr)
		return;
	const ibDataViewItem& sel = m_orderView->GetSelection();
	if (!sel.IsOk())
		return;
	const size_t index = reinterpret_cast<size_t>(sel.GetID());   // 1-based
	if (index == 0 || !o->Remove(index - 1))
		return;
	if (m_orderModel != nullptr)
		m_orderModel->ResetFromList();
}

// ORDER IS THE MEANING of a sort list, so moving a line is a first-class command,
// not something the user emulates by deleting and re-adding in the right sequence.
void ibDialogListSettings::MoveOrderLine(int delta)
{
	ibValueSortList* o = GetOrderList();
	if (o == nullptr || m_orderView == nullptr)
		return;
	const ibDataViewItem& sel = m_orderView->GetSelection();
	if (!sel.IsOk())
		return;
	const size_t index = reinterpret_cast<size_t>(sel.GetID());   // 1-based
	if (index == 0 || !o->Move(index - 1, delta))
		return;
	if (m_orderModel != nullptr)
		m_orderModel->ResetFromList();
	// The row travelled — the cursor goes with it, or the next press moves a
	// different line.
	const size_t moved = (size_t)((int)index + delta);
	m_orderView->Select(ibDataViewItem(reinterpret_cast<void*>(moved)));
}

// ===========================================================================
//  GROUP tab
// ===========================================================================


size_t ibDialogListSettings::GroupIndexAt(const ibDataViewItem& row) const
{
	const size_t id = reinterpret_cast<size_t>(row.GetID());
	return id > 0 ? id - 1 : (size_t)-1;
}

// Add the chosen available field to the grouping list (BUFFER + model refresh).
void ibDialogListSettings::AddGroupForField(const wxTreeItemId& item)
{
	ibSourceFieldNode* node = item.IsOk()
		? dynamic_cast<ibSourceFieldNode*>(m_groupFieldTree->GetItemData(item)) : nullptr;
	if (node == nullptr || node->m_leafId == wxNOT_FOUND)
		return;
	ibValueGroupList* g = GetGroupList();
	if (g == nullptr)
		return;
	ibValueCompositionField* field = new ibValueCompositionField(node->m_path, node->m_presentation);
	field->SetTypeInfo(node->m_leafId, node->m_type);
	g->Add(field, ibQueryDimUnfold::Elements);
	if (m_groupModel != nullptr)
		m_groupModel->ResetFromList();
	SelectLastRow(m_groupView, g->Count());
}

void ibDialogListSettings::OnGroupAdd(wxCommandEvent&)
{
	ibValueGroupList* g = GetGroupList();
	if (g == nullptr)
		return;
	g->Add(wxEmptyString, ibQueryDimUnfold::Elements);
	if (m_groupModel != nullptr)
		m_groupModel->ResetFromList();
	SelectLastRow(m_groupView, g->Count());
}
void ibDialogListSettings::OnGroupFieldActivated(wxTreeEvent& e) { AddGroupForField(e.GetItem()); }

void ibDialogListSettings::OnGroupRemove(wxCommandEvent&)
{
	ibValueGroupList* g = GetGroupList();
	if (g == nullptr || m_groupView == nullptr)
		return;
	const ibDataViewItem& sel = m_groupView->GetSelection();
	if (!sel.IsOk())
		return;
	// The list removes its own line — rebuilding the whole grouping to drop one
	// row lost the field OBJECT each line carried (its type, its presentation) and
	// rebuilt a bare field from the path.
	const size_t index = reinterpret_cast<size_t>(sel.GetID());   // 1-based
	if (index == 0 || !g->Remove(index - 1))
		return;
	if (m_groupModel != nullptr)
		m_groupModel->ResetFromList();
}

void ibDialogListSettings::MoveGroupLine(int delta)
{
	ibValueGroupList* g = GetGroupList();
	if (g == nullptr || m_groupView == nullptr)
		return;
	const ibDataViewItem& sel = m_groupView->GetSelection();
	if (!sel.IsOk())
		return;
	const size_t index = reinterpret_cast<size_t>(sel.GetID());   // 1-based
	if (index == 0 || !g->Move(index - 1, delta))
		return;
	if (m_groupModel != nullptr)
		m_groupModel->ResetFromList();
	const size_t moved = (size_t)((int)index + delta);
	m_groupView->Select(ibDataViewItem(reinterpret_cast<void*>(moved)));
}

// ===========================================================================
//  Commit and entry points
// ===========================================================================


void ibDialogListSettings::OnOk(wxCommandEvent&)
{
	ApplyToSettings();   // UI → buffer
	// COMMIT the buffer onto the composer (the store) + refresh — the whole transaction lands atomically on OK.
	// Cancel never reaches here, so the composer stays untouched.
	// THE SAME CHECK THE RUNTIME MAKES. A half-written line raises there; here that
	// exception becomes a warning and the form stays open on the offending setting,
	// instead of closing and quietly dropping it.
	try {
		ibValidateSettings(m_settings);
	}
	catch (const ibBackendException& err) {
		wxMessageBox(err.GetErrorDescription(), _("List settings"), wxOK | wxICON_WARNING, this);
		return;   // stay in the dialog — nothing is committed
	}

	if (m_model != nullptr && m_settings != nullptr) {
		// THE TREE GOES BACK TO THE MODEL, not only to the composer. The composer
		// takes the filter as ONE expression, which cannot be read back out of it —
		// so a tree committed only there is applied but invisible: the next open
		// shows an empty Filter tab over a list that is very obviously filtered.
		// The model's settings are where the tree lives and what gets serialised.
		if (ibValueListSettings* live = m_model->GetListSettings())
			live->SetFilterRoot(m_settings->GetFilterRoot());
		ibCommitSettingsToComposer(m_model->GetModelComposer(), m_settings);
		m_model->NotifyReset();
	}
	EndModal(wxID_OK);
}

// ---------------------------------------------------------------------------
//  Backend hook
// ---------------------------------------------------------------------------

bool ibDialogListSettings::ShowListSettingsDialog(ibValueDynamicList* list)
{
	if (list == nullptr)
		return false;
	wxWindow* top = (wxTheApp != nullptr) ? wxTheApp->GetTopWindow() : nullptr;
	ibDialogListSettings dlg(top, list);
	if (dlg.ShowModal() == wxID_OK) {
		list->RefreshComposerSettings();   // OK -> apply the edits onto the composer (L5)
		return true;
	}
	return false;
}

bool ibDialogListSettings::ShowListSettingsDialog(ibValueModel* model)
{
	if (model == nullptr)
		return false;
	// A dynamic list IS-A ibValueModel: route it through the list overload so it keeps
	// its source-explorer field picker (PATH B) and composer refresh. Any other model
	// uses the column-based field source (PATH A); OnOk commits the dialog's buffer onto
	// the model's composer (ibCommitSettingsToComposer + NotifyReset).
	if (ibValueDynamicList* list = dynamic_cast<ibValueDynamicList*>(model))
		return ShowListSettingsDialog(list);

	wxWindow* top = (wxTheApp != nullptr) ? wxTheApp->GetTopWindow() : nullptr;
	ibDialogListSettings dlg(top, model);
	// OK commits the dialog's buffer onto the composer + refreshes (OnOk); Cancel leaves the composer untouched.
	return dlg.ShowModal() == wxID_OK;
}
