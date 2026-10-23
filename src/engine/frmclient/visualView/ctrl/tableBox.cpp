////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : a table of a view
////////////////////////////////////////////////////////////////////////////

#include "tableBox.h"

#include <wx/itemattr.h>   // wxItemAttr — the header's look
#include <wx/renderer.h>   // wxRendererNative::DrawCheckMark — a boolean cell

#include "core/serialize/dataBuilder.h"              // ibDataValue — a column's width, as its property takes it
#include "frmclient/backend/appData.h"               // DesignerMode — no table's rows are read there
#include "frmclient/visualView/commandBar.h"
#include "frmclient/visualView/visualHostClient.h"   // a designer's host selects and moves by it
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
	const ibProtocolNode state = node.FindChild(ibProtocol::ibForm::ibState::State);
	ibProtocolNode properties = node.Clone();
	properties.Remove(ibProtocol::ibNode::Children);
	properties.Remove(ibProtocol::ibForm::ibState::State);
	properties.SetValue(ibProtocol::ibForm::ibProperty::Enabled, state.GetBool(ibProtocol::ibForm::ibState::Enabled, true))
		.SetValue(ibProtocol::ibForm::ibProperty::Visible, state.GetBool(ibProtocol::ibForm::ibState::Visible, true))
		.SetValue(ibProtocol::ibForm::ibProperty::Tooltip, state.GetString(ibProtocol::ibForm::ibState::Tooltip));
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

void ibValueModelTableBox::Create(wxWindow* parent, const ibProtocolNode& node)
{
	m_panel = new wxPanel(parent, wxID_ANY);
	wxBoxSizer* const sizer = new wxBoxSizer(wxVERTICAL);

	// The chrome: its command bar above it.
	m_commandBar = std::make_unique<ibViewCommandBar>(m_panel, GetVisualHost(), [this](const ibProtocolNode& args) {
		CallAsAction(ibProtocol::ibEvent::ibKind::Command, args);
	});
	sizer->Add(m_commandBar->GetWindow(), 0, wxEXPAND);

	m_dataView = new ibTableViewCtrl(m_panel,
		wxDV_SINGLE | wxDV_HORIZ_RULES | wxDV_VERT_RULES | wxDV_ROW_LINES | wxDV_VARIABLE_LINE_HEIGHT | wxBORDER_SIMPLE);
	m_dataView->SetBackgroundColour(*wxWHITE);
	sizer->Add(m_dataView, 1, wxEXPAND);
	m_panel->SetSizer(sizer);
	m_panel->SetMinSize(m_panel->FromDIP(wxSize(150, 75)));

	// The rows — read once the table has its size and its columns (the data view's bootstrap, on the next idle), on
	// its thread; what an answer leaves to the UI comes back here, if this is still here. State's Rows (the first page)
	// is served here once when the reset asks from the top — no wire call.
	std::weak_ptr<bool> alive = m_alive;
	ibViewFetcher wire = MakeFetcher();
	// …none in the designer: its tables have no rows to show, the server reads none there (ibValueModelTableBox::Fetch) —
	// a form tried out in it was refused (404), each of its tables (2026-10-15).
	const bool designer = appData->DesignerMode();
	ibViewFetcher fetcher = [this, wire, designer](const ibProtocolNode& request, ibProtocolNode& answer) -> bool {
		// The page State's Rows was read as: from the top, or (Anchor) around the row the server stands on, with its Count
		// and as many rows ahead (Before; fewer — none more above). Taken for a read it holds all of: that Anchor, no
		// larger a Count, no more rows ahead than it has — or the top reached.
		const long long held = m_firstDraw.GetInt(ibProtocol::ibForm::ibTable::Count);
		const long long ahead = m_firstDraw.GetInt(ibProtocol::ibForm::ibTable::Before);
		const long long wanted = request.GetInt(ibProtocol::ibForm::ibTable::Before);
		const bool reset = m_hasFirstDraw
			&& request.GetInt(ibProtocol::ibForm::ibTable::Direction) == static_cast<int>(ibProtocol::ibForm::ibTable::ibDirection::Reset)
			&& request.GetInt(ibProtocol::ibForm::ibTable::Parent) == 0;
		if (reset
			&& request.GetInt(ibProtocol::ibForm::ibTable::Anchor) == m_firstDraw.GetInt(ibProtocol::ibForm::ibTable::Anchor)
			&& (held == 0 || request.GetInt(ibProtocol::ibForm::ibTable::Count) <= held)
			&& (wanted == 0 || (m_firstDraw.Has(ibProtocol::ibForm::ibTable::Before) && (ahead >= wanted || ahead < held)))) {
			answer = std::move(m_firstDraw);
			m_hasFirstDraw = false;
			m_firstDraw = ibProtocolNode();
			return true;
		}
		// …AND ONE THAT IS THE WHOLE LEVEL — the top reached (fewer rows ahead than its Count) and End — answers a read
		// anew around any row of it: the read after a cell's Change names the client's own cursor (its top row), not the
		// row the server stands on. The page is cut out of it as the server would have read it.
		const long long anchor = request.GetInt(ibProtocol::ibForm::ibTable::Anchor);
		if (reset && anchor != 0 && held > 0 && m_firstDraw.GetBool(ibProtocol::ibForm::ibTable::End)
			&& m_firstDraw.Has(ibProtocol::ibForm::ibTable::Before) && ahead < held) {
			const std::vector<ibProtocolNode> rows = m_firstDraw.Children();
			std::size_t at = 0;
			while (at < rows.size() && rows[at].GetId() != anchor)
				++at;
			if (at < rows.size()) {
				const long long count = request.GetInt(ibProtocol::ibForm::ibTable::Count);
				const std::size_t from = at - std::min<std::size_t>(at, static_cast<std::size_t>(std::max(0LL, wanted)));
				const std::size_t to = count > 0 ? std::min(rows.size(), at + static_cast<std::size_t>(count)) : rows.size();
				ibProtocolNode page;
				for (const auto& entry : m_firstDraw.Entries()) {
					if (entry.first != ibProtocol::ibNode::Children && entry.first != ibProtocol::ibForm::ibTable::Before
						&& entry.first != ibProtocol::ibForm::ibTable::End && entry.first != ibProtocol::ibForm::ibTable::Anchor
						&& entry.first != ibProtocol::ibForm::ibTable::Count)
						page.SetValue(entry.first, entry.second);
				}
				for (std::size_t idx = from; idx < to; ++idx) {
					ibProtocolNode row = page.AddChild();
					for (const auto& entry : rows[idx].Entries())
						row.SetValue(entry.first, entry.second);
				}
				if (request.Has(ibProtocol::ibForm::ibTable::Before))
					page.SetValue(ibProtocol::ibForm::ibTable::Before, static_cast<long long>(at - from));
				if (to == rows.size())
					page.SetValue(ibProtocol::ibForm::ibTable::End, true);
				answer = std::move(page);
				m_hasFirstDraw = false;
				m_firstDraw = ibProtocolNode();
				return true;
			}
		}
		return !designer && wire(request, answer);
	};
	ibViewTableModel* const model = new ibViewTableModel(std::move(fetcher), [this, alive](const ibViewTableModel::ibAnswer& answer) {
		wxTheApp->CallAfter([this, alive, answer]() {
			if (alive.lock())
				OnAnswered(answer);
		});
	});
	// The table takes the focus as its form opens — the desktop's, as its model is attached — but only where it can be
	// seen: on a notebook page that is not selected (a hidden ancestor) focusing it makes the notebook switch to that page,
	// and the form opens on the table's page instead of its first. What it stands in is drawn already, before what is in
	// it; its own Visible is the State's, which Update lays on.
	if (!designer) {
		bool visible = node.FindChild(ibProtocol::ibForm::ibState::State).GetBool(ibProtocol::ibForm::ibState::Visible, true);
		for (wxWindow* w = parent; visible && w != nullptr && !w->IsTopLevel(); w = w->GetParent())
			visible = w->IsShown();
		if (visible)
			m_dataView->SetFocus();
	}
	m_dataView->AssociateModel(model);
	model->DecRef();   // the data view holds it

	m_dataView->Bind(wxEVT_IDLE, &ibValueModelTableBox::OnIdle, this);
	m_dataView->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, &ibValueModelTableBox::OnSelectionChanged, this);
	m_dataView->GetMainWindow()->Bind(wxEVT_LEFT_UP, &ibValueModelTableBox::OnCellCursor, this);
	m_dataView->GetMainWindow()->Bind(wxEVT_KEY_UP, &ibValueModelTableBox::OnCellCursor, this);
	m_dataView->GetMainWindow()->Bind(wxEVT_LEFT_DOWN, &ibValueModelTableBox::OnMainWindowClick, this);
	m_dataView->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, &ibValueModelTableBox::OnItemActivated, this);
	m_dataView->Bind(wxEVT_DATAVIEW_ITEM_START_EDITING, &ibValueModelTableBox::OnItemStartEditing, this);
	m_dataView->Bind(wxEVT_DATAVIEW_ITEM_EDITING_STARTED, &ibValueModelTableBox::OnItemEditingStarted, this);
	m_dataView->Bind(wxEVT_DATAVIEW_ITEM_EDITING_DONE, &ibValueModelTableBox::OnItemEditingDone, this);
	// The cell editor's buttons — they travel up from the editor to the table, which knows the cell.
	m_dataView->Bind(wxEVT_CONTROL_BUTTON_SELECT, &ibValueModelTableBox::OnEditorButton, this);
	m_dataView->Bind(wxEVT_CONTROL_BUTTON_OPEN, &ibValueModelTableBox::OnEditorButton, this);
	m_dataView->Bind(wxEVT_CONTROL_BUTTON_CLEAR, &ibValueModelTableBox::OnEditorButton, this);
	m_dataView->Bind(wxEVT_DATAVIEW_COLUMN_HEADER_CLICK, &ibValueModelTableBox::OnColumnClick, this);
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
		m_dataView->ShowHeaderWindow(node.GetBool(ibProtocol::ibForm::ibTable::Header, true));
		m_dataView->SetHeaderHeight(static_cast<int>(node.GetInt(ibProtocol::ibForm::ibTable::HeaderHeight, 1)));
		m_dataView->ShowFooterWindow(node.GetBool(ibProtocol::ibForm::ibTable::Footer, false));
		m_dataView->SetFooterHeight(static_cast<int>(node.GetInt(ibProtocol::ibForm::ibTable::FooterHeight, 1)));
		m_dataView->FreezeTo(static_cast<int>(node.GetInt(ibProtocol::ibForm::ibTable::FreezeRow)),
			static_cast<int>(node.GetInt(ibProtocol::ibForm::ibTable::FreezeCol)));
		m_dataView->SetSelectionMode(node.GetInt(ibProtocol::ibForm::ibTable::RowSelectionMode, ibDataViewSelectRow) == ibDataViewSelectCell
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

	UpdateState(node.FindChild(ibProtocol::ibForm::ibState::State), nullptr);
}

void ibValueModelTableBox::Update(const ibProtocolNode& node, const ibProtocolNode& patch)
{
	// Its properties among what changed — laid on as a whole (Update); its State alone — only what of it changed: an
	// answer that moved the cursor neither builds the command bar again nor repaints it.
	if (patch.GetChangeCount() > (patch.Has(ibProtocol::ibForm::ibState::State) ? 1u : 0u)) {
		Update(node);
		return;
	}
	const ibProtocolNode changed = patch.FindChild(ibProtocol::ibForm::ibState::State);
	UpdateState(node.FindChild(ibProtocol::ibForm::ibState::State), &changed);
}

void ibValueModelTableBox::UpdateState(const ibProtocolNode& state, const ibProtocolNode* changed)
{
	// ⭐ "ALL OF IT" IS NO NODE: an ibProtocolNode() is a node with no entries, and as `changed` it named nothing — a
	// table built anew had no command bar, no context menu, no current row.
	const bool whole = changed == nullptr;

	if (whole || changed->Has(ibProtocol::ibForm::ibCommandBar::CommandBar))
		m_commandBar->Update(state.FindChild(ibProtocol::ibForm::ibCommandBar::CommandBar));
	if (whole || changed->Has(ibProtocol::ibForm::ibTable::ContextMenu)) {
		// A copy — the frame changes under a handle into it with the next answer.
		const ibProtocolNode contextMenu = state.FindChild(ibProtocol::ibForm::ibTable::ContextMenu);
		m_contextMenu = contextMenu.IsNode() ? contextMenu.Clone() : ibProtocolNode();
	}

	// How the rows are shown — the State's: the person switches it.
	if (whole || changed->Has(ibProtocol::ibForm::ibTable::ViewMode))
		m_dataView->SetViewMode(static_cast<ibDataViewViewMode>(state.GetInt(ibProtocol::ibForm::ibTable::ViewMode,
			static_cast<int>(ibProtocol::ibForm::ibTable::ibViewMode::Hierarchical))));

	// The first page may already ride in this State's frame (Rows). Else the data view fetches, as before.
	if (whole || changed->Has(ibProtocol::ibForm::ibTable::Rows)) {
		if (state.Has(ibProtocol::ibForm::ibTable::Rows)) {
			m_firstDraw = state.FindChild(ibProtocol::ibForm::ibTable::Rows).Clone();
			m_hasFirstDraw = true;
		}
	}

	// The rows read anew when the server has — the first time, the data view reads them by itself (or from Rows above).
	if (whole || changed->Has(ibProtocol::ibForm::ibTable::Version)) {
		const long long version = state.GetInt(ibProtocol::ibForm::ibTable::Version);
		if (m_version != -1 && version != m_version)
			m_dataView->SchedulePagedRefresh();
		m_version = version;
	}

	// The row the server moved the cursor to — it says none back that the client told it.
	if (whole || changed->Has(ibProtocol::ibForm::ibTable::CurrentRow)) {
		const long long currentRow = state.GetInt(ibProtocol::ibForm::ibTable::CurrentRow);
		if (currentRow != 0)
			SelectRow(currentRow);
	}

	// A row activated: the server opens a cell's editor (an editable row) — the answer that says so opens it here.
	if (whole || changed->Has(ibProtocol::ibForm::ibTable::Edit)) {
		const ibProtocolNode edit = state.FindChild(ibProtocol::ibForm::ibTable::Edit);
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

	const ibDataViewItem row = m_dataView->FindRow(edit.GetInt(ibProtocol::ibForm::ibTable::Row));
	if (!row.IsOk())
		return;
	const long long columnId = edit.GetInt(ibProtocol::ibForm::ibTable::Column);
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

void ibValueModelTableBox::CallAsAction(ibProtocol::ibEvent::ibKind event, const ibProtocolNode& args)
{
	// As the row's activation (OnItemActivated): the editor opens here when the row has a cell that may be edited — its
	// Input tells the server the cell; otherwise the server decides — a list's row opens its object's form, and an
	// editor it opens comes in the answer (State's Edit), awaited from before the command goes.
	if ((args.GetInt(ibProtocol::ibEvent::Id) & eStartEditingFlag) != 0 && !EditCurrentRow(m_dataView->GetSelection()))
		m_awaitEdit = true;
	Send(event, args);
}

void ibValueModelTableBox::SendCell(ibProtocol::ibEvent::ibKind event, const ibProtocolNode& args)
{
	ibProtocolNode cell = args.IsNode() ? args.Clone() : ibProtocolNode();
	cell.SetValue(ibProtocol::ibForm::ibTable::Row, m_editRow).SetValue(ibProtocol::ibForm::ibTable::Column, m_editColumn);
	Send(event, cell);
}

void ibValueModelTableBox::OnAnswered(const ibViewTableModel::ibAnswer& answer)
{
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
	args.SetValue(ibProtocol::ibEvent::Row, handle);
	if (columnId != 0)
		args.SetValue(ibProtocol::ibEvent::Column, columnId);
	Send(ibProtocol::ibEvent::ibKind::Row, args);
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
			cell.SetValue(ibProtocol::ibForm::ibTable::Row, handle).SetValue(ibProtocol::ibForm::ibTable::Column, m_currentColumn);
			Send(ibProtocol::ibEvent::ibKind::Row, cell);
		}
		m_awaitEdit = true;
	}

	// What else activating it is the server decides — the row's object opened, a choice made — and it raises the row's
	// activation for the form.
	ibProtocolNode args;
	args.SetValue(ibProtocol::ibEvent::Row, handle);
	Send(ibProtocol::ibEvent::ibKind::Open, args);
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
	SendCell(ibProtocol::ibEvent::ibKind::Input, ibProtocolNode());
}

void ibValueModelTableBox::OnItemEditingDone(ibDataViewEvent& event)
{
	// What was typed, committed — or nothing, the edit cancelled.
	if (m_editRow != 0 && !event.IsEditCancelled()) {
		ibProtocolNode args;
		args.SetValue(ibProtocol::ibEvent::Text, event.GetValue().GetString());
		SendCell(ibProtocol::ibEvent::ibKind::Change, args);
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

	SendCell(type == wxEVT_CONTROL_BUTTON_SELECT ? ibProtocol::ibEvent::ibKind::Select
		: type == wxEVT_CONTROL_BUTTON_OPEN ? ibProtocol::ibEvent::ibKind::Open : ibProtocol::ibEvent::ibKind::Clear, ibProtocolNode());
}

void ibValueModelTableBox::OnColumnClick(ibDataViewEvent& event)
{
	// The sort is the server's: asked of it, the arrow and the rows come back with its answer. Not passed on — the
	// data view would turn its arrow by itself.
	const ibDataViewColumn* const column = event.GetDataViewColumn();
	if (column == nullptr)
		return;

	// The designer's: the column selected in its editor (the desktop's) — there are no rows to sort.
	if (GetVisualHost().IsDesignerHost()) {
		GetVisualHost().SelectControl(GetVisualHost().GetValueForm()->FindControlByID(column->GetModelColumn()));
		return;
	}
	if (!column->IsSortable())
		return;

	// The rows come back in another order: the read after it starts at the top, not at the row that was the top
	// (the data view's own header click does the same).
	m_dataView->SetPagedSkipRestoreCapture();

	ibProtocolNode args;
	args.SetValue(ibProtocol::ibEvent::Column, static_cast<long long>(column->GetModelColumn()));
	Send(ibProtocol::ibEvent::ibKind::Sort, args);
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

	const unsigned int pos = at != wxNOT_FOUND ? wxMin((unsigned int)at, holder->GetChildCount()) : holder->GetChildCount();
	columnObject->SetParent(holder);
	holder->AddChild(pos, columnObject);

	// …and in the form: the designer's, its editor's command (the desktop's RefreshEditor over its tree so moved); the
	// runtime's is the server's — the same move there (OnColumnMoved).
	if (GetVisualHost().IsDesignerHost()) {
		GetVisualHost().MovePosition(columnObject, holder, pos);
		return;
	}
	ibProtocolNode args;
	args.SetValue(ibProtocol::ibEvent::Column, columnObject->GetControlID())
		.SetValue(ibProtocol::ibEvent::Holder, holder != this ? holder->GetControlID() : 0)
		.SetValue(ibProtocol::ibEvent::Position, static_cast<long long>(at));
	Send(ibProtocol::ibEvent::ibKind::Move, args);
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
		wxMenuItem* const item = menu.Append(kFirstEntryId + static_cast<int>(idx), entries[idx].GetString(ibProtocol::ibForm::ibCommandBar::Caption));
		const wxBitmap picture = ibProtocolPicture(entries[idx].GetString(ibProtocol::ibForm::ibCommandBar::Picture));
		if (picture.IsOk())
			item->SetBitmap(picture);
		item->Enable(entries[idx].GetBool(ibProtocol::ibForm::ibCommandBar::Enabled, true));
	}

	const int chosen = m_dataView->GetPopupMenuSelectionFromUser(menu, event.GetPosition());
	if (chosen == wxID_NONE)
		return;
	const std::size_t idx = static_cast<std::size_t>(chosen - kFirstEntryId);
	if (idx >= entries.size())
		return;

	ibProtocolNode args;
	args.SetValue(ibProtocol::ibEvent::Id, entries[idx].GetInt(ibProtocol::ibForm::ibCommandBar::Id));
	CallAsAction(ibProtocol::ibEvent::ibKind::Action, args);
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
		// The designer's: the width the column's property in the form, and the column selected (the desktop's
		// SetWidthColumn and SelectControl).
		if (GetVisualHost().IsDesignerHost()) {
			ibValueFrame* const control = GetVisualHost().GetValueForm()->FindControlByID(column->GetModelColumn());
			if (control == nullptr)
				return;
			ibPropertyGate::SetValue(control, control->GetProperty(ibProtocol::ibForm::ibTable::Width),
				ibDataValue::UInt(static_cast<std::uint64_t>(column->WXGetSpecifiedWidth())));
			GetVisualHost().SelectControl(control);
			return;
		}
		ibProtocolNode args;
		args.SetValue(ibProtocol::ibEvent::Column, static_cast<long long>(column->GetModelColumn()))
			.SetValue(ibProtocol::ibEvent::Width, column->WXGetSpecifiedWidth());
		Send(ibProtocol::ibEvent::ibKind::Resize, args);
	});
}

void ibValueModelTableBox::OnMainWindowClick(wxMouseEvent& event)
{
	// The designer's: a click on the rows selects the table, its column selected or not (the desktop's) — the
	// application's click leaves the selection on a column, a table's part.
	if (GetVisualHost().IsDesignerHost())
		GetVisualHost().SelectControl(this);
	event.Skip();
}

//***********************************************************************************
//*                                    Column                                       *
//***********************************************************************************

void ibValueModelTableBoxColumn::Create(wxWindow* /*parent*/, const ibProtocolNode& node)
{
	// Built bare, as on the desktop, and hung on the group it is in; what it shows is its State's (Update).
	m_column = new ibViewTableColumnObject(*this, static_cast<unsigned int>(node.GetInt(ibProtocol::ibForm::ControlId)));
	if (ibDataViewColumnGroup* const holder = GetParent() != nullptr ? GetParent()->GetColumnHolder() : nullptr)
		holder->AppendColumn(m_column);
}

void ibValueModelTableBoxColumn::Update(const ibProtocolNode& node)
{
	const ibProtocolNode state = node.FindChild(ibProtocol::ibForm::ibState::State);

	// The desktop's column OnUpdated. The header and the footer — the captions, the pictures, or both, as its
	// representation says (the server resolves Auto, and sends no picture for a text one).
	const bool picture = state.GetInt(ibProtocol::ibForm::ibState::Representation) == static_cast<int>(ibProtocol::ibForm::ibState::ibRepresentation::Picture);
	m_column->SetTitle(picture ? ibString() : state.GetString(ibProtocol::ibForm::ibState::Caption));
	m_column->SetBitmap(ibProtocolPicture(state.GetString(ibProtocol::ibForm::ibTable::HeaderPicture)));
	m_column->SetFooterTitle(picture ? ibString() : state.GetString(ibProtocol::ibForm::ibTable::FooterText));
	m_column->SetFooterBitmap(ibProtocolPicture(state.GetString(ibProtocol::ibForm::ibTable::FooterPicture)));

	m_column->SetWidth(static_cast<int>(node.GetInt(ibProtocol::ibForm::ibTable::Width, 80)));
	m_column->SetAlignment(static_cast<wxAlignment>(node.GetInt(ibProtocol::ibForm::ibTable::HeaderAlign, wxALIGN_LEFT)));
	m_column->SetFooterAlignment(static_cast<wxAlignment>(node.GetInt(ibProtocol::ibForm::ibTable::FooterAlign, wxALIGN_LEFT)));

	m_column->SetHidden(!state.GetBool(ibProtocol::ibForm::ibState::Visible, true));
	m_column->SetSortable(state.GetBool(ibProtocol::ibForm::ibTable::Sortable));
	m_column->SetResizeable(node.GetBool(ibProtocol::ibForm::ibTable::Resizable, true));
	m_column->SetReorderable(node.GetBool(ibProtocol::ibForm::ibTable::Reorderable, true));

	// How its cells are typed into is its node's; the server says whether the column may be written.
	m_passwordMode = node.GetBool(ibProtocol::ibForm::ibProperty::PasswordMode);
	m_multilineMode = node.GetBool(ibProtocol::ibForm::ibProperty::MultilineMode);
	m_textEditMode = node.GetBool(ibProtocol::ibForm::ibTable::TexteditMode, true);
	m_selectButton = node.GetBool(ibProtocol::ibForm::ibProperty::ButtonSelect, true);
	m_openButton = node.GetBool(ibProtocol::ibForm::ibProperty::ButtonOpen);
	m_clearButton = node.GetBool(ibProtocol::ibForm::ibProperty::ButtonClear, true);
	m_readOnly = state.GetBool(ibProtocol::ibForm::ibTable::ReadOnly);

	// The sort the server keeps, shown by the arrow; a click asks it for another.
	m_sort = state.GetInt(ibProtocol::ibForm::ibTable::Sort);
	if (m_sort != 0)
		m_column->SetSortOrder(m_sort > 0);
	else if (m_column->IsSortKey())
		m_column->UnsetAsSortKey();
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

	const ibProtocolNode state = node.FindChild(ibProtocol::ibForm::ibState::State);
	m_group->SetTitle(state.GetString(ibProtocol::ibForm::ibState::Caption));
	m_group->SetKind(static_cast<ibColumnGroupKind>(node.GetInt(ibProtocol::ibForm::ibTable::Grouping, ibColumnGroupVertical)));
	m_group->SetTitleShown(node.GetBool(ibProtocol::ibForm::ibTable::ShowTitle));
	m_group->SetAlignment(static_cast<wxAlignment>(node.GetInt(ibProtocol::ibForm::ibTable::HeaderAlign, wxALIGN_CENTER)));
	m_group->SetHidden(!state.GetBool(ibProtocol::ibForm::ibState::Visible, true));

	// The geometry the group decides is the data view's to work out again — asked of the group.
	if (ibDataViewCtrl* const dataView = m_group->GetOwner())
		dataView->InvalidateColumnLayout();
}
