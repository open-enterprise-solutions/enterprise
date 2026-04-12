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

#include "frontend/frontend.h"

class FRONTEND_API ibValueFrame;

class FRONTEND_API ibVisualHost : public wxScrolledCanvas {
	wxDECLARE_ABSTRACT_CLASS(ibVisualHost);
public:

	ibVisualHost(wxWindow* parent,
		wxWindowID id,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = wxScrolledWindowStyle) : wxScrolledCanvas(parent, id, pos, size, style | wxBORDER_SUNKEN)
	{
		wxScrolledCanvas::SetDoubleBuffered(true);
		wxScrolledCanvas::SetScrollRate(5, 5);
#ifdef __WXOSX__
		wxScrolledCanvas::SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
		wxScrolledCanvas::SetBackgroundStyle(wxBG_STYLE_SYSTEM);
#endif
	}

	virtual ~ibVisualHost() {/* ClearVisualHost(); */ }

	ibValueFrame* GetObjectBase(const wxObject* wxobject) const;
	wxObject* GetWxObject(const ibValueFrame* baseobject) const;
	wxSizer* GetFrameSizer() const { return GetBackgroundWindow()->GetSizer(); }

	bool CreateAndUpdateVisualHost() {
		return ClearVisualHost() &&
			CreateVisualHost() && UpdateVisualHost();
	}

	bool CreateVisualHost();
	bool UpdateVisualHost();
	bool ClearVisualHost();

	virtual bool IsShownHost() const { return true; }
	virtual bool IsDesignerHost() const { return false; }

	virtual class ibValueForm* GetValueForm() const = 0;

	virtual wxWindow* GetParentBackgroundWindow() const = 0;
	virtual wxWindow* GetBackgroundWindow() const = 0;

	virtual void OnClickFromApp(wxWindow* currentWindow, wxMouseEvent& event) {}

protected:

	virtual void SetCaption(const wxString& strCaption) = 0;
	virtual void SetOrientation(int orient) = 0;

	virtual void UpdateHostSize() {}

private:

	//Generate component 
	void GenerateControl(ibValueFrame* obj, wxWindow* wxparent, wxObject* parentObject, bool firstCreated = false);

	//Update component
	void RefreshControl(ibValueFrame* obj, wxWindow* wxparent, wxObject* parentObject);

protected:

	//Insert new control
	void CreateControl(ibValueFrame* obj, ibValueFrame* parent = nullptr, bool firstCreated = false);

	//Update exist control
	void UpdateControl(ibValueFrame* obj, ibValueFrame* parent = nullptr);

	//Remove control
	void RemoveControl(ibValueFrame* obj, ibValueFrame* parent = nullptr);

	// Give components an opportunity to cleanup
	void ClearControl(ibValueFrame* control, bool force = false);

	// Calculate label size for static text
	bool CalculateLabelSize(ibValueFrame* control = nullptr);

	//Update virtual size
	void UpdateVirtualSize();

	//*********************************************************
	//*                 Events for visual                     *
	//*********************************************************

	/**
	* Create an instance of the wxObject and return a pointer
	*/
	virtual wxObject* Create(ibValueFrame* control, wxWindow* wndParent);

	/**
	* Allows components to do something after they have been created.
	* For example, Abstract components like NotebookPage and SizerItem can
	* add the actual widget to the Notebook or sizer.
	*
	* @param wxobject The object which was just created.
	* @param wxparent The wxWidgets parent - the wxObject that the created object was added to.
	*/
	virtual void OnCreated(ibValueFrame* control, wxObject* obj, wxWindow* wndParent, bool firstСreated = false);

	/**
	* Allows components to respond when selected in object tree.
	* For example, when a wxNotebook's page is selected, it can switch to that page
	*/
	virtual void OnSelected(ibValueFrame* control, wxObject* obj);

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

private:

	inline void AppendInnerControl(ibValueFrame* control, wxObject* wx_object) {
		m_baseObjects.insert_or_assign(control, wx_object);
	}

	inline void RemoveInnerControl(ibValueFrame* control) {
		m_baseObjects.erase(control);
	}

protected:

	friend class ibValueForm;
	friend class ibValueModelTableBox;

	//controls
	std::unordered_map<ibValueFrame*, wxObject* > m_baseObjects;
};

#include "frontend/docView/docView.h"

class FRONTEND_API ibFrontendVisualEditorNotebook {
public:

	static ibFrontendVisualEditorNotebook* FindEditorByForm(const ibValueFrame* valueForm);

	ibFrontendVisualEditorNotebook();
	virtual ~ibFrontendVisualEditorNotebook();

	virtual void CreateControl(const wxString& controlName) = 0;
	virtual void RemoveControl(ibValueFrame* obj) = 0;
	virtual void CutControl(ibValueFrame* obj, bool force = false) = 0;
	virtual void CopyControl(ibValueFrame* obj) = 0;
	virtual bool PasteControl(ibValueFrame* parent) = 0;
	virtual void InsertControl(ibValueFrame* obj, ibValueFrame* parent) = 0;
	virtual void ExpandControl(ibValueFrame* obj, bool expand) = 0;
	virtual void SelectControl(ibValueFrame* obj) = 0;

	virtual void ModifyEvent(class ibEvent* event, const wxVariant& oldValue, const wxVariant& newValue) = 0;
	virtual void ModifyProperty(class ibProperty* prop, const wxVariant& oldValue, const wxVariant& newValue) = 0;

	virtual void RefreshEditor() = 0;

	virtual ibValueFrame* GetValueForm() const = 0;
	virtual ibMetaDocument* GetEditorDocument() const = 0;
	virtual ibVisualHost* GetVisualHost() const = 0;

	virtual wxEvtHandler* GetHighlightPaintHandler(wxWindow* wnd) const = 0;

private:
	static std::set<ibFrontendVisualEditorNotebook*> ms_visualEditorArray;
};

#define g_visualHostContext FindVisualEditor()

#endif 