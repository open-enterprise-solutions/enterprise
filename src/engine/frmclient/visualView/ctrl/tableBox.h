#ifndef _FRMCLIENT_VIEW_TABLE_BOX_H__
#define _FRMCLIENT_VIEW_TABLE_BOX_H__

#include <memory>

#include "frmclient/visualView/ctrl/window.h"
#include "frmclient/visualView/controlEnum.h"   // a column's alignments, its representation
#include "frmclient/backend/compiler/enumUnit.h"
#include "frmclient/backend/propertyManager/property/propertyEnum.h"
#include "frmclient/backend/propertyManager/property/propertyFormat.h"
#include "frmclient/backend/propertyManager/property/propertyNumber.h"
#include "frmclient/win/ctrls/dataview/dataview.h"      // ibDataViewSelectionMode, ibDataViewViewMode
#include "frmclient/win/ctrls/dataview/datavlayout.h"   // ibColumnGroupKind
#include "frmclient/visualView/tableModel.h"

class ibViewCommandBar;
class ibViewTableColumnObject;
class ibTableViewCtrl;
class ibDataViewColumnGroup;
class ibDataViewEvent;
class ibHeaderGenericCtrlEvent;

class ibValueEnumTableBoxSelectionMode :
	public ibValueEnumeration<ibDataViewSelectionMode> {
	public:
	ibValueEnumTableBoxSelectionMode() : ibValueEnumeration() {}
	virtual void CreateEnumeration() {
		AddEnumeration(ibDataViewSelectionMode::ibDataViewSelectCell, wxT("SelectCell"), _("Select cell"));
		AddEnumeration(ibDataViewSelectionMode::ibDataViewSelectRow, wxT("SelectRow"), _("Select row"));
	}
private:
};

class ibValueEnumTableBoxViewMode :
	public ibValueEnumeration<ibDataViewViewMode> {
	public:
	ibValueEnumTableBoxViewMode() : ibValueEnumeration() {}
	virtual void CreateEnumeration() {
		AddEnumeration(ibDataViewViewMode::ibDataViewHierarchical, wxT("Hierarchical"), _("Hierarchical"));
		AddEnumeration(ibDataViewViewMode::ibDataViewTree, wxT("Tree"), _("Tree"));
		AddEnumeration(ibDataViewViewMode::ibDataViewList, wxT("List"), _("List"));
	}
private:
};

class ibValueEnumTableBoxColumnGrouping :
	public ibValueEnumeration<ibColumnGroupKind> {
	public:
	ibValueEnumTableBoxColumnGrouping() : ibValueEnumeration() {}
	virtual void CreateEnumeration() {
		AddEnumeration(ibColumnGroupHorizontal, wxT("Horizontal"), _("Horizontal"));
		AddEnumeration(ibColumnGroupVertical, wxT("Vertical"), _("Vertical"));
		AddEnumeration(ibColumnGroupInCell, wxT("InCell"), _("In cell"));
	}
private:
};

// A TABLE OF A VIEW — the desktop's (frontend/visualView/ctrl/tableBox), a composite: its command bar above the data
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

	// The desktop's says the source it shows; its source is the server's — here, its name.
	virtual wxString GetControlTitle() const override { return _("TableBox") + wxT(": ") + GetControlName(); }

	//load & save object in control
	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:

	ibPropertyCategory* m_categoryInfo = ibPropertyObject::CreatePropertyCategory(wxT("Info"), _("Info"));
	ibPropertyBoolean* m_propertyHeader = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryInfo, wxT("Header"), _("Header"),
		_("Whether the column headers are shown above the rows. On by default; clicking a header sorts by that column."), true);
	ibPropertyUInteger* m_propertyHeaderHeight = ibPropertyObject::CreateProperty<ibPropertyUInteger>(m_categoryInfo, wxT("HeaderHeight"), _("Header height"),
		_("The header's height in text lines. At least 1, and never less than the column groups need, so a grouped header always fits."), 1);
	ibPropertyBoolean* m_propertyFooter = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryInfo, wxT("Footer"), _("Footer"),
		_("Whether a footer row is shown under the rows, carrying each column's footer text (a total, for example). Off by default."), false);
	ibPropertyUInteger* m_propertyFooterHeight = ibPropertyObject::CreateProperty<ibPropertyUInteger>(m_categoryInfo, wxT("FooterHeight"), _("Footer height"),
		_("The footer's height in text lines, used when the footer is shown. Default 1."), 1);
	ibPropertyUInteger* m_propertyFreezeRow = ibPropertyObject::CreateProperty<ibPropertyUInteger>(m_categoryInfo, wxT("FrezeeRow"), _("Frezee row"),
		_("How many leading rows stay in place while the rest scroll vertically. 0 (the default): none."), 0);
	ibPropertyUInteger* m_propertyFreezeCol = ibPropertyObject::CreateProperty<ibPropertyUInteger>(m_categoryInfo, wxT("FrezeeCol"), _("Frezee column"),
		_("How many leading columns stay in place while the rest scroll horizontally - keeps a name column visible in a wide table. 0 (the default): none."), 0);
	ibPropertyCategory* m_categoryData = ibPropertyObject::CreatePropertyCategory(wxT("Data"), _("Data"));
	ibPropertyEnum<ibValueEnumTableBoxSelectionMode>* m_propertyRowSelectionMode = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumTableBoxSelectionMode>>(m_categoryData, wxT("RowSelectionMode"), _("Row selection mode"),
		_("What the cursor highlights. Select cell (the default): one cell, moved cell by cell - suits a table edited in place. Select row: the whole row - suits a list rows are picked or opened from."),
		ibDataViewSelectionMode::ibDataViewSelectCell);
	ibPropertyEnum<ibValueEnumTableBoxViewMode>* m_propertyViewMode = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumTableBoxViewMode>>(m_categoryData, wxT("ViewMode"), _("View mode"),
		_("How a source with folders or parents is shown. Hierarchical (the default): one level at a time, entering a folder to see its contents. Tree: an expandable tree. List: every row flat, hierarchy ignored. The user can switch it at run time."),
		ibDataViewViewMode::ibDataViewHierarchical);
	ibPropertyBoolean* m_propertyChoiceMode = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryData, wxT("ChoiceMode"), _("Choice mode"),
		_("Whether the table works as a picker: double-click or Enter hands the current row back to whoever opened the form, instead of opening or editing it, and a Select command leads its command bar. Set automatically on a choice form's list."),
		false);


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
	void SendCell(ibProtocolEvent event, const ibProtocolNode& args);
	// The desktop's CallAsAction: a command of the table's (its command bar's, its context menu's), to the server — and
	// one that edits (eStartEditingFlag) opens the current row's editor here first (EditCurrentRow).
	void CallAsAction(ibProtocolEvent event, const ibProtocolNode& args);

	void OnIdle(wxIdleEvent& event);
	void OnSelectionChanged(ibDataViewEvent& event);
	void OnCellCursor(wxEvent& event);
	void OnItemActivated(ibDataViewEvent& event);
	void OnItemStartEditing(ibDataViewEvent& event);
	void OnItemEditingStarted(ibDataViewEvent& event);
	void OnItemEditingDone(ibDataViewEvent& event);
	void OnEditorButton(wxCommandEvent& event);
	void OnColumnHeaderClick(ibDataViewEvent& event);
	void OnColumnReordered(ibDataViewEvent& event);
	void OnContextMenu(ibDataViewEvent& event);
	void OnHeaderEndResize(ibHeaderGenericCtrlEvent& event);

	wxPanel*                          m_panel = nullptr;
	ibTableViewCtrl*                  m_dataView = nullptr;
	std::unique_ptr<ibViewCommandBar> m_commandBar;
	ibProtocolNode                    m_contextMenu;   // the State's ContextMenu — what a right click offers
	ibProtocolNode                    m_properties;    // the table's own properties as last laid on (PropertiesOf)

	long long m_version = -1;            // the rows' version read last — -1: none yet
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
	bool GetPasswordMode() const { return m_propertyPasswordMode->GetValueAsBoolean(); }
	bool GetMultilineMode() const { return m_propertyMultilineMode->GetValueAsBoolean(); }
	bool GetTextEditMode() const { return m_propertyTexteditMode->GetValueAsBoolean() && !m_readOnly; }
	bool GetSelectButton() const { return m_propertySelectButton->GetValueAsBoolean(); }
	bool GetOpenButton() const { return m_propertyOpenButton->GetValueAsBoolean(); }
	bool GetClearButton() const { return m_propertyClearButton->GetValueAsBoolean(); }

	//load & save object in control
	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:

	ibPropertyCategory* m_categoryInfo = ibPropertyObject::CreatePropertyCategory(wxT("Info"), _("Info"));
	ibPropertyTString* m_propertyTitle = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categoryInfo, wxT("Title"), _("Title"),
		_("The column's header text. Empty: the synonym of the bound field, or the column's name when nothing is bound. Can be written per language."), wxT(""));
	ibPropertyBoolean* m_propertyPasswordMode = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryInfo, wxT("PasswordMode"), _("Password mode"), _("Whether the cell editor hides what is typed behind placeholder characters, for secrets such as passwords. Off by default."), false);
	ibPropertyBoolean* m_propertyMultilineMode = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryInfo, wxT("MultilineMode"), _("Multiline mode"), _("Whether the cell editor accepts several lines of text (Enter starts a new line). Off by default: Enter finishes the edit."), false);
	ibPropertyBoolean* m_propertyTexteditMode = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryInfo, wxT("TexteditMode"), _("Textedit mode"), _("Whether the value can be typed into the cell. Off: it can only be picked with the Select button or cleared. On by default; a read-only column never takes typing."), true);
	ibPropertyFormat* m_propertyFormat = ibPropertyObject::CreateProperty<ibPropertyFormat>(m_categoryInfo, wxT("Format"), _("Format"),
		_("How the column shows its cells, written per language: digits after the point, separators, a date pattern. Empty: the bound attribute's format, and without one a number shows as many digits after the point as its type keeps."), wxT(""));
	ibPropertyTString* m_propertyFooterText = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categoryInfo, wxT("FooterText"), _("Footer text"),
		_("The text in this column's footer cell, shown when the table's footer is on - a label or a total set from code. Can be written per language."), wxT(""));
	ibPropertyCategory* m_categoryButton = ibPropertyObject::CreatePropertyCategory(wxT("Button"), _("Button"));
	ibPropertyBoolean* m_propertySelectButton = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryButton, wxT("ButtonSelect"), _("Select button"),
		_("Whether the cell editor shows the Select button (...), which opens a choice form or list to pick the value from. On by default."), true);
	ibPropertyBoolean* m_propertyClearButton = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryButton, wxT("ButtonClear"), _("Clear button"),
		_("Whether the cell editor shows the Clear button (X), which empties the value. On by default."), true);
	ibPropertyBoolean* m_propertyOpenButton = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryButton, wxT("ButtonOpen"), _("Open button"),
		_("Whether the cell editor shows the Open button, which opens the referenced object's form. Off by default."), false);
	ibPropertyCategory* m_categoryStyle = ibPropertyObject::CreatePropertyCategory(wxT("Style"), _("Style"));
	ibPropertyUInteger* m_propertyWidth = ibPropertyObject::CreateProperty<ibPropertyUInteger>(m_categoryStyle, wxT("Width"), _("Width"),
		_("The column's starting width, in pixels. The user can change it when the column is resizable."), wxDVC_DEFAULT_WIDTH);
	ibPropertyEnum<ibValueEnumHorizontalAlignment>* m_propertyHeaderAlign = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumHorizontalAlignment>>(m_categoryStyle, wxT("HeaderAlign"), _("Header align"),
		_("How the header text is aligned: left (the default), center or right."), wxALIGN_LEFT);
	ibPropertyEnum<ibValueEnumHorizontalAlignment>* m_propertyFooterAlign = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumHorizontalAlignment>>(m_categoryStyle, wxT("FooterAlign"), _("Footer align"),
		_("How the footer text is aligned: left (the default), center or right. Right suits a total under a numeric column."), wxALIGN_LEFT);
	ibPropertyEnum<ibValueEnumRepresentation>* m_propertyRepresentation = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumRepresentation>>(m_categoryStyle, wxT("Representation"), _("Representation"),
		_("What the header and footer cells show: text, picture, or both. Auto: the title and the picture, whichever are set."),
		ibRepresentation::ibRepresentation_Auto);
	ibPropertyBoolean* m_propertyVisible = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryStyle, wxT("Visible"), _("Visible"),
		_("Whether the column is shown. A hidden column keeps its data and can be shown again from code."), true);
	ibPropertyBoolean* m_propertyResizable = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryStyle, wxT("Resizable"), _("Resizable"),
		_("Whether the user can change the column's width by dragging its header edge. On by default."), true);
	ibPropertyBoolean* m_propertyReorderable = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryStyle, wxT("Reorderable"), _("Reorderable"),
		_("Whether the user can move the column to another place by dragging its header. On by default."), true);


	ibViewTableColumnObject* m_column = nullptr;   // the table's

	long long m_sort = 0;          // 1 ascending, -1 descending, 0 none
	bool      m_readOnly = false;  // the server's: the column may not be written
};

// A group of a table's columns — the desktop's ibValueModelTableBoxColumnGroup: how its columns are laid out, its
// title over them.
class ibValueModelTableBoxColumnGroup : public ibValueControl {
public:

	ibValueModelTableBoxColumnGroup(ibVisualHostClient& host, long long controlId) : ibValueControl(host, controlId) {}

	virtual void Create(wxWindow* parent, const ibProtocolNode& node) override;
	virtual void Update(const ibProtocolNode& node) override;
	virtual ibDataViewColumnGroup* GetColumnHolder() const override { return m_group; }

	//load & save object in control
	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:

	ibPropertyCategory* m_categoryInfo = ibPropertyObject::CreatePropertyCategory(wxT("Info"), _("Info"));
	ibPropertyTString* m_propertyTitle = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categoryInfo, wxT("Title"), _("Title"),
		_("The group's title in the header, shown when Show title is on (an in-cell group always shows it as its one header cell). Can be written per language."), wxT(""));
	ibPropertyEnum<ibValueEnumTableBoxColumnGrouping>* m_propertyGrouping = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumTableBoxColumnGrouping>>(m_categoryInfo, wxT("Grouping"), _("Grouping"),
		_("Where the group's columns go. Vertical (the default): stacked within one width, so the row grows taller instead of wider. Horizontal: side by side under the group's title. In cell: side by side but merged under one header cell, reading as one field. Groups nest."),
		ibColumnGroupVertical);
	ibPropertyBoolean* m_propertyShowTitle = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryInfo, wxT("ShowTitle"), _("Show title"),
		_("Whether the group's title takes a band of the header above its columns. Off (the default): the group only arranges its columns and costs the header no height."),
		false);
	ibPropertyCategory* m_categoryStyle = ibPropertyObject::CreatePropertyCategory(wxT("Style"), _("Style"));
	ibPropertyEnum<ibValueEnumHorizontalAlignment>* m_propertyHeaderAlign = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumHorizontalAlignment>>(m_categoryStyle, wxT("HeaderAlign"), _("Header align"),
		_("How the group's title is aligned in its header cell: left, center (the default) or right."), wxALIGN_CENTER);
	ibPropertyBoolean* m_propertyVisible = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryStyle, wxT("Visible"), _("Visible"),
		_("Whether the group and all its columns are shown. Hidden columns keep their data and can be shown again from code."), true);


	ibDataViewColumnGroup* m_group = nullptr;   // the table's
};

#endif
