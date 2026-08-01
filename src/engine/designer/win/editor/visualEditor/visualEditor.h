#ifndef __VISUAL_EDITOR_H__
#define __VISUAL_EDITOR_H__

#include "innerFrame.h"
#include "win/editor/codeEditor/codeEditorDesigner.h"

#include "frontend/visualView/ctrl/form.h"
#include "frontend/visualView/ctrl/sizer.h"
#include "frontend/visualView/ctrl/widgets.h"

#include "frontend/visualView/visualHost.h"

#include "backend/sourceDescription.h"   // ibSourceHop — the attribute-tree node path carries the pinned reference type per hop
#include "backend/commandDescription.h"  // ibCommandDescription — the command-tree node carries the command-hop path (symmetric to the source path)

#include <wx/dnd.h>
#include <functional>
#include <vector>
#include <memory>

class ibPropertyObject;        // common base of every inspector-selectable element (control / attribute / command)
class ibValueLayerObject;      // common base for a layer node (the bar OR one command)
class ibValueCommandBar;       // command-interface layer — tree-node payload
class ibValueCommandBarItem;   // one child command — tree-node payload
struct ibSourceDescription;    // "oes_source_drag" drop payload — a binding source path (raw ids)
class ibFormDragItem;          // one polymorphic draggable KIND — format + (de)serialize + apply (visualEditorDragItem.h)

// wxDropTarget for a node dragged from a form-editor tree. Shared by the form CANVAS, the OBJECT TREE and each
// tablebox grid. It holds the registered draggable KINDS (ibFormDragItem — a source path, a command); OnData
// matches the RECEIVED format to a kind, decodes it and enacts the drop. NO per-format branch — adding a draggable
// is a new ibFormDragItem subclass registered in the ctor, nothing here changes. Each surface supplies only a
// RESOLVER (drop point → the frame to act on, in that surface's coordinates) and an APPLIER (runs the decoded
// item's ApplyDrop with the surface's editor). `deferred` runs the apply through CallAfter — for a tablebox grid
// whose own target re-wires (frees) itself when the drop rebuilds the editor.
class ibSourceDragDropTarget : public wxDropTarget {
public:
	using PointResolver = std::function<ibValueFrame*(wxCoord, wxCoord)>;                  // drop point → target frame (may be null)
	using DropApply     = std::function<void(const ibFormDragItem&, ibValueFrame*)>;       // enact a decoded item on a frame
	ibSourceDragDropTarget(PointResolver resolver, DropApply apply, bool deferred = false);
	~ibSourceDragDropTarget() override;   // out-of-line: unique_ptr<ibFormDragItem> (forward-declared) destroyed here
	wxDragResult OnData(wxCoord x, wxCoord y, wxDragResult def) override;
private:
	PointResolver m_resolver;
	DropApply     m_apply;
	bool          m_deferred;
	std::vector<std::unique_ptr<ibFormDragItem>> m_items;   // registered kinds (source, command)
	std::vector<wxCustomDataObject*>             m_data;    // composite sub-objects, index-parallel to m_items
};

//////////////////////////////////////////////////////////////////////////////////////////

#define wxNOTEBOOK_PAGE_DESIGNER 0
#define wxNOTEBOOK_PAGE_CODE_EDITOR 1

///////////////////////////////////////////////////////////////////////////////
// ibDesignerWindow - Extends the ibInnerFrame to show the object highlight
///////////////////////////////////////////////////////////////////////////////

class ibDesignerWindow : public ibInnerFrame {
	wxDECLARE_CLASS(ibDesignerWindow);
private:

	void DrawRectangle(wxDC& dc, const wxPoint& point, const wxSize& size, ibValueFrame* object);

public:

	// Augh!, this class is needed to paint the highlight in the
	// frame content panel.
	class ibHighlightPaintHandler : public wxEvtHandler {
		wxDECLARE_EVENT_TABLE();
	private:
		wxWindow* m_dsgnWin;
	public:
		ibHighlightPaintHandler(wxWindow* win);
		void OnPaint(wxPaintEvent& event);
	};

public:

	ibDesignerWindow(wxWindow* parent, int id, const wxPoint& pos, const wxSize& size = wxDefaultSize,
		long style = 0, const wxString& name = wxT("designer_win"));
	virtual ~ibDesignerWindow();

	void SetGrid(int x, int y);
	void SetSelectedSizer(wxSizer* sizer) { m_selSizer = sizer; }
	void SetSelectedItem(wxObject* item) { m_selItem = item; }
	void SetSelectedObject(ibValueFrame* object) { m_selObj = object; }
	void SetSelectedPanel(wxWindow* actPanel) { m_actPanel = actPanel; }

	wxSizer* GetSelectedSizer() const { return m_selSizer; }
	wxObject* GetSelectedItem() const { return m_selItem; }
	ibValueFrame* GetSelectedObject() const { return m_selObj; }
	wxWindow* GetActivePanel() const { return m_actPanel; }

	void HighlightSelection(wxDC& dc);
	void OnPaint(wxPaintEvent& event);

private:

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

class ibVisualEditorNotebook : public ibFrontendVisualEditorNotebook, public wxAuiNotebook {

public: class ibVisualEditor :
	public wxPanel {

	class ibCommandProcessor {
	public:

		ibCommandProcessor() : m_savePoint(0) {}

		~ibCommandProcessor() {
			while (!m_redoStack.empty()) {
				wxDELETE(m_redoStack.top());
				m_redoStack.pop();
			}
			while (!m_undoStack.empty()) {
				wxDELETE(m_undoStack.top());
				m_undoStack.pop();
			}
		}

		void Execute(ibVisualEditorCmd* command) {
			command->Execute();
			m_undoStack.push(command);
			while (!m_redoStack.empty()) {
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

		void Reset() {
			while (!m_redoStack.empty())
				m_redoStack.pop();
			while (!m_undoStack.empty())
				m_undoStack.pop();
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

		bool IsAtSavePoint() {
			return m_savePoint == m_undoStack.size();
		}

	private:
		std::stack<ibVisualEditorCmd*> m_undoStack, m_redoStack;
		unsigned int m_savePoint;
	};

public:

	class ibVisualEditorHost : public ibVisualHost {
	public:

		ibVisualEditorHost(ibVisualEditor* handler, wxWindow* parent, wxWindowID id = wxID_ANY);
		virtual ~ibVisualEditorHost() override;

		//*********************************************************
		//*                 Events for visual                     *
		//*********************************************************

		/**
		* Create an instance of the wxObject and return a pointer
		*/
		virtual wxObject* Create(ibValueFrame* control, wxWindow* wxparent);

		/**
		* Allows components to do something after they have been created.
		* For example, Abstract components like NotebookPage and SizerItem can
		* add the actual widget to the Notebook or sizer.
		*
		* @param wxobject The object which was just created.
		* @param wxparent The wxWidgets parent - the wxObject that the created object was added to.
		*/
		virtual void OnCreated(ibValueFrame* control, wxObject* obj, wxWindow* wxparent, bool firstCreated = false);

		/**
		* Allows components to do something after they have been updated.
		*/
		virtual void Update(ibValueFrame* control, wxObject* obj);

		/**
		* Allows components to do something after they have been updated.
		* For example, Abstract components like NotebookPage and SizerItem can
		* add the actual widget to the Notebook or sizer.
		*
		* @param wxobject The object which was just updated.
		* @param wxparent The wxWidgets parent - the wxObject that the updated object was added to.
		*/
		virtual void OnUpdated(ibValueFrame* control, wxObject* obj, wxWindow* wndParent);

		/**
		 * Cleanup (do the reverse of Create)
		 */
		virtual void Cleanup(ibValueFrame* control, wxObject* obj);

		/////////////////////////////////////////////////////////////////////////////////////////

		//override designer host  
		virtual bool IsDesignerHost() const { return true; }
		virtual bool IsShownHost() const { return ibVisualHost::IsShown(); }

		virtual ibValueForm* GetValueForm() const;
		virtual void SetValueForm(ibValueForm* valueForm);

		virtual wxWindow* GetParentBackgroundWindow() const { return m_back; }
		virtual wxWindow* GetBackgroundWindow() const { return m_back->GetFrameContentPanel(); }

		/////////////////////////////////////////////////////////////////////////////////////////

		void OnResizeBackPanel(wxCommandEvent& event);
		void OnClickBackPanel(wxMouseEvent& event);
		void PreventOnSelected(bool prevent = true);
		void PreventOnModified(bool prevent = true);

		bool OnLeftClickFromApp(wxWindow* currentWindow);
		bool OnRightClickFromApp(wxWindow* currentWindow, wxMouseEvent& event);

		virtual void OnClickFromApp(wxWindow* currentWindow, wxMouseEvent& event);

		//set and create window
		void SetObjectSelect(ibValueFrame* obj);
		void ClearObjectSelect();   // drop the canvas control highlight (a non-control element is now selected)
		void ScrollToObject(ibValueFrame* obj);

	protected:

		virtual void SetCaption(const wxString& strCaption);
		virtual void UpdateHostSize();

		friend class ibVisualEditor;
		friend class ibVisualEditorObjectTree;

		friend class ibVisualEditorExpandObjectCmd;
		friend class ibVisualEditorInsertObjectCmd;
		friend class ibVisualEditorRemoveObjectCmd;
		friend class ibVisualEditorModifyPropertyCmd;
		friend class ibVisualEditorModifyEventCmd;
		friend class ibVisualEditorShiftChildCmd;
		friend class ibVisualEditorCutObjectCmd;

		//designer 
		ibDesignerWindow* m_back;
		//form handler
		ibVisualEditor* m_formHandler;
		// Prevent OnSelected in components
		bool m_stopSelectedEvent;
		// Prevent OnModified in components
		bool m_stopModifiedEvent;

		wxDECLARE_EVENT_TABLE();
	};

	//////////////////////////////////////////////////////////////////////////////////////////

	class ibVisualEditorObjectTree : public wxPanel {

		/**
		 * Builds the tree completely.
		 */
		void CreateTree();
		void RebuildTree();
		void AddChildren(ibValueFrame* child, const wxTreeItemId& parent, bool is_root = false);

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
	public:
		// Select (and scroll to) a command-bar child after a rebuild — looks it up in
		// m_layerItemMap. No-op if the item isn't in the current tree (e.g. after delete).
		// Public: the notebook's SelectPropertyObject calls it to reveal on a toolbar click.
		void SelectCommandItem(ibValueCommandBarItem* citem);
	private:
		// Decode a tree item's payload and select it: a layer node (bar/command) → the inspector;
		// a control node → the visual editor. Shared by click-reselect and selection-changed.
		void SelectItemData(wxTreeItemData* item_data);
		void AddItem(ibValueFrame* item, ibValueFrame* parent);
		void RemoveItem(ibValueFrame* item) {
			// remove affected object tree items only
			std::map< ibValueFrame*, wxTreeItemId>::iterator it = m_listItem.find(item);
			if ((it != m_listItem.end()) && it->second.IsOk())
			{
				m_tcObjects->Delete(it->second);
				// clear map records for all item's children
				ClearMap(it->first);
			}
		}

		void ClearMap(ibValueFrame* obj) {
			m_listItem.erase(obj);
			for (unsigned int i = 0; i < obj->GetChildCount(); i++) {
				ClearMap(obj->GetChild(i));
			}
		}

		ibValueFrame* GetObjectFromTreeItem(const wxTreeItemId& item) {
			if (item.IsOk()) {
				wxTreeItemData* item_data = m_tcObjects->GetItemData(item);
				if (item_data) {
					ibValueFrame* obj(((ibVisualEditorObjectTreeItemData*)item_data)->GetObject());
					return obj;
				}
			}

			return nullptr;
		}

	public:

		void OnEditorLoaded();
		void OnEditorRefresh();

		void OnObjectCreated(ibValueFrame* obj);
		void OnObjectSelected(ibValueFrame* obj);
		void OnObjectExpanded(ibValueFrame* obj);
		void OnObjectRemoved(ibValueFrame* obj);

		void OnPropertyModified(ibProperty* prop);

		ibVisualEditorObjectTree(ibVisualEditor* owner, wxWindow* parent, int id = wxID_ANY);
		virtual ~ibVisualEditorObjectTree() override {}

	protected:

		void OnSelChanged(wxTreeEvent& event);
		void OnRightClick(wxTreeEvent& event);
		void OnBeginDrag(wxTreeEvent& event);
		void OnEndDrag(wxTreeEvent& event);
		void OnExpansionChange(wxTreeEvent& event);
		void OnKeyDown(wxTreeEvent& event);

	private:

		ibVisualEditor* m_formHandler = nullptr;

		wxImageList* m_iconList = nullptr;

		std::map<ibValueFrame*, wxTreeItemId> m_listItem;
		// Parallel map for LAYER nodes — the command bar itself AND its command children. They are not
		// ibValueFrame, so they can't live in m_listItem; used to re-select a bar / command by identity
		// after a rebuild (a deferred RefreshEditor would otherwise drop the highlight onto the root form).
		std::map<ibValueLayerObject*, wxTreeItemId> m_layerItemMap;
		std::map<wxString, int> m_iconIdx;

		wxTreeCtrl* m_tcObjects = nullptr;
		wxTreeItemId m_draggedItem;

		bool m_altKeyIsDown, m_notifySelecting;

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
		// A LAYER node — the command-interface (the bar) OR one command (an item). Both are
		// ibValueLayerObject, so the tree keeps ONE pointer and never branches per kind:
		// selection, context menu and expand-state are all virtual on the base.
		ibVisualEditorObjectTreeItemData(ibValueLayerObject* layer) : m_layerObject(layer) {}
		ibValueFrame* GetObject() { return m_object; }
		ibValueLayerObject* GetLayerObject() { return m_layerObject; }
	private:
		ibValueFrame* m_object = nullptr;
		ibValueLayerObject* m_layerObject = nullptr;
	};

	//////////////////////////////////////////////////////////////////////////////////////////
	// ibVisualEditorAttributeTree — the form's source-attribute tree (sibling of the control
	// tree, separated by a splitter). Lists the form's attribute store; all actions (add /
	// edit / delete / set-main / properties / copy / paste) live on a context menu with icons;
	// tree items carry the default meta-attribute icon. Selection drives the object inspector.
	//////////////////////////////////////////////////////////////////////////////////////////

	class ibVisualEditorAttributeTree : public wxPanel {
	public:

		ibVisualEditorAttributeTree(ibVisualEditor* owner, wxWindow* parent, int id = wxID_ANY);
		virtual ~ibVisualEditorAttributeTree() override {}

		void OnEditorLoaded() { RebuildTree(); }
		void OnEditorRefresh() { RebuildTree(); }
		void RebuildTree();
		// Reactive update on a property change (driven by NotifyPropertyModified, like the control tree): a Type/
		// source edit changes an attribute's [+] composition picker. DEFERRED (CallAfter) so it runs once the value
		// has finished materialising — an inline rebuild still sees the pre-change state.
		void OnPropertyModified(ibProperty* prop);

	protected:

		void OnSelChanged(wxTreeEvent& event);
		void OnContextMenu(wxContextMenuEvent& event);        // right-click anywhere (incl. empty)
		void OnActivated(wxTreeEvent& event);                 // double-click → activate properties
		void OnItemExpanding(wxTreeEvent& event);             // lazy: build a node's composition on first expand
		void OnBeginDrag(wxTreeEvent& event);                 // drag a node onto the form -> create + bind a control
		void OnAdd(wxCommandEvent& event);
		void OnAddColumn(wxCommandEvent& event);              // add a column to a value-table attribute
		void OnEdit(wxCommandEvent& event);                   // inline rename (EditLabel)
		void OnRemove(wxCommandEvent& event);
		void OnSetMain(wxCommandEvent& event);
		void OnProperties(wxCommandEvent& event);             // show inspector
		void OnCopy(wxCommandEvent& event);                   // clipboard copy (serialized)
		void OnCut(wxCommandEvent& event);                    // clipboard copy + remove (undoable)
		void OnPaste(wxCommandEvent& event);                  // clipboard paste (deserialized)
		void OnEndLabelEdit(wxTreeEvent& event);              // apply / veto a rename (uniqueness)

		// The HOLDER (facade) the tree item carries — what the inspector selects (it owns the
		// attribute + the generated value and intercepts their property changes).
		class ibFormAttributeValue* GetEntryFromItem(const wxTreeItemId& item) const;

		// Select the tree item carrying this attribute holder (nullptr = none), BY IDENTITY — the
		// form owns the holders in m_attributes and RebuildTree recreates only the tree items, so a
		// survivor's pointer is stable. Mirrors the object tree keying m_listItem by ibValueFrame*.
		// Used after an add to land on the new attribute and by RebuildTree to keep the selection.
		void SelectEntry(class ibFormAttributeValue* entry);

		// The single decode-and-select point — surfaces an attribute holder in the object inspector.
		// Shared by the click-reselect, selection-changed, activate and menu paths, the way the
		// object tree funnels every path through SelectItemData.
		void SelectInInspector(class ibFormAttributeValue* entry);

		// A composition node UNDER a value-table attribute carries no holder — resolve its column-info
		// (which IS an editable property object: Name + Type) from the binding path [attrId, columnId]
		// and surface THAT in the inspector. Returns true when it handled the item (the caller then
		// skips the holder path); false leaves the current attribute-holder behaviour untouched.
		bool SelectColumnInInspector(const wxTreeItemId& item);

	private:

		ibVisualEditor* m_formHandler = nullptr;
		wxTreeCtrl* m_tcAttributes = nullptr;
		// True while RebuildTree tears the tree down and re-appends — suppresses OnSelChanged so a
		// selection-changed event fired mid-rebuild never touches a stale/freed holder (the object
		// tree guards the same way with m_notifySelecting).
		bool m_rebuilding = false;

		wxDECLARE_EVENT_TABLE();
	};

	class ibVisualEditorAttributeTreeItemData : public wxTreeItemData {
	public:
		// A ROOT node carries the attribute HOLDER (facade) — the selectable; the attribute is reached
		// via it. A COMPOSITION node (a field / table section / reference target built lazily on expand)
		// passes entry == nullptr and carries the binding PATH (head = attribute id) + view flags. A
		// non-empty refTypes list means "expand me through a reference-as-source" (the design-time value
		// hop); m_loaded guards the one-time build so references stay lazy — no cyclic-reference blow-up.
		ibVisualEditorAttributeTreeItemData(class ibFormAttributeValue* entry, const std::vector<ibSourceHop>& path,
			const std::vector<ibMetaID>& refTypes = {}, bool tableSection = false, ibClassID dropControl = 0,
			bool underList = false)
			: m_entry(entry), m_path(path), m_refTypes(refTypes), m_tableSection(tableSection), m_dropControl(dropControl),
			m_underList(underList) {}
		// A COMMAND node of the navigator: carries a draggable command projection as a HOP PATH
		// (ibCommandDescription) — SYMMETRIC to a source node carrying its binding path. A command is a 1-hop
		// [metaId]; a table command a 2-hop [source, action]. Dragged onto the form → a bound button.
		explicit ibVisualEditorAttributeTreeItemData(const ibCommandDescription& commandDesc)
			: m_commandDesc(commandDesc) {}
		const ibCommandDescription& GetCommandDesc() const { return m_commandDesc; }
		ibFormAttributeValue* GetEntry() const { return m_entry; }
		const std::vector<ibSourceHop>& GetPath() const { return m_path; }
		const std::vector<ibMetaID>& GetRefTypes() const { return m_refTypes; }
		// Child's prefix when descending THROUGH this node into a reference TARGET: this node's OWN hop takes the
		// pinned branch type, so the drag / serialized path carries it (a composite reference resolves the branch).
		std::vector<ibSourceHop> ChildPrefix(const ibClassID& hopType) const {
			std::vector<ibSourceHop> child = m_path;
			if (!child.empty()) child.back().m_type = hopType;
			return child;
		}
		bool HasRef() const { return !m_refTypes.empty(); }
		bool IsTableSection() const { return m_tableSection; }
		bool IsLoaded() const { return m_loaded; }
		void SetLoaded() { m_loaded = true; }
		// The control-class CLSID this node maps to (0 = not draggable — an object container with no single
		// control; drag its fields instead). Computed at build time from the node's view + type. Used only as
		// the draggable gate now: the drag payload carries the PATH, and the drop side resolves the class.
		ibClassID GetDropControl() const { return m_dropControl; }
		// This node is INSIDE a list / table section → its own fields are COLUMNS, not standalone form
		// sources; a reference under it propagates the flag so its dot-walk fields stay non-draggable.
		bool IsUnderList() const { return m_underList; }
	private:
		ibFormAttributeValue* m_entry = nullptr;    // holder (root) or nullptr (composition node)
		std::vector<ibSourceHop> m_path;            // hop path from the attribute (head = attribute id); reference hops carry the pinned branch type
		std::vector<ibMetaID> m_refTypes;           // reference TARGET metaIDs (empty = not a reference)
		bool m_tableSection = false;                // view flag: this node is a table / section
		bool m_loaded = false;                      // composition already built? (lazy one-shot)
		ibClassID m_dropControl = 0;                // control-class CLSID this node maps to (0 = not draggable)
		bool m_underList = false;                   // inside a list/section (fields are columns, not sources)
		ibCommandDescription m_commandDesc;         // navigator command node: the command-hop path (empty = not a command)
	};

	// The COMMAND NAVIGATOR — its OWN panel beside the attribute tree (the "command door" parallel of the
	// attribute/source tree). It lists the commands available to THIS form (general + the object's own) by
	// group and is the surface a designer DRAGS from: a command dragged onto the form → a bound command
	// button, resolved live at click. A context menu ADDS a new command right here (a command under the
	// form's object, or a general one) so a designer needn't leave the form to make one — then drag it out.
	// Nodes carry the command by metaId (ibVisualEditorAttributeTreeItemData).
	class ibVisualEditorCommandTree : public wxPanel {
	public:

		ibVisualEditorCommandTree(ibVisualEditor* owner, wxWindow* parent, int id = wxID_ANY);
		virtual ~ibVisualEditorCommandTree() override {}

		void OnEditorLoaded() { RebuildTree(); }
		void OnEditorRefresh() { RebuildTree(); }
		// A command property edit (caption / picture / Action) must re-gather so the navigator's labels + icons
		// track live — the SAME hook name the object / attribute trees use for a property change.
		void OnPropertyModified(class ibProperty* prop) { RebuildTree(); }
		void RebuildTree();

	protected:

		void OnBeginDrag(wxTreeEvent& event);      // drag a command onto the form -> a bound command button
		void OnContextMenu(wxContextMenuEvent& event);   // right-click IN THE FORM COMMANDS GROUP -> add / copy / paste / delete
		void OnSelChanged(wxTreeEvent& event);     // selection CHANGED -> show the form command in the inspector
		void RevealSelectedCommand();              // surface the selected form command in the inspector (both click paths)
		void AddCommand(bool general);             // create a form-local command (property object) + reveal in the inspector
		void CopyCommand();                        // copy the selected form command to the clipboard (oes_clipboard_command)
		void CutCommand();                         // copy + delete the selected form command (undoable, own buffer)
		void PasteCommand();                       // paste a clipboard form command as a fresh entry on the form
		void DeleteCommand();                      // delete the selected form command from the form
		class ibFormCommandValue* SelectedFormCommand() const;   // the selected node's FORM COMMAND, else null
		void SelectCommand(class ibFormCommandValue* cmd);       // land the row on a form command BY IDENTITY (like the attribute tree's SelectEntry)

	private:

		ibVisualEditor* m_formHandler = nullptr;
		wxTreeCtrl* m_tcCommands = nullptr;
		wxTreeItemId m_formCommandsGroup;          // the "Form commands" section node — the ONLY place the menu shows
		bool m_rebuilding = false;                 // suppress OnSelChanged while RebuildTree churns items (same guard as the attribute tree)

		wxDECLARE_EVENT_TABLE();
	};

	/**
	 * Popup menu associated with each tree item.
	 *
	 * This object executes the menu commands related to the selected object.
	 */
	class ibVisualEditorItemPopupMenu : public wxMenu {
	public:

		bool HasDeleteObject();
		int GetSelectedID() const { return m_selID; }

		ibVisualEditorItemPopupMenu(ibVisualEditor* owner, wxWindow* parent, ibValueFrame* obj);

		void OnUpdateEvent(wxUpdateUIEvent& e);
		void OnMenuEvent(wxCommandEvent& event);

	protected:

		ibValueFrame* m_object = nullptr;
		ibVisualEditor* m_formHandler = nullptr;
		int m_selID;

		wxDECLARE_EVENT_TABLE();
	};

public:

	ibVisualEditorHost* GetVisualEditor() const { return m_visualEditor; }
	ibVisualEditorObjectTree* GetObjectTree() const { return m_objectTree; }

	ibValueForm* GetValueForm() const { return m_valueForm; }
	void SetValueForm(ibValueForm* valueForm) { m_valueForm = valueForm; }

	ibMetaDocument* GetEditorDocument() const { return m_document; }

	// View-only follows the FORM (its metaObject's IsEditable — a config opened read-only from a DB / file):
	// the ONE gate every editor surface reads (object tree move/delete/paste/drag, command tree add, attribute
	// tree). No editor-local flag — the form is the single source of truth. Null form (mid-construction) = editable.
	bool IsEditable() const { return m_valueForm == nullptr || m_valueForm->IsEditable(); }
	void SetReadOnly(bool readOnly = true) {}

	void ActivateEditor();

protected:

	// Notify every observer of the corresponding event
	void NotifyEditorLoaded();
	void NotifyEditorSaved();

	void NotifyEditorRefresh();

	void NotifyObjectCreated(ibValueFrame* obj);
	void NotifyObjectSelected(ibValueFrame* obj, bool force = false);
	void NotifyObjectExpanded(ibValueFrame* obj);
	void NotifyObjectRemoved(ibValueFrame* obj);

	void NotifyPropertyModified(ibProperty* prop);
	void NotifyEventModified(ibEvent* event);

	//Execute command 
	void Execute(ibVisualEditorCmd* cmd);

	/**
	* Search a size in the hierarchy of an object
	*/
	void PropagateExpansion(ibValueFrame* obj, bool expand, bool up);

	/**
	* Remove an object.
	*/
	void DoRemoveObject(ibValueFrame* object, bool cutObject, bool force = false);

public:

	ibVisualEditor();
	ibVisualEditor(ibMetaDocument* document, wxWindow* parent, int id = wxID_ANY);
	virtual ~ibVisualEditor();

	//Objects
	ibValueFrame* CreateObject(const wxString& name);
	// Like CreateObject, but under an explicit parent (walking up if it can't hold the control) and bound to
	// a source path — the drop counterpart of the palette's CreateObject. The control CLASS is resolved from
	// the type AT the path (the drop side owns the choice). Returns the created control (unwrapped) or nullptr.
	ibValueFrame* CreateBoundControl(ibValueFrame* parentHint, const ibSourceDescription& desc);
	// The shared create-a-control-on-the-form flow behind CreateBoundControl and CreateCommandButton: climb to a
	// parent that can hold `className`, create + unwrap it, let `bind` set its binding BEFORE the widget builds
	// (so it renders already bound), then insert (undoable) + notify + refill-if-source + select. Returns the
	// unwrapped control or nullptr. The ONLY thing that varies between droppers is the class + the bind step.
	ibValueFrame* CreateControlOfClass(ibValueFrame* parentHint, const wxString& className,
		const std::function<void(ibValueFrame*)>& bind);
	// A command dragged from the navigator, dropped onto the form → add a bound command projection. It lands
	// on the NEAREST command bar climbing up from the hit-tested target: a tablebox / list node → its OWN
	// command (a table / list command); the form / a plain control → the form's command bar (a button).
	// The dragged command-hop path (a command 1-hop, or a table's [source, action]) is bound onto the button.
	void CreateCommandButton(ibValueFrame* target, const ibCommandDescription& desc);
	// Set by the drop resolver right before ApplyDrop when the drop landed on a command-bar TOOLBAR (form or
	// tablebox): the command joins THAT bar as a new item instead of spawning a free button. CreateCommandButton
	// reads then clears it; null for any other drop. A per-drop side channel (the resolver returns only an
	// ibValueFrame*, and a command bar is a layer, not a frame).
	void SetPendingDropBar(ibValueCommandBar* bar) { m_pendingDropBar = bar; }
	// (Re)attach a drop target to every tablebox's own grid — a native grid swallows the OS drop before the
	// form-canvas target sees it, so drops over the rows need the grid's own target. Called on editor refresh.
	void WireTableboxDrops(ibValueFrame* obj);
	void RemoveObject(ibValueFrame* obj);
	void CutObject(ibValueFrame* obj, bool force = false);
	void CopyObject(ibValueFrame* obj);
	bool PasteObject(ibValueFrame* parent);
	void InsertObject(ibValueFrame* obj, ibValueFrame* parent);
	void ExpandObject(ibValueFrame* obj, bool expand);

	void ModifyProperty(ibProperty* prop, const wxVariant& oldValue, const wxVariant& newValue);
	void ModifyEvent(ibEvent* evt, const wxVariant& oldValue, const wxVariant& newValue);

	// Add / delete a form attribute through the command processor (undoable) — the holder is built
	// UNOWNED via ibValueForm::MakeAttribute; the command attaches / detaches it on the form.
	void InsertAttribute(ibValuePtr<ibFormAttributeValue> holder);
	void RemoveAttribute(ibFormAttributeValue* entry);

	void CreateWideGui();

	void DetermineObjectToSelect(ibValueFrame* parent, unsigned int pos);

	// Object will not be selected if it already is selected, unless force = true
	// Returns true if selection changed, false if already selected
	bool SelectObject(ibValueFrame* obj, bool force = false, bool notify = true);

	void MovePosition(ibValueFrame* obj, unsigned int toPos);
	void MovePosition(ibValueFrame* obj, bool right, unsigned int num = 1);

	void ScrollToObject(ibValueFrame* obj);

	// The editor's ONE selection, SELF-DESCRIBING. Built FROM the concrete type — a control, an attribute or a
	// command (any other inspector property via the generic ctor) — so the KIND is captured BY CONSTRUCTION: you
	// cannot store the wrong kind, and it is never re-derived by RTTI. AsFrame STATIC-casts back only when the
	// selection IS a control (the kind guarantees the layout), else null. One value = pointer + kind, set together.
	class ibEditorSelection {
	public:
		ibEditorSelection() = default;
		ibEditorSelection(class ibValueFrame* frame);              // a control  -> Frame
		ibEditorSelection(class ibFormAttributeValue* attribute);  // an attribute
		ibEditorSelection(class ibFormCommandValue* command);      // a command
		ibEditorSelection(ibPropertyObject* other);                // any other inspector property (e.g. a value-table column)
		ibPropertyObject* GetObject() const { return m_object; }   // the element for the inspector (any kind)
		// The three typed views — each STATIC-casts only when the kind matches, else null ("wrong type -> null").
		ibValueFrame*               AsFrame()     const;
		class ibFormAttributeValue* AsAttribute() const;
		class ibFormCommandValue*   AsCommand()   const;
		// LIGHT the inspector on THIS selection — THE single place a selection ever reaches objectInspector. forceOpen
		// raises a hidden inspector (explicit "Properties" / a live-surface click); otherwise fill only an already-open
		// inspector — a passive select auto-activates the property ONLY while the inspector is up, never pops it open.
		// Out-of-line (uses objectInspector). Callers go through SetCurrentElement, not here directly.
		void Reveal(bool forceOpen = false) const;
	private:
		enum Kind { None, Frame, Attribute, Command, Property };
		ibPropertyObject* m_object = nullptr;
		Kind              m_kind   = None;
	};

	// The frame VIEW of the ONE selection — for canvas ops (highlight / copy-paste / insert position). DERIVED, not
	// stored, so it can NEVER drift from the selection. When the current element IS a control -> that control;
	// otherwise the form root.
	ibValueFrame* GetSelectedObject() const;

	// THE single selection AND the one entry point. Sets the current element (control / attribute / command), syncs the
	// canvas highlight (a control highlights; anything else clears it — no "two selected"), and LIGHTS the inspector
	// through the selection itself (ibEditorSelection::Reveal). Every surface — canvas, attribute tree, command tree,
	// undo/redo, the live toolbar — calls ONLY this; nothing else touches objectInspector. The argument's TYPE picks the
	// kind (implicit ctor); whichever surface selected LAST wins and is restored on re-activation. forceOpen force-opens
	// a hidden inspector (explicit "Properties" / a live click); default is the SOFT reveal. Out-of-line (uses objectInspector).
	void SetCurrentElement(ibEditorSelection element, bool forceOpen = false);
	ibPropertyObject* GetCurrentElement() const { return m_selected.GetObject(); }

	// SHOW the CURRENT element — the explicit "Properties" entry. Force-opens a hidden inspector and reveals it. This
	// is the door that controls SHOWING; a thin wrapper over the one selector, SetCurrentElement(<current>, forceOpen).
	void ShowCurrentElement(bool forceOpen = true);

	// REFRESH the CURRENT element in place — just RE-FIRES the selector's soft reveal on the current selection; the
	// recompute is NOT new force, it's the force already in Reveal (SelectObject(obj, true) rebuilds the grid even on
	// the same object). Adds only: don't change the selection, don't open the panel, no-op when hidden. Exists because
	// ModifyPropertyCmd is a separate class and m_selected is private — it needs a public hook. REFRESH, not SHOW.
	void RefreshCurrentElement() { m_selected.Reveal(false); }

	void RefreshEditor() {
		// Coalesce + DEFER — one rebuild per burst, on the SETTLED model. A single edit fans out to several
		// RefreshEditor calls (the ModifyProperty command AND the objinspect child→parent bubble, plus a Type
		// change cascading further); a full rebuild is idempotent, but inline calls rebuild against a not-yet-
		// settled model (a Type change's value is still materialising its source/columns → the [+] composition
		// would be missing). So defer the single rebuild past the current stack and skip any further call while
		// one is pending (m_refreshPending cleared FIRST inside the lambda so a throw can't wedge it). Same
		// pattern as ibObjectInspector::Create. No per-caller reasoning about who "should" refresh.
		if (m_refreshPending)
			return;
		m_refreshPending = true;
		CallAfter([this] {
			// Keep the guard SET for the whole rebuild (RAII clears it on exit, even on a throw): UpdateVisualHost
			// re-creates the control tree, and a tablebox's render can re-fire RefreshEditor — while one rebuild
			// runs, a nested one must be a no-op, or it spawns an OVERLAPPING deferred rebuild on half-built
			// controls (the old tablebox-render loop). Cleared only after this rebuild finishes.
			struct ResetOnExit { bool& flag; ~ResetOnExit() { flag = false; } } reset{ m_refreshPending };
			if (m_visualEditor != nullptr)
				m_visualEditor->UpdateVisualHost();
			NotifyEditorRefresh();
		});
	}

	void Undo();
	void Redo();

	bool CanUndo() const {
		return m_cmdProc->CanUndo();
	}

	bool CanRedo() const {
		return m_cmdProc->CanRedo();
	}

	bool CanPasteObject() const;
	bool CanCopyObject() const;

	bool IsModified() const {
		return m_document != nullptr ?
			m_document->IsModified() : false;
	}

	void Modify(bool mod) {
		if (m_document != nullptr)
			m_document->Modify(mod);
	}

	void Activate() const {
		if (m_document != nullptr)
			m_document->Activate();
	}

	/**
	* Calculates the position where the object should be inserted.
	*
	* Given a "parent" object and a "selected" object, this routine calculates the
	* insertion position of an object under "parent" so that the object ends up
	* right after the "selected" object.
	*
	* The algorithm walks up the tree from the "selected" object until it finds an
	* object whose parent is the same as "parent", in which case the position right
	* after that object is taken.
	*
	* @param parent the "parent" object
	* @param selected the "selected" object.
	* @return insertion position (-1 if it cannot be inserted).
	*/
	int CalcPositionOfInsertion(ibValueFrame* selected, ibValueFrame* parent);

	bool LoadForm();
	bool SaveForm();

	void TestForm();

private:

	ibEditorSelection m_selected;   // THE selection — self-describing (control / attribute / command); the frame view derives

	// Undo/Redo command processor
	ibCommandProcessor* m_cmdProc;

	//Form handler 
	ibValueForm* m_valueForm;

	//Elements form
	ibVisualEditorHost* m_visualEditor;
	ibVisualEditorObjectTree* m_objectTree;
	ibVisualEditorAttributeTree* m_attributeTree = nullptr;
	ibVisualEditorCommandTree* m_commandTree = nullptr;   // command navigator (its own panel)

	// A command dropped on a command-bar toolbar joins THAT bar. EVERY drop resolver sets this fresh (the canvas one to
	// the tagged toolbar or null, the tablebox / object-tree ones to null), so it always reflects the CURRENT drop — no
	// stale value survives from a prior drop. CreateCommandButton reads + clears it. See SetPendingDropBar.
	ibValueCommandBar* m_pendingDropBar = nullptr;

	//Document & view
	ibMetaDocument* m_document;

	//Splitter for designer
	wxSplitterWindow* m_splitter = nullptr;
	//Splitter between the control tree and the attribute tree
	wxSplitterWindow* m_treeSplitter = nullptr;
	//Splitter between the attribute tree and the command navigator (the right pane's own split)
	wxSplitterWindow* m_rightSplitter = nullptr;

	// One-shot guard: the three splitter sashes are balanced (canvas strip + equal-thirds navigators) on the FIRST
	// real size, then left to gravity — so a later manual drag is not overwritten. See CreateWideGui's size hook.
	bool m_layoutBalanced = false;

	// One-rebuild-per-burst guard for RefreshEditor (see RefreshEditor): true while a refresh has already run
	// this event-loop iteration, cleared next tick by a CallAfter.
	bool m_refreshPending = false;

	//access to private object
	friend class ibVisualEditorNotebook;
	friend class ibVisualEditorHost;
	friend class ibVisualEditorObjectTree;
	friend class ibVisualEditorAttributeTree;

	wxDECLARE_DYNAMIC_CLASS(ibVisualEditor);
};

public:

	ibVisualEditorNotebook(ibMetaDocument* document, wxWindow* parent, wxWindowID id, long flags) :
		wxAuiNotebook(parent, id, wxDefaultPosition, wxDefaultSize, wxAUI_NB_BOTTOM | wxAUI_NB_TAB_FIXED_WIDTH),
		m_visualEditor(new ibVisualEditor(document, this)), m_codeEditor(new ibCodeEditorDesigner(document, this)) {
		CreateVisualEditor(document, parent, id, flags);
	}

	virtual ~ibVisualEditorNotebook() {
		DestroyVisualEditor();
	}

	void CreateVisualEditor(ibMetaDocument* document, wxWindow* parent, wxWindowID id, long flags);
	void DestroyVisualEditor();

	void Copy() {
		if (wxAuiNotebook::GetSelection() == wxNOTEBOOK_PAGE_DESIGNER) {
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
		if (wxAuiNotebook::GetSelection() == wxNOTEBOOK_PAGE_DESIGNER) {
		}
		else if (wxAuiNotebook::GetSelection() == wxNOTEBOOK_PAGE_CODE_EDITOR) {
			m_codeEditor->SelectAll();
		}
	}

	bool Undo();
	bool Redo();

	bool CanUndo() const;
	bool CanRedo() const;

	void CreateControl(const wxString& controlName) {
		m_visualEditor->CreateObject(controlName);
		if (wxAuiNotebook::GetSelection() != wxNOTEBOOK_PAGE_DESIGNER)
			wxAuiNotebook::SetSelection(wxNOTEBOOK_PAGE_DESIGNER);
	}

	void RemoveControl(ibValueFrame* obj) {
		m_visualEditor->RemoveObject(obj);
		if (wxAuiNotebook::GetSelection() != wxNOTEBOOK_PAGE_DESIGNER)
			wxAuiNotebook::SetSelection(wxNOTEBOOK_PAGE_DESIGNER);
	}

	void CutControl(ibValueFrame* obj, bool force = false) {
		m_visualEditor->CutObject(obj, force);
		if (wxAuiNotebook::GetSelection() != wxNOTEBOOK_PAGE_DESIGNER)
			wxAuiNotebook::SetSelection(wxNOTEBOOK_PAGE_DESIGNER);
	}

	void CopyControl(ibValueFrame* obj) {
		m_visualEditor->CopyObject(obj);
		wxAuiNotebook::SetSelection(wxNOTEBOOK_PAGE_DESIGNER);
	}

	bool PasteControl(ibValueFrame* parent) {
		bool result = m_visualEditor->PasteObject(parent);
		wxAuiNotebook::SetSelection(wxNOTEBOOK_PAGE_DESIGNER);
		return result;
	}

	void InsertControl(ibValueFrame* obj, ibValueFrame* parent) {
		m_visualEditor->InsertObject(obj, parent);
		if (wxAuiNotebook::GetSelection() != wxNOTEBOOK_PAGE_DESIGNER)
			wxAuiNotebook::SetSelection(wxNOTEBOOK_PAGE_DESIGNER);
	}

	void ExpandControl(ibValueFrame* obj, bool expand) {
		m_visualEditor->ExpandObject(obj, expand);
		if (wxAuiNotebook::GetSelection() != wxNOTEBOOK_PAGE_DESIGNER)
			wxAuiNotebook::SetSelection(wxNOTEBOOK_PAGE_DESIGNER);
	}

	void SelectControl(ibValueFrame* obj) {
		m_visualEditor->SelectObject(obj);
		if (wxAuiNotebook::GetSelection() != wxNOTEBOOK_PAGE_DESIGNER)
			wxAuiNotebook::SetSelection(wxNOTEBOOK_PAGE_DESIGNER);
	}

	// Hand any property object straight to the inspector (defined in the .cpp — needs objinspect).
	void SelectPropertyObject(ibPropertyObject* obj) override;

	void ModifyEvent(ibEvent* event, const wxVariant& oldValue, const wxVariant& newValue);
	void ModifyProperty(ibProperty* prop, const wxVariant& oldValue, const wxVariant& newValue) {
		m_visualEditor->ModifyProperty(prop, oldValue, newValue);
		if (wxAuiNotebook::GetSelection() != wxNOTEBOOK_PAGE_DESIGNER)
			wxAuiNotebook::SetSelection(wxNOTEBOOK_PAGE_DESIGNER);
	}

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

	bool SyntaxControl(bool throwMessage = true) {
		bool result = m_codeEditor->SyntaxControl(throwMessage);
		if (!result && wxAuiNotebook::GetSelection() != wxNOTEBOOK_PAGE_CODE_EDITOR)
			wxAuiNotebook::SetSelection(wxNOTEBOOK_PAGE_CODE_EDITOR);
		return result;
	}

	void SetCurrentLine(int line, bool setLine = true) {
		if (wxAuiNotebook::GetSelection() != wxNOTEBOOK_PAGE_CODE_EDITOR)
			wxAuiNotebook::SetSelection(wxNOTEBOOK_PAGE_CODE_EDITOR);
	}

	bool LoadForm() {
		return m_visualEditor->LoadForm() &&
			m_codeEditor->LoadModule();
	}

	bool SaveForm() {
		return m_visualEditor->SaveForm() &&
			m_codeEditor->SaveModule();
	}

	void TestForm() {
		m_visualEditor->TestForm();
	}

	bool IsEditable() const {
		return m_visualEditor->IsEditable() &&
			m_codeEditor->IsEditable();
	}

	ibVisualEditor* GetVisualEditor() const { return m_visualEditor; }
	ibCodeEditor* GetCodeEditor() const { return m_codeEditor; }

	virtual void RefreshEditor() {

		if (m_visualEditor != nullptr)
			m_visualEditor->RefreshEditor();

		if (m_codeEditor != nullptr)
			m_codeEditor->RefreshEditor();
	}

	virtual	ibValueForm* GetValueForm() const { return m_visualEditor->GetValueForm(); }
	virtual	ibMetaDocument* GetEditorDocument() const { return m_visualEditor->GetEditorDocument(); }
	virtual ibVisualHost* GetVisualHost() const { return m_visualEditor->GetVisualEditor(); }

	virtual wxEvtHandler* GetHighlightPaintHandler(wxWindow* wnd) const { return new ibDesignerWindow::ibHighlightPaintHandler(wnd); }

	void ActivateEditor();

protected:
	//A general selection function
	virtual int DoModifySelection(size_t n, bool events) override {
		wxAuiNotebook::Freeze();
		const int selection = wxAuiNotebook::DoModifySelection(n, events);
		wxAuiNotebook::Thaw();
		return selection;
	}
	void OnPageChanged(wxAuiNotebookEvent& event);
private:
	ibVisualEditor* m_visualEditor;
	ibCodeEditorDesigner* m_codeEditor;

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