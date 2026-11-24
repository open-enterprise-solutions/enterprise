#ifndef _FRMCLIENT_VIEW_TABLE_BOX_H__
#define _FRMCLIENT_VIEW_TABLE_BOX_H__

#include <memory>
#include <optional>

#include "frmclient/visualView/ctrl/window.h"
#include "deskgui/univ/dataview/dataview.h"      // ibDataViewSelectionMode, ibDataViewViewMode
#include "deskgui/univ/dataview/datavlayout.h"   // ibColumnGroupKind
#include "frmclient/visualView/tableModel.h"
#include "deskgui/univ/panel.h"
#include "core/inputMask.h"   // ibInputMask — what a column's cells are typed through

class ibCheckBox;
class ibTextCtrl;
class ibViewCommandBar;
class ibViewTableColumnObject;
class ibTableViewCtrl;
class ibDataViewColumnGroup;
class ibDataViewEvent;
class ibHeaderGenericCtrlEvent;
class wxFindDialogEvent;

// A TABLE OF A VIEW — the old desktop's, a composite: its command bar above the data
// view (the desktop's chrome, ibValueWindowComposite). Its columns and their groups are built by the host under it, as
// any control is, and hang themselves on it (GetColumnHolder); its rows are fetched (ibViewTableModel). The server
// re-reads its rows and says so by the Version of its State — the client reads them again then; the cursor it moves,
// a row opened, a header clicked, a column's edge dragged go back as events.
class ibValueModelTableBox : public ibValueWindow {
public:

	ibValueModelTableBox(ibVisualHostClient& host, long long controlId);
	virtual ~ibValueModelTableBox();

	virtual void Create(wxWindow* parent, const ibProtocolNode& node) override;
	virtual void Update(const ibProtocolNode& node) override;
	virtual void Update(const ibProtocolNode& node, const ibProtocolNode& patch) override;
	virtual wxWindow* GetWindow() const override;
	virtual ibDataViewColumnGroup* GetColumnHolder() const override;
	virtual ibViewCommandBar* GetCommandBar() const override { return m_commandBar.get(); }

	// The desktop's says the source it shows; its source is the server's — here, its name.
	virtual wxString GetControlTitle() const override { return _("Table") + wxT(": ") + GetControlName(); }

	// …and a cell named outright, the row and the column, no editor open in it: a flag's box toggled.
	void SendCell(ibProtocol::ibEvent::ibKind event, long long row, long long column, const ibProtocolNode& args);

private:

	// What its State shows — of it, what `changed` (the State's node in a patch) names; all of it when none is given.
	void UpdateState(const ibProtocolNode& state, const ibProtocolNode* changed);
	// The row the server has current, the cursor put on — now, or when it is read.
	void SelectRow(long long handle);
	// After a read: its current row, the pictures it carried.
	void OnAnswered(const ibViewTableModel::ibAnswer& answer);

	// The cursor, to the server — the row and the column it stands on, when either is another than it was told.
	void SendCursor();
	// …and a cell named, the same way: the one a context menu was opened on — the cell the command reads goes before it.
	void SendCursor(const ibDataViewItem& item, const ibDataViewColumn* column);

	// The cell whose editor the server opened (State's Edit) — opened here, once the activation asked for it.
	void OpenEditor(const ibProtocolNode& edit);
	// The desktop's EditCurrentRow: the row's editable cell opened at once — the current column's, else the first that
	// may be edited; false when none may.
	bool EditCurrentRow(const ibDataViewItem& item);
	// What is done in the cell edited, to the server — the cell named with it.
	void SendCell(ibProtocol::ibEvent::ibKind event, const ibProtocolNode& args);
	// The desktop's CallAsAction: a command of the table's (its command bar's, its context menu's), to the server — and
	// one that edits (eStartEditingFlag) opens the current row's editor here first (EditCurrentRow).
	void CallAsAction(ibProtocol::ibEvent::ibKind event, const ibProtocolNode& args);

	void OnIdle(wxIdleEvent& event);
	void OnSelectionChanged(ibDataViewEvent& event);
	void OnCellCursor(wxEvent& event);
	void OnItemActivated(ibDataViewEvent& event);
	void OnItemStartEditing(ibDataViewEvent& event);
	void OnItemEditingStarted(ibDataViewEvent& event);
	void OnItemEditingDone(ibDataViewEvent& event);
	void OnEditorButton(wxCommandEvent& event);
	void OnColumnClick(ibDataViewEvent& event);
	void OnColumnReordered(ibDataViewEvent& event);
	void OnContextMenu(ibDataViewEvent& event);
	// A key on the rows: the context menu's command it is the key of (Shortcut), run on the cell the cursor is on; any
	// other — the data view's.
	void OnKeyDown(wxKeyEvent& event);
	void OnHeaderEndResize(ibHeaderGenericCtrlEvent& event);
	void OnMainWindowClick(wxMouseEvent& event);
	// Find, handed to the table by its form's view (ibVisualHostView): to the server with what the dialog says, which puts
	// the cursor on the next cell holding the text — the cursor told first, so it looks from where the person sees it.
	void OnFind(wxFindDialogEvent& event);

	ibPanel*                         m_panel = nullptr;
	ibTableViewCtrl*                  m_dataView = nullptr;
	std::unique_ptr<ibViewCommandBar> m_commandBar;
	ibProtocolNode                    m_contextMenu;   // the State's ContextMenu — what a right click offers
	ibProtocolNode                    m_properties;    // the table's own properties as last laid on (PropertiesOf)

	long long m_version = -1;            // the rows' version read last — -1: none yet
	ibProtocolNode m_firstDraw;          // State's Rows for this Version — served once by the fetcher, then cleared
	bool      m_hasFirstDraw = false;    // m_firstDraw holds a page
	long long m_currentRow = 0;          // the row the server has current
	long long m_currentColumn = 0;       // the column the server was told the cursor stands on — 0: none
	bool      m_selecting = false;       // the cursor put by the server — no event of the person's
	bool      m_selectPending = false;   // …put once the read out lands

	long long m_editRow = 0;             // the cell being edited — 0: none
	long long m_editColumn = 0;
	bool      m_awaitEdit = false;       // a row activated: the server may open a cell's editor in the answer
	std::shared_ptr<bool> m_alive;   // what an answer handed to the UI asks before it touches this
};

// A column of a table — the desktop's ibValueModelTableBoxColumn: built bare, hung on the group it is in, drawn
// by its State. Its model column is its id: a row's cells are by the columns' ids.
class ibValueModelTableBoxColumn : public ibValueControl {
public:

	ibValueModelTableBoxColumn(ibVisualHostClient& host, long long controlId) : ibValueControl(host, controlId) {}

	// Built with its view: its cells drawn and edited as the view is (an Input's editor, a Flag's box, a Label's text).
	virtual void Create(wxWindow* parent, const ibProtocolNode& node) override;
	virtual void Update(const ibProtocolNode& node) override;

	// The table it serves — asked up the tree, as the server's column asks it: a column may stand in a group.
	virtual ibValueModelTableBox* GetOwner() const override;

	// What its data view column and its cells ask of it, as the desktop's column was asked: the sort the server keeps,
	// how its cells are typed into — and whether they may be, which the server says (State's ReadOnly).
	long long GetSort() const { return m_sort; }
	bool GetPasswordMode() const { return m_passwordMode; }
	bool GetMultilineMode() const { return m_multilineMode; }
	const ibInputMask& GetMask() const { return m_mask; }
	bool GetTextEditMode() const { return m_textEditMode && !m_readOnly; }
	bool GetSelectButton() const { return m_selectButton; }
	bool GetOpenButton() const { return m_openButton; }
	bool GetClearButton() const { return m_clearButton; }

private:

	ibViewTableColumnObject* m_column = nullptr;   // the table's

	long long m_sort = 0;          // 1 ascending, -1 descending, 0 none
	bool      m_readOnly = false;  // the server's: the column may not be written

	// How its cells are typed into — its node's, as drawn last (Update).
	bool m_passwordMode = false;
	bool m_multilineMode = false;
	ibInputMask m_mask;            // the server's, resolved — none when empty
	bool m_textEditMode = true;
	bool m_selectButton = true;
	bool m_openButton = false;
	bool m_clearButton = true;
};

// THE VIEW A COLUMN'S CELLS ARE DRAWN WITH — the server's ibColumnViewProvider, this client's half: a renderer of the
// data view's for each view (ibProtocol::ibForm::ibProperty::ibColumnView), made when the column is built (Create).

// A CELL DRAWN — the desktop's ibDataViewValueRenderer: what it draws is asked of the value — a boolean a tick where
// the text would start, anything else its text; where it stands is the cell's look (a number right). A Label's cell is
// this alone: shown, never edited.
class ibViewCellRenderer : public ibDataViewCustomRenderer {
public:

	explicit ibViewCellRenderer(const ibValueModelTableBoxColumn& column, ibDataViewCellMode mode = wxDATAVIEW_CELL_INERT)
		: ibDataViewCustomRenderer(GetDefaultType(), mode), m_column(column) {}

	virtual bool SetValue(const ibVariant& value) override;
	virtual bool GetValue(ibVariant& /*value*/) const override { return true; }
	virtual bool Render(wxRect rect, wxDC* dc, int state) override;
	virtual wxSize GetSize() const override;

protected:

	const ibValueModelTableBoxColumn& m_column;

private:

	std::optional<bool> m_flag;
	wxString            m_text;
};

// ⭐ INPUT — edited: the field's editor stands in the cell, as its column says — a check box for a boolean (Max,
// 2026-11-24: "a boolean opens a boolean editor, not a text one"); what is typed or ticked goes to the server when the
// edit is done, and the text editor's buttons' presses travel up to the table, which knows the cell.
class ibViewCellRendererInput : public ibViewCellRenderer {
public:

	explicit ibViewCellRendererInput(const ibValueModelTableBoxColumn& column)
		: ibViewCellRenderer(column, wxDATAVIEW_CELL_EDITABLE) {}

	virtual bool HasEditorCtrl() const override { return true; }
	// The desktop's CreateEditorCtrl: the field's editor in the cell, as its column says, the table's look on it.
	virtual wxWindow* CreateEditorCtrl(wxWindow* parent, wxRect labelRect, const ibVariant& value) override;
	// The text in the editor it made — or the truth, a boolean's.
	virtual bool GetValueFromEditorCtrl(wxWindow* editor, ibVariant& value) override;

private:

	ibTextCtrl* m_textEditor = nullptr;    // the editor it made, while the cell is edited
	ibCheckBox* m_checkEditor = nullptr;   // …a boolean's
};

// ⭐ FLAG — the value's truth as a check box where the text would start. A click on the box, or Space on the cell, is a
// Toggle to the server with the state it was toggled to — only where the server lets the cell take an edit (it wrote its
// Value); the box shows it when the rows are read again. A cell with no truth (a group's heading) shows none.
class ibViewCellRendererFlag : public ibDataViewCustomRenderer {
public:

	explicit ibViewCellRendererFlag(const ibValueModelTableBoxColumn& column)
		: ibDataViewCustomRenderer(GetDefaultType(), wxDATAVIEW_CELL_ACTIVATABLE), m_column(column) {}

	virtual bool SetValue(const ibVariant& value) override;
	virtual bool GetValue(ibVariant& /*value*/) const override { return true; }
	virtual bool Render(wxRect rect, wxDC* dc, int state) override;
	virtual wxSize GetSize() const override;
	virtual bool ActivateCell(const wxRect& cell, ibDataViewModel* model, const ibDataViewItem& item, unsigned int col,
		const wxMouseEvent* mouseEvent) override;

private:

	// Where the box stands in `rect`: at its start, in the middle of its height.
	wxRect BoxIn(const wxRect& rect) const;

	const ibValueModelTableBoxColumn& m_column;
	std::optional<bool> m_flag;
};

// A group of a table's columns — the desktop's ibValueModelTableBoxColumnGroup: how its columns are laid out, its
// title over them.
class ibValueModelTableBoxColumnGroup : public ibValueControl {
public:

	ibValueModelTableBoxColumnGroup(ibVisualHostClient& host, long long controlId) : ibValueControl(host, controlId) {}

	virtual void Create(wxWindow* parent, const ibProtocolNode& node) override;
	virtual void Update(const ibProtocolNode& node) override;
	virtual ibDataViewColumnGroup* GetColumnHolder() const override { return m_group; }

private:
	ibDataViewColumnGroup* m_group = nullptr;   // the table's
};

#endif
