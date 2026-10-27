#ifndef _FRMCLIENT_VIEW_TABLE_BOX_H__
#define _FRMCLIENT_VIEW_TABLE_BOX_H__

#include <memory>

#include "frmclient/visualView/ctrl/window.h"
#include "frmclient/win/ctrls/dataview/dataview.h"      // ibDataViewSelectionMode, ibDataViewViewMode
#include "frmclient/win/ctrls/dataview/datavlayout.h"   // ibColumnGroupKind
#include "frmclient/visualView/tableModel.h"

class ibViewCommandBar;
class ibViewTableColumnObject;
class ibTableViewCtrl;
class ibDataViewColumnGroup;
class ibDataViewEvent;
class ibHeaderGenericCtrlEvent;

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
	virtual wxString GetControlTitle() const override { return _("TableBox") + wxT(": ") + GetControlName(); }

private:

	// What its State shows — of it, what `changed` (the State's node in a patch) names; all of it when none is given.
	void UpdateState(const ibProtocolNode& state, const ibProtocolNode* changed);
	// The row the server has current, the cursor put on — now, or when it is read.
	void SelectRow(long long handle);
	// After a read: its current row, the pictures it carried.
	void OnAnswered(const ibViewTableModel::ibAnswer& answer);

	// The cursor, to the server — the row and the column it stands on, when either is another than it was told.
	void SendCursor();

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
	void OnHeaderEndResize(ibHeaderGenericCtrlEvent& event);
	void OnMainWindowClick(wxMouseEvent& event);

	wxPanel*                         m_panel = nullptr;
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

	virtual void Create(wxWindow* parent, const ibProtocolNode& node) override;
	virtual void Update(const ibProtocolNode& node) override;

	// What its data view column and its cells ask of it, as the desktop's column was asked: the sort the server keeps,
	// how its cells are typed into — and whether they may be, which the server says (State's ReadOnly).
	long long GetSort() const { return m_sort; }
	bool GetPasswordMode() const { return m_passwordMode; }
	bool GetMultilineMode() const { return m_multilineMode; }
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
	bool m_textEditMode = true;
	bool m_selectButton = true;
	bool m_openButton = false;
	bool m_clearButton = true;
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
