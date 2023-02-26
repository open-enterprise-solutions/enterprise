#ifndef __VISUAL_EDITOR_H__
#define __VISUAL_EDITOR_H__

#include <set>

#include <wx/artprov.h>
#include <wx/config.h>
#include <wx/cmdproc.h>
#include <wx/docview.h>
#include <wx/splitter.h>
#include <wx/spinbutt.h>
#include <wx/treectrl.h>

#include "innerFrame.h"
#include "pageWindow.h"

#include "controls/form.h"
#include "controls/sizers.h"
#include "controls/widgets.h"

class CCommand {
	bool m_executed;
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

	CCommand() : m_executed(false) {}

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
};

class CCommandProcessor {
	typedef std::stack<CCommand*> CommandStack;
	CommandStack m_undoStack, m_redoStack;
	unsigned int m_savePoint;
public:

	CCommandProcessor() : m_savePoint(0) {}

	~CCommandProcessor() {
		while (!m_redoStack.empty()) {
			CCommand* redoCmd = m_redoStack.top();
			delete redoCmd;
			m_redoStack.pop();
		}
		while (!m_undoStack.empty()) {
			CCommand* undoCmd = m_undoStack.top();
			delete undoCmd;
			m_undoStack.pop();
		}
	}

	void CCommandProcessor::Execute(CCommand* command) {
		command->Execute();
		m_undoStack.push(command);
		while (!m_redoStack.empty()) {
			m_redoStack.pop();
		}
	}

	bool CCommandProcessor::Undo() {
		if (!m_undoStack.empty()) {
			CCommand* command = m_undoStack.top();
			m_undoStack.pop();
			command->Restore();
			m_redoStack.push(command);
		}
		return true;
	}

	bool CCommandProcessor::Redo() {
		if (!m_redoStack.empty()) {
			CCommand* command = m_redoStack.top();
			m_redoStack.pop();
			command->Execute();
			m_undoStack.push(command);
		}
		return true;
	}

	void CCommandProcessor::Reset() {
		while (!m_redoStack.empty())
			m_redoStack.pop();
		while (!m_undoStack.empty())
			m_undoStack.pop();
		m_savePoint = 0;
	}

	bool CCommandProcessor::CanUndo() const {
		return (!m_undoStack.empty());
	}

	bool CCommandProcessor::CanRedo() const {
		return (!m_redoStack.empty());
	}

	void CCommandProcessor::SetSavePoint() {
		m_savePoint = m_undoStack.size();
	}

	bool CCommandProcessor::IsAtSavePoint() {
		return m_savePoint == m_undoStack.size();
	}
};

#include "visualInterface.h"
#include "visualEvent.h"

#include "frontend/codeEditor/codeEditorCtrl.h"

//////////////////////////////////////////////////////////////////////////////////////////

#include "core.h"

#define wxNOTEBOOK_PAGE_DESIGNER 0
#define wxNOTEBOOK_PAGE_CODE_EDITOR 1

/**
 * Extends the CInnerFrame to show the object highlight
*/
class CDesignerWindow : public CInnerFrame {
	wxDECLARE_CLASS(CDesignerWindow);
private:
	int m_x;
	int m_y;
	wxSizer* m_selSizer = NULL;
	wxObject* m_selItem = NULL;
	IPropertyObject* m_selObj = NULL;
	wxWindow* m_actPanel = NULL;
private:

	void DrawRectangle(wxDC& dc, const wxPoint& point, const wxSize& size, IPropertyObject* object);

public:

	// Augh!, this class is needed to paint the highlight in the
	// frame content panel.
	class CHighlightPaintHandler : public wxEvtHandler {
		wxDECLARE_EVENT_TABLE();
	private:
		wxWindow* m_dsgnWin;
	public:
		CHighlightPaintHandler(wxWindow* win);
		void OnPaint(wxPaintEvent& event);
	};

public:

	CDesignerWindow(wxWindow* parent, int id, const wxPoint& pos, const wxSize& size = wxDefaultSize,
		long style = 0, const wxString& name = wxT("designer_win"));
	virtual ~CDesignerWindow();

	void SetGrid(int x, int y);
	void SetSelectedSizer(wxSizer* sizer) { m_selSizer = sizer; }
	void SetSelectedItem(wxObject* item) { m_selItem = item; }
	void SetSelectedObject(IPropertyObject* object) { m_selObj = object; }
	void SetSelectedPanel(wxWindow* actPanel) { m_actPanel = actPanel; }

	wxSizer* GetSelectedSizer() const {
		return m_selSizer;
	}

	wxObject* GetSelectedItem() const {
		return m_selItem;
	}

	IPropertyObject* GetSelectedObject() const {
		return m_selObj;
	}

	wxWindow* GetActivePanel() const {
		return m_actPanel;
	}

	void HighlightSelection(wxDC& dc);
	void OnPaint(wxPaintEvent& event);

protected:
	wxDECLARE_EVENT_TABLE();
};

class CORE_API CVisualEditorNotebook : public wxAuiNotebook {

public: class CORE_API CVisualEditorCtrl : public wxPanel {
	wxDECLARE_DYNAMIC_CLASS(CVisualEditorCtrl);
public:

	class CVisualEditorHost : public IVisualHost {
		friend class CVisualEditorCtrl;
		friend class CVisualEditorObjectTree;
		//designer 
		CDesignerWindow* m_back;
		//form handler
		CVisualEditorCtrl* m_formHandler;
		// Prevent OnSelected in components
		bool m_stopSelectedEvent;
		// Prevent OnModified in components
		bool m_stopModifiedEvent;
	public:

		friend class ExpandObjectCmd;
		friend class InsertObjectCmd;
		friend class RemoveObjectCmd;
		friend class ModifyPropertyCmd;
		friend class ModifyEventHandlerCmd;
		friend class ShiftChildCmd;
		friend class CutObjectCmd;
		friend class ReparentObjectCmd;

		CVisualEditorHost(CVisualEditorCtrl* handler, wxWindow* parent, wxWindowID id = wxID_ANY);
		virtual ~CVisualEditorHost() override;

		//*********************************************************
		//*                 Events for visual                     *
		//*********************************************************

		/**
		* Create an instance of the wxObject and return a pointer
		*/
		virtual wxObject* Create(IValueFrame* control, wxWindow* wxparent);

		/**
		* Allows components to do something after they have been created.
		* For example, Abstract components like NotebookPage and SizerItem can
		* add the actual widget to the Notebook or sizer.
		*
		* @param wxobject The object which was just created.
		* @param wxparent The wxWidgets parent - the wxObject that the created object was added to.
		*/
		virtual void OnCreated(IValueFrame* control, wxObject* obj, wxWindow* wxparent, bool firstСreated = false);

		/**
		* Allows components to do something after they have been updated.
		*/
		virtual void Update(IValueFrame* control, wxObject* obj);

		/**
		* Allows components to do something after they have been updated.
		* For example, Abstract components like NotebookPage and SizerItem can
		* add the actual widget to the Notebook or sizer.
		*
		* @param wxobject The object which was just updated.
		* @param wxparent The wxWidgets parent - the wxObject that the updated object was added to.
		*/
		virtual void OnUpdated(IValueFrame* control, wxObject* obj, wxWindow* wndParent);

		/**
		 * Cleanup (do the reverse of Create)
		 */
		virtual void Cleanup(IValueFrame* control, wxObject* obj);

		/////////////////////////////////////////////////////////////////////////////////////////

		void OnResizeBackPanel(wxCommandEvent& event);
		void OnClickBackPanel(wxMouseEvent& event);
		void PreventOnSelected(bool prevent = true);
		void PreventOnModified(bool prevent = true);

		bool OnLeftClickFromApp(wxWindow* currentWindow);
		bool OnRightClickFromApp(wxWindow* currentWindow, wxMouseEvent& event);

		virtual void OnClickFromApp(wxWindow* currentWindow, wxMouseEvent& event);

		virtual wxWindow* GetParentBackgroundWindow() const {
			return m_back;
		}

		virtual wxWindow* GetBackgroundWindow() const {
			return m_back->GetFrameContentPanel();
		}

		//override designer host  
		virtual bool IsDesignerHost() const {
			return true;
		}

		virtual CValueForm* GetValueForm() const;
		virtual void SetValueForm(CValueForm* valueForm);

		//set and create window
		void SetObjectSelect(IValueFrame* obj);

		//Setup window 
		void CreateVisualEditor();

		//Update window 
		void UpdateVisualEditor();

		//Clear visualeditor
		void ClearVisualEditor();

	protected:
		wxDECLARE_EVENT_TABLE();
	};

	//////////////////////////////////////////////////////////////////////////////////////////

	class CVisualEditorObjectTree : public wxPanel {
		CVisualEditorCtrl* m_formHandler = NULL;
	private:

		wxImageList* m_iconList = NULL;

		std::map< IValueFrame*, wxTreeItemId> m_aItems;
		std::map<wxString, int> m_iconIdx;

		wxTreeCtrl* m_tcObjects = NULL;
		wxTreeItemId m_draggedItem;

		bool m_altKeyIsDown;

		/**
		 * Crea el arbol completamente.
		 */
		void RebuildTree();
		void AddChildren(IValueFrame* child, const wxTreeItemId& parent, bool is_root = false);
		int GetImageIndex(const wxString& type);
		void UpdateItem(const wxTreeItemId& id, IValueFrame* obj);
		void RestoreItemStatus(IValueFrame* obj);
		void AddItem(IValueFrame* item, IValueFrame* parent);
		void RemoveItem(IValueFrame* item);
		void ClearMap(IValueFrame* obj);

		IValueFrame* GetObjectFromTreeItem(const wxTreeItemId& item);

		wxDECLARE_EVENT_TABLE();

	public:

		void RefreshTree() {
			RebuildTree();
		}

		CVisualEditorObjectTree(CVisualEditorCtrl* owner, wxWindow* parent, int id = wxID_ANY);
		virtual ~CVisualEditorObjectTree() override;

		void Create();

		void OnSelChanged(wxTreeEvent& event);
		void OnRightClick(wxTreeEvent& event);
		void OnBeginDrag(wxTreeEvent& event);
		void OnEndDrag(wxTreeEvent& event);
		void OnExpansionChange(wxTreeEvent& event);

		void OnProjectLoaded(wxFrameEvent& event);
		void OnProjectSaved(wxFrameEvent& event);
		void OnObjectExpanded(wxFrameObjectEvent& event);
		void OnObjectSelected(wxFrameObjectEvent& event);
		void OnObjectCreated(wxFrameObjectEvent& event);
		void OnObjectRemoved(wxFrameObjectEvent& event);
		void OnPropertyModified(wxFramePropertyEvent& event);
		void OnProjectRefresh(wxFrameEvent& event);
		void OnKeyDown(wxTreeEvent& event);
	};

	/**
	 * Gracias a que podemos asociar un objeto a cada item, esta clase nos va
	 * a facilitar obtener el objeto (IValueFrame) asociado a un item para
	 * seleccionarlo pinchando en el item.
	 */
	class CVisualEditorObjectTreeItemData : public wxTreeItemData
	{
	private:
		IValueFrame* m_object = NULL;
	public:
		CVisualEditorObjectTreeItemData(IValueFrame* obj);
		IValueFrame* GetObject() { return m_object; }
	};

	/**
	 * Menu popup asociado a cada item del arbol.
	 *
	 * Este objeto ejecuta los comandos incluidos en el menu referentes al objeto
	 * seleccionado.
	 */
	class CVisualEditorItemPopupMenu : public wxMenu {
		IValueFrame* m_object = NULL;
		CVisualEditorCtrl* m_formHandler = NULL;
		int m_selID;
	public:

		bool HasDeleteObject();
		int GetSelectedID() const { return m_selID; }

		CVisualEditorItemPopupMenu(CVisualEditorCtrl* owner, wxWindow* parent, IValueFrame* obj);

		void OnUpdateEvent(wxUpdateUIEvent& e);
		void OnMenuEvent(wxCommandEvent& event);

	protected:
		wxDECLARE_EVENT_TABLE();
	};

	//////////////////////////////////////////////////////////////////////////////////////////

	IValueFrame* m_selObj = NULL;     // Objeto seleccionado
	IValueFrame* m_clipboard = NULL;

	std::vector< wxEvtHandler* > m_handlers;

	bool m_bReadOnly;
	bool m_copyOnPaste; // flag que indica si hay que copiar el objeto al pegar

	// Procesador de comandos Undo/Redo
	CCommandProcessor* m_cmdProc;

	//Elements form 
	CVisualEditorHost* m_visualEditor;
	CVisualEditorObjectTree* m_objectTree;

	//Document & view 
	CDocument* m_document;

	//access to private object  
	friend class CValueNotebook;
	friend class CValueNotebookPage;

	friend class CValueToolbar;
	friend class CValueToolBarItem;
	friend class CValueToolBarSeparator;

	friend class CValueTableBox;
	friend class CValueTableBoxColumn;

	friend class CVisualDesignerCommandProcessor;
	friend class CVisualCommand;

	friend class CValueForm;

private:

	CValueForm* m_valueForm;

	friend class CVisualEditorHost;
	friend class CVisualEditorObjectTree;

private:

	wxSplitterWindow* m_splitter = NULL;

public:

	CVisualEditorHost* GetVisualEditor() const {
		return m_visualEditor;
	}

	CVisualEditorObjectTree* GetObjectTree() const {
		return m_objectTree;
	}

	CValueForm* GetValueForm() const {
		return m_valueForm;
	}

	void SetValueForm(CValueForm* valueForm) {
		m_valueForm = valueForm;
	}

	bool IsEditable() const {
		return !m_bReadOnly;
	}

	void SetReadOnly(bool readOnly = true) {
		m_bReadOnly = readOnly;
	}

protected:

	//Notify event 
	void NotifyEvent(wxFrameEvent& event, bool forcedelayed = false);

	// Notifican a cada observador el evento correspondiente
	void NotifyProjectLoaded();
	void NotifyProjectSaved();
	void NotifyObjectExpanded(IValueFrame* obj);
	void NotifyObjectSelected(IValueFrame* obj, bool force = false);
	void NotifyObjectCreated(IValueFrame* obj);
	void NotifyObjectRemoved(IValueFrame* obj);
	void NotifyPropertyModified(Property* prop);
	void NotifyEventModified(Event* event);
	void NotifyProjectRefresh();
	void NotifyCodeGeneration(bool panelOnly = false, bool force = false);

	//Execute command 
	void Execute(CCommand* cmd);

	/**
	* Search a size in the hierarchy of an object
	*/
	IValueFrame* SearchSizerInto(IValueFrame* obj);

	void PropagateExpansion(IValueFrame* obj, bool expand, bool up);

	/**
	* Eliminar un objeto.
	*/
	void DoRemoveObject(IValueFrame* object, bool cutObject, bool force = false);

public:

	CVisualEditorCtrl();
	CVisualEditorCtrl(CDocument* document, wxWindow* parent, int id = wxID_ANY);
	virtual ~CVisualEditorCtrl();

	// Procedures for register/unregister wxEvtHandlers to be notified of wxOESEvents
	void AddHandler(wxEvtHandler* handler);
	void RemoveHandler(wxEvtHandler* handler);

	// Servicios específicos, no definidos en DataObservable
	void SetClipboardObject(IValueFrame* obj) {
		m_clipboard = obj;
	}

	IValueFrame* GetClipboardObject() const {
		return m_clipboard;
	}

	//Objects 
	IValueFrame* CreateObject(const wxString& name);
	void RemoveObject(IValueFrame* obj);
	void CutObject(IValueFrame* obj, bool force = false);
	void CopyObject(IValueFrame* obj);
	bool PasteObject(IValueFrame* parent, IValueFrame* objToPaste = NULL);
	void InsertObject(IValueFrame* obj, IValueFrame* parent);
	void ExpandObject(IValueFrame* obj, bool expand);

	void ModifyProperty(Property* prop, const wxVariant& newValue);
	void ModifyEvent(Event* evt, const wxString& newValue);

	void CreateWideGui();

	void DetermineObjectToSelect(IValueFrame* parent, unsigned int pos);

	// Object will not be selected if it already is selected, unless force = true
	// Returns true if selection changed, false if already selected
	bool SelectObject(IValueFrame* obj, bool force = false, bool notify = true);

	void MovePosition(IValueFrame* obj, unsigned int toPos);
	void MovePosition(IValueFrame* obj, bool right, unsigned int num = 1);

	// Servicios para los observadores
	IValueFrame* GetSelectedObject() const {
		return m_selObj;
	}

	void RefreshEditor() {
		if (m_visualEditor != NULL) {
			// then update control 
			m_visualEditor->UpdateVisualEditor();
		}
		NotifyProjectRefresh();
	}

	void RefreshTree() {
		if (m_objectTree != NULL)
			m_objectTree->RefreshTree();
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

	bool IsModified() const;

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
	int CalcPositionOfInsertion(IValueFrame* selected, IValueFrame* parent);

	void ToggleBorderFlag(IValueFrame* obj, int border);
	void CreateBoxSizerWithObject(IValueFrame* obj);

	bool LoadForm();
	bool SaveForm();

	void TestForm();
};

private:

	CVisualEditorCtrl* m_visualEditor;
	CCodeEditorCtrl* m_codeEditor;

public:

	static CVisualEditorNotebook* FindEditorByForm(CValueForm* valueForm);

	CVisualEditorNotebook(CDocument* document, wxWindow* parent, wxWindowID id, long flags) : wxAuiNotebook(parent, id, wxDefaultPosition, wxDefaultSize, wxAUI_NB_BOTTOM | wxAUI_NB_TAB_FIXED_WIDTH),
		m_visualEditor(new CVisualEditorCtrl(document, this)), m_codeEditor(new CCodeEditorCtrl(document, this)) {
		CreateVisualEditor(document, parent, id, flags);
	}

	virtual ~CVisualEditorNotebook() {
		DestroyVisualEditor();
	}

	void CreateVisualEditor(CDocument* document, wxWindow* parent, wxWindowID id, long flags);
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

	void RemoveControl(IValueFrame* obj) {
		m_visualEditor->RemoveObject(obj);
		if (wxAuiNotebook::GetSelection() != wxNOTEBOOK_PAGE_DESIGNER)
			wxAuiNotebook::SetSelection(wxNOTEBOOK_PAGE_DESIGNER);
	}

	void CutControl(IValueFrame* obj, bool force = false) {
		m_visualEditor->CutObject(obj, force);
		if (wxAuiNotebook::GetSelection() != wxNOTEBOOK_PAGE_DESIGNER)
			wxAuiNotebook::SetSelection(wxNOTEBOOK_PAGE_DESIGNER);
	}

	void CopyControl(IValueFrame* obj) {
		m_visualEditor->CopyObject(obj);
		wxAuiNotebook::SetSelection(wxNOTEBOOK_PAGE_DESIGNER);
	}

	bool PasteControl(IValueFrame* parent, IValueFrame* objToPaste = NULL) {
		bool result = m_visualEditor->PasteObject(parent, objToPaste);
		wxAuiNotebook::SetSelection(wxNOTEBOOK_PAGE_DESIGNER);
		return result;
	}

	void InsertControl(IValueFrame* obj, IValueFrame* parent) {
		m_visualEditor->InsertObject(obj, parent);
		if (wxAuiNotebook::GetSelection() != wxNOTEBOOK_PAGE_DESIGNER)
			wxAuiNotebook::SetSelection(wxNOTEBOOK_PAGE_DESIGNER);
	}

	void ExpandControl(IValueFrame* obj, bool expand) {
		m_visualEditor->ExpandObject(obj, expand);
		if (wxAuiNotebook::GetSelection() != wxNOTEBOOK_PAGE_DESIGNER)
			wxAuiNotebook::SetSelection(wxNOTEBOOK_PAGE_DESIGNER);
	}

	void SelectControl(IValueFrame* obj) {
		m_visualEditor->SelectObject(obj);
		if (wxAuiNotebook::GetSelection() != wxNOTEBOOK_PAGE_DESIGNER)
			wxAuiNotebook::SetSelection(wxNOTEBOOK_PAGE_DESIGNER);
	}

	void ModifyEvent(Event* event, const wxString& newValue);
	void ModifyProperty(Property* prop, const wxVariant& newValue) {
		m_visualEditor->ModifyProperty(prop, newValue);
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

	void RefreshEditor() {

		if (m_visualEditor != NULL)
			m_visualEditor->RefreshEditor();

		if (m_codeEditor != NULL)
			m_codeEditor->RefreshEditor();
	}

	void RefreshTree() {

		if (m_visualEditor != NULL)
			m_visualEditor->RefreshTree();

	}

	bool IsEditable() const {
		return m_visualEditor->IsEditable() &&
			m_codeEditor->IsEditable();
	}

	CValueForm* GetValueForm() const {
		return m_visualEditor->GetValueForm();
	}

	IVisualHost* GetVisualHost() const {
		return m_visualEditor->GetVisualEditor();
	}

	CVisualEditorCtrl* GetVisualEditor() const {
		return m_visualEditor;
	}

	CCodeEditorCtrl* GetCodeEditor() const {
		return m_codeEditor;
	}

protected:

	void OnPageChanged(wxAuiNotebookEvent& event);
};

//////////////////////////////////////////////////////////////////////////////////////////

class CVisualDesignerCommandProcessor : public wxCommandProcessor {
	CVisualEditorNotebook* m_visualNotebook;
public:

	CVisualDesignerCommandProcessor(CVisualEditorNotebook* visualNotebook) : wxCommandProcessor(),
		m_visualNotebook(visualNotebook) {}

	virtual bool Undo() override {
		return m_visualNotebook->Undo();
	}

	virtual bool Redo() override {
		return m_visualNotebook->Redo();
	}

	virtual bool CanUndo() const override {
		return m_visualNotebook->CanUndo();
	}

	virtual bool CanRedo() const override {
		return m_visualNotebook->CanRedo();
	}
};

#endif