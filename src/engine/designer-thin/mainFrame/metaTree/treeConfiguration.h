#ifndef _METATREE_H__
#define _METATREE_H__

#include <map>
#include <memory>
#include <set>

#include <wx/aui/aui.h>
#include <wx/choice.h>
#include <wx/srchctrl.h>
#include <wx/treectrl.h>

#include "frmclient/backend/metaCollection/metaObject.h"   // the object the inspector shows
#include "protocol/protocol.h"
#include "protocol/protocolNode.h"

class ibMetaView;

// THE DESIGNER'S TREES — the desktop's ibMetaTreeBase and its heirs (designer/mainFrame/metaTree): the panel, its
// toolbar, its search, its menus and keys. The ROWS are the server's: its ibConfigurationTree (frmserver/metaTree)
// writes them into the frame's MetaData, an external file's tree into its tab's View; a row is NodeId — its object's
// id, or below 0 a group row's place among its siblings — Title, Picture, Group (what New makes under it), Type,
// Position, Menu (protocol.h, ibProtocol::ibSchema::ibKind::MetaData). What a person does goes back as the doors of schema
// MetaData (ibProtocol::ibSchema::ibMetaData::ibCommand), and the rows come back with the frame: a click ASKS, the row follows from the
// metadata — as on the desktop it followed the stage and never the click.
class ibMetaTreeBase : public wxPanel {
public:

	virtual ~ibMetaTreeBase() override;

	// THE ROWS, as the frame has them now — `root` the metadata's own row, everything under it. Drawn over what is shown,
	// row by row by their ids: what was open, selected and scrolled to stays so. An external file's tree draws its header
	// too.
	virtual void Draw(const ibProtocolNode& root);

	// The tree's tab — 0: the configuration's (the frame's MetaData), else its tab's: an external file's, or a configuration
	// browsed.
	long long GetTab() const { return m_tab; }

	// THE DESKTOP'S SetReadOnly — New / Delete / Up / Down / Sort shut; Edit and Open still work for browsing.
	void SetReadOnly(bool readOnly = true) { m_readOnly = readOnly; }
	bool IsEditable() const { return !m_readOnly; }

	// Its tab became the active one, or the tree the focus — the object it holds is sent to the inspector once the event
	// is done, received anew only where another was shown (the desktop's ibMetaTreeView::OnActivateView).
	void ActivateTree();

	// THE MODULE, SHOWN — the desktop's (ibConfigurationTree::EditModule), by the address a runtime stopped or failed
	// at: its editor opened by the server (Open {Module}), the line put before the person — the run line (setRunLine)
	// or a failure's.
	void EditModule(const ibGuid& moduleName, int line, bool setRunLine = true);

protected:

	// `kind` — what it draws: a Configuration's tree (its root shown, searched — in the frame or in a tab), or an external
	// file's (DataProcessor, Report: its root hidden, its rows framed — the desktop's tree control of those).
	ibMetaTreeBase(wxWindow* parent, long long tab, ibProtocol::ibView::ibKind kind);

	// THE MODAL EDITOR over an object, the desktop's — its row's menu opens it (the engine's ProcessCommand, by the item's
	// Name). A tree that has none answers by doing nothing: an external file's.
	virtual void EditPredefinedValues(class ibValueMetaObjectRecordDataHierarchyMutableRef* WXUNUSED(metaObject)) {}
	virtual void EditHomePage(class ibValueMetaObjectConfiguration* WXUNUSED(metaObject)) {}

	// THE DOORS — schema MetaData's parameters: the command, its arguments, the tab's id with them; and the command
	// asked, the frame drawn after it. False: refused (said to the person).
	ibProtocolNode GetParams(ibProtocol::ibSchema::ibMetaData::ibCommand command, ibProtocolNode args) const;
	bool Command(ibProtocol::ibSchema::ibMetaData::ibCommand command, ibProtocolNode args) const;
	// …and a header's choice drawn from its property's row: "<not selected>", what may be chosen (its children: Title,
	// Id), the one chosen by its Value.
	void UpdateChoiceSelection(wxChoice* choice, const ibProtocolNode& property);

	ibProtocolNode m_root;            // the rows drawn — a copy: the frame changes under a handle into it

private:

	// A ROW AS IT IS DRAWN — the server's node, and its place: the ids from the root down, which is what finds it again
	// (an id alone does not: a group row's is its place among its siblings).
	class ibTreeRow : public wxTreeItemData {
	public:
		ibTreeRow(const ibProtocolNode& row, const wxString& path) : m_row(row), m_path(path) {}
		ibProtocolNode m_row;
		const wxString m_path;
	};

	// THE TREE'S VIEW — the desktop's (ibMetaTreeCtrl::ibMetaTreeView): no document, no frame. The configuration's tree
	// given the focus is the current view in the editor's place, the editor's menu and toolbar shut
	// (ibFrontendMainFrame::ActivateView); its activation is the tree's (ActivateTree).
	class ibMetaTreeView;

	enum {
		ID_METATREE_NEW = 15000,
		ID_METATREE_EDIT,
		ID_METATREE_DELETE,
		ID_METATREE_PROPERTY,

		ID_METATREE_UP,
		ID_METATREE_DOWM,

		ID_METATREE_SORT,

		// The two texts — the desktop's; no door on the server yet, shown shut.
		ID_METATREE_HELP,
		ID_METATREE_NOTES,
	};

	// The toolbar — the desktop's: New, Edit, Delete | Up, Down | Sort.
	void CreateToolBar(wxWindow* parent);

	// A ROW AND EVERYTHING UNDER IT, put at that place among its parent's — and a row shown drawn again as it is now,
	// its children matched to theirs by their ids: one kept, one new put in, one gone taken out.
	wxTreeItemId InsertRow(const wxTreeItemId& parent, size_t position, const ibProtocolNode& row, const wxString& parentPath);
	void DrawRow(const wxTreeItemId& item, const ibProtocolNode& row);
	// Whether the search keeps the row — its object's name or synonym has the text, or a row under it is kept. Without
	// a search, every row.
	bool IsFound(const ibProtocolNode& row) const;
	// The picture of a row, in the control's list — once per picture.
	int GetImage(const wxString& picture);
	// The item that stands at that place now — none: none does.
	wxTreeItemId FindItem(const wxTreeItemId& item, const wxString& path) const;

	// THE ROW AND WHAT IT STANDS FOR — its node (none: no row), its object's id (0: a group row), the class New makes there
	// and under which object (the nearest row with a Group, and the nearest object at or above it; 0 — the
	// configuration's own).
	const ibTreeRow* GetRow(const wxTreeItemId& item) const;
	long long GetObject(const wxTreeItemId& item) const;
	bool GetPlace(const wxTreeItemId& item, wxString& type, long long& parent) const;

	// What the person does — the desktop's verbs, each a door.
	void CreateItem();
	void EditItem();
	void RemoveItem();
	void PropertyItem();
	void SelectItem();
	void UpItem();
	void DownItem();
	void SortItem();
	void CopyItem();
	void PasteItem();
	// …a data processor or a report as the file of that extension it is kept in (its menu's Load, Unload).
	void LoadItem(const wxString& extension);
	void UnloadItem(const wxString& extension);
	// …the row put where its sibling stands (Up, Down).
	void MoveItem(const wxTreeItemId& item, const wxTreeItemId& sibling);
	// …the row of the object not among `drawn` selected and opened (Create, Paste); false — there is none.
	bool SelectCreated(const std::set<long long>& drawn);

	void UpdateToolbar();
	void ShowContextMenu(const wxTreeItemId& item, const wxPoint& pos);
	// What the row's object offers to open — its Menu (the engine's CollectContextMenu); true when the tree's own New /
	// Edit / Remove apply to it (Standard).
	bool AppendMetaMenu(wxMenu& menu, const ibProtocolNode& row);

	void Search(const wxString& search);

	// events:
	void OnLeftDown(wxMouseEvent& event);
	void OnLeftDClick(wxMouseEvent& event);
	void OnItemActivated(wxTreeEvent& event);
	void OnSelected(wxTreeEvent& event);
	void OnSetFocus(wxFocusEvent& event);
	void OnExpanded(wxTreeEvent& event);
	void OnCollapsed(wxTreeEvent& event);
	void OnItemMenu(wxTreeEvent& event);
	void OnEditHelp(wxCommandEvent& event);
	void OnEditNotes(wxCommandEvent& event);
	void OnStartSearch(wxCommandEvent& event);
	void OnCancelSearch(wxCommandEvent& event);

	bool m_readOnly = false;
	const long long m_tab;

	// THE OBJECT THE INSPECTOR SHOWS — made when a row is selected, of the server's rows for it, and linked to this tree's
	// tab (an edit goes to the configuration it came from); the next row selected makes the next. Shared: an editor opened
	// while it is shown keeps it too (ibCodeEditor).
	std::shared_ptr<ibValueMetaObject> m_object;

	ibMetaView* m_metaView = nullptr;
	// ⭐ TRUE WHILE OnSetFocus IS ITSELF MOVING THE ACTIVATION — the desktop's: on Cocoa a focus moved tells the window
	// that held it inside SetFocus, and the handler answered its own move until the stack ran out.
	bool m_switchingActivation = false;

	wxAuiToolBar* m_metaTreeToolbar = nullptr;
	wxTreeCtrl* m_metaTreeCtrl = nullptr;
	wxSearchCtrl* m_searchTree = nullptr;

	wxString m_search;                 // the text the rows are found by — none: all of them
	std::set<wxString> m_expanded;      // the rows the person opened, by their places
	std::map<wxString, int> m_images;   // the control's pictures, by the rows' Picture
	bool m_drawing = false;             // what the drawing does to the control is not the person's
};

// THE CONFIGURATION'S TREE — the designer's pane on the left (the desktop's ibConfigurationTree): its rows the frame's
// MetaData, the search above them.
class ibConfigurationTree : public ibMetaTreeBase {
public:
	explicit ibConfigurationTree(wxWindow* parent) : ibMetaTreeBase(parent, 0, ibProtocol::ibView::ibKind::Configuration) {}
	// A configuration browsed in a tab (File → Open / Open database configuration) — the same rows, schema MetaData with
	// that Tab.
	ibConfigurationTree(wxWindow* parent, long long tab) : ibMetaTreeBase(parent, tab, ibProtocol::ibView::ibKind::Configuration) {}

	// The predefined values editor over the object (ibDialogPredefinedEditor), its items read from the server as it opens.
	virtual void EditPredefinedValues(class ibValueMetaObjectRecordDataHierarchyMutableRef* metaObject) override;
	// The home page workspace editor over the root (ibDialogHomePageEditor), its workspace and forms read as it opens.
	virtual void EditHomePage(class ibValueMetaObjectConfiguration* metaObject) override;

	// THE CONFIGURATION ITSELF, as the desktop's window asked it (ibMetaDataConfigurationBase) — whether it differs from
	// the base's (ibMetaData::IsModified: what Rollback and Update have to do), whether somebody edited it, and its name,
	// as its root row says.
	bool IsModified() const;
	bool IsEdited() const;
	wxString GetConfigName() const;

	// …and its verbs, the desktop's Configuration menu's: the doors of schema MetaData — Save, Rollback, Apply (its
	// ledger the server asks), Load from a file of the person's uploaded, Unload into one uploaded empty and taken down
	// to theirs. How each went the server says to the person; false — refused (said too), or the file not read or written.
	bool SaveDatabase() const;
	bool RollbackConfiguration() const;
	bool ApplyConfiguration() const;
	bool LoadConfigFromFile(const wxString& strFileName) const;
	bool SaveConfigToFile(const wxString& strFileName) const;
	// …and as a directory of files, one a part (ibFileProvider): read and written here, where the directory is, the
	// files travelling to the server and back as one node. A directory that is no export, or holds other files, refuses.
	bool LoadConfigFromFiles(const wxString& directory) const;
	bool SaveConfigToFiles(const wxString& directory) const;
};

#endif
