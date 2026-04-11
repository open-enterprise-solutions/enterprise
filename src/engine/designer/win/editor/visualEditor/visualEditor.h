#ifndef __VISUAL_EDITOR_H__
#define __VISUAL_EDITOR_H__

#include "innerFrame.h"
#include "win/editor/codeEditor/codeEditor.h"

#include "frontend/visualView/ctrl/form.h"
#include "frontend/visualView/ctrl/sizer.h"
#include "frontend/visualView/ctrl/widgets.h"

#include "frontend/visualView/visualHost.h"

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
	 * Ejecuta el comando.
	 */
	virtual void DoExecute() = 0;

	/**
	 * Restaura el estado previo a la ejecución del comando.
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
		virtual void OnCreated(ibValueFrame* control, wxObject* obj, wxWindow* wxparent, bool firstСreated = false);

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
		void ScrollToObject(ibValueFrame* obj);

	protected:

		virtual void SetCaption(const wxString& strCaption);
		virtual void SetOrientation(int orient);
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
		 * Crea el arbol completamente.
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

			// mostramos el nombre
			wxString class_name(obj->GetClassName());
			wxString obj_name(obj->GetControlName());

			wxString text = obj_name + wxT(" : ") + class_name;

			// actualizamos el item
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
		std::map<wxString, int> m_iconIdx;

		wxTreeCtrl* m_tcObjects = nullptr;
		wxTreeItemId m_draggedItem;

		bool m_altKeyIsDown, m_notifySelecting;

		wxDECLARE_EVENT_TABLE();
	};

	/**
	 * Gracias a que podemos asociar un objeto a cada item, esta clase nos va
	 * a facilitar obtener el objeto (ibValueFrame) asociado a un item para
	 * seleccionarlo pinchando en el item.
	 */
	class ibVisualEditorObjectTreeItemData : public wxTreeItemData {
	public:
		ibVisualEditorObjectTreeItemData(ibValueFrame* obj) : m_object(obj) {}
		ibValueFrame* GetObject() { return m_object; }
	private:
		ibValueFrame* m_object = nullptr;
	};

	/**
	 * Menu popup asociado a cada item del arbol.
	 *
	 * Este objeto ejecuta los comandos incluidos en el menu referentes al objeto
	 * seleccionado.
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

	bool IsEditable() const { return true; }
	void SetReadOnly(bool readOnly = true) {}

	void ActivateEditor();

protected:

	// Notifican a cada observador el evento correspondiente
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
	* Eliminar un objeto.
	*/
	void DoRemoveObject(ibValueFrame* object, bool cutObject, bool force = false);

public:

	ibVisualEditor();
	ibVisualEditor(ibMetaDocument* document, wxWindow* parent, int id = wxID_ANY);
	virtual ~ibVisualEditor();

	//Objects 
	ibValueFrame* CreateObject(const wxString& name);
	void RemoveObject(ibValueFrame* obj);
	void CutObject(ibValueFrame* obj, bool force = false);
	void CopyObject(ibValueFrame* obj);
	bool PasteObject(ibValueFrame* parent);
	void InsertObject(ibValueFrame* obj, ibValueFrame* parent);
	void ExpandObject(ibValueFrame* obj, bool expand);

	void ModifyProperty(ibProperty* prop, const wxVariant& oldValue, const wxVariant& newValue);
	void ModifyEvent(ibEvent* evt, const wxVariant& oldValue, const wxVariant& newValue);

	void CreateWideGui();

	void DetermineObjectToSelect(ibValueFrame* parent, unsigned int pos);

	// Object will not be selected if it already is selected, unless force = true
	// Returns true if selection changed, false if already selected
	bool SelectObject(ibValueFrame* obj, bool force = false, bool notify = true);

	void MovePosition(ibValueFrame* obj, unsigned int toPos);
	void MovePosition(ibValueFrame* obj, bool right, unsigned int num = 1);

	void ScrollToObject(ibValueFrame* obj);

	// Servicios para los observadores
	ibValueFrame* GetSelectedObject() const {
		return m_selObj != nullptr ? m_selObj : m_valueForm;
	}

	void RefreshEditor() {
		if (m_visualEditor != nullptr) {
			// then update control 
			m_visualEditor->UpdateVisualHost();
		}
		NotifyEditorRefresh();
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
	* Calcula la posición donde deberá ser insertado el objeto.
	*
	* Dado un objeto "padre" y un objeto "seleccionado", esta rutina calcula la
	* posición de inserción de un objeto debajo de "parent" de forma que el objeto
	* quede a continuación del objeto "seleccionado".
	*
	* El algoritmo consiste ir subiendo en el arbol desde el objeto "selected"
	* hasta encontrar un objeto cuyo padre sea el mismo que "parent" en cuyo
	* caso se toma la posición siguiente a ese objeto.
	*
	* @param parent objeto "padre"
	* @param selected objeto "seleccionado".
	* @return posición de insercción (-1 si no se puede insertar).
	*/
	int CalcPositionOfInsertion(ibValueFrame* selected, ibValueFrame* parent);

	bool LoadForm();
	bool SaveForm();

	void TestForm();

private:

	ibValueFrame* m_selObj = nullptr;     // Objeto seleccionado

	// Procesador de comandos Undo/Redo
	ibCommandProcessor* m_cmdProc;

	//Form handler 
	ibValueForm* m_valueForm;

	//Elements form 
	ibVisualEditorHost* m_visualEditor;
	ibVisualEditorObjectTree* m_objectTree;

	//Document & view 
	ibMetaDocument* m_document;

	//Splitter for designer 
	wxSplitterWindow* m_splitter = nullptr;

	//access to private object  
	friend class ibVisualEditorNotebook;
	friend class ibVisualEditorHost;
	friend class ibVisualEditorObjectTree;

	wxDECLARE_DYNAMIC_CLASS(ibVisualEditor);
};

public:

	ibVisualEditorNotebook(ibMetaDocument* document, wxWindow* parent, wxWindowID id, long flags) :
		wxAuiNotebook(parent, id, wxDefaultPosition, wxDefaultSize, wxAUI_NB_BOTTOM | wxAUI_NB_TAB_FIXED_WIDTH),
		m_visualEditor(new ibVisualEditor(document, this)), m_codeEditor(new ibCodeEditor(document, this)) {
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
	ibCodeEditor* m_codeEditor;

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