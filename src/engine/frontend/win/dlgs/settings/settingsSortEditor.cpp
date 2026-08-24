////////////////////////////////////////////////////////////////////////////
//	The SORT editor — shared by both settings worlds
////////////////////////////////////////////////////////////////////////////

#include "frontend/win/dlgs/settings/settingsSortEditor.h"
#include "frontend/win/dlgs/settings/settingsFieldTree.h"
#include "frontend/win/dlgs/settings/settingsStyle.h"
#include "frontend/win/dlgs/callbackDropTarget.h"   // the same-process drag: the source knows what moved
#include "frontend/win/dlgs/rowValueCell.h"         // ibRowValueCellRenderer — the shared value cell

#include <wx/menu.h>
#include <wx/sizer.h>
#include <wx/splitter.h>
#include <wx/stattext.h>
#include <wx/toolbar.h>
#include <wx/treectrl.h>

// COLUMN 0 IS RESERVED by the ibDataViewCtrl fork (a model column 0 paints blank and does not edit)
// — which is why the columns start at 1. Starting at 0 is what once made the Field cell impossible
// to open at all: F2 and the Select button had nothing to open.
enum { eOrderField = 1, eOrderDir };

// ---- The model — a virtual list over the buffer's sort list (Field + editable Direction). ----
class ibSortEditor::ibSortLineModel : public ibDataViewVirtualListModel {
	ibSortEditor* m_editor;
public:
	explicit ibSortLineModel(ibSortEditor* editor) : ibDataViewVirtualListModel(), m_editor(editor) {}
	// ⭐ ONE WORD FOR ONE THING. This class was `ibOrderModel` and this getter `GetOrder`, standing
	// over `ibSortDescription` / `m_sort` / `SetSort` — the same concept under a second name, in the
	// one file where a reader is most likely to be looking for it (audit, 2026-08-24). The tier says
	// **sort**; *order* survives only where it must: the query's `ORDER BY`, and the node key on
	// disk, which is an opaque key and not a name.
	ibSortDescription* GetSortDesc() const { return m_editor->GetSort(); }
	void ResetFromList() { ibSortDescription* o = GetSortDesc(); Reset(o != nullptr ? (unsigned int)o->m_lines.size() : 0u); }
	virtual void GetValueByRow(wxVariant& variant, unsigned row, unsigned col) const override {
		ibSortDescription* o = GetSortDesc();
		if (o == nullptr) return;
		// BOUNDS FIRST. The view paints rows it has, the list may already have fewer (a Reset lands
		// after the paint is queued) — reading past the end crashed on repaint.
		if (row >= o->m_lines.size())
			return;
		const ibSortLineDescription& line = o->m_lines[row];
		// EVERY COLUMN READS AS WHAT IT IS — the field by its path, the direction by
		// its enumeration caption. No indices, no parallel lists.
		if (col == eOrderField)
			variant = line.m_path;
		else if (col == eOrderDir)
			variant = ibValue::CreateEnumObject<ibValueEnumSortDirection>(
				line.m_ascending ? ibSortDirection_Ascending : ibSortDirection_Descending).GetString();
	}
	// ⛔ NOTHING WRITES THROUGH THIS MODEL. Both columns are drawn by ibRowValueCellRenderer, whose
	// GetValueFromEditorCtrl returns false unconditionally — the cell hands its result to the
	// renderer's own setter, never to the model. The body that stood here decoded a direction out of
	// a wxVariant and could not be reached; the grouping model next door (listSettings.cpp) already
	// says the same thing the honest way.
	virtual bool SetValueByRow(const wxVariant&, unsigned, unsigned) override { return false; }
};

// ===========================================================================

ibSortEditor::ibSortEditor(wxWindow* parent, ibSortDescription* sort, ibSettingsFieldTree* fields)
	: wxPanel(parent, wxID_ANY), m_sort(sort), m_fieldSource(fields)
{
	wxSplitterWindow* splitter = new wxSplitterWindow(this, wxID_ANY,
		wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3DSASH);
	splitter->SetMinimumPaneSize(FromDIP(120));

	// ---- LEFT pane: available fields (dot-walkable) ----
	wxPanel* leftPane = new wxPanel(splitter);
	wxBoxSizer* leftSizer = new wxBoxSizer(wxVERTICAL);
	leftSizer->Add(new wxStaticText(leftPane, wxID_ANY, _("Available fields")), 0, wxALL, FromDIP(4));
	m_fieldCtrl = new wxTreeCtrl(leftPane, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTR_HAS_BUTTONS | wxTR_SINGLE | wxTR_HIDE_ROOT | wxTR_LINES_AT_ROOT | wxTR_NO_LINES | wxTR_TWIST_BUTTONS);
	leftSizer->Add(m_fieldCtrl, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, FromDIP(4));
	leftPane->SetSizer(leftSizer);

	// ---- RIGHT pane: the sort list — Field + editable Direction (choice), model-driven ----
	wxPanel* rightPane = new wxPanel(splitter);
	wxBoxSizer* rightSizer = new wxBoxSizer(wxVERTICAL);
	m_view = new ibDataViewCtrl(rightPane, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxDV_ROW_LINES | wxDV_SINGLE);
	ibStyleSettingsGrid(m_view);

	// WHO KNOWS THE SOURCE opens the picker — here, the field tree this editor was handed.
	ibRowValueCellRenderer::FieldChooser chooser =
		[this](wxWindow* pickerParent, const wxString& held) -> ibValueCompositionField* {
			return m_fieldSource != nullptr ? m_fieldSource->ChooseField(pickerParent, held) : nullptr;
		};

	// FIELD and DIRECTION are both VALUES of the row — a composition field and a
	// SortDirection member — so one cell serves both: the field opens the source
	// tree, the direction opens its enumeration. Nothing here spells a list.
	m_view->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(_("Field"),
		new ibRowValueCellRenderer(this, chooser,
			[this](const ibDataViewItem& row) -> ibValue {
				// ⚠ BUILT FROM THE PATH. A sort LINE is a path and a direction — data — so the field
				// the picker speaks is minted here and owned by the ibValue that wraps it.
				ibSortDescription* o = GetSort();
				const size_t idx = IndexAt(row);
				if (o == nullptr || idx >= o->m_lines.size())
					return ibValue();
				const wxString path = o->m_lines[idx].m_path;
				return path.IsEmpty() ? ibValue() : ibValue(new ibValueCompositionField(path));
			},
			[this](const ibDataViewItem& row, const ibValue& value) {
				ibSortDescription* o = GetSort();
				const size_t idx = IndexAt(row);
				if (o == nullptr || idx >= o->m_lines.size())
					return;
				// CLEARING SENDS AN EMPTY VALUE, and an empty value fails the cast — so testing only
				// "did it convert" made the clear button do nothing at all.
				ibValueCompositionField* field = nullptr;
				const bool chosen = value.ConvertToValue(field) && field != nullptr;
				o->m_lines[idx].m_path = chosen ? field->GetPath() : wxString();
				if (m_model != nullptr) m_model->ResetFromList();
			}),
		eOrderField, wxNOT_FOUND, wxAlignment::wxALIGN_LEFT));

	m_view->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(_("Direction"),
		new ibRowValueCellRenderer(this, chooser,
			[this](const ibDataViewItem& row) -> ibValue {
				ibSortDescription* o = GetSort();
				const size_t idx = IndexAt(row);
				return (o != nullptr && idx < o->m_lines.size())
					? ibValue::CreateEnumObject<ibValueEnumSortDirection>(
						o->m_lines[idx].m_ascending ? ibSortDirection_Ascending : ibSortDirection_Descending)
					: ibValue();
			},
			[this](const ibDataViewItem& row, const ibValue& value) {
				ibSortDescription* o = GetSort();
				const size_t idx = IndexAt(row);
				if (o != nullptr && idx < o->m_lines.size())
					o->m_lines[idx].m_ascending =
						value.ConvertToEnumValue<ibSortDirection>() == ibSortDirection_Ascending;
				if (m_model != nullptr) m_model->ResetFromList();
			}),
		eOrderDir, wxNOT_FOUND, wxAlignment::wxALIGN_LEFT));

	rightSizer->Add(m_view, 1, wxALL | wxEXPAND, FromDIP(4));

	// THE SAME COMMANDS AS THE FILTER EDITOR, on the same kind of toolbar: add,
	// delete, and the two that make an ordered list an ordered list. Two buttons at
	// the bottom and no way to reorder was a form that could only express "these
	// fields", never "in this order" — which is the whole content of a sort.
	wxToolBar* toolbar = m_toolbar = new wxToolBar(rightPane, wxID_ANY, wxDefaultPosition, wxDefaultSize,
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
	// THE BAR KEEPS THE GRID'S MARGINS. Stretched edge to edge over a view inset by 4, it overhung
	// the grid's top-left corner and the left border read as unfinished (Max, 2026-08-19: "the left
	// border where the field is is not fully visible - and it is like that everywhere").
	rightSizer->Insert(0, toolbar, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, FromDIP(4));
	rightPane->SetSizer(rightSizer);

	toolbar->Bind(wxEVT_TOOL, &ibSortEditor::OnAdd, this, wxID_ADD);
	toolbar->Bind(wxEVT_TOOL, &ibSortEditor::OnRemove, this, wxID_REMOVE);
	toolbar->Bind(wxEVT_TOOL, [this](wxCommandEvent&) { MoveLine(-1); }, wxID_UP);
	toolbar->Bind(wxEVT_TOOL, [this](wxCommandEvent&) { MoveLine(+1); }, wxID_DOWN);

	// WIDE ENOUGH FOR WHAT THE FIELDS ARE CALLED. At 180 a name like "Account dimension Dr1"
	// is cut after "Account dimension", so eight distinct slots read as eight identical rows and
	// the only way to tell them apart is to count positions. The pane is user-resizable; what
	// changes here is what it shows BEFORE anyone resizes it.
	splitter->SplitVertically(leftPane, rightPane, FromDIP(260));
	wxBoxSizer* panelSizer = new wxBoxSizer(wxVERTICAL);
	panelSizer->Add(splitter, 1, wxEXPAND);
	SetSizer(panelSizer);

	if (m_fieldSource != nullptr) {
		m_fieldSource->Populate(m_fieldCtrl);
		m_fieldSource->Attach(m_fieldCtrl);   // unfold a reference, drag a field out
	}
	m_fieldCtrl->Bind(wxEVT_TREE_ITEM_ACTIVATED, [this](wxTreeEvent& e) { AddForField(e.GetItem()); });
	rightPane->SetDropTarget(new ibCallbackDropTarget([this] {
		if (m_fieldSource != nullptr) AddForField(m_fieldSource->GetDragItem());
	}));

	// DOUBLE CLICK OPENS THE EDITOR — the grid sends an ACTIVATE for it, and without
	// this binding the cell could only be opened with F2.
	m_view->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, [this](ibDataViewEvent& e) {
		if (m_view != nullptr)
			m_view->EditItem(e.GetItem(), e.GetDataViewColumn());
		e.Skip();
	});

	m_model = new ibSortLineModel(this);
	m_view->AssociateModel(m_model);
	m_view->Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, &ibSortEditor::OnContextMenu, this);
	m_view->Bind(wxEVT_DATAVIEW_ITEM_START_EDITING, &ibSortEditor::OnStartEditing, this);
	// A CELL edit writes the buffer without changing the number of lines, so it never reaches
	// RefreshLines. One bind covers every column.
	m_view->Bind(wxEVT_DATAVIEW_ITEM_VALUE_CHANGED, [this](ibDataViewEvent& e) {
		e.Skip();
		if (!m_reloading && m_onChanged)
			m_onChanged();
	});
	m_model->ResetFromList();
}

void ibSortEditor::SetSort(ibSortDescription* sort)
{
	m_sort = sort;
	Reload();
}

void ibSortEditor::Reload()
{
	// QUIET — filling from the buffer is not somebody editing it. See ibFilterEditor::Reload.
	const bool wasReloading = m_reloading;
	m_reloading = true;
	RefreshLines();
	m_reloading = wasReloading;
}

// RE-READ THE LINES, AND SAY THEY CHANGED. The four mutating commands all ended in the same two
// lines; they end here now, so "a sort was edited" is stated once instead of four times.
void ibSortEditor::RefreshLines()
{
	if (m_model != nullptr)
		m_model->ResetFromList();
	if (!m_reloading && m_onChanged)
		m_onChanged();
}

void ibSortEditor::ReloadFields()
{
	if (m_fieldSource != nullptr)
		m_fieldSource->Populate(m_fieldCtrl);
}

// THE ROW'S INDEX, which is what an EDIT needs. A virtual-list row id is 1-based.
size_t ibSortEditor::IndexAt(const ibDataViewItem& row) const
{
	const size_t id = reinterpret_cast<size_t>(row.GetID());
	return id > 0 ? id - 1 : (size_t)-1;
}

// Add the chosen available field to the sort list (default Ascending; direction is edited
// inline in the Direction column). Mutates the BUFFER, then refreshes the model.
void ibSortEditor::AddForField(const wxTreeItemId& item)
{
	// VIEW ONLY — guarded here, where the toolbar, the menu, the field-tree double-click and the
	// drop all meet. See ibFilterEditor::AddFilterForField.
	if (m_readOnly)
		return;
	// THE FIELD THE TREE RESOLVED, whole: adding by path alone would rebuild a bare
	// field and lose the readable path and the type behind it.
	ibValuePtr<ibValueCompositionField> field(ibSettingsFieldTree::FieldAt(m_fieldCtrl, item));
	ibSortDescription* o = GetSort();
	if (!field || o == nullptr)
		return;
	o->Append(field->GetPath(), true);
	RefreshLines();
	ibSelectLastSettingsRow(m_view, o->m_lines.size());
}

// A NEW LINE IS EMPTY — the field is chosen in the row, by hand. Taking whatever is
// selected in the field tree conjures a line the user did not ask for.
void ibSortEditor::OnAdd(wxCommandEvent&)
{
	ibSortDescription* o = GetSort();
	if (o == nullptr)
		return;
	o->Append(wxEmptyString, true);
	RefreshLines();
	ibSelectLastSettingsRow(m_view, o->m_lines.size());
}

void ibSortEditor::OnRemove(wxCommandEvent&)
{
	// ONE LINE LEAVES, the rest stay where they are. Rebuilding the whole sort order
	// to drop one row was how this used to work — and it lost whatever a rebuild
	// could not spell back.
	ibSortDescription* o = GetSort();
	if (o == nullptr || m_view == nullptr)
		return;
	const ibDataViewItem& sel = m_view->GetSelection();
	if (!sel.IsOk())
		return;
	const size_t index = reinterpret_cast<size_t>(sel.GetID());   // 1-based
	if (index == 0 || index > o->m_lines.size())
		return;
	o->m_lines.erase(o->m_lines.begin() + (index - 1));
	RefreshLines();
}

// ORDER IS THE MEANING of a sort list, so moving a line is a first-class command,
// not something the user emulates by deleting and re-adding in the right sequence.
void ibSortEditor::MoveLine(int delta)
{
	ibSortDescription* o = GetSort();
	if (o == nullptr || m_view == nullptr)
		return;
	const ibDataViewItem& sel = m_view->GetSelection();
	if (!sel.IsOk())
		return;
	const size_t index = reinterpret_cast<size_t>(sel.GetID());   // 1-based
	if (index == 0 || index > o->m_lines.size())
		return;
	const int target = static_cast<int>(index - 1) + delta;
	if (target < 0 || target >= static_cast<int>(o->m_lines.size()))
		return;   // already at that end
	std::swap(o->m_lines[index - 1], o->m_lines[static_cast<size_t>(target)]);
	RefreshLines();
	// The row travelled — the cursor goes with it, or the next press moves a
	// different line.
	const size_t moved = (size_t)((int)index + delta);
	m_view->Select(ibDataViewItem(reinterpret_cast<void*>(moved)));
}

// THE SAME FOUR VERBS THE TOOLBAR CARRIES — add, delete, and the two that make an
// ordered list ordered. Both roads end in one handler and cannot drift apart.
// ⭐ VIEW ONLY — the twin of ibFilterEditor::SetReadOnly, word for word, because the two editors
// stand side by side and a different answer from one of them reads as a different rule.
void ibSortEditor::SetReadOnly(bool readOnly)
{
	m_readOnly = readOnly;
	if (m_toolbar != nullptr)
		m_toolbar->Enable(!readOnly);
}

void ibSortEditor::OnStartEditing(ibDataViewEvent& event)
{
	if (m_readOnly)
		event.Veto();
	else
		event.Skip();
}

void ibSortEditor::OnContextMenu(ibDataViewEvent&)
{
	// VIEW ONLY — the second road to the verbs the toolbar already refuses.
	if (m_readOnly)
		return;

	wxMenu menu;
	ibAppendCmd(menu, wxID_ADD, _("Add") + wxT("\tIns"), wxASCII_STR(wxART_NEW), this);
	ibAppendCmd(menu, wxID_REMOVE, _("Delete") + wxT("\tDel"), wxASCII_STR(wxART_DELETE), this);
	menu.AppendSeparator();
	ibAppendCmd(menu, wxID_UP, _("Move up"), wxASCII_STR(wxART_GO_UP), this);
	ibAppendCmd(menu, wxID_DOWN, _("Move down"), wxASCII_STR(wxART_GO_DOWN), this);

	menu.Bind(wxEVT_MENU, [this](wxCommandEvent& e) { OnAdd(e); }, wxID_ADD);
	menu.Bind(wxEVT_MENU, [this](wxCommandEvent& e) { OnRemove(e); }, wxID_REMOVE);
	menu.Bind(wxEVT_MENU, [this](wxCommandEvent&)   { MoveLine(-1); }, wxID_UP);
	menu.Bind(wxEVT_MENU, [this](wxCommandEvent&)   { MoveLine(+1); }, wxID_DOWN);

	PopupMenu(&menu);
}
