#ifndef __VISUAL_EDITOR__
#define __VISUAL_EDITOR__

#include <set>

#include "innerFrame.h"
#include "pageWindow.h"
#include "frontend/objinspect/events.h"

/**
 * Extends the CInnerFrame to show the object highlight
 */
class CDesignerWindow : public CInnerFrame
{
	int m_x;
	int m_y;

	wxSizer *m_selSizer = NULL;
	wxObject *m_selItem = NULL;
	IValueFrame*m_selObj = NULL;
	wxWindow *m_actPanel = NULL;

private:

	void DrawRectangle(wxDC& dc, const wxPoint& point, const wxSize& size, IValueFrame* object);

	DECLARE_CLASS(CDesignerWindow)

	// Augh!, this class is needed to paint the highlight in the
	// frame content panel.
	class HighlightPaintHandler : public wxEvtHandler
	{
		wxDECLARE_EVENT_TABLE();

		wxWindow *m_dsgnWin;

	public:
		HighlightPaintHandler(wxWindow *win);
		void OnPaint(wxPaintEvent &event);
	};

public:
	CDesignerWindow(wxWindow *parent, int id, const wxPoint& pos, const wxSize &size = wxDefaultSize,
		long style = 0, const wxString &name = wxT("designer_win"));
	~CDesignerWindow();
	void SetGrid(int x, int y);
	void SetSelectedSizer(wxSizer *sizer) { m_selSizer = sizer; }
	void SetSelectedItem(wxObject *item) { m_selItem = item; }
	void SetSelectedObject(IValueFrame* object) { m_selObj = object; }
	void SetSelectedPanel(wxWindow *actPanel) { m_actPanel = actPanel; }
	wxSizer *GetSelectedSizer() { return m_selSizer; }
	wxObject* GetSelectedItem() { return m_selItem; }
	IValueFrame* GetSelectedObject() { return m_selObj; }
	wxWindow* GetActivePanel() { return m_actPanel; }
	static wxMenu* GetMenuFromObject(IValueFrame* menu);
	void SetFrameWidgets(IValueFrame* menubar, wxWindow *toolbar, wxWindow* statusbar);
	void HighlightSelection(wxDC& dc);
	void OnPaint(wxPaintEvent &event);

protected:
	wxDECLARE_EVENT_TABLE();
};

class wxFrameEvent;
class wxFramePropertyEvent;
class wxFrameObjectEvent;

class CVisualEditorContextForm;

#include "controls/form.h"
#include "controls/sizers.h"
#include "controls/widgets.h"

#include "visualEditorBase.h"
#include "common/cmdProc.h"

#include <wx/artprov.h>
#include <wx/config.h>
#include <wx/cmdproc.h>
#include <wx/docview.h>
#include <wx/splitter.h>
#include <wx/spinbutt.h>
#include <wx/treectrl.h>

class CDocument;
class CView;

//////////////////////////////////////////////////////////////////////////////////////////

#include "core.h"
#include <wx/docview.h>

class CORE_API CVisualEditorContextForm : public wxPanel
{
	wxDECLARE_DYNAMIC_CLASS(CVisualEditorContextForm);

public:

	class CORE_API CVisualEditor : public IVisualHost
	{
		friend class CVisualEditorContextForm;
		friend class CVisualEditorObjectTree;

	private:

		CDesignerWindow *m_back;

		// Prevent OnSelected in components
		bool m_stopSelectedEvent;
		// Prevent OnModified in components
		bool m_stopModifiedEvent;

		IValueFrame *m_activeControl;

	public:

		friend class ExpandObjectCmd;
		friend class InsertObjectCmd;
		friend class RemoveObjectCmd;
		friend class ModifyPropertyCmd;
		friend class ModifyEventHandlerCmd;
		friend class ShiftChildCmd;
		friend class CutObjectCmd;
		friend class ReparentObjectCmd;

		CVisualEditor(CVisualEditorContextForm *handler, wxWindow *parent);
		virtual ~CVisualEditor() override;

		void OnResizeBackPanel(wxCommandEvent &event);
		void OnClickBackPanel(wxMouseEvent& event);
		void PreventOnSelected(bool prevent = true);
		void PreventOnModified(bool prevent = true);

		bool OnLeftClickFromApp(wxWindow *currentWindow);
		bool OnRightClickFromApp(wxWindow *currentWindow, wxMouseEvent &event);

		virtual void OnClickFromApp(wxWindow *currentWindow, wxMouseEvent &event);

		wxWindow *GetParentBackgroundWindow() { return m_back; }
		wxWindow *GetBackgroundWindow() { return m_back->GetFrameContentPanel(); }

		//override designer host  
		virtual bool IsDesignerHost() override { return true; }
		virtual CValueForm *GetValueForm() override;
		virtual void SetValueForm(class CValueForm *valueForm) override;

		//set and create window
		void SetObjectSelect(IValueFrame *obj);
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

	class CVisualEditorObjectTree : public wxPanel
	{
		CVisualEditorContextForm *m_formHandler = NULL;

	private:

		wxImageList *m_iconList = NULL;

		std::map< IValueFrame *, wxTreeItemId> m_aItems;
		std::map<wxString, int> m_iconIdx;

		wxTreeCtrl* m_tcObjects = NULL;
		wxTreeItemId m_draggedItem;

		bool m_altKeyIsDown;

		/**
		 * Crea el arbol completamente.
		 */
		void RebuildTree();
		void AddChildren(IValueFrame *child, const wxTreeItemId &parent, bool is_root = false);
		int GetImageIndex(const wxString &type);
		void UpdateItem(const wxTreeItemId id, IValueFrame *obj);
		void RestoreItemStatus(IValueFrame *obj);
		void AddItem(IValueFrame *item, IValueFrame *parent);
		void RemoveItem(IValueFrame *item);
		void ClearMap(IValueFrame *obj);

		IValueFrame *GetObjectFromTreeItem(const wxTreeItemId &item);

		wxDECLARE_EVENT_TABLE();

	public:

		CVisualEditorObjectTree(CVisualEditorContextForm *handler, wxWindow *parent, int id = wxID_ANY);
		virtual ~CVisualEditorObjectTree() override;

		void Create();

		void OnSelChanged(wxTreeEvent &event);
		void OnRightClick(wxTreeEvent &event);
		void OnBeginDrag(wxTreeEvent &event);
		void OnEndDrag(wxTreeEvent &event);
		void OnExpansionChange(wxTreeEvent &event);

		void OnProjectLoaded(wxFrameEvent &event);
		void OnProjectSaved(wxFrameEvent &event);
		void OnObjectExpanded(wxFrameObjectEvent& event);
		void OnObjectSelected(wxFrameObjectEvent &event);
		void OnObjectCreated(wxFrameObjectEvent &event);
		void OnObjectRemoved(wxFrameObjectEvent &event);
		void OnPropertyModified(wxFramePropertyEvent &event);
		void OnProjectRefresh(wxFrameEvent &event);
		void OnKeyDown(wxTreeEvent &event);
	};

	/**
	 * Gracias a que podemos asociar un objeto a cada item, esta clase nos va
	 * a facilitar obtener el objeto (IValueFrame) asociado a un item para
	 * seleccionarlo pinchando en el item.
	 */
	class CVisualEditorObjectTreeItemData : public wxTreeItemData
	{
	private:
		IValueFrame * m_object = NULL;
	public:
		CVisualEditorObjectTreeItemData(IValueFrame * obj);
		IValueFrame * GetObject() { return m_object; }
	};

	/**
	 * Menu popup asociado a cada item del arbol.
	 *
	 * Este objeto ejecuta los comandos incluidos en el menu referentes al objeto
	 * seleccionado.
	 */
	class CVisualEditorItemPopupMenu : public wxMenu
	{
		IValueFrame* m_object = NULL;
		CVisualEditorContextForm *m_formHandler = NULL;

		int m_selID;

	public:

		bool HasDeleteObject();
		int GetSelectedID() { return m_selID; }

		CVisualEditorItemPopupMenu(CVisualEditorContextForm *handler, wxWindow *parent, IValueFrame * obj);

		void OnUpdateEvent(wxUpdateUIEvent& e);
		void OnMenuEvent(wxCommandEvent & event);

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
	CCommandProcessor *m_cmdProc;

	//Elements form 
	CVisualEditor *m_visualEditor;
	CVisualEditorObjectTree *m_objectTree;

	//Document & view 
	CDocument *m_document;
	CView *m_view;

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

	CValueForm *m_valueForm;

	friend class CVisualEditor;
	friend class CVisualEditorObjectTree;

private:

	wxSplitterWindow *m_splitter = NULL;

public:

	CVisualEditor *GetVisualEditor() { return m_visualEditor; }
	CVisualEditorObjectTree *GetObjectTree() { return m_objectTree; }

	CValueForm *GetValueForm() { return m_valueForm; }
	void SetValueForm(CValueForm *valueForm) { m_valueForm = valueForm; }

	bool IsEditable() { return !m_bReadOnly; }
	void SetReadOnly(bool readOnly = true) { m_bReadOnly = readOnly; }

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

	/*
	* Check name conflict
	*/
	bool IsCorrectName(const wxString &name);

	//Execute command 
	void Execute(CCommand *cmd);

	/**
	* Search a size in the hierarchy of an object
	*/
	IValueFrame *SearchSizerInto(IValueFrame *obj);

	void PropagateExpansion(IValueFrame* obj, bool expand, bool up);

	/**
	* Eliminar un objeto.
	*/
	void DoRemoveObject(IValueFrame* object, bool cutObject, bool force = false);

public:

	CVisualEditorContextForm();
	CVisualEditorContextForm(CDocument *document, CView *view, wxWindow *parent, int id = wxID_ANY);

	// Procedures for register/unregister wxEvtHandlers to be notified of wxOESEvents
	void AddHandler(wxEvtHandler* handler);
	void RemoveHandler(wxEvtHandler* handler);

	void ActivateObject();
	void DeactivateObject();

	// Servicios específicos, no definidos en DataObservable
	void SetClipboardObject(IValueFrame* obj) { m_clipboard = obj; }
	IValueFrame* GetClipboardObject() { return m_clipboard; }

	//Objects 
	IValueFrame *CreateObject(const wxString &name);
	void RemoveObject(IValueFrame* obj);
	void CutObject(IValueFrame* obj, bool force = false);
	void CopyObject(IValueFrame* obj);
	bool PasteObject(IValueFrame* parent, IValueFrame* objToPaste = NULL);
	void InsertObject(IValueFrame* obj, IValueFrame* parent);
	void ExpandObject(IValueFrame* obj, bool expand);

	void ModifyProperty(Property* prop, const wxString &value);
	void ModifyEventHandler(Event* evt, const wxString &value);

	void CreateWideGui();

	void DetermineObjectToSelect(IValueFrame* parent, unsigned int pos);

	// Object will not be selected if it already is selected, unless force = true
	// Returns true if selection changed, false if already selected
	bool SelectObject(IValueFrame* obj, bool force = false, bool notify = true);

	void MovePosition(IValueFrame *obj, unsigned int toPos);
	void MovePosition(IValueFrame *obj, bool right, unsigned int num = 1);

	// Servicios para los observadores
	IValueFrame* GetSelectedObject();
	CValueForm* GetSelectedForm();

	void RefreshEditor() { NotifyProjectRefresh(); }

	void Undo();
	void Redo();

	bool CanUndo() { return m_cmdProc->CanUndo(); }
	bool CanRedo() { return m_cmdProc->CanRedo(); }

	bool CanPasteObject();
	bool CanCopyObject();

	bool IsModified();

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

	void ToggleBorderFlag(IValueFrame *obj, int border);
	void CreateBoxSizerWithObject(IValueFrame *obj);

	bool LoadForm();
	bool SaveForm();
	void RunForm();

	void SetCommandProcessor(CCommandProcessor *cmdProc) { m_cmdProc = cmdProc; }

	~CVisualEditorContextForm();

	// Events
	void OnProjectLoaded(wxFrameEvent &event);
	void OnProjectSaved(wxFrameEvent &event);
	void OnObjectSelected(wxFrameObjectEvent &event);
	void OnObjectCreated(wxFrameObjectEvent &event);
	void OnObjectRemoved(wxFrameObjectEvent &event);
	void OnPropertyModified(wxFramePropertyEvent &event);
	void OnProjectRefresh(wxFrameEvent &event);
	void OnCodeGeneration(wxFrameEventHandlerEvent &event);

	wxDECLARE_EVENT_TABLE();
};

#endif