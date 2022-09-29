#ifndef _VISUALEDITOR_BASE_H__
#define _VISUALEDITOR_BASE_H__

#include <wx/wx.h>
#include <wx/scrolwin.h>
#include <map>

class IValueFrame;
class CVisualEditorContextForm;

#include "core.h"

class CORE_API IVisualHost : public wxScrolledCanvas {
	wxDECLARE_ABSTRACT_CLASS(IVisualHost);
public:

	IVisualHost() : wxScrolledCanvas(), m_mainBoxSizer(NULL) { }
	IVisualHost(wxWindow* parent,
		wxWindowID id,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = wxScrolledWindowStyle,
		const wxString& name = wxASCII_STR(wxFrameNameStr)) : wxScrolledCanvas(parent, id, pos, size, style | wxBORDER_SUNKEN, name), m_mainBoxSizer(NULL)
	{
		wxScrolledCanvas::SetDoubleBuffered(true);
		wxScrolledCanvas::SetScrollRate(5, 5);
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
	void CreateControl(IValueFrame* obj, IValueFrame* parent = NULL, bool firstCreated = false);
	//Update exist control
	void UpdateControl(IValueFrame* obj, IValueFrame* parent = NULL);
	//Remove control
	void RemoveControl(IValueFrame* obj, IValueFrame* parent = NULL);
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
	virtual wxObject* Create(IValueFrame* control, wxObject* parent);

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

extern CVisualEditorContextForm* g_visualHostContext;

#endif 