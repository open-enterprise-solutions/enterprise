#ifndef _METATREE_H__
#define _METATREE_H__

#include <map>
#include <set>

#include <wx/aui/aui.h>
#include <wx/choice.h>
#include <wx/srchctrl.h>
#include <wx/treectrl.h>

#include "protocol/protocol.h"
#include "protocol/protocolNode.h"

// THE DESIGNER'S TREES — the desktop's ibMetaTreeBase and its heirs (designer/mainFrame/metaTree): the panel, its
// toolbar, its search, its menus and keys. The ROWS are the server's: its ibConfigurationTree (frmserver/metaTree)
// writes them into the frame's MetaData, an external file's tree into its tab's View; a row is NodeId — its object's
// id, or below 0 a group row's place among its siblings — Title, Picture, Group (what New makes under it), Type,
// Position, Menu (protocol.h, ibProtocolSchema::MetaData). What a person does goes back as the doors of schema
// MetaData (ibProtocolMetaDataCommand), and the rows come back with the frame: a click ASKS, the row follows from the
// metadata — as on the desktop it followed the stage and never the click.
class ibMetaTreeBase : public wxPanel {
public:

	// THE ROWS, as the frame has them now — `root` the metadata's own row, everything under it. Drawn over what is shown,
	// row by row by their ids: what was open, selected and scrolled to stays so. An external file's tree draws its header
	// too.
	virtual void Draw(const ibProtocolNode& root);

	// The tree's tab — 0: the configuration's (the frame's MetaData), else an external file's.
	long long GetTab() const { return m_tab; }

	// Its tab became the active one — the inspector shows what is selected in it (the desktop's).
	void ActivateTree() { SelectItem(); }

protected:

	ibMetaTreeBase(wxWindow* parent, long long tab, bool search);

	// THE DOORS — schema MetaData's parameters: the command, its arguments, the tab's id with them; and the command
	// asked, the frame drawn after it. False: refused (said to the person).
	ibProtocolNode GetParams(ibProtocolMetaDataCommand command, ibProtocolNode args) const;
	bool Command(ibProtocolMetaDataCommand command, ibProtocolNode args) const;
	// …and an object's property of that name given the Value of `args` — the inspector's door (schema Properties, Save),
	// by the object's id and the tab: the header above an external file's rows.
	bool SaveProperty(long long id, const wxString& name, ibProtocolNode args) const;
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
	// …the row put where its sibling stands (Up, Down).
	void MoveItem(const wxTreeItemId& item, const wxTreeItemId& sibling);
	// …the row of the object not among `drawn` selected and opened (Create, Paste).
	void SelectCreated(const std::set<long long>& drawn);

	void UpdateToolbar();
	void ShowContextMenu(const wxTreeItemId& item, const wxPoint& pos);
	// What the row's object offers to open — its Menu (the engine's CollectContextMenu); true when the tree's own New /
	// Edit / Remove apply to it (Standard).
	bool AppendMetaMenu(wxMenu& menu, const ibProtocolNode& row);

	void Search(const wxString& search);

	// events:
	void OnItemActivated(wxTreeEvent& event);
	void OnSelected(wxTreeEvent& event);
	void OnSetFocus(wxFocusEvent& event);
	void OnExpanded(wxTreeEvent& event);
	void OnCollapsed(wxTreeEvent& event);
	void OnItemMenu(wxTreeEvent& event);
	void OnStartSearch(wxCommandEvent& event);
	void OnCancelSearch(wxCommandEvent& event);

	const long long m_tab;

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
	explicit ibConfigurationTree(wxWindow* parent) : ibMetaTreeBase(parent, 0, true) {}

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
};

#endif
