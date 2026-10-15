#ifndef __VISUAL_EDITOR_H__
#define __VISUAL_EDITOR_H__

#include "innerFrame.h"
#include "designer-thin/win/editor/codeEditor/codeEditorDesigner.h"

#include "frmclient/visualView/visualHostClient.h"

#include "core/description/commandDescription.h"  // ibCommandDescription — a command row's path
#include "core/description/sourceDescription.h"   // ibSourceHop — an attribute row's path

#include <wx/aui/auibook.h>
#include <wx/splitter.h>
#include <wx/treectrl.h>

#include <wx/dnd.h>

#include <functional>
#include <map>
#include <memory>
#include <stack>
#include <vector>

class ibModuleEditDocument;
class ibPropertySerializable;

class ibFormAttributeValue;
class ibFormCommandValue;
class ibFormDragItem;          // one polymorphic draggable KIND — format + (de)serialize + apply (visualEditorDragItem.h)
class ibValueLayerObject;
class ibValueCommandBar;
class ibValueCommandBarItem;

// THE DESKTOP'S FORM EDITOR (designer/win/editor/visualEditor/visualEditor.h) IN A TAB THE SERVER RAISED. ⭐ THE CLIENT
// EDITS THE FORM, THE SERVER HOLDS IT (Max, 2026-10-15): the tab's answer brings the form whole — its node as a frame
// draws it (Form), each control's rows (Controls), the classes a control may be made of (Classes: a placed one's node
// and rows) — and the module's text; the form is drawn by the host the runtime draws it with (ibVisualHostClient) and
// edited here, as the desktop's editor did: its command processor over the form's node, the form handed back whole
// after each command, as the desktop's wrote it into its metaobject, and when the tab is saved or closed (SaveForm).
// The form's attributes and commands travel in its node (Attributes, FormCommands): their trees are the server's rows of it, and their doors the server's, asked with the form as it stands
// — the answer taken as an edit of the client's, undone as one.

#define wxNOTEBOOK_PAGE_DESIGNER 0
#define wxNOTEBOOK_PAGE_CODE_EDITOR 1

#define oes_clipboard_frame		wxT("oes_clipboard_frame")
#define oes_clipboard_attribute	wxT("oes_clipboard_attribute")
#define oes_clipboard_command	wxT("oes_clipboard_command")
// A command of a control's command bar — on the system's clipboard as the others are: the desktop held it in its process
// (commandBar.cpp's s_commandClipboard), out of reach of another window.
#define oes_clipboard_command_bar_item	wxT("oes_clipboard_command_bar_item")

// wxDropTarget for a node dragged from a form-editor tree — the desktop's (designer visualEditor.h). Shared by the form
// CANVAS and the OBJECT TREE. It holds the registered draggable KINDS (ibFormDragItem — a source path, a command
// path); OnData matches the RECEIVED format to a kind, decodes it and enacts the drop. NO per-format branch — adding a
// draggable is a new ibFormDragItem subclass registered in the ctor, nothing here changes. Each surface supplies only a RESOLVER (drop
// point → the frame to act on, in that surface's coordinates) and an APPLIER (runs the decoded item's ApplyDrop with
// the surface's editor). `deferred` runs the apply through CallAfter — for a tablebox whose own target goes with it
// when the drop draws the form anew.
class ibSourceDragDropTarget : public wxDropTarget {
public:
	using PointResolver = std::function<ibValueFrame*(wxCoord, wxCoord)>;                  // drop point → target frame (may be null)
	using DropApply     = std::function<void(const ibFormDragItem&, ibValueFrame*)>;       // enact a decoded item on a frame
	ibSourceDragDropTarget(PointResolver resolver, DropApply apply, bool deferred = false);
	virtual ~ibSourceDragDropTarget() override;   // out-of-line: unique_ptr<ibFormDragItem> (forward-declared) destroyed here
	virtual wxDragResult OnData(wxCoord x, wxCoord y, wxDragResult def) override;
private:
	PointResolver m_resolver;
	DropApply     m_apply;
	bool          m_deferred;
	std::vector<std::unique_ptr<ibFormDragItem>> m_items;   // registered kinds (source)
	std::vector<wxCustomDataObject*>             m_data;    // composite sub-objects, index-parallel to m_items
};

///////////////////////////////////////////////////////////////////////////////
// ibDesignerWindow - Extends the ibInnerFrame to show the object highlight
///////////////////////////////////////////////////////////////////////////////

// THE DESKTOP'S CARD — the form's frame on the canvas, and the box around the selected control. The desktop pushed its
// paint handler onto every panel a control can sit in as the panel was made (GetHighlightPaintHandler); the client
// builds those panels itself, so the panel a box is drawn on is bound as it is selected (SetSelectedPanel) — bound, not
// pushed: a window that goes takes a binding with it.
class ibDesignerWindow : public ibInnerFrame {
	wxDECLARE_CLASS(ibDesignerWindow);
private:

	void DrawRectangle(wxDC& dc, const wxPoint& point, const wxSize& size, ibValueFrame* object);

public:

	ibDesignerWindow(wxWindow* parent, int id, const wxPoint& pos, const wxSize& size = wxDefaultSize,
		long style = 0, const wxString& name = wxT("designer_win"));
	virtual ~ibDesignerWindow();

	void SetGrid(int x, int y);
	void SetSelectedSizer(wxSizer* sizer) { m_selSizer = sizer; }
	void SetSelectedItem(wxObject* item) { m_selItem = item; }
	void SetSelectedObject(ibValueFrame* object) { m_selObj = object; }
	void SetSelectedPanel(wxWindow* actPanel);

	// Clear the selection — the four pointers go together, none is meaningful without the others. The editor clears it
	// before every draw of the form: a frame of another shape builds the controls anew, and the canvas never paints from
	// a control that is gone.
	void ClearSelection() { m_selSizer = nullptr; m_selItem = nullptr; m_selObj = nullptr; m_actPanel = nullptr; }

	wxSizer* GetSelectedSizer() const { return m_selSizer; }
	wxObject* GetSelectedItem() const { return m_selItem; }
	ibValueFrame* GetSelectedObject() const { return m_selObj; }
	wxWindow* GetActivePanel() const { return m_actPanel; }

	void HighlightSelection(wxDC& dc);
	void OnPaint(wxPaintEvent& event);

private:

	// The panel the box is drawn on, painted — its own paint first, the box over it.
	void OnPanelPaint(wxPaintEvent& event);

	int m_x;
	int m_y;
	wxSizer* m_selSizer = nullptr;
	wxObject* m_selItem = nullptr;
	ibValueFrame* m_selObj = nullptr;
	wxWindow* m_actPanel = nullptr;

	wxDECLARE_EVENT_TABLE();
};

///////////////////////////////////////////////////////////////////////////////
// ibVisualEditorCmd
///////////////////////////////////////////////////////////////////////////////

// THE DESKTOP'S COMMAND — done, and undone to what was before it.
class ibVisualEditorCmd {
protected:

	/**
	 * Executes the command.
	 */
	virtual void DoExecute() = 0;

	/**
	 * Restores the state prior to the command's execution.
	 */
	virtual void DoRestore() = 0;

public:

	ibVisualEditorCmd() : m_executed(false) {}

	// Virtual: the processor deletes commands through this type.
	virtual ~ibVisualEditorCmd() = default;

	void Execute() {
		if (!m_executed) {
			DoExecute();
			m_executed = true;
		}
	}

	void Restore() {
		if (m_executed) {
			DoRestore();
			m_executed = false;
		}
	}

protected:

	bool m_executed;
};

///////////////////////////////////////////////////////////////////////////////
// ibVisualEditorNotebook
///////////////////////////////////////////////////////////////////////////////

class ibVisualEditorNotebook : public wxAuiNotebook {

public: class ibVisualEditor :
	public wxPanel {

	// THE DESKTOP'S COMMAND PROCESSOR — the commands done and undone, and where the form was last handed back (its save
	// point).
	class ibCommandProcessor {
	public:

		ibCommandProcessor() : m_savePoint(0) {}

		~ibCommandProcessor() { Reset(); }

		void Execute(ibVisualEditorCmd* command) {
			command->Execute();
			m_undoStack.push(command);
			// A new command kills the redo branch — and the stack owns what it drops.
			while (!m_redoStack.empty()) {
				wxDELETE(m_redoStack.top());
				m_redoStack.pop();
			}
		}

		bool Undo() {
			if (!m_undoStack.empty()) {
				ibVisualEditorCmd* command = m_undoStack.top();
				m_undoStack.pop();
				command->Restore();
				m_redoStack.push(command);
			}
			return true;
		}

		bool Redo() {
			if (!m_redoStack.empty()) {
				ibVisualEditorCmd* command = m_redoStack.top();
				m_redoStack.pop();
				command->Execute();
				m_undoStack.push(command);
			}
			return true;
		}

		// Drop the whole history — destroying it, not forgetting it.
		void Reset() {
			while (!m_redoStack.empty()) {
				wxDELETE(m_redoStack.top());
				m_redoStack.pop();
			}
			while (!m_undoStack.empty()) {
				wxDELETE(m_undoStack.top());
				m_undoStack.pop();
			}
			m_savePoint = 0;
		}

		bool CanUndo() const {
			return (!m_undoStack.empty());
		}

		bool CanRedo() const {
			return (!m_redoStack.empty());
		}

		void SetSavePoint() {
			m_savePoint = m_undoStack.size();
		}

		bool IsAtSavePoint() const {
			return m_savePoint == m_undoStack.size();
		}

	private:
		std::stack<ibVisualEditorCmd*> m_undoStack, m_redoStack;
		size_t m_savePoint;
	};

public:

	// THE CANVAS — the desktop's ibVisualEditorHost: the card on the powder background, the form drawn in the card by the
	// host the runtime draws it with; this one holds the card, finds what a click is on and boxes the selection.
	class ibVisualEditorHost : public ibVisualHostClient {
	public:

		ibVisualEditorHost(ibVisualEditor* handler, wxWindow* parent, long long tabId);

		//override designer host
		virtual bool IsDesignerHost() const override { return true; }
		virtual wxWindow* GetBackgroundWindow() const override;
		virtual void OnClickFromApp(wxWindow* currentWindow, wxMouseEvent& event) override;
		virtual void SelectControl(ibValueFrame* control) override { m_formHandler->SelectObject(control); }
		virtual void ModifyProperty(ibProperty* prop, const wxVariant& oldValue, const wxVariant& newValue) override {
			m_formHandler->ModifyProperty(prop, oldValue, newValue);
		}
		virtual void MovePosition(ibValueFrame* control, ibValueFrame* parent, unsigned int pos) override {
			m_formHandler->MovePosition(control, parent, pos);
		}

		void OnClickBackPanel(wxMouseEvent& event);
		void OnResizeBackPanel(wxCommandEvent& event);

		bool OnLeftClickFromApp(wxWindow* currentWindow);
		bool OnRightClickFromApp(wxWindow* currentWindow, wxMouseEvent& event);
		// The control (or the form) whose command bar the window is on — none: on no bar.
		ibValueFrame* GetCommandBarOwner(wxWindow* window) const;

		//set and create window
		void SetObjectSelect(ibValueFrame* obj);
		void ClearObjectSelect();   // drop the canvas control highlight
		void ScrollToObject(ibValueFrame* obj);

		// The card's title — the form's caption, else what it is the form of (the View's State.Caption, composed by the
		// server where the metadata is).
		void SetCaption(const wxString& strCaption);

	protected:

		// The card sized to the form it holds, and the canvas to the card.
		virtual void UpdateVirtualSize() override;

	private:

		//designer
		ibDesignerWindow* m_back = nullptr;
		//form handler
		ibVisualEditor* m_formHandler = nullptr;
	};

	//////////////////////////////////////////////////////////////////////////////////////////

	class ibVisualEditorObjectTree : public wxPanel {
	public:

		ibVisualEditorObjectTree(ibVisualEditor* owner, wxWindow* parent, int id = wxID_ANY);
		virtual ~ibVisualEditorObjectTree() override;

		// The icons of the classes a control may be made of — the tab's answer's Classes (the desktop's tree took them
		// from the registry of control classes).
		void CreateTree(const ibProtocolNode& classes);

		void OnEditorLoaded();
		void OnEditorRefresh();
		void OnObjectSelected(ibValueFrame* obj);
		void OnObjectExpanded(ibValueFrame* obj);
		void OnPropertyModified(ibProperty* prop);

		// The holder whose property that is — none: no layer's (the attribute tree's FindEntry).
		ibValueLayerObject* FindEntry(const ibPropertyObject* owner) const;
		// …and a control's command bar's, by the control's id — none: no bar of it in the tree.
		ibValueLayerObject* FindEntry(long long controlId) const;

		// THE DESKTOP'S SelectCommandItem — a layer's node selected after an edit of it, the inspector showing it: the
		// holder, or a control's command by its place (one just added, pasted, moved).
		void SelectCommandItem(const ibValueLayerObject* layer);
		void SelectCommandItem(long long controlId, unsigned int index);

	protected:

		void OnSelChanged(wxTreeEvent& event);
		void OnRightClick(wxTreeEvent& event);
		void OnBeginDrag(wxTreeEvent& event);
		void OnEndDrag(wxTreeEvent& event);
		void OnExpansionChange(wxTreeEvent& event);
		void OnKeyDown(wxTreeEvent& event);

	private:

		/**
		 * Builds the tree completely.
		 */
		void RebuildTree();
		void AddChildren(ibValueFrame* child, const wxTreeItemId& parent, bool is_root = false);
		// The desktop's "Command interface" node under a control with a command bar — its commands under it, as its
		// node has them (Layers, CommandBar).
		void AddCommandBar(ibValueFrame* obj, const wxTreeItemId& parent);

		int GetImageIndex(const wxString& name) {
			int index = wxNOT_FOUND; //default icon

			std::map<wxString, int>::iterator it = m_iconIdx.find(name);
			if (it != m_iconIdx.end()) { index = it->second; }
			return index;
		}

		void UpdateItem(const wxTreeItemId& id, ibValueFrame* obj) {

			// show the name
			wxString class_name(obj->GetClassName());
			wxString obj_name(obj->GetControlName());

			wxString text = obj_name + wxT(" : ") + class_name;

			// update the item
			m_tcObjects->SetItemText(id, text);

			if (m_formHandler != nullptr &&
				obj == m_formHandler->GetSelectedObject()) {
				m_notifySelecting = true;
				m_tcObjects->EnsureVisible(id);
				m_tcObjects->SelectItem(id);
				m_tcObjects->SetItemBold(id);
				m_notifySelecting = false;
			}
		}

		void RestoreItemStatus(ibValueFrame* obj);

		// A tree item's payload selected — a control node to the visual editor (which also scrolls to it), a layer's to
		// the inspector.
		void SelectItemData(wxTreeItemData* item_data);

		ibValueFrame* GetObjectFromTreeItem(const wxTreeItemId& item);

		ibVisualEditor* m_formHandler = nullptr;

		wxImageList* m_iconList = nullptr;

		std::map<ibValueFrame*, wxTreeItemId> m_listItem;
		std::map<wxString, int> m_iconIdx;

		// THE LAYERS' HOLDERS — a control's command bar by the control's id, a command of it by the id and its place:
		// made as the tree is built, kept by those while it is built anew, gone with the node.
		std::map<long long, std::unique_ptr<ibValueCommandBar>> m_commandBars;
		std::map<std::pair<long long, unsigned int>, std::unique_ptr<ibValueCommandBarItem>> m_commandBarItems;
		std::map<const ibValueLayerObject*, wxTreeItemId> m_layerItemMap;

		wxTreeCtrl* m_tcObjects = nullptr;
		wxTreeItemId m_draggedItem;

		// The form as the tree last showed it — each control, its class and name in order, nested as they are: a draw
		// that changes none of them leaves the tree alone.
		wxString m_shape;

		bool m_notifySelecting = false;

		wxDECLARE_EVENT_TABLE();
	};

	/**
	 * Since we can associate an object with each item, this class makes it
	 * easy to get the object (ibValueFrame) associated with an item so it
	 * can be selected by clicking on the item.
	 */
	class ibVisualEditorObjectTreeItemData : public wxTreeItemData {
	public:
		ibVisualEditorObjectTreeItemData(ibValueFrame* obj) : m_object(obj) {}
		ibVisualEditorObjectTreeItemData(ibValueLayerObject* layerObject) : m_layerObject(layerObject) {}
		ibValueFrame* GetObject() { return m_object; }
		ibValueLayerObject* GetLayerObject() { return m_layerObject; }
	private:
		ibValueFrame* m_object = nullptr;
		ibValueLayerObject* m_layerObject = nullptr;
	};

	//////////////////////////////////////////////////////////////////////////////////////////
	// ibVisualEditorAttributeTree — THE DESKTOP'S (designer visualEditorAttributeTree.cpp) OVER THE SERVER'S WALK: the
	// form's attributes and their composition are the server's (its holders, their source explorers, the metadata), so
	// its rows are — the answer's AttributeTree, a row's composition asked when it is expanded (Composition). The holder a
	// root row carries is the client's (ibFormAttributeValue): its properties the server's inspector's rows. Every action
	// is the server's editor's door (`Attribute`), the form as it stands asked with.
	//////////////////////////////////////////////////////////////////////////////////////////

	class ibVisualEditorAttributeTree : public wxPanel {
	public:

		ibVisualEditorAttributeTree(ibVisualEditor* owner, wxWindow* parent, int id = wxID_ANY);
		virtual ~ibVisualEditorAttributeTree() override;

		// THE ROWS DRAWN — the tree built anew when they, or the form's `attributes` they are of, say other than it shows
		// (the desktop's RebuildTree, on its commands): the selected attribute kept by its id, an expanded row's
		// composition asked again as it opens.
		void Draw(const ibProtocolNode& rows, const ibProtocolNode& attributes);

		// The holder whose property that is — none: no attribute's.
		ibFormAttributeValue* FindEntry(const ibPropertyObject* owner) const;

	protected:

		void OnSelChanged(wxTreeEvent& event);
		void OnContextMenu(wxContextMenuEvent& event);        // right-click anywhere (incl. empty)
		void OnActivated(wxTreeEvent& event);                 // double-click → activate properties
		void OnItemExpanding(wxTreeEvent& event);             // lazy: the node's composition asked on first expand
		void OnBeginDrag(wxTreeEvent& event);                 // drag a node onto the form → a bound control
		void OnAdd(wxCommandEvent& event);
		void OnAddColumn(wxCommandEvent& event);              // add a column to a value-table attribute
		void OnEdit(wxCommandEvent& event);                   // inline rename (EditLabel)
		void OnRemove(wxCommandEvent& event);
		void OnSetMain(wxCommandEvent& event);
		void OnProperties(wxCommandEvent& event);             // show inspector
		void OnCopy(wxCommandEvent& event);                   // clipboard copy (the attribute's node of the form)
		void OnCut(wxCommandEvent& event);                    // clipboard copy + remove (undoable)
		void OnPaste(wxCommandEvent& event);                  // clipboard paste (made on the server)
		void OnEndLabelEdit(wxTreeEvent& event);              // the name given — the server takes or refuses it

	private:

		// The rows under one, as the server wrote them — each with its item data; one with a composition of its own not yet
		// asked gets the desktop's dummy [+].
		void AppendRows(const wxTreeItemId& parent, const ibProtocolNode& rows);

		// The holder the tree item carries — what the inspector selects.
		ibFormAttributeValue* GetEntryFromItem(const wxTreeItemId& item) const;

		// Select the tree item carrying this attribute (0 = none), by its id — the rows are built anew under the same ids.
		void SelectEntry(long long id);

		// The single decode-and-select point (the desktop's) — the holder the editor's current element: shown in the
		// inspector, what the server says of it received (ibFormAttributeValue::Receive).
		void SelectInInspector(ibFormAttributeValue* entry);

		// A row under a value-table attribute carries no holder — its column's (the path [attribute, column]) shown in
		// the inspector instead. True when it handled the item (the caller then skips the holder path).
		bool SelectColumnInInspector(const wxTreeItemId& item);
		// …and the row of that column selected — the attribute's row opened so its columns are asked; false: not there.
		bool SelectColumn(long long attributeId, long long columnId);

		ibVisualEditor* m_formHandler = nullptr;
		wxTreeCtrl* m_tcAttributes = nullptr;

		// THE HOLDERS — one per root row, by the attribute's id; made by the row, and gone with it.
		std::map<long long, std::unique_ptr<ibFormAttributeValue>> m_entries;
		// …and a value table's column of one, by the attribute's id and the column's: made as its row is selected, gone
		// with the attribute.
		std::map<std::pair<long long, long long>, std::unique_ptr<ibFormAttributeValue>> m_columns;

		// The rows and the attributes as the tree last showed them — a draw that says the same leaves the tree alone
		// (expanded rows stay so).
		wxString m_shape;
		// The attribute selected, by its id.
		long long m_selectedId = 0;

		// True while the tree is torn down and re-appended — a selection changed then is no person's.
		bool m_rebuilding = false;

		wxDECLARE_EVENT_TABLE();
	};

	class ibVisualEditorAttributeTreeItemData : public wxTreeItemData {
	public:
		// A ROOT node carries the attribute HOLDER — the selectable; a COMPOSITION node (a field / table section /
		// reference target, asked on expand) passes entry == nullptr and carries the binding PATH (head = attribute id)
		// + view flags, as the server wrote them — what it is asked with again. A non-empty refTypes list means "expand me
		// through a reference-as-source"; m_loaded guards the one-time ask.
		ibVisualEditorAttributeTreeItemData(ibFormAttributeValue* entry, const std::vector<ibSourceHop>& path,
			const std::vector<ibMetaID>& refTypes = {}, bool tableSection = false, ibClassID dropControl = 0,
			bool underList = false)
			: m_entry(entry), m_path(path), m_refTypes(refTypes), m_tableSection(tableSection), m_dropControl(dropControl),
			m_underList(underList) {}
		// A COMMAND node of the navigator: carries a draggable command projection as a HOP PATH (ibCommandDescription) —
		// SYMMETRIC to a source node carrying its binding path. A command is a 1-hop [metaId]; a table command a 2-hop
		// [source, action]. Dragged onto the form → a bound button.
		explicit ibVisualEditorAttributeTreeItemData(const ibCommandDescription& commandDesc)
			: m_commandDesc(commandDesc) {}
		const ibCommandDescription& GetCommandDesc() const { return m_commandDesc; }
		ibFormAttributeValue* GetEntry() const { return m_entry; }
		const std::vector<ibSourceHop>& GetPath() const { return m_path; }
		const std::vector<ibMetaID>& GetRefTypes() const { return m_refTypes; }
		bool HasRef() const { return !m_refTypes.empty(); }
		bool IsTableSection() const { return m_tableSection; }
		bool IsLoaded() const { return m_loaded; }
		void SetLoaded() { m_loaded = true; }
		// The control-class CLSID this node maps to (0 = not draggable) — the server's, the drag's gate.
		ibClassID GetDropControl() const { return m_dropControl; }
		bool IsUnderList() const { return m_underList; }
	private:
		ibFormAttributeValue* m_entry = nullptr;
		std::vector<ibSourceHop> m_path;
		std::vector<ibMetaID> m_refTypes;
		bool m_tableSection = false;
		ibClassID m_dropControl = 0;
		bool m_underList = false;
		bool m_loaded = false;
		ibCommandDescription m_commandDesc;
	};

	//////////////////////////////////////////////////////////////////////////////////////////
	// ibVisualEditorCommandTree — THE DESKTOP'S (designer visualEditorCommandTree.cpp) OVER THE SERVER'S GATHER: the
	// commands the form can bind are the server's (GatherFormCommands), so its rows are — the answer's CommandTree, the
	// three sections and what is in them. A form command's row carries its id, and the holder by it is the client's
	// (ibFormCommandValue): its properties the server's inspector's rows. Every action is the server's editor's door
	// (`FormCommand`), the form as it stands asked with.
	//////////////////////////////////////////////////////////////////////////////////////////

	class ibVisualEditorCommandTree : public wxPanel {
	public:

		ibVisualEditorCommandTree(ibVisualEditor* owner, wxWindow* parent, int id = wxID_ANY);
		virtual ~ibVisualEditorCommandTree() override;

		// THE ROWS DRAWN — the tree built anew when they say other than it shows (the desktop's RebuildTree): the selected
		// row kept by its command.
		void Draw(const ibProtocolNode& rows);

		// The holder whose property that is — none: no form command's.
		ibFormCommandValue* FindEntry(const ibPropertyObject* owner) const;

	protected:

		void OnBeginDrag(wxTreeEvent& event);      // drag a command onto the form -> a bound command button
		void OnContextMenu(wxContextMenuEvent& event);   // right-click IN THE FORM COMMANDS GROUP -> add / copy / paste / delete
		void OnSelChanged(wxTreeEvent& event);     // selection CHANGED -> show the form command in the inspector

	private:

		// The rows under one, as the server wrote them — a group's under it, a command's with its picture (into `images`);
		// the one carrying `keepDesc` given back in `keepNode`.
		void AppendRows(const wxTreeItemId& parent, const ibProtocolNode& rows, wxImageList* images,
			const ibCommandDescription& keepDesc, wxTreeItemId& keepNode);

		void AddCommand();                         // create a form-local command (property object) + reveal in the inspector
		void CopyCommand();                        // copy the selected form command to the clipboard (oes_clipboard_command)
		void CutCommand();                         // copy + delete the selected form command
		void PasteCommand();                       // paste a clipboard form command as a fresh entry on the form
		void DeleteCommand();                      // delete the selected form command from the form

		ibFormCommandValue* SelectedFormCommand() const;   // the selected node's FORM COMMAND, else null
		void SelectCommand(long long id);          // land the row on a form command BY IDENTITY (like the attribute tree's SelectEntry)
		void RevealSelectedCommand();              // surface the selected form command in the inspector (both click paths)

		ibVisualEditor* m_formHandler = nullptr;
		wxTreeCtrl* m_tcCommands = nullptr;
		wxTreeItemId m_formCommandsGroup;          // the "Form commands" section node — the ONLY place the menu shows

		// THE HOLDERS — one per form command's row, by its id; made by the row, and gone with it.
		std::map<long long, std::unique_ptr<ibFormCommandValue>> m_entries;

		// The rows as the tree last showed them — a draw that says the same leaves the tree alone.
		wxString m_shape;
		// The row selected, by its command.
		ibCommandDescription m_selectedDesc;

		bool m_rebuilding = false;                 // suppress OnSelChanged while the rows churn (same guard as the attribute tree)

		wxDECLARE_EVENT_TABLE();
	};

	//////////////////////////////////////////////////////////////////////////////////////////

	class ibVisualEditorItemPopupMenu : public wxMenu {
	public:

		ibVisualEditorItemPopupMenu(ibVisualEditor* owner, wxWindow* parent, ibValueFrame* obj);

		bool HasDeleteObject();
		int GetSelectedID() const { return m_selID; }

		void OnUpdateEvent(wxUpdateUIEvent& e);
		void OnMenuEvent(wxCommandEvent& event);

	protected:

		ibValueFrame* m_object = nullptr;
		ibVisualEditor* m_formHandler = nullptr;
		int m_selID;

		// The classes the control adds in itself (its class's Menu — the desktop's PrepareDefaultMenu), by their items'
		// place from the first id.
		std::vector<wxString> m_classNames;

		wxDECLARE_EVENT_TABLE();
	};

public:

	ibVisualEditorHost* GetVisualEditor() const { return m_visualEditor; }
	ibVisualEditorObjectTree* GetObjectTree() const { return m_objectTree; }
	ibVisualEditorAttributeTree* GetAttributeTree() const { return m_attributeTree; }
	ibVisualEditorCommandTree* GetCommandTree() const { return m_commandTree; }

	ibValueForm* GetValueForm() const { return m_visualEditor->GetValueForm(); }

	ibModuleEditDocument* GetEditorDocument() const { return m_document; }

	// The tab's view activated again — its menu and toolbar asked anew, for the page now shown.
	void Activate() const;

	// View-only follows the configuration (the View's State.ReadOnly — opened read-only, or locked by another).
	bool IsEditable() const { return !m_readOnly; }
	void SetReadOnly(bool readOnly = true) { m_readOnly = readOnly; }

	void ActivateEditor();

protected:

	// Notify every observer of the corresponding event
	void NotifyEditorLoaded();
	void NotifyEditorRefresh();
	void NotifyObjectSelected(ibValueFrame* obj, bool force = false);

	void NotifyPropertyModified(ibProperty* prop);

public:

	ibVisualEditor(ibModuleEditDocument* document, ibVisualEditorNotebook* parent, int id = wxID_ANY);
	virtual ~ibVisualEditor();

	void CreateWideGui();

	// THE DESKTOP'S DOORS (designer/win/editor/visualEditor/visualEditorCmdProc.cpp) OVER THE FORM'S NODE: what its
	// command processor did to the controls is done to the node — a command of the form before and after it — and the
	// form drawn again. A new control is its class's from the tab's answer (Classes, Node), put where the desktop's form
	// took it (ibValueForm::CreateObject) under ids and a name of the form's own; the clipboard is this computer's.
	void CreateObject(const wxString& name);
	// …and a control bound to a source dropped on one (an attribute tree's row), and a command dropped on one (a command
	// tree's row): what is made of them is the server's to say (its metadata) — asked with the form as it stands, the
	// answer the command's form.
	void CreateBoundControl(ibValueFrame* parentHint, const ibSourceDescription& desc);
	void CreateCommandButton(ibValueFrame* target, const ibCommandDescription& desc);
	// Set by the drop resolver right before ApplyDrop when the drop landed on a command-bar TOOLBAR (form or tablebox):
	// the command joins THAT bar as a new item instead of spawning a free button — the bar by the id of the control it
	// is of. CreateCommandButton reads then clears it; 0 for any other drop.
	void SetPendingDropBar(long long controlId) { m_pendingDropBar = controlId; }
	// (Re)attach a drop target to every tablebox's own window — its grid swallows the OS drop before the form-canvas
	// target sees it, so drops over the rows need the table's own target. Called on editor refresh.
	void WireTableboxDrops(ibValueFrame* obj);
	void RemoveObject(ibValueFrame* obj);
	void CutObject(ibValueFrame* obj);
	void CopyObject(ibValueFrame* obj);
	bool PasteObject(ibValueFrame* dstObject);

	void MovePosition(ibValueFrame* obj, bool right, unsigned int num = 1);
	// …and under `parent`, at `toPos` among its children — the desktop's table moved a column so by its header
	// (OnColumnReordered): done once the event that moved it is over, as the form drawn anew builds that table anew.
	void MovePosition(ibValueFrame* obj, ibValueFrame* parent, unsigned int toPos);
	// ⭐ OPEN OR CLOSED IN THE TREE IS THE FORM'S — saved with it (Expanded, as SaveNode writes it) and undone as a
	// command, as the desktop's was: the control, and — closed — every one under it, — opened — every one it is under.
	void ExpandObject(ibValueFrame* obj, bool expand);

	// ⭐ AN EDIT IS THE CLIENT'S — the control's node in the form takes the property (as the server's WriteData writes
	// it: under its name, its node value) and the control is drawn again with it, by a patch of the frame; a layer's (the
	// command bar's, a command's of it) in the control's node, the form drawn again by the server; an attribute's, a form
	// command's — the server's editor's door (Set). An event given a handler writes its procedure first (ModifyEvent).
	void ModifyProperty(ibProperty* prop, const wxVariant& oldValue, const wxVariant& newValue);

	// …and the attribute tree's, by the attribute's id (`Attribute`) — the server's editor's doors of the same names:
	// a new one (String, uniquely named) and a pasted one — its id, the tree lands on it (0: refused) —, its name given
	// (false: refused — empty, or another's), the main one (none: null).
	long long CreateAttribute();
	void RemoveAttribute(ibFormAttributeValue* entry);
	void CutAttribute(ibFormAttributeValue* entry);
	void CopyAttribute(ibFormAttributeValue* entry);
	long long PasteAttribute();
	bool RenameAttribute(ibFormAttributeValue* entry, const wxString& newName);
	void SetMainAttribute(ibFormAttributeValue* entry);
	// …a value table's column (the desktop's OnAddColumn) — the new one's id, the tree lands on it (0: refused).
	long long AddColumn(ibFormAttributeValue* entry);

	// …and the command tree's, by the form command's id (`FormCommand`): a new one (uniquely named, its Action empty) and
	// a pasted one — its id, the tree lands on it (0: refused).
	long long CreateFormCommand();
	void RemoveFormCommand(ibFormCommandValue* entry);
	void CutFormCommand(ibFormCommandValue* entry);
	void CopyFormCommand(ibFormCommandValue* entry);
	long long PasteFormCommand();

	void Undo();
	void Redo();

	// The form opened as the runtime shows it — the form as it stands handed with it (the desktop's TestForm).
	void TestForm();

	bool CanUndo() const { return m_cmdProc.CanUndo(); }
	bool CanRedo() const { return m_cmdProc.CanRedo(); }

	bool CanPasteObject() const;
	bool CanCopyObject() const;
	bool CanPasteAttribute() const;
	bool CanPasteFormCommand() const;

	// Object will not be selected if it already is selected, unless force = true
	// Returns true if selection changed, false if already selected
	bool SelectObject(ibValueFrame* obj, bool force = false, bool notify = true);

	void ScrollToObject(ibValueFrame* obj);

	// THE ONE SELECTION — kept by the control's id: a form drawn anew is made of other controls, the same ids. None
	// found — the form.
	ibValueFrame* GetSelectedObject() const;

	// ⭐ THE CURRENT ELEMENT — the desktop's (SetCurrentElement), when it is no control: an attribute, a column of one, a
	// form command, a layer of this form. No control selected, its highlight dropped (no "two selected"), and the
	// inspector shows it — soft: an open one filled, never popped open.
	void SetCurrentElement(ibPropertyObject* element);
	// …the one current — the selected control, or else the element set, while a tree holds it still (a holder whose row
	// is gone went with it): none.
	ibPropertyObject* GetCurrentElement() const;

	// SHOW the current element in the inspector — the explicit "Properties": the inspector raised.
	void ShowCurrentElement(bool forceOpen = true);

	void RefreshEditor() { NotifyEditorRefresh(); }

	// THE FORM AS A COMMAND LEAVES IT — drawn (by what `patch` changed; none: whole), each control built anew reading
	// its rows and every one its values from its node, the selection the control with that id; its attributes' and its
	// commands' trees as the server's last answer about it wrote them (`trees`: AttributeTree, CommandTree).
	void Draw(const ibProtocolNode& form, const ibProtocolNode& trees, const ibProtocolNode& patch, long long selected);

	// ⭐ THE FORM FROM THE TAB'S ANSWER — drawn (Form), each control reading its rows (Controls), the tree's icons and the
	// classes a control is made of (Classes); the desktop's LoadForm read the metaobject in its own process.
	bool LoadForm(const ibProtocolNode& answer);
	// …and handed back whole when it has changed since (Change with Form): the server loads it over the one it holds.
	bool SaveForm();

private:

	void Execute(ibVisualEditorCmd* cmd);
	// ⭐ A DOOR OF THE SERVER'S EDITOR — asked with the form as it stands (Form, unless the request carries another), its
	// answer the command's form: drawn, its controls' rows kept, its trees, the selection it names (Selected; none: as
	// it was). False — refused: nothing done.
	bool Execute(ibProtocolNode request, ibProtocolNode& answer);

	// The node of the attribute (`owner` Attributes) or command (FormCommands) with that id put on the clipboard in that
	// format — what Paste hands to the server's editor.
	void CopyEntry(const char* owner, long long id, const wxString& format, const wxString& name);
	// …that node itself — the system's clipboard, so another window, another designer, pastes it; `name` its text.
	void CopyEntry(const ibProtocolNode& entry, const wxString& format, const wxString& name);
	// …and the one on it, in that format — none: an empty node.
	ibProtocolNode PasteEntry(const wxString& format) const;

	void DoRemoveObject(ibValueFrame* obj, bool cutObject);

	// Each control of the node drawn receives what it holds: its rows (the answers', m_rows) when it was built anew, its
	// values from the node.
	void Receive(const ibProtocolNode& node);

	// A SOURCE PATH as the attribute tree's rows write it (Path — its hops, items {Id, Type}): what a row is asked
	// about again (Composition) and what a drop binds (BoundControl).
	static void WritePath(ibProtocolNode& node, const std::vector<ibSourceHop>& path);
	// …and a COMMAND's, as the command tree's rows write it (CommandPath — its hops, items {Id}; Kind): what a drop binds.
	static void WritePath(ibProtocolNode& node, const ibCommandDescription& desc);
	// …an object receives its values, as its node has them now — a control's, a layer's (a scalar the wire brings back a
	// field).
	static void Receive(ibPropertyObject* object, const ibProtocolNode& node);

	long long m_selected = 0;   // the selected control's id — the form's when none other is; none: the element is current
	// The current element when it is no control (SetCurrentElement) — a tree's holder; read through GetCurrentElement.
	ibPropertyObject* m_element = nullptr;

	// A command dropped on a command-bar toolbar joins THAT bar (the id of its control). EVERY drop resolver sets this
	// fresh, so it always reflects the CURRENT drop. CreateCommandButton reads + clears it. See SetPendingDropBar.
	long long m_pendingDropBar = 0;

	// THE FORM AS IT IS EDITED — its node from the tab's answer, each command's after it: what the host draws, and what
	// goes back to the server whole. Never changed in place: a command holds the one before.
	ibProtocolNode m_formNode;
	// Its attributes' and its commands' trees, as the server's last answer about it wrote them (AttributeTree,
	// CommandTree) — a command holds the ones before it with the form.
	ibProtocolNode m_trees;
	// The classes a control may be made of, as the tab's answer had them (Name, Picture, Type, Menu).
	ibProtocolNode m_classes;
	// Each control's rows, by its id — the tab's answer's, and each door's that made one.
	std::map<long long, ibProtocolNode> m_rows;

	ibCommandProcessor m_cmdProc;

	//Elements form
	ibVisualEditorHost* m_visualEditor = nullptr;
	ibVisualEditorObjectTree* m_objectTree = nullptr;
	ibVisualEditorAttributeTree* m_attributeTree = nullptr;
	ibVisualEditorCommandTree* m_commandTree = nullptr;   // command navigator (its own panel)

	//Document & view
	ibModuleEditDocument* m_document;
	// The notebook it is the Designer page of — an event's procedure is its Code page's (ModifyEvent).
	ibVisualEditorNotebook* m_visualNotebook;

	//Splitter for designer
	wxSplitterWindow* m_splitter = nullptr;
	//Splitter between the control tree and the attribute tree
	wxSplitterWindow* m_treeSplitter = nullptr;
	//Splitter between the attribute tree and the command navigator (the right pane's own split)
	wxSplitterWindow* m_rightSplitter = nullptr;

	// One-shot guard: the three splitter sashes are balanced (canvas strip + equal-thirds navigators) on the FIRST
	// real size, then left to gravity — so a later manual drag is not overwritten. See CreateWideGui's size hook.
	bool m_layoutBalanced = false;

	bool m_readOnly = false;

	//access to private object
	friend class ibVisualEditorNotebook;
	friend class ibVisualEditorHost;
	friend class ibVisualEditorObjectTree;
	friend class ibVisualEditorAttributeTree;
	friend class ibVisualEditorCommandTree;
	friend class ::ibFormAttributeValue;
	friend class ::ibFormCommandValue;
	friend class ::ibValueLayerObject;
	friend class ::ibValueCommandBar;
	friend class ::ibValueCommandBarItem;
};

public:

	ibVisualEditorNotebook(ibModuleEditDocument* document, wxWindow* parent, wxWindowID id, long flags) :
		wxAuiNotebook(parent, id, wxDefaultPosition, wxDefaultSize, wxAUI_NB_BOTTOM | wxAUI_NB_TAB_FIXED_WIDTH),
		m_visualEditor(new ibVisualEditor(document, this)), m_codeEditor(new ibCodeEditorDesigner(document, this)) {
		CreateVisualEditor(document, parent, id, flags);
	}

	virtual ~ibVisualEditorNotebook() {
		DestroyVisualEditor();
	}

	void CreateVisualEditor(ibModuleEditDocument* document, wxWindow* parent, wxWindowID id, long flags);
	void DestroyVisualEditor();

	// A control the palette or the Form menu adds — done once the menu is gone, on the Designer page.
	void CreateControl(const wxString& controlName) {
		CallAfter([this, controlName]() {
			m_visualEditor->CreateObject(controlName);
			if (wxAuiNotebook::GetSelection() != wxNOTEBOOK_PAGE_DESIGNER)
				wxAuiNotebook::SetSelection(wxNOTEBOOK_PAGE_DESIGNER);
		});
	}

	// THE DESKTOP'S ModifyEvent — an event given a handler: its procedure in the module renamed, deleted, found or
	// written from the template with the event's parameters, and the Code page shown on it.
	void ModifyEvent(ibPropertySerializable* event, const wxVariant& oldValue, const wxVariant& newValue);

	// The form opened as the runtime shows it (the Form menu's Test form).
	void TestForm() { m_visualEditor->TestForm(); }

	void Copy() {
		if (wxAuiNotebook::GetSelection() == wxNOTEBOOK_PAGE_DESIGNER) {
			if (m_visualEditor->CanCopyObject())
				m_visualEditor->CopyObject(m_visualEditor->GetSelectedObject());
		}
		else if (wxAuiNotebook::GetSelection() == wxNOTEBOOK_PAGE_CODE_EDITOR) {
			m_codeEditor->Copy();
		}
	}

	void Paste() {
		if (wxAuiNotebook::GetSelection() == wxNOTEBOOK_PAGE_DESIGNER) {
			m_visualEditor->PasteObject(m_visualEditor->GetSelectedObject());
		}
		else if (wxAuiNotebook::GetSelection() == wxNOTEBOOK_PAGE_CODE_EDITOR) {
			m_codeEditor->Paste();
		}
	}

	void SelectAll() {
		if (wxAuiNotebook::GetSelection() == wxNOTEBOOK_PAGE_CODE_EDITOR) {
			m_codeEditor->SelectAll();
		}
	}

	bool Undo();
	bool Redo();

	bool CanUndo() const;
	bool CanRedo() const;

	void FindText(const wxString& findString, int wxflags) {
		m_codeEditor->FindText(findString, wxflags);
		if (wxAuiNotebook::GetSelection() != wxNOTEBOOK_PAGE_CODE_EDITOR)
			wxAuiNotebook::SetSelection(wxNOTEBOOK_PAGE_CODE_EDITOR);
	}

	void ShowGotoLine() {
		m_codeEditor->ShowGotoLine();
		if (wxAuiNotebook::GetSelection() != wxNOTEBOOK_PAGE_CODE_EDITOR)
			wxAuiNotebook::SetSelection(wxNOTEBOOK_PAGE_CODE_EDITOR);
	}

	void ShowMethods() {
		m_codeEditor->ShowMethods();
		if (wxAuiNotebook::GetSelection() != wxNOTEBOOK_PAGE_CODE_EDITOR)
			wxAuiNotebook::SetSelection(wxNOTEBOOK_PAGE_CODE_EDITOR);
	}

	void SyntaxControl() {
		if (wxAuiNotebook::GetSelection() != wxNOTEBOOK_PAGE_CODE_EDITOR)
			wxAuiNotebook::SetSelection(wxNOTEBOOK_PAGE_CODE_EDITOR);
		m_codeEditor->SyntaxControl();
	}

	void SetCurrentLine(int line, bool setLine = true) {
		if (wxAuiNotebook::GetSelection() != wxNOTEBOOK_PAGE_CODE_EDITOR)
			wxAuiNotebook::SetSelection(wxNOTEBOOK_PAGE_CODE_EDITOR);
		m_codeEditor->SetCurrentLine(line, setLine);
	}

	// ⭐ ONE ANSWER OPENS BOTH PAGES — the tab's: the form to the Designer page, the module's text to the Code page.
	bool LoadForm(const ibProtocolNode& answer) {
		return m_visualEditor->LoadForm(answer) &&
			m_codeEditor->LoadModule(answer);
	}

	bool IsEditable() const {
		return m_visualEditor->IsEditable() &&
			m_codeEditor->IsEditable();
	}

	ibVisualEditor* GetVisualEditor() const { return m_visualEditor; }
	ibCodeEditor* GetCodeEditor() const { return m_codeEditor; }

	void RefreshEditor() {

		if (m_visualEditor != nullptr)
			m_visualEditor->RefreshEditor();

		if (m_codeEditor != nullptr)
			m_codeEditor->RefreshEditor();
	}

	ibValueForm* GetValueForm() const { return m_visualEditor->GetValueForm(); }
	ibVisualHostClient* GetVisualHost() const { return m_visualEditor->GetVisualEditor(); }

	void ActivateEditor();

protected:
	//A general selection function
	virtual int DoModifySelection(size_t n, bool events) override {
		wxAuiNotebook::Freeze();
		const int selection = wxAuiNotebook::DoModifySelection(n, events);
		wxAuiNotebook::Thaw();
		return selection;
	}
private:

	void OnPageChanged(wxAuiNotebookEvent& event);

	ibVisualEditor* m_visualEditor;
	ibCodeEditorDesigner* m_codeEditor;
};

///////////////////////////////////////////////////////////////////////////////
// ibFormAttributeValue
///////////////////////////////////////////////////////////////////////////////

// THE DESKTOP'S ATTRIBUTE HOLDER, AS ITS TREE AND ITS INSPECTOR HAVE IT (frontend visualView/ctrl/formAttribute.h) — the
// attribute is the server's (frmserver ibFormAttributeValue): its name and whether it is main are its row's, its
// properties the rows the server's inspector writes of it; a value given back is the editor's door, as a control's is
// (ModifyProperty).
class ibFormAttributeValue : public ibPropertyObject {
public:

	// …or the holder of a value table's column of it (`column`: its id) — the desktop's column info, a property object
	// of its own.
	ibFormAttributeValue(ibVisualEditorNotebook::ibVisualEditor* formHandler, long long id, long long column = 0);

	virtual wxString GetClassName() const override { return wxT("FormAttributeValue"); }
	virtual wxString GetObjectTypeName() const override { return m_name; }
	virtual bool IsEditable() const override;

	// What the server says of it, asked as it is shown — the server's editor's Properties, with the form as it stands: the
	// attribute's rows, or its column's.
	virtual bool Receive() override;
	virtual void OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue) override;

	long long GetId() const { return m_attributeId; }
	long long GetColumnId() const { return m_columnId; }
	const wxString& GetName() const { return m_name; }
	bool IsMain() const { return m_main; }
	// What its row adds to the tree's menu (a value table's "Add column": Name Column) — as a class's Menu.
	const ibProtocolNode& GetMenu() const { return m_menu; }

	// Its row of the tree (Name, Main, Menu).
	void ReadRow(const ibProtocolNode& row);

private:

	ibVisualEditorNotebook::ibVisualEditor* m_formHandler;
	const long long m_attributeId;
	const long long m_columnId;
	wxString m_name;
	bool m_main = false;
	ibProtocolNode m_menu;
};

///////////////////////////////////////////////////////////////////////////////
// ibFormCommandValue
///////////////////////////////////////////////////////////////////////////////

// THE DESKTOP'S FORM COMMAND, AS ITS TREE AND ITS INSPECTOR HAVE IT (frontend visualView/ctrl/formCommand.h) — the
// command is the server's (frmserver ibFormCommandValue): its name its row's, its properties the rows the server's
// inspector writes of it; a value given back is the editor's door, as an attribute's is.
class ibFormCommandValue : public ibPropertyObject {
public:

	ibFormCommandValue(ibVisualEditorNotebook::ibVisualEditor* formHandler, long long id);

	virtual wxString GetClassName() const override { return wxT("FormCommandValue"); }
	virtual wxString GetObjectTypeName() const override { return m_name; }
	virtual bool IsEditable() const override;

	// What the server says of it, asked as it is shown — as an attribute's is.
	virtual bool Receive() override;
	virtual void OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue) override;

	long long GetId() const { return m_commandId; }
	const wxString& GetName() const { return m_name; }

	// Its row of the tree (Name).
	void ReadRow(const ibProtocolNode& row);

private:

	ibVisualEditorNotebook::ibVisualEditor* m_formHandler;
	const long long m_commandId;
	wxString m_name;
};

///////////////////////////////////////////////////////////////////////////////
// ibValueLayerObject
///////////////////////////////////////////////////////////////////////////////

// THE DESKTOP'S LAYER (frontend visualView/layers/commandBar.h), AS ITS TREE AND ITS INSPECTOR HAVE IT — what a control
// carries beside its children (Layers): its properties the rows the server wrote of it with the control's (Controls),
// its values its node in the control's. ⭐ AN EDIT IS THE CLIENT'S, of that node (the desktop's layer changed itself):
// the form so edited is drawn again by the server — what the bar shows is its to compose.
class ibValueLayerObject : public ibPropertyObject {
public:

	ibValueLayerObject(ibVisualEditorNotebook::ibVisualEditor* formHandler, long long controlId);

	virtual bool IsEditable() const override;

	// What it holds, received as it is shown: its rows as the last answer brought them with its control's (Controls), in
	// place of what it had — a command's presentations (what its command is called, its picture) are its own and the
	// server's to say — and its values as its node has them now.
	virtual bool Receive() override;
	virtual void OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue) override;

	// Its node in the control's node — none: an empty node.
	virtual ibProtocolNode FindNode(const ibProtocolNode& control) const = 0;
	// …and its rows in the control's rows (Controls) — none: an empty node.
	virtual ibProtocolNode FindRows(const ibProtocolNode& control) const = 0;

	// THE DESKTOP'S LAYER MENU — what it fills the tree's popup with, and what a choice does.
	virtual void PrepareDefaultMenu(wxMenu* menu) const = 0;
	virtual void ExecuteMenu(int menuId) = 0;

	// Open in the tree (the desktop's) — a command always; the bar as it was left.
	virtual bool IsTreeExpanded() const { return true; }
	virtual void SetTreeExpanded(bool value) {}

	long long GetControlID() const { return m_controlId; }
	// Its node in the form as it is drawn — none: an empty node.
	ibProtocolNode GetNode() const;

protected:

	// The form as it stands with the command bar of this control given those commands — drawn by the server, a command
	// the editor's; then the one at `selected` shown (wxNOT_FOUND: none).
	void SetCommandItems(const std::vector<ibProtocolNode>& items, int selected) const;

	ibVisualEditorNotebook::ibVisualEditor* m_formHandler;
	const long long m_controlId;
};

// The control's command bar — the desktop's "Command interface" node: its AutoFill, and its commands under it.
class ibValueCommandBar : public ibValueLayerObject {
public:

	ibValueCommandBar(ibVisualEditorNotebook::ibVisualEditor* formHandler, long long controlId) :
		ibValueLayerObject(formHandler, controlId) {
	}

	virtual wxString GetClassName() const override { return ibProtocolName::CommandBar; }
	virtual wxString GetObjectTypeName() const override { return ibProtocolName::CommandBar; }

	virtual ibProtocolNode FindNode(const ibProtocolNode& control) const override;
	virtual ibProtocolNode FindRows(const ibProtocolNode& control) const override;

	virtual void PrepareDefaultMenu(wxMenu* menu) const override;
	virtual void ExecuteMenu(int menuId) override;

	virtual bool IsTreeExpanded() const override { return m_treeExpanded; }
	virtual void SetTreeExpanded(bool value) override { m_treeExpanded = value; }

private:

	bool m_treeExpanded = true;
};

// A command of the bar — by its place among the bar's (its node's NodeChildren: the bar reads them in order).
class ibValueCommandBarItem : public ibValueLayerObject {
public:

	ibValueCommandBarItem(ibVisualEditorNotebook::ibVisualEditor* formHandler, long long controlId, unsigned int index) :
		ibValueLayerObject(formHandler, controlId), m_index(index) {
	}

	virtual wxString GetClassName() const override { return ibProtocolName::CommandBarItem; }
	virtual wxString GetObjectTypeName() const override { return ibProtocolName::CommandBarItem; }

	// Its name stays unique among the bar's commands (the desktop's).
	virtual bool OnPropertyChanging(ibProperty* property, const wxVariant& newValue) override;

	virtual ibProtocolNode FindNode(const ibProtocolNode& control) const override;
	// …a command's own, by its name, among its bar's (Items) — none such: a new command's (CommandBarItem).
	virtual ibProtocolNode FindRows(const ibProtocolNode& control) const override;

	virtual void PrepareDefaultMenu(wxMenu* menu) const override;
	virtual void ExecuteMenu(int menuId) override;

	unsigned int GetIndex() const { return m_index; }

private:

	const unsigned int m_index;
};

///////////////////////////////////////////////////////////////////////////////
// ibVisualDesignerCommandProcessor
///////////////////////////////////////////////////////////////////////////////

class ibVisualDesignerCommandProcessor : public wxCommandProcessor {
public:

	ibVisualDesignerCommandProcessor(ibVisualEditorNotebook* visualNotebook) : wxCommandProcessor(),
		m_visualNotebook(visualNotebook) {
	}

	virtual bool Undo() override { return m_visualNotebook->Undo(); }
	virtual bool Redo() override { return m_visualNotebook->Redo(); }

	virtual bool CanUndo() const override { return m_visualNotebook->CanUndo(); }
	virtual bool CanRedo() const override { return m_visualNotebook->CanRedo(); }

private:

	ibVisualEditorNotebook* m_visualNotebook;
};

#endif
