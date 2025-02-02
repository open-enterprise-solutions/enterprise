#ifndef _VISUAL_EDITOR_BASE_H__
#define _VISUAL_EDITOR_BASE_H__

#include <set>

#include <wx/artprov.h>
#include <wx/config.h>
#include <wx/cmdproc.h>
#include <wx/docview.h>
#include <wx/splitter.h>
#include <wx/spinbutt.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/treectrl.h>

#include <map>

class IValueFrame;

#include "frontend/frontend.h"

class FRONTEND_API IVisualHost : public wxScrolledWindow {
	wxDECLARE_ABSTRACT_CLASS(IVisualHost);
public:

	IVisualHost(wxWindow* parent,
		wxWindowID id,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = wxScrolledWindowStyle) : wxScrolledWindow(parent, id, pos, size, style | wxBORDER_SUNKEN), m_mainBoxSizer(nullptr)
	{
		wxScrolledWindow::SetDoubleBuffered(true);
		wxScrolledWindow::SetScrollRate(5, 5);
	}

	IValueFrame* GetObjectBase(wxObject* wxobject) const;
	wxObject* GetWxObject(IValueFrame* baseobject) const;

	wxBoxSizer* GetFrameSizer() const {
		return m_mainBoxSizer;
	}

	virtual bool IsDemonstration() const {
		return false;
	}

	virtual bool IsDesignerHost() const {
		return false;
	}

	virtual wxWindow* GetParentBackgroundWindow() const = 0;
	virtual wxWindow* GetBackgroundWindow() const = 0;

	virtual void OnClickFromApp(wxWindow* currentWindow, wxMouseEvent& event) {}

protected:

	friend class CValueTableBox;

	friend class CValueNotebook;
	friend class CValueNotebookPage;

	friend class CVisualDocument;

	//Insert new control
	void CreateControl(IValueFrame* obj, IValueFrame* parent = nullptr, bool firstCreated = false);
	//Update exist control
	void UpdateControl(IValueFrame* obj, IValueFrame* parent = nullptr);
	//Remove control
	void RemoveControl(IValueFrame* obj, IValueFrame* parent = nullptr);
	//Generate component 
	void GenerateControl(IValueFrame* obj, wxWindow* wxparent, wxObject* parentObject, bool firstCreated = false);
	//Update component
	void RefreshControl(IValueFrame* obj, wxWindow* wxparent, wxObject* parentObject);
	// Give components an opportunity to cleanup
	void DeleteRecursive(IValueFrame* control, bool force = false);
	//Update virtual size
	void UpdateVirtualSize();

	//*********************************************************
	//*                 Events for visual                     *
	//*********************************************************

	/**
	* Create an instance of the wxObject and return a pointer
	*/
	virtual wxObject* Create(IValueFrame* control, wxWindow* wndParent);

	/**
	* Allows components to do something after they have been created.
	* For example, Abstract components like NotebookPage and SizerItem can
	* add the actual widget to the Notebook or sizer.
	*
	* @param wxobject The object which was just created.
	* @param wxparent The wxWidgets parent - the wxObject that the created object was added to.
	*/
	virtual void OnCreated(IValueFrame* control, wxObject* obj, wxWindow* wndParent, bool first—reated = false);

	/**
	* Allows components to respond when selected in object tree.
	* For example, when a wxNotebook's page is selected, it can switch to that page
	*/
	virtual void OnSelected(IValueFrame* control, wxObject* obj);

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

protected:

	//controls
	wxBoxSizer* m_mainBoxSizer;

	std::map<wxObject*, IValueFrame*> m_wxObjects;
	std::map<IValueFrame*, wxObject* > m_baseObjects;
};

class FRONTEND_API IVisualEditorNotebook {
	static std::vector<IVisualEditorNotebook*> sm_visualEditor;
public:

	static IVisualEditorNotebook* FindEditorByForm(IValueFrame* valueForm);

	IVisualEditorNotebook() {
		CreateVisualEditor();
	}

	virtual ~IVisualEditorNotebook() {
		DestroyVisualEditor();
	}

	virtual IVisualHost* GetVisualHost() const = 0;

	virtual void CreateControl(const wxString& controlName) = 0;
	virtual void RemoveControl(IValueFrame* obj) = 0;
	virtual void CutControl(IValueFrame* obj, bool force = false) = 0;
	virtual void CopyControl(IValueFrame* obj) = 0;
	virtual bool PasteControl(IValueFrame* parent, IValueFrame* objToPaste = nullptr) = 0;
	virtual void InsertControl(IValueFrame* obj, IValueFrame* parent) = 0;
	virtual void ExpandControl(IValueFrame* obj, bool expand) = 0;
	virtual void SelectControl(IValueFrame* obj) = 0;

	virtual void ModifyEvent(class Event* event, const wxVariant& oldValue, const wxVariant& newValue) = 0;
	virtual void ModifyProperty(class Property* prop, const wxVariant& oldValue, const wxVariant& newValue) = 0;

	virtual IValueFrame* GetValueForm() const = 0;

	virtual void RefreshEditor() = 0;
	virtual void RefreshTree() = 0;

	virtual wxEvtHandler* GetHighlightPaintHandler(wxWindow* wnd) const = 0;

private:
	void CreateVisualEditor();
	void DestroyVisualEditor();
};

#define g_visualHostContext FindVisualEditor()

#endif 