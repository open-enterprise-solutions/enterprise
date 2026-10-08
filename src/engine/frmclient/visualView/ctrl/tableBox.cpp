////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : a table of a view
////////////////////////////////////////////////////////////////////////////

#include "tableBox.h"

#include <wx/itemattr.h>   // wxItemAttr — the header's look
#include <wx/renderer.h>   // wxRendererNative::DrawCheckMark — a boolean cell

#include "frmclient/visualView/commandBar.h"
#include "frmclient/win/picture.h"
#include "frmclient/win/ctrls/controlTextEditor.h"
#include "frmclient/win/ctrls/dataview/dataview.h"
#include "frmclient/win/ctrls/dataview/headerctrlg.h"

namespace {

// A CELL DRAWN — the desktop's ibDataViewValueRenderer: what it draws is asked of the value — a boolean a tick where
// the text would start (a mark, not a box: a list cell is not ticked by a click), anything else its text; where it
// stands is the cell's look (a number right). Edited, the field's editor stands in it, as its column says; what is typed
// goes to the server when the edit is done, and its buttons' presses travel up to the table, which knows the cell.
class ibViewCellRenderer : public ibDataViewCustomRenderer {
public:

	explicit ibViewCellRenderer(const ibValueModelTableBoxColumn& column)
		: ibDataViewCustomRenderer(GetDefaultType(), wxDATAVIEW_CELL_EDITABLE), m_column(column) {}

	virtual bool IsCompatibleVariantType(const wxString& /*variantType*/) const override { return true; }

	virtual bool SetValue(const wxVariant& value) override
	{
		m_flag.reset();
		m_text.clear();
		if (value.GetType() == wxT("bool"))
			m_flag = value.GetBool();
		else if (!value.IsNull())
			m_text = value.GetString();
		return true;
	}

	virtual bool GetValue(wxVariant& /*value*/) const override { return true; }

	virtual bool Render(wxRect rect, wxDC* dc, int state) override
	{
		if (m_flag.has_value()) {
			if (*m_flag) {
				wxWindow* const view = GetView();
				const wxSize mark = wxRendererNative::Get().GetCheckMarkSize(view);
				const wxRect at(rect.x, rect.y + (rect.height - mark.y) / 2, mark.x, mark.y);
				wxRendererNative::Get().DrawCheckMark(view, *dc, at);
			}
			return true;
		}
		RenderText(m_text, 0, rect, dc, state);
		return true;
	}

	virtual wxSize GetSize() const override
	{
		if (m_flag.has_value())
			return wxRendererNative::Get().GetCheckMarkSize(GetView());
		return GetTextExtent(m_text);
	}

	virtual bool HasEditorCtrl() const override { return true; }

	// The desktop's CreateEditorCtrl: the field's editor in the cell, as its column says, the table's look on it.
	virtual wxWindow* CreateEditorCtrl(wxWindow* parent, wxRect labelRect, const wxVariant& value) override
	{
		ibControlTextEditor* const textEditor = new ibControlTextEditor;
		textEditor->SetDVCMode(true);
		textEditor->Show(false);   // hidden while it is made: no flicker
		if (!textEditor->Create(parent, wxID_ANY, value.IsNull() ? wxString() : value.GetString(),
			labelRect.GetPosition(), labelRect.GetSize()))
			return nullptr;

		textEditor->ShowSelectButton(m_column.GetSelectButton());
		textEditor->ShowOpenButton(m_column.GetOpenButton());
		textEditor->ShowClearButton(m_column.GetClearButton());

		wxWindow* const view = GetView();
		textEditor->SetBackgroundColour(view->GetBackgroundColour());
		textEditor->SetForegroundColour(view->GetForegroundColour());
		textEditor->SetFont(view->GetFont());

		textEditor->SetPasswordMode(m_column.GetPasswordMode());
		textEditor->SetMultilineMode(m_column.GetMultilineMode());
		textEditor->SetTextEditMode(m_column.GetTextEditMode());

		textEditor->LayoutControls();
		textEditor->Show(true);
		textEditor->SetInsertionPointEnd();
		m_textEditor = textEditor;
		return textEditor;
	}

	// The text in the editor it made.
	virtual bool GetValueFromEditorCtrl(wxWindow* editor, wxVariant& value) override
	{
		if (editor == nullptr || editor != m_textEditor)
			return false;
		value = m_textEditor->GetValue();
		m_textEditor = nullptr;
		return true;
	}

private:

	const ibValueModelTableBoxColumn& m_column;
	std::optional<bool>      m_flag;
	wxString                 m_text;
	ibControlTextEditor*     m_textEditor = nullptr;   // the editor it made, while the cell is edited
};

// A table's own properties, as its node writes them — itself less its columns and its State — with what of the State
// its window takes (UpdateWindow): what laying on again would only lay out and repaint.
ibProtocolNode PropertiesOf(const ibProtocolNode& node)
{
	const ibProtocolNode state = node.FindChild(ibProtocolName::State);
	ibProtocolNode properties = node.Clone();
	properties.Remove(ibProtocolName::NodeChildren);
	properties.Remove(ibProtocolName::State);
	properties.SetValue(ibProtocolName::Enabled, state.GetBool(ibProtocolName::Enabled, true))
		.SetValue(ibProtocolName::Visible, state.GetBool(ibProtocolName::Visible, true))
		.SetValue(ibProtocolName::Tooltip, state.GetString(ibProtocolName::Tooltip));
	return properties;
}

} // namespace

// THE TABLE'S DATA VIEW — the desktop's ibTableViewCtrl: the data view, and what a table asks of it beside — the row
// read that a handle names. The data view finds a row by its object; the server names it by its handle.
class ibTableViewCtrl : public ibDataViewCtrl {
public:

	ibTableViewCtrl(wxWindow* parent, long style)
		: ibDataViewCtrl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, style) {}

	// The row read of that handle — none: not among the rows read.
	ibDataViewItem FindRow(long long handle) const
	{
		for (unsigned int row = 0; row < GetRowCount(); ++row) {
			const ibDataViewItem item = GetItemByRow(row);
			if (ibViewTableModel::HandleOf(item) == handle)
				return item;
		}
		return ibDataViewItem();
	}
};

// THE DATA VIEW'S COLUMN OF A TABLE COLUMN — the desktop's ibDataViewColumnObject, of its column control: the arrow
// the data view drops after each read is put back as the column says the server sorts (SyncSortArrowFromModel).
class ibViewTableColumnObject : public ibDataViewColumn {
public:

	ibViewTableColumnObject(const ibValueModelTableBoxColumn& column, unsigned int modelColumn)
		: ibDataViewColumn(wxEmptyString, new ibViewCellRenderer(column), modelColumn, wxDVC_DEFAULT_WIDTH,
			wxALIGN_CENTER, wxDATAVIEW_COL_REORDERABLE), m_column(column) {}

	virtual void SyncSortArrowFromModel() override
	{
		if (m_column.GetSort() != 0)
			SetSortOrder(m_column.GetSort() > 0);
	}

private:

	const ibValueModelTableBoxColumn& m_column;
};

//***********************************************************************************
//*                                   tableBox                                      *
//***********************************************************************************

ibValueModelTableBox::ibValueModelTableBox(ibVisualHostClient& host, long long controlId)
	: ibValueWindow(host, controlId), m_alive(std::make_shared<bool>(true))
{
}

ibValueModelTableBox::~ibValueModelTableBox() = default;

void ibValueModelTableBox::Create(wxWindow* parent, const ibProtocolNode& /*node*/)
{
	m_panel = new wxPanel(parent, wxID_ANY);
	wxBoxSizer* const sizer = new wxBoxSizer(wxVERTICAL);

	// The chrome: its command bar above it.
	m_commandBar = std::make_unique<ibViewCommandBar>(m_panel, [this](const ibProtocolNode& args) {
		Send(ibProtocolEvent::Command, args);
	});
	sizer->Add(m_commandBar->GetWindow(), 0, wxEXPAND);

	m_dataView = new ibTableViewCtrl(m_panel,
		wxDV_SINGLE | wxDV_HORIZ_RULES | wxDV_VERT_RULES | wxDV_ROW_LINES | wxDV_VARIABLE_LINE_HEIGHT | wxBORDER_SIMPLE);
	m_dataView->SetBackgroundColour(*wxWHITE);
	sizer->Add(m_dataView, 1, wxEXPAND);
	m_panel->SetSizer(sizer);
	m_panel->SetMinSize(m_panel->FromDIP(wxSize(150, 75)));

	// The rows — read once the table has its size and its columns (the data view's bootstrap, on the next idle), on
	// its thread; what an answer leaves to the UI comes back here, if this is still here.
	std::weak_ptr<bool> alive = m_alive;
	ibViewTableModel* const model = new ibViewTableModel(MakeFetcher(), [this, alive](const ibViewTableModel::ibAnswer& answer) {
		wxTheApp->CallAfter([this, alive, answer]() {
			if (alive.lock())
				OnAnswered(answer);
		});
	});
	m_dataView->AssociateModel(model);
	model->DecRef();   // the data view holds it

	m_dataView->Bind(wxEVT_IDLE, &ibValueModelTableBox::OnIdle, this);
	m_dataView->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, &ibValueModelTableBox::OnSelectionChanged, this);
	m_dataView->GetMainWindow()->Bind(wxEVT_LEFT_UP, &ibValueModelTableBox::OnCellCursor, this);
	m_dataView->GetMainWindow()->Bind(wxEVT_KEY_UP, &ibValueModelTableBox::OnCellCursor, this);
	m_dataView->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, &ibValueModelTableBox::OnItemActivated, this);
	m_dataView->Bind(wxEVT_DATAVIEW_ITEM_START_EDITING, &ibValueModelTableBox::OnItemStartEditing, this);
	m_dataView->Bind(wxEVT_DATAVIEW_ITEM_EDITING_STARTED, &ibValueModelTableBox::OnItemEditingStarted, this);
	m_dataView->Bind(wxEVT_DATAVIEW_ITEM_EDITING_DONE, &ibValueModelTableBox::OnItemEditingDone, this);
	// The cell editor's buttons — they travel up from the editor to the table, which knows the cell.
	m_dataView->Bind(wxEVT_CONTROL_BUTTON_SELECT, &ibValueModelTableBox::OnEditorButton, this);
	m_dataView->Bind(wxEVT_CONTROL_BUTTON_OPEN, &ibValueModelTableBox::OnEditorButton, this);
	m_dataView->Bind(wxEVT_CONTROL_BUTTON_CLEAR, &ibValueModelTableBox::OnEditorButton, this);
	m_dataView->Bind(wxEVT_DATAVIEW_COLUMN_HEADER_CLICK, &ibValueModelTableBox::OnColumnHeaderClick, this);
	m_dataView->Bind(wxEVT_DATAVIEW_COLUMN_REORDERED, &ibValueModelTableBox::OnColumnReordered, this);
	m_dataView->Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, &ibValueModelTableBox::OnContextMenu, this);
	m_dataView->GenericGetHeader()->Bind(wxEVT_HEADER_END_RESIZE, &ibValueModelTableBox::OnHeaderEndResize, this);
}

void ibValueModelTableBox::Update(const ibProtocolNode& node)
{
	// THE TABLE'S OWN PROPERTIES — laid on only when they are others than laid last. The desktop laid them once per
	// update of its form; here the table is drawn again on every answer that moved its State — the cursor, the rows
	// read anew, an editor opened — and each of them lays the data view out again and repaints it (FreezeTo, the
	// header's height and look, the panel's sizes): the table blinked on every move.
	const ibProtocolNode properties = PropertiesOf(node);
	if (properties != m_properties) {
		m_properties = properties;

		// The desktop's OnUpdated: the header and the footer, their heights in lines, the rows and columns frozen, how
		// the cursor stands.
		m_dataView->ShowHeaderWindow(node.GetBool(ibProtocolName::Header, true));
		m_dataView->SetHeaderHeight(static_cast<int>(node.GetInt(ibProtocolName::HeaderHeight, 1)));
		m_dataView->ShowFooterWindow(node.GetBool(ibProtocolName::Footer, false));
		m_dataView->SetFooterHeight(static_cast<int>(node.GetInt(ibProtocolName::FooterHeight, 1)));
		m_dataView->FreezeTo(static_cast<int>(node.GetInt(ibProtocolName::FreezeRow)),
			static_cast<int>(node.GetInt(ibProtocolName::FreezeCol)));
		m_dataView->SetSelectionMode(node.GetInt(ibProtocolName::RowSelectionMode, ibDataViewSelectRow) == ibDataViewSelectCell
			? ibDataViewSelectCell : ibDataViewSelectRow);

		UpdateWindow(m_panel, node);

		// The table's look onto the data view itself and its header — the desktop's OnUpdated: the panel around it is
		// the chrome, the font and the colours are the table's.
		ApplyLook(m_dataView, node);
		m_dataView->SetHeaderAttr(wxItemAttr(m_dataView->GetForegroundColour(), m_dataView->GetBackgroundColour(),
			m_dataView->GetFont()));
		// …and its command bar takes the table's look too (the desktop's ApplyLook(part)).
		ApplyLook(m_commandBar->GetWindow(), node);
	}

	UpdateState(node.FindChild(ibProtocolName::State), nullptr);
}

void ibValueModelTableBox::Update(const ibProtocolNode& node, const ibProtocolNode& patch)
{
	// Its properties among what changed — laid on as a whole (Update); its State alone — only what of it changed: an
	// answer that moved the cursor neither builds the command bar again nor repaints it.
	if (patch.GetChangeCount() > (patch.Has(ibProtocolName::State) ? 1u : 0u)) {
		Update(node);
		return;
	}
	const ibProtocolNode changed = patch.FindChild(ibProtocolName::State);
	UpdateState(node.FindChild(ibProtocolName::State), &changed);
}

void ibValueModelTableBox::UpdateState(const ibProtocolNode& state, const ibProtocolNode* changed)
{
	// ⭐ "ALL OF IT" IS NO NODE: an ibProtocolNode() is a node with no entries, and as `changed` it named nothing — a
	// table built anew had no command bar, no context menu, no current row.
	const bool whole = changed == nullptr;

	if (whole || changed->Has(ibProtocolName::CommandBar))
		m_commandBar->Update(state.FindChild(ibProtocolName::CommandBar));
	if (whole || changed->Has(ibProtocolName::ContextMenu)) {
		// A copy — the frame changes under a handle into it with the next answer.
		const ibProtocolNode contextMenu = state.FindChild(ibProtocolName::ContextMenu);
		m_contextMenu = contextMenu.IsNode() ? contextMenu.Clone() : ibProtocolNode();
	}

	// How the rows are shown — the State's: the person switches it.
	if (whole || changed->Has(ibProtocolName::ViewMode))
		m_dataView->SetViewMode(static_cast<ibDataViewViewMode>(state.GetInt(ibProtocolName::ViewMode,
			static_cast<int>(ibProtocolViewMode::Hierarchical))));

	// The rows read anew when the server has — the first time, the data view reads them by itself.
	if (whole || changed->Has(ibProtocolName::Version)) {
		const long long version = state.GetInt(ibProtocolName::Version);
		if (m_version != -1 && version != m_version)
			m_dataView->SchedulePagedRefresh();
		m_version = version;
	}

	// The row the server moved the cursor to — it says none back that the client told it.
	if (whole || changed->Has(ibProtocolName::CurrentRow)) {
		const long long currentRow = state.GetInt(ibProtocolName::CurrentRow);
		if (currentRow != 0)
			SelectRow(currentRow);
	}

	// A row activated: the server opens a cell's editor (an editable row) — the answer that says so opens it here.
	if (whole || changed->Has(ibProtocolName::Edit)) {
		const ibProtocolNode edit = state.FindChild(ibProtocolName::Edit);
		if (m_awaitEdit && edit.IsNode()) {
			m_awaitEdit = false;
			OpenEditor(edit);
		}
	}
}

wxWindow* ibValueModelTableBox::GetWindow() const
{
	return m_panel;
}

ibDataViewColumnGroup* ibValueModelTableBox::GetColumnHolder() const
{
	return m_dataView->GetRootColumnGroup();
}

void ibValueModelTableBox::SelectRow(long long handle)
{
	m_currentRow = handle;

	// NOT WHILE A READ IS OUT: a selection then sends the data view to read again (its restore channel drops the read
	// out and arms a new one) — and that read's answer names the current row again, for ever. It waits for the read to
	// land (OnIdle).
	m_selectPending = m_dataView->IsFetchInFlight();
	ibJournalInfo(wxT("table"), wxT("current row %lld%s, the cursor on %lld"), handle,
		m_selectPending ? wxT(" once the read lands") : wxT(""), ibViewTableModel::HandleOf(m_dataView->GetSelection()));
	if (m_selectPending)
		return;

	if (ibViewTableModel::HandleOf(m_dataView->GetSelection()) == handle)
		return;

	// The row itself — the one read. One not read yet (a choice form opened on a value) is the row the next read stands
	// on: the data view reads around it and above it, and the read's answer names it current again.
	const ibDataViewItem row = m_dataView->FindRow(handle);
	m_selecting = true;
	if (row.IsOk()) {
		m_dataView->Select(row);
		m_dataView->EnsureVisible(row);
	}
	else {
		m_dataView->SetPagedRestoreSelection(ibDataViewItem(new ibViewTableRow(handle, ibDataViewItem())));
	}
	m_selecting = false;
}

void ibValueModelTableBox::OpenEditor(const ibProtocolNode& edit)
{
	if (m_editRow != 0)
		return;   // a cell is being edited already

	const ibDataViewItem row = m_dataView->FindRow(edit.GetInt(ibProtocolName::Row));
	if (!row.IsOk())
		return;
	const long long columnId = edit.GetInt(ibProtocolName::Column);
	for (unsigned int idx = 0; idx < m_dataView->GetColumnCount(); ++idx) {
		const ibDataViewColumn* const column = m_dataView->GetColumn(idx);
		if (static_cast<long long>(column->GetModelColumn()) == columnId) {
			m_dataView->EditItem(row, column);
			return;
		}
	}
}

bool ibValueModelTableBox::EditCurrentRow(const ibDataViewItem& item)
{
	// The current column when its cell may be edited, else the first that may — the desktop's EditCurrentRow.
	const ibDataViewColumn* editColumn = nullptr;
	const ibDataViewColumn* const current = m_dataView->GetCurrentColumn();
	if (current != nullptr && ibViewTableModel::IsCellEditable(item, current->GetModelColumn()))
		editColumn = current;
	else
		for (unsigned int idx = 0; idx < m_dataView->GetColumnCount(); ++idx) {
			const ibDataViewColumn* const column = m_dataView->GetColumn(idx);
			if (column != nullptr && ibViewTableModel::IsCellEditable(item, column->GetModelColumn())) {
				editColumn = column;
				break;
			}
		}

	if (editColumn == nullptr)
		return false;

	m_dataView->EditItem(item, editColumn);
	return true;
}

void ibValueModelTableBox::SendCell(ibProtocolEvent event, const ibProtocolNode& args)
{
	ibProtocolNode cell = args.IsNode() ? args.Clone() : ibProtocolNode();
	cell.SetValue(ibProtocolName::Row, m_editRow).SetValue(ibProtocolName::Column, m_editColumn);
	Send(event, cell);
}

void ibValueModelTableBox::OnAnswered(const ibViewTableModel::ibAnswer& answer)
{
	// The pictures first — the rows they are of are drawn next.
	for (const auto& [id, picture] : answer.pictures)
		ibProtocolPictureKeep(id, ibProtocolPicture(picture));
	if (!answer.pictures.empty())
		m_dataView->Refresh();

	if (answer.currentRow != 0)
		SelectRow(answer.currentRow);
}

//*******************************************************************
//*                             Events                              *
//*******************************************************************

void ibValueModelTableBox::OnIdle(wxIdleEvent& event)
{
	event.Skip();
	if (m_selectPending && !m_dataView->IsFetchInFlight())
		SelectRow(m_currentRow);
}

void ibValueModelTableBox::OnSelectionChanged(ibDataViewEvent& event)
{
	event.Skip();
	if (m_selecting)
		return;

	// Told once the click is done with: the data view says the selection changed BEFORE it puts the column clicked
	// (its mouse handler), and the column is what a command by the current column reads on the server.
	m_dataView->CallAfter([this, alive = m_alive]() {
		if (*alive)
			SendCursor();
	});
}

void ibValueModelTableBox::OnCellCursor(wxEvent& event)
{
	event.Skip();

	// A click or a key among the rows — the cursor may have gone across its row to another column, which the data
	// view says nothing of: told once the data view is done with it.
	m_dataView->CallAfter([this, alive = m_alive]() {
		if (*alive)
			SendCursor();
	});
}

void ibValueModelTableBox::SendCursor()
{
	if (m_selecting)
		return;

	const long long handle = ibViewTableModel::HandleOf(m_dataView->GetSelection());
	const ibDataViewColumn* const column = m_dataView->GetCurrentColumn();
	const long long columnId = column != nullptr ? static_cast<long long>(column->GetModelColumn()) : 0;
	if (handle == 0 || (handle == m_currentRow && columnId == m_currentColumn))
		return;
	ibJournalInfo(wxT("table"), wxT("the cursor moved to %lld, column %lld, the current row %lld"), handle,
		columnId, m_currentRow);

	// The person went on: an activation's editor is not opened behind them.
	if (handle != m_currentRow)
		m_awaitEdit = false;
	m_currentRow = handle;
	m_currentColumn = columnId;

	// The column too, where the cursor stands on a cell — a column's model column is its id.
	ibProtocolNode args;
	args.SetValue(ibProtocolName::Row, handle);
	if (columnId != 0)
		args.SetValue(ibProtocolName::Column, columnId);
	Send(ibProtocolEvent::Row, args);
}

void ibValueModelTableBox::OnItemActivated(ibDataViewEvent& event)
{
	const long long handle = ibViewTableModel::HandleOf(event.GetItem());
	if (handle == 0) {
		event.Skip();
		return;
	}

	// THE CELL FIRST — the one clicked, else the one the cursor stands on (Enter).
	const ibDataViewColumn* const column = event.GetDataViewColumn() != nullptr
		? event.GetDataViewColumn() : m_dataView->GetCurrentColumn();
	if (column != nullptr) {
		m_currentRow = handle;
		m_currentColumn = static_cast<long long>(column->GetModelColumn());
	}

	// The desktop's ActivateRow: an editable cell opens here, at once, as F2 opens it (EditCurrentRow) — and its Input
	// says the row and the column (OnItemEditingStarted), so no Row goes before it. Otherwise the server opens its
	// editor in the column it has current, and a move across one row was never said (Row is sent when the row changes).
	if (!EditCurrentRow(event.GetItem())) {
		if (column != nullptr) {
			ibProtocolNode cell;
			cell.SetValue(ibProtocolName::Row, handle).SetValue(ibProtocolName::Column, m_currentColumn);
			Send(ibProtocolEvent::Row, cell);
		}
		m_awaitEdit = true;
	}

	// What else activating it is the server decides — the row's object opened, a choice made — and it raises the row's
	// activation for the form.
	ibProtocolNode args;
	args.SetValue(ibProtocolName::Row, handle);
	Send(ibProtocolEvent::Open, args);
}

void ibValueModelTableBox::OnItemStartEditing(ibDataViewEvent& event)
{
	// Only a cell the server lets be edited — it wrote its value.
	const ibDataViewColumn* const column = event.GetDataViewColumn();
	if (column == nullptr || !ibViewTableModel::IsCellEditable(event.GetItem(), column->GetModelColumn()))
		event.Veto();
}

void ibValueModelTableBox::OnItemEditingStarted(ibDataViewEvent& event)
{
	const ibDataViewColumn* const column = event.GetDataViewColumn();
	m_editRow = ibViewTableModel::HandleOf(event.GetItem());
	m_editColumn = column != nullptr ? static_cast<long long>(column->GetModelColumn()) : 0;
	m_awaitEdit = false;

	// The typing started: the server takes the cell (its Edit) — the object is modified, nothing is committed.
	SendCell(ibProtocolEvent::Input, ibProtocolNode());
}

void ibValueModelTableBox::OnItemEditingDone(ibDataViewEvent& event)
{
	// What was typed, committed — or nothing, the edit cancelled.
	if (m_editRow != 0 && !event.IsEditCancelled()) {
		ibProtocolNode args;
		args.SetValue(ibProtocolName::Text, event.GetValue().GetString());
		SendCell(ibProtocolEvent::Change, args);
	}
	m_editRow = 0;
	m_editColumn = 0;
}

void ibValueModelTableBox::OnEditorButton(wxCommandEvent& event)
{
	if (m_editRow == 0) {
		event.Skip();
		return;
	}
	const wxEventType type = event.GetEventType();

	// Cleared — into the open editor too, as the desktop's column wrote the empty value there (SetControlValue): the
	// text left standing in it went back with the edit's end and found the value it had cleared again.
	if (type == wxEVT_CONTROL_BUTTON_CLEAR) {
		if (wxTextCtrl* const text = wxDynamicCast(event.GetEventObject(), wxTextCtrl))
			text->SetValue(wxEmptyString);
	}

	SendCell(type == wxEVT_CONTROL_BUTTON_SELECT ? ibProtocolEvent::Select
		: type == wxEVT_CONTROL_BUTTON_OPEN ? ibProtocolEvent::Open : ibProtocolEvent::Clear, ibProtocolNode());
}

void ibValueModelTableBox::OnColumnHeaderClick(ibDataViewEvent& event)
{
	// The sort is the server's: asked of it, the arrow and the rows come back with its answer. Not passed on — the
	// data view would turn its arrow by itself.
	const ibDataViewColumn* const column = event.GetDataViewColumn();
	if (column == nullptr || !column->IsSortable())
		return;

	// The rows come back in another order: the read after it starts at the top, not at the row that was the top
	// (the data view's own header click does the same).
	m_dataView->SetPagedSkipRestoreCapture();

	ibProtocolNode args;
	args.SetValue(ibProtocolName::Column, static_cast<long long>(column->GetModelColumn()));
	Send(ibProtocolEvent::Sort, args);
}

void ibValueModelTableBox::OnColumnReordered(ibDataViewEvent& event)
{
	event.Skip();

	// The desktop's OnColumnReordered: the column moved in the form's tree to where the header put it — the group it now
	// stands in (the table's own root group: the table), at its place there.
	const ibDataViewColumn* const column = event.GetDataViewColumn();
	ibDataViewColumnGroup* const holderGroup = column != nullptr ? column->GetParent() : nullptr;
	if (holderGroup == nullptr)
		return;

	// Under the table, the control of a column (its model column is its id) and the group that holds it — none: the table.
	ibValueFrame* columnObject = nullptr;
	ibValueFrame* holder = this;
	std::function<void(ibValueFrame*)> find = [&](ibValueFrame* control) {
		for (unsigned int idx = 0; idx < control->GetChildCount(); idx++) {
			ibValueFrame* const child = control->GetChild(idx);
			if (child->GetControlID() == static_cast<long long>(column->GetModelColumn()))
				columnObject = child;
			else if (child->GetColumnHolder() == holderGroup)
				holder = child;
			find(child);
		}
	};
	find(this);
	if (columnObject == nullptr)
		return;

	const int at = holderGroup->GetMemberPosition(column);

	// ONE PATH for both cases — a move inside the same holder and a move into another one differ only in which parent it
	// is taken off. Here so the form's editor, opened next, finds it where it stands.
	ibValuePtr<ibValueFrame> keep(columnObject);
	if (ibValueFrame* oldHolder = columnObject->GetParent())
		oldHolder->RemoveChild(columnObject);

	columnObject->SetParent(holder);
	holder->AddChild(at != wxNOT_FOUND
		? wxMin((unsigned int)at, holder->GetChildCount()) : holder->GetChildCount(),
		columnObject);

	// …and in the server's, which is the form: the same move there (OnColumnMoved).
	ibProtocolNode args;
	args.SetValue(ibProtocolName::Column, columnObject->GetControlID())
		.SetValue(ibProtocolName::Holder, holder != this ? holder->GetControlID() : 0)
		.SetValue(ibProtocolName::Position, static_cast<long long>(at));
	Send(ibProtocolEvent::Move, args);
}

void ibValueModelTableBox::OnContextMenu(ibDataViewEvent& event)
{
	// The desktop's OnContextMenu: the table's standard commands, as the server offers them (State's ContextMenu) — the
	// one picked goes back as Action {Id}; a command greyed there is not offered here either.
	const std::vector<ibProtocolNode> entries = m_contextMenu.Children();
	if (entries.empty())
		return;

	constexpr int kFirstEntryId = wxID_HIGHEST + 1;
	wxMenu menu;
	for (std::size_t idx = 0; idx < entries.size(); ++idx) {
		wxMenuItem* const item = menu.Append(kFirstEntryId + static_cast<int>(idx), entries[idx].GetString(ibProtocolName::Caption));
		const wxBitmap picture = ibProtocolPicture(entries[idx].GetString(ibProtocolName::Picture));
		if (picture.IsOk())
			item->SetBitmap(picture);
		item->Enable(entries[idx].GetBool(ibProtocolName::Enabled, true));
	}

	const int chosen = m_dataView->GetPopupMenuSelectionFromUser(menu, event.GetPosition());
	if (chosen == wxID_NONE)
		return;
	const std::size_t idx = static_cast<std::size_t>(chosen - kFirstEntryId);
	if (idx >= entries.size())
		return;

	ibProtocolNode args;
	args.SetValue(ibProtocolName::Id, entries[idx].GetInt(ibProtocolName::Id));
	Send(ibProtocolEvent::Action, args);
}

void ibValueModelTableBox::OnHeaderEndResize(ibHeaderGenericCtrlEvent& event)
{
	event.Skip();

	// The width the column asks for, not the one it shows stretched (the desktop's OnHeaderResizing) — read once the
	// drag is done with it.
	ibDataViewColumn* const column = m_dataView->GetColumn(static_cast<unsigned int>(event.GetColumn()));
	if (column == nullptr)
		return;
	m_dataView->CallAfter([this, column]() {
		ibProtocolNode args;
		args.SetValue(ibProtocolName::Column, static_cast<long long>(column->GetModelColumn()))
			.SetValue(ibProtocolName::Width, column->WXGetSpecifiedWidth());
		Send(ibProtocolEvent::Resize, args);
	});
}

bool ibValueModelTableBox::ReadData(const ibDataNode& node)
{
	m_propertyHeader->SetNodeValue(node.GetProperty(m_propertyHeader->GetName()));
	m_propertyHeaderHeight->SetNodeValue(node.GetProperty(m_propertyHeaderHeight->GetName()));
	m_propertyFooter->SetNodeValue(node.GetProperty(m_propertyFooter->GetName()));
	m_propertyFooterHeight->SetNodeValue(node.GetProperty(m_propertyFooterHeight->GetName()));

	m_propertyFreezeRow->SetNodeValue(node.GetProperty(m_propertyFreezeRow->GetName()));
	m_propertyFreezeCol->SetNodeValue(node.GetProperty(m_propertyFreezeCol->GetName()));

	m_propertyRowSelectionMode->SetNodeValue(node.GetProperty(m_propertyRowSelectionMode->GetName()));
	m_propertyChoiceMode->SetNodeValue(node.GetProperty(m_propertyChoiceMode->GetName()));
	m_propertyViewMode->SetNodeValue(node.GetProperty(m_propertyViewMode->GetName()));

	return ibValueWindow::ReadData(node);
}

bool ibValueModelTableBox::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyHeader->GetName(), m_propertyHeader->GetNodeValue());
	node.SetProperty(m_propertyHeaderHeight->GetName(), m_propertyHeaderHeight->GetNodeValue());
	node.SetProperty(m_propertyFooter->GetName(), m_propertyFooter->GetNodeValue());
	node.SetProperty(m_propertyFooterHeight->GetName(), m_propertyFooterHeight->GetNodeValue());

	node.SetProperty(m_propertyFreezeRow->GetName(), m_propertyFreezeRow->GetNodeValue());
	node.SetProperty(m_propertyFreezeCol->GetName(), m_propertyFreezeCol->GetNodeValue());

	node.SetProperty(m_propertyRowSelectionMode->GetName(), m_propertyRowSelectionMode->GetNodeValue());
	node.SetProperty(m_propertyChoiceMode->GetName(), m_propertyChoiceMode->GetNodeValue());
	node.SetProperty(m_propertyViewMode->GetName(), m_propertyViewMode->GetNodeValue());

	return ibValueWindow::WriteData(node);
}

//***********************************************************************************
//*                                    Column                                       *
//***********************************************************************************

void ibValueModelTableBoxColumn::Create(wxWindow* /*parent*/, const ibProtocolNode& node)
{
	// Built bare, as on the desktop, and hung on the group it is in; what it shows is its State's (Update).
	m_column = new ibViewTableColumnObject(*this, static_cast<unsigned int>(node.GetInt(ibProtocolName::ControlId)));
	if (ibDataViewColumnGroup* const holder = GetParent() != nullptr ? GetParent()->GetColumnHolder() : nullptr)
		holder->AppendColumn(m_column);
}

void ibValueModelTableBoxColumn::Update(const ibProtocolNode& node)
{
	const ibProtocolNode state = node.FindChild(ibProtocolName::State);

	// The desktop's column OnUpdated. The header and the footer — the captions, the pictures, or both, as its
	// representation says (the server resolves Auto, and sends no picture for a text one).
	const bool picture = state.GetInt(ibProtocolName::Representation) == static_cast<int>(ibProtocolRepresentation::Picture);
	m_column->SetTitle(picture ? wxString() : state.GetString(ibProtocolName::Caption));
	m_column->SetBitmap(ibProtocolPicture(state.GetString(ibProtocolName::HeaderPicture)));
	m_column->SetFooterTitle(picture ? wxString() : state.GetString(ibProtocolName::FooterText));
	m_column->SetFooterBitmap(ibProtocolPicture(state.GetString(ibProtocolName::FooterPicture)));

	m_column->SetWidth(static_cast<int>(node.GetInt(ibProtocolName::Width, 80)));
	m_column->SetAlignment(static_cast<wxAlignment>(node.GetInt(ibProtocolName::HeaderAlign, wxALIGN_LEFT)));
	m_column->SetFooterAlignment(static_cast<wxAlignment>(node.GetInt(ibProtocolName::FooterAlign, wxALIGN_LEFT)));

	m_column->SetHidden(!state.GetBool(ibProtocolName::Visible, true));
	m_column->SetSortable(state.GetBool(ibProtocolName::Sortable));
	m_column->SetResizeable(node.GetBool(ibProtocolName::Resizable, true));
	m_column->SetReorderable(node.GetBool(ibProtocolName::Reorderable, true));

	// How its cells are typed into is its properties' (ReadData); the server says whether the column may be written.
	m_readOnly = state.GetBool(ibProtocolName::ReadOnly);

	// The sort the server keeps, shown by the arrow; a click asks it for another.
	m_sort = state.GetInt(ibProtocolName::Sort);
	if (m_sort != 0)
		m_column->SetSortOrder(m_sort > 0);
	else if (m_column->IsSortKey())
		m_column->UnsetAsSortKey();
}

bool ibValueModelTableBoxColumn::ReadData(const ibDataNode& node)
{
	m_propertyTitle->SetNodeValue(node.GetProperty(m_propertyTitle->GetName()));
	m_propertyRepresentation->SetNodeValue(node.GetProperty(m_propertyRepresentation->GetName()));
	m_propertyFooterText->SetNodeValue(node.GetProperty(m_propertyFooterText->GetName()));
	m_propertyPasswordMode->SetNodeValue(node.GetProperty(m_propertyPasswordMode->GetName()));
	m_propertyMultilineMode->SetNodeValue(node.GetProperty(m_propertyMultilineMode->GetName()));
	m_propertyTexteditMode->SetNodeValue(node.GetProperty(m_propertyTexteditMode->GetName()));
	m_propertyFormat->SetNodeValue(node.GetProperty(m_propertyFormat->GetName()));
	m_propertySelectButton->SetNodeValue(node.GetProperty(m_propertySelectButton->GetName()));
	m_propertyOpenButton->SetNodeValue(node.GetProperty(m_propertyOpenButton->GetName()));
	m_propertyClearButton->SetNodeValue(node.GetProperty(m_propertyClearButton->GetName()));
	m_propertyHeaderAlign->SetNodeValue(node.GetProperty(m_propertyHeaderAlign->GetName()));
	m_propertyFooterAlign->SetNodeValue(node.GetProperty(m_propertyFooterAlign->GetName()));
	m_propertyWidth->SetNodeValue(node.GetProperty(m_propertyWidth->GetName()));
	m_propertyVisible->SetNodeValue(node.GetProperty(m_propertyVisible->GetName()));
	m_propertyResizable->SetNodeValue(node.GetProperty(m_propertyResizable->GetName()));
	m_propertyReorderable->SetNodeValue(node.GetProperty(m_propertyReorderable->GetName()));

	return ibValueControl::ReadData(node);
}

bool ibValueModelTableBoxColumn::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyTitle->GetName(), m_propertyTitle->GetNodeValue());
	node.SetProperty(m_propertyRepresentation->GetName(), m_propertyRepresentation->GetNodeValue());
	node.SetProperty(m_propertyFooterText->GetName(), m_propertyFooterText->GetNodeValue());
	node.SetProperty(m_propertyPasswordMode->GetName(), m_propertyPasswordMode->GetNodeValue());
	node.SetProperty(m_propertyMultilineMode->GetName(), m_propertyMultilineMode->GetNodeValue());
	node.SetProperty(m_propertyTexteditMode->GetName(), m_propertyTexteditMode->GetNodeValue());
	node.SetProperty(m_propertyFormat->GetName(), m_propertyFormat->GetNodeValue());
	node.SetProperty(m_propertySelectButton->GetName(), m_propertySelectButton->GetNodeValue());
	node.SetProperty(m_propertyOpenButton->GetName(), m_propertyOpenButton->GetNodeValue());
	node.SetProperty(m_propertyClearButton->GetName(), m_propertyClearButton->GetNodeValue());
	node.SetProperty(m_propertyHeaderAlign->GetName(), m_propertyHeaderAlign->GetNodeValue());
	node.SetProperty(m_propertyFooterAlign->GetName(), m_propertyFooterAlign->GetNodeValue());
	node.SetProperty(m_propertyWidth->GetName(), m_propertyWidth->GetNodeValue());
	node.SetProperty(m_propertyVisible->GetName(), m_propertyVisible->GetNodeValue());
	node.SetProperty(m_propertyResizable->GetName(), m_propertyResizable->GetNodeValue());
	node.SetProperty(m_propertyReorderable->GetName(), m_propertyReorderable->GetNodeValue());

	return ibValueControl::WriteData(node);
}

//***********************************************************************************
//*                                 Column group                                    *
//***********************************************************************************

void ibValueModelTableBoxColumnGroup::Create(wxWindow* /*parent*/, const ibProtocolNode& /*node*/)
{
	// Hung on its holder exactly as a column is: a group inside a group, otherwise the table's root group.
	if (ibDataViewColumnGroup* const holder = GetParent() != nullptr ? GetParent()->GetColumnHolder() : nullptr)
		m_group = holder->AppendColumnGroup(wxEmptyString);
}

void ibValueModelTableBoxColumnGroup::Update(const ibProtocolNode& node)
{
	if (m_group == nullptr)
		return;

	const ibProtocolNode state = node.FindChild(ibProtocolName::State);
	m_group->SetTitle(state.GetString(ibProtocolName::Caption));
	m_group->SetKind(static_cast<ibColumnGroupKind>(node.GetInt(ibProtocolName::Grouping, ibColumnGroupVertical)));
	m_group->SetTitleShown(node.GetBool(ibProtocolName::ShowTitle));
	m_group->SetAlignment(static_cast<wxAlignment>(node.GetInt(ibProtocolName::HeaderAlign, wxALIGN_CENTER)));
	m_group->SetHidden(!state.GetBool(ibProtocolName::Visible, true));

	// The geometry the group decides is the data view's to work out again — asked of the group.
	if (ibDataViewCtrl* const dataView = m_group->GetOwner())
		dataView->InvalidateColumnLayout();
}

bool ibValueModelTableBoxColumnGroup::ReadData(const ibDataNode& node)
{
	m_propertyTitle->SetNodeValue(node.GetProperty(m_propertyTitle->GetName()));
	m_propertyGrouping->SetNodeValue(node.GetProperty(m_propertyGrouping->GetName()));
	m_propertyShowTitle->SetNodeValue(node.GetProperty(m_propertyShowTitle->GetName()));
	m_propertyHeaderAlign->SetNodeValue(node.GetProperty(m_propertyHeaderAlign->GetName()));
	m_propertyVisible->SetNodeValue(node.GetProperty(m_propertyVisible->GetName()));

	return ibValueControl::ReadData(node);
}

bool ibValueModelTableBoxColumnGroup::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyTitle->GetName(), m_propertyTitle->GetNodeValue());
	node.SetProperty(m_propertyGrouping->GetName(), m_propertyGrouping->GetNodeValue());
	node.SetProperty(m_propertyShowTitle->GetName(), m_propertyShowTitle->GetNodeValue());
	node.SetProperty(m_propertyHeaderAlign->GetName(), m_propertyHeaderAlign->GetNodeValue());
	node.SetProperty(m_propertyVisible->GetName(), m_propertyVisible->GetNodeValue());

	return ibValueControl::WriteData(node);
}
